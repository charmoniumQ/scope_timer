#pragma once // NOLINT(llvm-header-guard)

#include "process.hpp"
#include "source_loc.hpp"
#include "thread.hpp"
#include "type_eraser.hpp"

namespace charmonium::scope_timer::detail {

	struct ScopeTimerArgs {
		TypeEraser info;
		const char* name;
		Process* process;
		Thread* thread;
		SourceLoc source_loc;

		ScopeTimerArgs set_info(TypeEraser&& new_info) && {
			return ScopeTimerArgs{std::move(new_info), name, process, thread, std::move(source_loc)};
		}

		ScopeTimerArgs set_name(const char* new_name) && {
			return ScopeTimerArgs{std::move(info), new_name, process, thread, std::move(source_loc)};
		}

		ScopeTimerArgs set_process(Process* new_process) {
			return ScopeTimerArgs{std::move(info), name, new_process, thread, std::move(source_loc)};
		}

		ScopeTimerArgs set_thread(Thread* new_thread) {
			return ScopeTimerArgs{std::move(info), name, process, new_thread, std::move(source_loc)};
		}

		ScopeTimerArgs set_source_loc(SourceLoc&& new_source_loc) {
			return ScopeTimerArgs{std::move(info), name, process, thread, std::move(new_source_loc)};
		}
	};

	/**
	 * @brief An RAII context for creating, stopping, and storing Timers.
	 */
	class ScopeTimer {
	private:
		ScopeTimerArgs args;
		bool enabled;
	public:
		/**
		 * @brief Begins a new RAII context for a Timer in Thread, if enabled.
		 */
		ScopeTimer(ScopeTimerArgs&& args_)
			: args{std::move(args_)}
			, enabled{args.process->is_enabled()}
		{
			if (enabled) {
				args.thread->enter_stack_frame(args.name, std::move(args.info), std::move(args.source_loc));
			}
		}

		/*
		 * I have a custom destructor, and should there be a copy, the destructor will run twice, and double-count the frame.
		 * Therefore, I disallow copies.
		 */
		ScopeTimer(const ScopeTimer&) = delete;
		ScopeTimer& operator=(const ScopeTimer&) = delete;

		// I could support this, but I don't think I need to.
		ScopeTimer(ScopeTimer&&) = delete;
		ScopeTimer& operator=(ScopeTimer&&) = delete;

		/**
		 * @brief Completes the Timer in Thread.
		 */
		~ScopeTimer() {
			if (enabled) {
				args.thread->exit_stack_frame();
			}
		}
	};
} // namespace charmonium::scope_timer::detail
