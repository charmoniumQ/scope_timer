#pragma once // NOLINT(llvm-header-guard)

/**
 * This library helps measure time at a "scope" granularity.
 *
 * # Example
 *
 * For each block you want to time, write
 *
 * \code{.cpp}
 * {
 *     // Time this scope
 *     SCOPE_TIMER();
 *
 *    // Do comuptation
 *    // ...
 * }
 * \endcode
 *
 * All of the finished timers will get collected and sent to a callback, which
 * can be configured as such:
 *
 * \code{.cpp}
 * class Callback : public scope_timer::CallbackType {
 * public:
 *     void thread_start(scope_timer::Thread&) override { }
 *     void thread_in_situ(scope_timer::Thread&) override { }
 *     void thread_stop(scope_timer::Thread&) override { }
 * };
 *
 * int main() {
 *     auto& proc = scope_timer::get_process();
 *     proc.emplace_callback<Callback>();
 *     proc.set_enabled(true);
 *
 *     // ...
 *     // Do program
 * }
 * \endcode
 *
 * Note that this callback _process-global_. You can include scope_timer in
 * dynamically and statically linked libraries, and they will all feed into the
 * same callback.
 *
 * See ../example/main.cpp for more details.
 *
 * # Motivation
 *
 * While perf exists, it is Linux-specific (doesn't even work in Docker) and it
 * can't record dynamic information.
 *
 * Recording dynamic information is important when the functions compute time is
 * input-dependent. One would needs to know not only the time taken, but what
 * the input was.
 *
 * Scope timer involves instrumenting the source code. This makes timing "opt
 * in" rather than "opt out." You can ignore code that gets timed a lot
 * (e.g. `malloc()`) but time the caller of it. It uses RAII, so just have to
 * add one line time a block.
 *
 * This timer not only times the scope itself, it connects it the other timers
 * (i.e. a call tree). This way, you can time the whole and parts of the whole,
 * without double-counting.
 *
 * Unlike perf and prof/gprof, this timer collects wall time in addition to CPU
 * time.
 *
 * # Overhead
 *
 * These timers have a ~400ns overhead (check clocks + storing frame overhead)
 * per frame timed on my system. Run ./test.sh to check on yours.
 *
 * I use clock_gettime with CLOCK_THREAD_CPUTIME_ID (cpu time) and
 * CLOCK_MONOTONIC (wall time). rdtsc won't track CPU time if the thread gets
 * interrupted [2], and I *need* the extra work that
 * clock_gettime(CLOCK_MONOTIC) does to convert tsc into a wall time. The VDSO
 * interface mitigates sycall overhead. In some cases, clock_gettime is faster
 * [1].
 *
 * [1]: https://stackoverflow.com/questions/7935518/is-clock-gettime-adequate-for-submicrosecond-timing
 * [2]: https://stackoverflow.com/questions/42189976/calculate-system-time-using-rdtsc
 *
 */

#include "scope_timer_internal.hpp"
#include "global_state.hpp"
namespace scope_timer {

	using Timers = detail::Timers;
	using Timer = detail::Timer;
	using ScopeTimer = detail::ScopeTimer;
	using ScopeTimerArgs = detail::ScopeTimerArgs;
	using CpuNs = detail::CpuTime;
	using WallNs = detail::WallTime;
	using CallbackType = detail::CallbackType;
	using Process = detail::Process;
	using Thread = detail::Thread;
	using TypeEraser = detail::TypeEraser;

	// Function aliases https://www.fluentcpp.com/2017/10/27/function-aliases-cpp/
	// Whoops, we don't use this anymore, bc we need to bind methods to their static object.

	SCOPE_TIMER_UNUSED static Process& get_process() { return detail::process_container.get_process(); }
	SCOPE_TIMER_UNUSED static Thread& get_thread() { return detail::thread_container.get_thread(); }

	static constexpr auto& type_eraser_default = detail::type_eraser_default;
	static constexpr auto& cpu_now = detail::cpu_now;
	static constexpr auto& wall_now = detail::wall_now;
	static constexpr auto& get_ns = detail::get_ns;

	// In C++14, we could use templated function aliases
	template <typename T>
	TypeEraser make_type_eraser(T* ptr) {
		return std::static_pointer_cast<void>(std::shared_ptr<T>{ptr});
	}
	template <typename T, class... Args>
	TypeEraser make_type_eraser(Args&&... args) {
		return std::static_pointer_cast<void>(std::make_shared<T>(std::forward<Args>(args)...));
	}
	template <typename T>
	const T& extract_type_eraser(const TypeEraser& type_eraser) {
		return *std::static_pointer_cast<T>(type_eraser);
	}
	template <typename T>
	T& extract_type_eraser(TypeEraser& type_eraser) {
		return *std::static_pointer_cast<T>(type_eraser);
	}

} // namespace scope_timer


#define SCOPE_TIMER_DEFAULT_ARGS() (scope_timer::ScopeTimerArgs{	\
			scope_timer::type_eraser_default,						\
			"",														\
			&scope_timer::get_process(),							\
			&scope_timer::get_thread(),								\
			SCOPE_TIMER_SOURCE_LOC() })

#define SCOPE_TIMER(args_dot_set_vars) \
    scope_timer::ScopeTimer SCOPE_TIMER_UNIQUE_NAME() {	\
		SCOPE_TIMER_DEFAULT_ARGS() args_dot_set_vars	\
	};
