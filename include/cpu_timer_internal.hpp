#pragma once // NOLINT(llvm-header-guard)
#include "clock.hpp"
#include "compiler_specific.hpp"
#include "type_eraser.hpp"
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

	static constexpr bool use_fences = true;

	class Frame;
	class Stack;
	class Process;
	class StackFrameContext;

	/**
	 * @brief Timing and runtime data relating to one stack-frame.
	 */
	class Frame {
	private:
		friend class Stack;

		WallTime process_start;

		const char* function_name;
		const char* file_name;
		size_t line;
		size_t caller_start_index;
		WallTime start_wall;
		CpuTime start_cpu;
		size_t start_index;
		WallTime stop_wall;
		CpuTime stop_cpu;
		size_t stop_index;
		TypeEraser info;

		void start_timer(size_t start_index_) {
			start_index = start_index_;

			assert(start_cpu == CpuTime{0} && "start_timer should only be called once");

			// very last thing:
			if (use_fences) { fence(); }
			start_wall = wall_now();
			start_cpu = cpu_now();
			if (use_fences) { fence(); }
		}
		void stop_timer(size_t stop_index_) {
			assert(stop_cpu == CpuTime{0} && "stop_timer should only be called once");
			// almost very first thing:
			if (use_fences) { fence(); }
			stop_wall = wall_now();
			stop_cpu = cpu_now();
			if (use_fences) { fence(); }

			assert(start_cpu != CpuTime{0} && "stop_timer should be called after start_timer");
			stop_index = stop_index_;
		}

	public:
		Frame(
			WallTime process_start_,
			const char* function_name_,
			const char* file_name_,
			size_t line_,
			size_t caller_start_index_,
			TypeEraser info_
		)
			: process_start{process_start_}
			, function_name{function_name_}
			, file_name{file_name_}
			, line{line_}
			, caller_start_index{caller_start_index_}
			, start_wall{0}
			, start_cpu{0}
			, start_index{0}
			, stop_wall{0}
			, stop_cpu{0}
			, stop_index{0}
			, info{std::move(info_)}
		{ }

		/**
		 * @brief User-specified meaning.
		 */
		const TypeEraser& get_info() const { return info; }
		TypeEraser& get_info() { return info; }

		const char* get_function_name() const { return function_name; }

		const char* get_file_name() const { return file_name; }

		size_t get_line() const { return line; }

		/**
		 * @brief The start_index of the Frame which called this one.
		 *
		 * The top of the stack is a loop.
		 *
		 * When a new Frame is created, we don't know the stop_index, since the caller has not yet stopped.
		 */
		size_t get_caller_start_index() const { return caller_start_index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_stop_wall() const { return stop_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_start_cpu() const { return start_cpu; }

		/**
		 * @brief An index according to the order that Frames started (AKA pre-order).
		 */
		size_t get_start_index() const { return start_index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_start_wall() const { return start_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_stop_cpu() const { return stop_cpu; }

		/**
		 * @brief An index according to the order that Frames stopped (AKA post-order).
		 */
		size_t get_stop_index() const { return stop_index; }
	};

	CPU_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const Frame& frame) {
		return os
			<< null_to_empty(frame.get_file_name()) << ":" << frame.get_line() << ":" << null_to_empty(frame.get_function_name()) << " called by " << frame.get_caller_start_index() << "\n"
			<< " cpu   : " << get_ns(frame.get_start_cpu  ()) << " + " << get_ns(frame.get_stop_cpu  () - frame.get_start_cpu  ()) << "\n"
			<< " wall  : " << get_ns(frame.get_start_wall ()) << " + " << get_ns(frame.get_stop_wall () - frame.get_start_wall ()) << "\n"
			<< " started: " << frame.get_start_index() << " stopped: " << frame.get_stop_index() << "\n";
			;
	}

	using CallbackType = std::function<void(const Stack&, std::deque<Frame>&&, const std::deque<Frame>&)>;

	class Stack {
	private:
		friend class Process;
		friend class Frame;
		friend class StackFrameContext;

		Process& process;
		const std::thread::id id;
		const std::thread::native_handle_type native_handle;
		std::string name;
		std::deque<Frame> stack;
		mutable std::mutex finished_mutex;
		std::deque<Frame> finished; // locked by finished_mutex
		size_t start_index;
		size_t stop_index;
		CpuTime last_log;

		void enter_stack_frame(const char* function_name, const char* file_name, size_t line, TypeEraser info) {
			stack.emplace_back(
				get_process_start(),
				function_name,
				file_name,
				line,
				(CPU_TIMER_UNLIKELY(stack.empty()) ? 0 : stack.back().get_start_index()),
				std::move(info)
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
				// std::lock_guard<std::mutex> finished_lock {finished_mutex};
				finished.emplace_back(stack.back());
			}
			stack.pop_back();

			maybe_flush();
		}

	public:

		const Frame& get_top() const { return stack.back(); }
		Frame& get_top() { return stack.back(); }

		Stack(Process& process_, std::thread::id id_, std::thread::native_handle_type native_handle_, std::string&& name_)
			: process{process_}
			, id{id_}
			, native_handle{native_handle_}
			, name{std::move(name_)}
			, start_index{0}
			, stop_index{0}
			, last_log{0}
		{
			enter_stack_frame(nullptr, nullptr, 0, type_eraser_default);
		}

		~Stack() {
			exit_stack_frame(nullptr);
			assert(stack.empty() && "somewhow enter_stack_frame was called more times than exit_stack_frame");
			flush();
			assert(finished.empty() && "flush() should drain this buffer, and nobody should be adding to it now. Somehow unflushed Frames are still present");
		}

		// I do stuff in the destructor that should only happen once per constructor-call.
		Stack(const Stack& other) = delete;
		Stack& operator=(const Stack& other) = delete;

		Stack(Stack&& other) noexcept
			: process{other.process}
			, id{other.id}
			, native_handle{other.native_handle}
			, name{std::move(other.name)}
			, stack{std::move(other.stack)}
			, finished{std::move(other.finished)}
			, start_index{other.start_index}
			, stop_index{other.stop_index}
			, last_log{other.last_log}
		{ }
		Stack& operator=(Stack&& other) = delete;

		/**
		 * @brief Calls callback on a batch containing all completed records, if any.
		 */
		void flush() {
			std::lock_guard<std::mutex> finished_lock {finished_mutex};
			if (!finished.empty()) {
				// std::lock_guard<std::mutex> config_lock {process.config_mutex};
				CallbackType process_callback = get_callback();
				flush_with_locks();
			}
		}

		std::thread::id get_id() const {
			return id;
		}

		std::thread::native_handle_type get_native_handle() const {
			return native_handle;
		}

		std::string get_name() const {
			return name;
		}
		void set_name(std::string&& name_) {
			name = std::move(name_);
		}

	private:
		void maybe_flush() {
			std::lock_guard<std::mutex> finished_lock {finished_mutex};
			// get CPU time is expensive. Instead we look at the last frame
			if (!finished.empty()) {
				CpuTime now = finished.back().get_stop_cpu();

				// std::lock_guard<std::mutex> config_lock {process.config_mutex};
				CpuTime process_log_period = get_log_period();
				CallbackType process_callback = get_callback();

				if (get_ns(process_log_period) != 0) {
					if (now > last_log + process_log_period) {
						flush_with_locks();
					}
				}
			}
		}

		void flush_with_locks() {
			std::deque<Frame> finished_buffer;
			finished.swap(finished_buffer);
			CallbackType callback = get_callback();
			if (callback) {
				callback(*this, std::move(finished_buffer), stack);
			}
		}

		CallbackType get_callback() const;
		CpuTime get_log_period() const;
		WallTime get_process_start() const;
	};

	/**
	 * @brief All stacks in the current process.
	 *
	 * This calls callback with one thread's batches of Frames, periodically not sooner than log_period, in the thread whose functions are in the batch.
	 */
	class Process {
	private:
		friend class Stack;
		friend class StackFrameContext;

		// std::atomic<bool> enabled;
		bool enabled;
		// std::mutex config_mutex;
		const WallTime start;
		CpuTime log_period; // locked by config_mutex
		CallbackType callback; // locked by config_mutex
		// Actually, I don't need to lock the config
		// If two threads race to modify the config, the "winner" is already non-deterministic
		// The callers should synchronize themselves.
		// If this thread writes while someone else reads, there is no guarantee they hadn't "already read" the values.
		// The caller should synchronize with the readers.
		std::unordered_map<std::thread::id, Stack> thread_to_stack; // locked by thread_to_stack_mutex
		std::unordered_map<std::thread::id, size_t> thread_use_count; // locked by thread_to_stack_mutex
		mutable std::mutex thread_to_stack_mutex;

	public:

		explicit Process()
			: enabled{false}
			, start{wall_now()}
			, log_period{0}
		{ }

		WallTime get_start() { return start; }

		/**
		 * @brief Create or get the stack.
		 *
		 * For efficiency, the caller should cache this in thread_local storage.
		 */
		Stack& create_stack(
				std::thread::id thread,
				std::thread::native_handle_type native_handle,
				std::string&& thread_name
		) {
			std::lock_guard<std::mutex> thread_to_stack_lock {thread_to_stack_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			// Could have also been set up by the caller.
			if (thread_to_stack.count(thread) == 0) {
				thread_to_stack.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(thread),
					std::forward_as_tuple(*this, thread, native_handle, std::move(thread_name))
				);
			}
			thread_use_count[thread]++;
			return thread_to_stack.at(thread);
		}

		/**
		 * @brief Call when a thread is disposed.
		 *
		 * This is neccessary because the OS can reuse old thread IDs.
		 */
		void delete_stack(std::thread::id thread) {
			std::lock_guard<std::mutex> thread_to_stack_lock {thread_to_stack_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			if (thread_to_stack.count(thread) != 0) {
				thread_use_count[thread]--;
				if (thread_use_count.at(thread) == 0) {
					thread_to_stack.erase(thread);
				}
			}
		}

		/**
		 * @brief Sets @p enabled for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_enabled(bool enabled_) {
			enabled = enabled_;
		}

		/**
		 * @brief Sets @p log_period for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_log_period(CpuTime log_period_) {
			// std::lock_guard<std::mutex> config_lock {config_mutex};
			log_period = log_period_;
		}

		bool is_enabled() {
			return enabled;
		}

		/**
		 * @brief Sets @p callback for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback(CallbackType callback_) {
			// std::lock_guard<std::mutex> config_lock {config_mutex};
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
	 * @brief An RAII context for creating, stopping, and storing Frames.
	 */
	class StackFrameContext {
	private:
		Process& process;
		Stack& stack;
		const char* function_name;
		bool enabled;
	public:
		/**
		 * @brief Begins a new RAII context for a StackFrame in Stack, if enabled.
		 */
		StackFrameContext(Process& process_, Stack& stack_, const char* function_name_, const char* file_name, size_t line, TypeEraser info)
			: process{process_}
			, stack{stack_}
			, function_name{function_name_}
			, enabled{process.is_enabled()}
		{
			if (enabled) {
				stack.enter_stack_frame(function_name, file_name, line, std::move(info));
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
			if (enabled) {
				stack.exit_stack_frame(function_name);
			}
		}
	};

	inline CallbackType Stack::get_callback() const { return process.callback; }

	inline CpuTime Stack::get_log_period() const { return process.log_period; }

	inline WallTime Stack::get_process_start() const { return process.start; }

} // namespace detail
} // namespace cpu_timer
