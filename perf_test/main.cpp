#include "include/cpu_timer.hpp"
#include <deque>
#include <stdexcept>
#include <thread>

static uint64_t exec_in_thread(const std::function<void()>& body) {
	cpu_timer::CpuNs start {0};
	cpu_timer::CpuNs stop  {0};
	std::thread th {[&] {
		cpu_timer::detail::fence();
		start = cpu_timer::detail::wall_now();
		cpu_timer::detail::fence();
		body();
		cpu_timer::detail::fence();
		stop = cpu_timer::detail::wall_now();
		cpu_timer::detail::fence();
	}};
	th.join();
	return cpu_timer::detail::get_ns(stop - start);
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

	uint64_t time_none = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_no_timing();
		}
	});

	cpu_timer::get_process().set_is_enabled(false);
	uint64_t time_rt_disabled = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{0});
	cpu_timer::get_process().flush();
	uint64_t time_logging = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});
	uint64_t time_batched_cb = exec_in_thread([&] {
		cpu_timer::get_process().flush();
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{1});
	cpu_timer::get_process().flush();
	uint64_t time_unbatched = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});

	uint64_t time_thready = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_no_timing();
		}
	});

	cpu_timer::get_process().set_is_enabled(true);
	cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{0});
	cpu_timer::get_process().flush();
	uint64_t time_thready_logging = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_timing();
		}
	});

	uint64_t time_check_clocks = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			check_clocks();
		}
	});

	uint64_t time_unbatched_cbs = time_unbatched - time_logging;

	std::cout
		<< "Trials = " << TRIALS << std::endl
		<< "Payload = " << time_none / TRIALS << "ns" << std::endl
		<< "Overhead when runtime-disabled = " << (time_rt_disabled - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead check clocks = " << (time_check_clocks - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead of storing frame = " << (time_logging - time_check_clocks) / TRIALS << "ns per call" << std::endl
		<< "Total overhead of cpu_timer = " << (time_logging - time_none) / TRIALS << "ns per call" << std::endl
		/*
		  I assume a linear model:
		  - time_unbatched_cbs = TRIALS * per_callback_overhead + TRIALS * per_frame_overhead
		  - time_batched_cb = per_callback_overhead + TRIALS * per_frame_overhead
		*/
		<< "Fixed overhead of flush = " << (time_unbatched_cbs - time_batched_cb) / (TRIALS - 1) << "ns" << std::endl
		<< "Variable overhead flush = " << (time_batched_cb - time_unbatched_cbs / TRIALS) / (TRIALS - 1) << "ns per frame" << std::endl
		<< "Thread overhead (due to OS) = " << (time_thready - time_none) / TRIALS << "ns per thread" << std::endl
		<< "Thread overhead (due to cpu_timer) = " << (time_thready_logging - time_thready) / TRIALS << "ns" << std::endl
		;

	return 0;
}
