#include "include/cpu_timer.hpp"
#include <deque>
#include <stdexcept>
#include <thread>

static void exec_in_thread(const std::function<void()>& fn) {
	std::thread th {fn};
	th.join();
}

constexpr size_t PAYLOAD_ITERATIONS = 1024;
static void noop() {
	for (size_t i = 0; i < PAYLOAD_ITERATIONS; ++i) {
		// NOLINTNEXTLINE(hicpp-no-assembler)
		asm ("" : /* ins */ : /* outs */ : "memory");
	}
}

static void callback(const cpu_timer::Stack&, std::deque<cpu_timer::StackFrame>&&, const std::deque<cpu_timer::StackFrame>&) {
	noop();
}

static void fn_no_timing() {
	noop();
}

static void fn_timing() {
	CPU_TIMER_TIME_FUNCTION();
	noop();
}

static void fn_thready_no_timing() {
	exec_in_thread(fn_no_timing);
}

static void fn_thready_timing() {
	exec_in_thread(fn_timing);
}

static void check_clocks() {
	CPU_TIMER_UNUSED auto start_cpu  = cpu_timer::detail::cpu_now ();
	CPU_TIMER_UNUSED auto start_wall = cpu_timer::detail::wall_now();
	CPU_TIMER_UNUSED auto stop_cpu   = cpu_timer::detail::cpu_now ();
	CPU_TIMER_UNUSED auto stop_wall  = cpu_timer::detail::wall_now();
	noop();
}

int main() {
	cpu_timer::make_process(false, cpu_timer::CpuNs{0}, &callback);

	constexpr uint64_t TRIALS = 1024 * 32;

	uint64_t time_none = 0;
	uint64_t time_rt_disabled = 0;
	uint64_t time_logging = 0;
	uint64_t time_batched_cb = 0;
	uint64_t time_unbatched = 0;
	uint64_t time_thready = 0;
	uint64_t time_thready_logging = 0;
	uint64_t time_check_clocks = 0;

	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_no_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_none = cpu_timer::detail::get_ns(stop - start);
	});

	cpu_timer::get_process().set_is_enabled(false);
	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_rt_disabled = cpu_timer::detail::get_ns(stop - start);
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{0});
	cpu_timer::get_process().flush();
	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_logging = cpu_timer::detail::get_ns(stop - start);
		start = cpu_timer::detail::wall_now();
		cpu_timer::get_process().flush();
		stop = cpu_timer::detail::wall_now();
		time_batched_cb = cpu_timer::detail::get_ns(stop - start);
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{1});
	cpu_timer::get_process().flush();
	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_unbatched = cpu_timer::detail::get_ns(stop - start);
	});

	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_no_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_thready = cpu_timer::detail::get_ns(stop - start);
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{0});
	cpu_timer::get_process().flush();
	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_timing();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_thready_logging = cpu_timer::detail::get_ns(stop - start);
	});

	exec_in_thread([&] {
		auto start = cpu_timer::detail::wall_now();
		for (size_t i = 0; i < TRIALS; ++i) {
			check_clocks();
		}
		auto stop = cpu_timer::detail::wall_now();
		time_check_clocks = cpu_timer::detail::get_ns(stop - start);
	});

	uint64_t time_unbatched_cbs = time_unbatched - time_logging;

	std::cout
		<< "Trials = " << TRIALS << std::endl
		<< "Payload = " << time_none / TRIALS << std::endl
		<< "Per call overhead when runtime-disabled = " << (time_rt_disabled - time_none) / TRIALS << std::endl
		<< "Per call overhead check clocks = " << (time_check_clocks - time_none) / TRIALS << std::endl
		<< "Per call overhead of storing frame = " << (time_logging - time_check_clocks) / TRIALS << " not counting check clocks" << std::endl
		/*
		  I assume a linear model:
		  - time_unbatched_cbs = TRIALS * per_callback_overhead + TRIALS * per_frame_overhead
		  - time_batched_cb = per_callback_overhead + TRIALS * per_frame_overhead
		*/
		<< "Per callback overhead of flush = " << (time_unbatched_cbs - time_batched_cb) / (TRIALS - 1) << std::endl
		<< "Per frame overhead of flush = " << (time_batched_cb - time_unbatched_cbs / TRIALS) / (TRIALS - 1) << std::endl
		<< "Per thread system-initialization = " << (time_thready - time_none) / TRIALS << std::endl
		<< "Per thread logging-initialization = " << (time_thready_logging - time_thready) / TRIALS << " not counting system-initialization" << std::endl
		;

	return 0;
}
