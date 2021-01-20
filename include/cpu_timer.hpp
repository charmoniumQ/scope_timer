#pragma once // NOLINT(llvm-header-guard)

/**
 * @brief This is the cpu_timer.
 *
 * Usage:
 *
 * 1. Use `cpu_timer::time_function()` and `cpu_timer::time_block()` to time your code.
 *
 * 2. By default, your code will not be timed. Set env
 *    CPU_TIMER_ENABLE=1 at runtime to enable it.
 *
 * 3. `#define CPU_TIMER_DISABLE` to disable it at compiletime. All of
 *    your calls to cpu_timer should compile away to noops. This
 *    allows you to measure the overhead of the cpu_timer itself.
 *
 * Motivation:
 *
 * - It is "opt-in" timing. Nothing is timed, except what you ask
 *   for. This is in contrast to `-pg`.
 *
 * - It uses RAII, so just have to add one line time a block.
 *
 * - Each timing record maintains a record to the most recent caller
 *   that opted-in to timing. The timer is "context-sensitive".
 *
 * - Each timing record can optionally contain arguments or runtime
 *   information.
 *
 * - I report both a wall clock (real time since program startup) and CPU time spent on that thread. Both of these should be monotonic.
 *
 * TODO:
 * - Add record for thread_caller.
 * - Log process start somewhere (in thread_caller)?
 * - Remove the mutex in ThreadContext.
 * - Make util headers not exported.
 */

#include "cpu_timer_internal.hpp"
#include "global_state.hpp"
namespace cpu_timer {

	// TODO(sam): front-end interface
	// TODO(sam): is_enabled
	// TODO(sam): perf test: (no annotations, disabled at compile-time, disabled at run-time, coalesced into 1 post-mortem batch but new thread, coalesced into 1 post-mortem batch, enabled coalesced into N batches) x (func call, func call in new thread) without a callback
	// This tells us: disabled at (compile|run)-time == no annotations, overhead of func-call logging, overhead of first func-call log in new thread, overhead of batch submission

	using CpuTime = detail::CpuTime;
	using WallTime = detail::WallTime;
	using CallbackType = detail::CallbackType;
	static const auto& make_process = detail::make_process;
	static const auto& get_process = detail::get_process;
	using Process = detail::Process;

} // namespace cpu_timer

#ifdef CPU_TIMER_DISABLE
#define CPU_TIMER_TIME_BLOCK(comment, site_name) const auto TOKENPASTE(cpu_timer_, __LINE__) = cpu_timer::detail::StackFrameContext{cpu_timer::detail::get_stack(), comment, site_name, __FILE__, __LINE__};
#else
#define CPU_TIMER_TIME_BLOCK(comment, site_name)
#endif
#define CPU_TIMER_TIME_FUNCTION_COMMENT(comment) CPU_TIMER_TIME_BLOCK(comment, __FUNC__)
#define CPU_TIMER_TIME_FUNCTION() CPU_TIMER_TIME_FUNCTION_COMMENT("")
