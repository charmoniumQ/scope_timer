#pragma once // NOLINT(llvm-header-guard)
#include "compiler_specific.hpp"
#include "timer.hpp"
#include <mutex>
#include <string>
#include <thread>

namespace charmonium::scope_timer::detail {
	class Thread;
	class Process;
	class Timer;
	class ScopeTimer;

	class CallbackType {
	protected:
		friend class Thread;
		virtual void thread_start(Thread&) { }
		virtual void thread_in_situ(Thread&) { }
		virtual void thread_stop(Thread&) { }
	public:
		virtual ~CallbackType() = default;
		CallbackType(const CallbackType&) = default;
		CallbackType(CallbackType&&) noexcept { }
		CallbackType& operator=(const CallbackType&) = default;
		CallbackType& operator=(CallbackType&&) noexcept { return *this; }
		CallbackType() = default;
	};

	class Thread {
	private:
		friend class Process;
		friend class Timer;
		friend class ScopeTimer;

		Process& process;
		const std::thread::id id;
		const std::thread::native_handle_type native_handle;
		std::string name;
		Timers stack;
		mutable std::mutex finished_mutex;
		Timers finished; // locked by finished_mutex
		IndexNo index;
		CpuTime last_log;

		void enter_stack_frame(const char* name, TypeEraser&& info, SourceLoc source_loc) {
			IndexNo caller_index = 0;
			IndexNo prev_index = 0;
			IndexNo this_index = index++;

			if (CHARMONIUM_SCOPE_TIMER_LIKELY(!stack.empty())) {
				Timer& caller = stack.back();

				caller_index = caller.index;

				prev_index = caller.youngest_child_index;
				             caller.youngest_child_index = this_index;
			}

			stack.emplace_back(
				get_process_start(),
				name,
				std::move(source_loc),
				this_index,
				caller_index,
				prev_index,
				std::move(info)
			);

			// very last:
			stack.back().start_timers();
		}

		void exit_stack_frame() {
			assert(!stack.empty() && "somehow exit_stack_frame was called more times than enter_stack_frame");

			// (almost) very first:
			stack.back().stop_timers();

			{
				// std::lock_guard<std::mutex> finished_lock {finished_mutex};
				finished.emplace_back(std::move(stack.back()));
			}
			stack.pop_back();

			maybe_flush();
		}

	public:

		const Timer& get_top() const { return stack.back(); }
		Timer& get_top() { return stack.back(); }

		Thread(Process& process_, std::thread::id id_, std::thread::native_handle_type native_handle_, std::string&& name_)
			: process{process_}
			, id{id_}
			, native_handle{native_handle_}
			, name{std::move(name_)}
			, index{0}
			, last_log{0}
		{
			enter_stack_frame("", TypeEraser{type_eraser_default}, SourceLoc{});
			get_callback().thread_start(*this);
		}

		~Thread() {
			// std::cerr << "Thread::~Thread: " << id << std::endl;
			exit_stack_frame();
			assert(stack.empty() && "somewhow enter_stack_frame was called more times than exit_stack_frame");
			get_callback().thread_stop(*this);
			// assert(finished.empty() && "flush() should drain this buffer, and nobody should be adding to it now. Somehow unflushed Timers are still present");
		}

		// I do stuff in the destructor that should only happen once per constructor-call.
		Thread(const Thread& other) = delete;
		Thread& operator=(const Thread& other) = delete;

		Thread(Thread&& other) noexcept
			: process{other.process}
			, id{other.id}
			, native_handle{other.native_handle}
			, name{std::move(other.name)}
			, stack{std::move(other.stack)}
			, finished{std::move(other.finished)}
			, index{other.index}
			, last_log{other.last_log}
		{ }
		Thread& operator=(Thread&& other) = delete;

		std::thread::id get_id() const { return id; }

		std::thread::native_handle_type get_native_handle() const { return native_handle; }

		std::string get_name() const { return name; }

		void set_name(std::string&& name_) { name = std::move(name_); }

		const Timers& get_stack() const { return stack; }

		Timers drain_finished() {
			Timers finished_buffer;
			finished.swap(finished_buffer);
			return finished_buffer;
		}

	private:
		void maybe_flush() {
			std::lock_guard<std::mutex> finished_lock {finished_mutex};
			// get CPU time is expensive. Instead we look at the last frame
			if (!finished.empty()) {
				CpuTime now = finished.back().get_stop_cpu();

				// std::lock_guard<std::mutex> config_lock {process.config_mutex};
				CpuTime process_callback_period = get_callback_period();

				if (get_ns(process_callback_period) != 0 && (get_ns(process_callback_period) == 1 || now > last_log + process_callback_period)) {
					get_callback().thread_in_situ(*this);
				}
			}
		}

		CallbackType& get_callback() const;
		CpuTime get_callback_period() const;
		WallTime get_process_start() const;
	};
} // namespace charmonium::scope_timer::detail
