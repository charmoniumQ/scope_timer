#pragma once // NOLINT(llvm-header-guard)
#include "compiler_specific.hpp"
#include "clock.hpp"
#include "util.hpp"
#include <cassert>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpu_timer {
namespace detail {

	class Stack;

	/**
	 * @brief Timing and runtime data relating to one stack-frame.
	 */
	class StackFrame {
	private:
		std::string comment;
		const char* function_name;
		const char* file_name;
		size_t line;
		size_t caller_start_index;
		WallTime process_start;
		WallTime start_wall;
		CpuTime start_cpu;
		size_t start_index;
		WallTime stop_wall;
		CpuTime stop_cpu;
		size_t stop_index;

		friend class Stack;

		void start_timer(size_t start_index_) {
			start_index = start_index_;

			assert(start_cpu == CpuTime{0} && "start_timer should only be called once");

			// very last thing:
			fence();
			start_wall = wall_now();
			start_cpu = cpu_now();
			fence();
		}
		void stop_timer(size_t stop_index_) {
			assert(stop_cpu == CpuTime{0} && "stop_timer should only be called once");
			// almost very first thing:
			fence();
			stop_wall = wall_now();
			stop_cpu = cpu_now();
			fence();

			assert(start_cpu != CpuTime{0} && "stop_timer should be called after start_timer");
			stop_index = stop_index_;
		}

	public:
		StackFrame(std::string&& comment_,
				   const char* function_name_,
				   const char* file_name_,
				   size_t line_,
				   size_t caller_start_index_,
				   WallTime process_start_)
			: comment{std::move(comment_)}
			, function_name{function_name_}
			, file_name{file_name_}
			, line{line_}
			, caller_start_index{caller_start_index_}
			, process_start{process_start_}
			, start_wall{0}
			, start_cpu{0}
			, start_index{0}
			, stop_wall{0}
			, stop_cpu{0}
			, stop_index{0}
		{ }

		/**
		 * @brief `comment` has a user-specified meaning.
		 */
		const std::string& get_comment() const { return comment; }

		const char* get_function_name() const { return function_name; }

		const char* get_file_name() const { return file_name; }

		size_t get_line() const { return line; }

