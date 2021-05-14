#pragma once // NOLINT(llvm-header-guard)
#include "clock.hpp"
#include "compiler_specific.hpp"
#include "type_eraser.hpp"
#include "source_loc.hpp"
#include "util.hpp"
#include <cassert>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scope_timer::detail {

	static constexpr bool use_fences = true;

	using IndexNo = size_t;

	class Timer;
	class Thread;
	class Process;
	class ScopeTimer;

	/**
	 * @brief Timing and runtime data relating to one stack-frame.
	 */
	class Timer {
	private:
		friend class Thread;

		WallTime process_start;
		const char* name;
		SourceLoc source_loc;

		// I don't want to use Timer* for pointers to other frames,
		// because they can be moved aroudn in memory (e.g. from stack to finished),
		// and pointers wouldn't work after serialization anyway.
		IndexNo index;
		IndexNo caller_index;
		IndexNo prev_index;
		WallTime start_wall;
		CpuTime start_cpu;
		WallTime stop_wall;
		CpuTime stop_cpu;
		TypeEraser info;

		IndexNo youngest_child_index;

		void start_timers() {
			assert(start_cpu == CpuTime{0} && "timer already started");

			// very last thing:
			if (use_fences) { fence(); }
			start_wall = wall_now();
			start_cpu = cpu_now();
			if (use_fences) { fence(); }
		}
		void stop_timers() {
			assert(stop_cpu == CpuTime{0} && "timer already started");
			// almost very first thing:
			if (use_fences) { fence(); }
			stop_wall = wall_now();
			stop_cpu = cpu_now();
			if (use_fences) { fence(); }

			assert(start_cpu != CpuTime{0} && "timer never started");
		}

		void start_and_stop_timers(bool wall_time, bool cpu_time) {
			if (use_fences) { fence(); }
			if (wall_time) {
				assert(start_wall == WallTime{0}  && "timer already started");
				assert(start_wall == WallTime{0}  && "timer already stopped" );
				start_wall = stop_wall = wall_now();
			}
			if (cpu_time) {
				start_cpu  = stop_cpu  = cpu_now ();
			}
			if (use_fences) { fence(); }
		}

	public:
		Timer(
			WallTime process_start_,
			const char* name_,
			SourceLoc&& source_loc_,
			IndexNo index_,
			IndexNo caller_index_,
			IndexNo prev_index_,
			TypeEraser&& info_
		)
			: process_start{process_start_}
			, name{name_}
			, source_loc{std::move(source_loc_)}
			, index{index_}
			, caller_index{caller_index_}
			, prev_index{prev_index_}
			, start_wall{0}
			, start_cpu{0}
			, stop_wall{0}
			, stop_cpu{0}
			, info{std::move(info_)}
			, youngest_child_index{0}
		{ }

		/**
		 * @brief User-specified meaning.
		 */
		const TypeEraser& get_info() const { return info; }
		TypeEraser& get_info() { return info; }

		const char* get_name() const { return name; }

		const SourceLoc& get_source_loc() const { return source_loc; }

		/**
		 * @brief The index of the "parent" Timer (the Timer which called this one).
		 *
		 * The top of the stack is a loop.
		 */
		IndexNo get_caller_index() const { return caller_index; }

		/**
		 * @brief The index of the "older sibling" Timer (the previous Timer with the same caller).
		 *
		 * 0 if this is the eldest child.
		 */
		IndexNo get_prev_index() const { return prev_index; }

		bool has_prev() const { return prev_index != 0; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_stop_wall() const { return stop_wall == WallTime{0} ? WallTime{0} : stop_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_start_cpu() const { return start_cpu; }

		/**
		 * @brief An index (0..n) according to the order that Timers started (AKA pre-order).
		 */
		IndexNo get_index() const { return index; }

		/**
		 * @brief See documentation of return type.
		 */
		WallTime get_start_wall() const { return start_wall == WallTime{0} ? WallTime{0} : start_wall - process_start; }

		/**
		 * @brief See documentation of return type.
		 */
		CpuTime get_stop_cpu() const { return stop_cpu; }

		/**
		 * @brief The index of the youngest child (the last direct callee of this frame).
		 */
		IndexNo get_youngest_callee_index() const { return youngest_child_index; }

		/**
		 * @brief If this Timer calls no other frames
		 */
		bool is_leaf() const { return youngest_child_index == 0; }
	};

	SCOPE_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const Timer& frame) {
		return os
			<< "frame[" << frame.get_index() << "] = "
			<< frame.get_source_loc()
			<< " called by frame[" << frame.get_caller_index() << "]"
			;
	}

	using Timers = std::deque<Timer>;

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

			if (SCOPE_TIMER_LIKELY(!stack.empty())) {
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

	/**
	 * @brief All threads in the current process.
	 *
	 * This calls callback with one thread's batches of Frames, periodically not sooner than callback_period, in the thread whose functions are in the batch.
	 */
	class Process {
	private:
		friend class Thread;
		friend class ScopeTimer;

		// std::atomic<bool> enabled;
		bool enabled {false};
		// std::mutex config_mutex;
		const WallTime start;
		CpuTime callback_period {0}; // locked by config_mutex
		std::unique_ptr<CallbackType> callback; // locked by config_mutex
		// Actually, I don't need to lock the config
		// If two threads race to modify the config, the "winner" is already non-deterministic
		// The callers should synchronize themselves.
		// If this thread writes while someone else reads, there is no guarantee they hadn't "already read" the values.
		// The caller should synchronize with the readers.
		std::unordered_map<std::thread::id, Thread> threads; // locked by threads_mutex
		std::unordered_map<std::thread::id, size_t> thread_use_count; // locked by threads_mutex
		mutable std::recursive_mutex threads_mutex;

	public:

		explicit Process()
			: start{wall_now()}
			, callback{new CallbackType}
		{ }

		WallTime get_start() { return start; }

		/**
		 * @brief Create or get the thread.
		 *
		 * For efficiency, the caller should cache this in thread_local storage.
		 */
		Thread& create_thread(
				std::thread::id thread,
				std::thread::native_handle_type native_handle,
				std::string&& thread_name
		) {
			std::lock_guard<std::recursive_mutex> threads_lock {threads_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			// Could have also been set up by the caller.
			if (threads.count(thread) == 0) {
				threads.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(thread),
					std::forward_as_tuple(*this, thread, native_handle, std::move(thread_name))
				);
			}
			thread_use_count[thread]++;
			return threads.at(thread);
		}

		/**
		 * @brief Call when a thread is disposed.
		 *
		 * This is neccessary because the OS can reuse old thread IDs.
		 */
		void delete_thread(std::thread::id thread) {
			// std::cerr << "Process::delete_thread: " << thread << std::endl;
			std::lock_guard<std::recursive_mutex> threads_lock {threads_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			if (threads.count(thread) != 0) {
				thread_use_count[thread]--;
				if (thread_use_count.at(thread) == 0) {
					threads.erase(thread);
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
		 * @brief Sets @p callback_period for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback_period(CpuTime callback_period_) {
			// std::lock_guard<std::recursive_mutex> config_lock {config_mutex};
			callback_period = callback_period_;
		}

		/**
		 * @brief Calls callback after every frame.
		 *
		 * This is usually too inefficient.
		 */
		void callback_every_frame() { set_callback_period(CpuTime{1}); }

		/**
		 * @brief Call callback in destructor.
		 *
		 * This is the most efficient, putting the entire lifetime of
		 * each thread into one batch and calling the callback.
		 */
		void callback_once() { set_callback_period(CpuTime{0}); }

		bool is_enabled() const {
			return enabled;
		}

		/**
		 * @brief Sets @p callback for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback(std::unique_ptr<CallbackType>&& callback_) {
			// std::lock_guard<std::mutex> config_lock {config_mutex};
			callback = std::move(callback_);
		}

		CallbackType& get_callback() {
			return *callback;
		}


		// get_thread() returns pointers into this, so it should not be copied or moved.
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
		Process(const Process&&) = delete;
		Process& operator=(const Process&&) = delete;
		~Process() {
			// std::cout << "Process::~Process" << std::endl;
			for (const auto& pair : threads) {
				std::cerr << pair.first << " is still around. Going to kick their logs out.\n";
			}
		}
	};

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

	inline CallbackType& Thread::get_callback() const { return *process.callback; }

	inline CpuTime Thread::get_callback_period() const { return process.callback_period; }

	inline WallTime Thread::get_process_start() const { return process.start; }

	// TODO(grayson5): Figure out which threads the callback could be called from.
	// thread_in_situ will always be called from the target thread.
	// I believe thread_local ThreadContainer call create_thread and delete_thread, so I think they will always be called from the target thread.
	// But the main thread might get deleted by the destructor of its containing map, in the main thread.

} // namespace scope_timer::detail
