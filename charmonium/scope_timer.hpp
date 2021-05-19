#pragma once // NOLINT(llvm-header-guard)

/**
 * See https://github.com/charmoniumQ/scope_timer for documentation.
 */

#include "scope_timer/global_state.hpp"
#include "scope_timer/scope_timer.hpp"
namespace charmonium::scope_timer {

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

	CHARMONIUM_SCOPE_TIMER_UNUSED static Process& get_process() { return detail::process_container.get_process(); }
	CHARMONIUM_SCOPE_TIMER_UNUSED static Thread& get_thread() { return detail::thread_container.get_thread(); }

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

} // namespace charmonium::scope_timer


#define SCOPE_TIMER(args_dot_set_vars)                                            \
    charmonium::scope_timer::ScopeTimer CHARMONIUM_SCOPE_TIMER_UNIQUE_NAME() {(   \
        charmonium::scope_timer::ScopeTimerArgs{                                  \
            charmonium::scope_timer::type_eraser_default,                         \
            "",                                                                   \
            false,                                                                \
            &charmonium::scope_timer::get_process(),                              \
            &charmonium::scope_timer::get_thread(),                               \
            CHARMONIUM_SCOPE_TIMER_SOURCE_LOC()                                   \
        } args_dot_set_vars                                                       \
    )};