		/**
		 * @brief The start_index of the StackFrame which called this one.
		 *
		 * The top of the stack is a loop.
		 *
		 * When a new StackFrame is created, we don't know the stop_index, since the caller has not yet stopped.
		 */
		size_t get_caller_start_index() const { return caller_start_index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_start_wall() const { return start_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_start_cpu() const { return start_cpu; }

		/**
		 * @brief An index according to the order that StackFrames started (AKA pre-order).
		 */
		size_t get_start_index() const { return start_index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_stop_wall() const { return stop_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_stop_cpu() const { return stop_cpu; }

		/**
		 * @brief An index according to the order that StackFrames stopped (AKA post-order).
		 */
		size_t get_stop_index() const { return stop_index; }
	};

	CPU_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const StackFrame& frame) {
		return os
			<< frame.get_file_name() << ":" << frame.get_line() << ":" << frame.get_function_name() << " called by " << frame.get_caller_start_index() << "\n"
			<< " cpu   : " << get_ns(frame.get_start_cpu  ()) << " + " << get_ns(frame.get_stop_cpu  () - frame.get_start_cpu  ()) << "\n"
			<< " wall  : " << get_ns(frame.get_start_wall ()) << " + " << get_ns(frame.get_stop_wall () - frame.get_start_wall ()) << "\n"
			<< " started: " << frame.get_start_index() << " stopped: " << frame.get_stop_index() << "\n";
			;
	}

	class Process;

	using CallbackType = std::function<void(std::thread::id, std::deque<StackFrame>&&, const std::deque<StackFrame>&)>;

	class Stack {
	private:
		friend class Process;
		friend class StackFrameContext;

		static constexpr const char* const thread_main = "thread_main";
		const std::thread::id thread_id;
		const bool is_enabled;
		const WallTime process_start;
		const CpuTime log_period;
		const CallbackType callback;
		size_t start_index;
		size_t stop_index;
		std::deque<StackFrame> stack;
		std::deque<StackFrame> finished;
		CpuTime last_log;
		std::mutex finished_mutex;

	public:

		Stack(std::thread::id thread_id_, bool is_enabled_, WallTime process_start_, CpuTime log_period_, CallbackType callback_)
			: thread_id{thread_id_}
			, is_enabled{is_enabled_}
			, process_start{process_start_}
			, log_period{log_period_}
			, callback{std::move(callback_)}
			, start_index{0}
			, stop_index{0}
			, last_log{0}
		{
			enter_stack_frame("", thread_main, thread_main, 0);
		}

		~Stack() {
			exit_stack_frame(thread_main);
			assert(stack.empty() && "somewhow enter_stack_frame was called more times than exit_stack_frame");
			if (!finished.empty()) {
				flush();
				assert(finished.empty() && "flush() should drain this buffer, and nobody should be adding to it now");
			}
		}

		// I do stuff in the destructor that should only happen once per constructor-call.
		Stack(const Stack& other) = delete;
		Stack& operator=(const Stack& other) = delete;

		Stack(Stack&& other) noexcept
			: thread_id{({std::cerr << "Stack::Stack(Stack&&)" << std::endl; other.thread_id;})}
			, is_enabled{other.is_enabled}
			, process_start{other.process_start}
			, log_period{other.log_period}
			, callback{other.callback}
			, start_index{other.start_index}
			, stop_index{other.stop_index}
			, stack{std::move(other.stack)}
			, finished{std::move(other.finished)}
			, last_log{other.last_log}
		{ }
		Stack& operator=(Stack&& other) = delete;

		void enter_stack_frame(std::string&& comment, const char* function_name, const char* file_name, size_t line) {
			stack.emplace_back(
				std::move(comment),
				function_name,
				file_name,
				line,
				(stack.empty() ? 0 : stack.back().get_start_index()),
				process_start
			);

			// very last:
			stack.back().start_timer(start_index++);
		}

		void exit_stack_frame(CPU_TIMER_UNUSED const char* function_name) {
			assert(!stack.empty() && "somehow exit_stack_frame was called more times than enter_stack_frame");

			// (almost) very first:
			stack.back().stop_timer(stop_index++);

			assert(function_name == stack.back().get_function_name() && "somehow enter_stack_frame and exit_stack_frame for this frame are misaligned");
			{
				std::lock_guard<std::mutex> finished_lock {finished_mutex};
				finished.emplace_back(stack.back());
			}
			stack.pop_back();

			maybe_flush();
		}

		void flush() {
			if (callback) {
				std::lock_guard<std::mutex> finished_lock {finished_mutex};
				std::deque<StackFrame> finished_buffer;
				finished.swap(finished_buffer);
				callback(thread_id, std::move(finished_buffer), stack);
			}
		}

	private:
		void maybe_flush() {
			bool finished_empty {false};
			CpuTime now {0};
			{
				std::lock_guard<std::mutex> finished_lock {finished_mutex};
				finished_empty = finished.empty();
				now = finished.back().get_stop_cpu();
			}
			if (get_ns(log_period) != 0 && !finished_empty && now > last_log + log_period) {
				flush();
			}
		}
	};

	/**
	 * @brief All stacks in the current process.
	 *
	 * This calls callback with one thread's batches of StackFrames, periodically not sooner than log_period, in the thread whose functions are in the batch.
	 */
	class Process {
	private:
		bool is_enabled;
		WallTime start;
		CpuTime log_period;
		CallbackType callback;
		std::unordered_map<std::thread::id, Stack> thread_to_stack;
		mutable std::mutex thread_to_stack_mutex;
	public:

		explicit Process(bool is_enabled_, CpuTime log_period_, CallbackType callback_)
			: is_enabled{is_enabled_}
			, start{wall_now()}
			, log_period{log_period_}
			, callback{std::move(callback_)}
		{ }

		Stack& create_stack(std::thread::id id) {
			std::lock_guard<std::mutex> thread_to_stack_lock {thread_to_stack_mutex};
			// A thread could be created twice if it crosses into an object file with its own static thread_local constructor.
			// assert(thread_to_stack.count(id) == 0);
			if (thread_to_stack.count(id) == 0) {
				thread_to_stack.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id, is_enabled, start, log_period, callback));
			}
			return thread_to_stack.at(id);
		}

		/**
		 * @brief Call when a thread is disposed.
		 *
		 * This is neccessary because the OS can reuse old thread IDs.
		 */
		void remove_stack(std::thread::id id) {
			std::lock_guard<std::mutex> thread_to_stack_lock {thread_to_stack_mutex};
			if (thread_to_stack.count(id) != 0) {
				thread_to_stack.erase(id);
			}
		}

		/**
		 * @brief Sets @p is_enabled for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_is_enabled(bool is_enabled_) {
			is_enabled = is_enabled_;
		}

		/**
		 * @brief Sets @p log_period for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_log_period(CpuTime log_period_) {
			log_period = log_period_;
		}

		/**
		 * @brief Sets @p callback for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback(CallbackType callback_) {
			callback = std::move(callback_);
		}

		/**
		 * @brief Flush all finished frames
		 */
		void flush() {
			std::lock_guard<std::mutex> thread_to_stack_lock {thread_to_stack_mutex};
			for (auto& pair : thread_to_stack) {
				pair.second.flush();
			}
		}

		// get_stack() returns pointers into this, so it should not be copied or moved.
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
		Process(const Process&&) = delete;
		Process& operator=(const Process&&) = delete;
		~Process() = default;
	};

	/**
	 * @brief An RAII context for creating, stopping, and storing StackFrames.
	 */
	class StackFrameContext {
	private:
		Stack& stack;
		const char* function_name;
	public:
		/**
		 * @brief Begins a new RAII context for a StackFrame in Stack, if enabled.
		 */
		StackFrameContext(Stack& stack_, std::string&& comment, const char* function_name_, const char* file_name, size_t line)
			: stack{stack_}
			, function_name{function_name_}
		{
			if (stack.is_enabled) {
				stack.enter_stack_frame(std::move(comment), function_name, file_name, line);
			}
		}

		/*
		 * I have a custom destructor, and should there be a copy, the destructor will run twice, and double-count the frame.
		 * Therefore, I disallow copies.
		 */
		StackFrameContext(const StackFrameContext&) = delete;
		StackFrameContext& operator=(const StackFrameContext&) = delete;

		// I could support this, but I don't think I need to.
		StackFrameContext(StackFrameContext&&) = delete;
		StackFrameContext& operator=(StackFrameContext&&) = delete;

		/**
		 * @brief Completes the StackFrame in Stack.
		 */
		~StackFrameContext() {
			if (stack.is_enabled) {
				stack.exit_stack_frame(function_name);
			}
		}
	};

} // namespace detail
} // namespace cpu_timer
