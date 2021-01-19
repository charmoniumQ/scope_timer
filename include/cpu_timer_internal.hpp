#include "util.hpp"
#include <deque>
#include <list>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cpu_timer {

	using ThreadId = size_t;

	using FrameId = size_t;

	using FunctionId = size_t;

	/**
	 * @brief Timing and runtime data relating to one stack-frame.
	 */
	class StackFrame {
	private:
		std::string comment;
		FrameId frame_id;
		FunctionId function_id;
		const StackFrame* caller;
		bool _is_started;
		bool _is_stopped;
		myclock::CpuTime start_cpu;
		myclock::CpuTime stop_cpu;
		myclock::WallTime start_wall;
		myclock::WallTime stop_wall;
	public:
		StackFrame(std::string&& comment_, FrameId frame_id_, FunctionId function_id_, const StackFrame* caller_)
			: comment{std::move(comment_)}
			, frame_id{frame_id_}
			, function_id{function_id_}
			, caller{caller_}
			, _is_started{false}
			, _is_stopped{false}
			, start_cpu{}
			, stop_cpu{}
			, start_wall{}
			, stop_wall{}
		{ }
		void start_timer() {
			assert(!_is_started);
			assert(!_is_stopped);
			_is_started = true;

			// very last thing:
			util::fence();
			start_cpu = myclock::cpu_now();
			start_wall = myclock::wall_now();
			util::fence();
		}
		void stop_timer() {
			// very first thing:
			util::fence();
			stop_cpu = myclock::cpu_now();
			stop_wall = myclock::wall_now();
			util::fence();

			assert(_is_started);
			assert(!_is_stopped);
			_is_stopped = true;
		}
		bool is_timed() const { return _is_started && _is_stopped; }
		FunctionId get_function_id() const { return function_id; }
		void serialize(std::ostream& os, ThreadId thread_id, myclock::WallTime process_start, const char* function_name) const {
			// Emitting [start, (start-stop)] is fewer bytes in CSV than [start, stop].
			// CpuTime already begins at 0, but WallTime gets subtracted from the process start.
			// This makes the time take fewer bytes in CSV, and it makes numbers comparable between runs.
			os
				<< thread_id << ","
				<< frame_id << ","
				<< function_id << ","
				<< (caller != nullptr ? caller->frame_id : 0) << ","
				<< myclock::get_nanoseconds(start_cpu) << ","
				<< myclock::get_nanoseconds(stop_cpu - start_cpu) << ","
				<< myclock::get_nanoseconds(start_wall - process_start) << ","
				<< myclock::get_nanoseconds(stop_wall - start_wall) << ","
				<< function_name << ","
				<< comment << "\n";
		}
	};

	/**
	 * @brief Runtime data for the current function stack.
	 */
	class Stack {
	private:
		ThreadId thread_id;
		FrameId first_unused_frame_id;
		std::deque<StackFrame> stack;
		std::deque<StackFrame> finished;
		std::unordered_map<const char*, size_t> function_name_to_id;
		std::vector<const char*> function_id_to_name;

		std::mutex mutex;

	public:
		explicit Stack(ThreadId thread_id_)
			: thread_id{thread_id_}
			, first_unused_frame_id{0}
		{
			// Reserve frame_id and function_id for thread_caller
			// first_unused_frame_id++;
			// lookup(function_name_to_id, function_id_to_name, thread_caller);
			// Reserve frame_id and function_id for thread_start
			first_unused_frame_id++;
			util::lookup(function_name_to_id, function_id_to_name, "thread_main");
		}
		void enter_stack_frame(const char* function_name, std::string&& comment) {
			std::lock_guard<std::mutex> lock{mutex};

			stack.emplace_back(
				std::move(comment),
				first_unused_frame_id++,
				util::lookup(function_name_to_id, function_id_to_name, function_name),
				(stack.empty() ? nullptr : &stack.back()
			));

			// very last:
			stack.back().start_timer();
		}
		void exit_stack_frame([[maybe_unused]] const char* function_name) {
			std::lock_guard<std::mutex> lock{mutex};
			assert(!stack.empty());

			// (almost) very first:
			stack.back().stop_timer();

			assert(function_name == function_id_to_name.at(stack.back().get_function_id()));
			finished.emplace_back(stack.back());
			stack.pop_back();
		}
		void serialize(std::ostream& os, myclock::WallTime process_start) {
			std::lock_guard<std::mutex> lock{mutex};

			std::vector<bool> function_id_seen (function_id_to_name.size(), false);
			for (const StackFrame& stack_frame : finished) {
				assert(stack_frame.is_timed());
				size_t function_id = stack_frame.get_function_id();
				assert(function_id == function_name_to_id[function_id_to_name[function_id]]);
				if (function_id_seen[function_id]) {
					stack_frame.serialize(os, thread_id, process_start, "");
				} else {
					stack_frame.serialize(os, thread_id, process_start, function_id_to_name[function_id]);
					function_id_seen[function_id] = true;
				}
			}
			finished.clear();
		}
		ThreadId get_thread_id() const { return thread_id; }

		// StacKFrame.caller points into Stack.stack, so the Stack should not be copied.
		Stack(const Stack&) = delete;
		Stack& operator=(const Stack&) = delete;
	};

	/**
	 * @brief almost-process-level data (collection of Stacks)
	 *
	 * This class holds holds stacks, and it dumps them when it gets
	 * destructed.
	 *
	 * This is not necessarily process-level, if you have compiled and
	 * linked multiple object-files. Each object-file will have its
	 * own context.
	 */
	class Process {
	private:
		myclock::WallTime start_time;
		std::list<Stack> threads;
		std::mutex proc_mutex;
	public:
		explicit Process(myclock::WallTime start_time_)
			: start_time{start_time_}
		{ }
		myclock::WallTime get_start_time() { return start_time; }
		Stack* make_stack() {
			std::lock_guard<std::mutex> proc_lock {proc_mutex};
			ThreadId this_thread_id = threads.size();
			threads.emplace_back(this_thread_id);
			return &threads.back();
		}
		void serialize(std::ostream& os) {
			std::lock_guard<std::mutex> proc_lock {proc_mutex};
			for (Stack& thread : threads) {
				thread.serialize(os, start_time);
			}
		}
		// make_stack() returns pointers into this.
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
	};


	/**
	 * @brief An RAII context for creating, stopping, and storing StackFrames.
	 */
	class StackFrameContext {
	private:
		Stack& thread;
		const char* site_name;
	public:
		StackFrameContext(const StackFrameContext&) = delete;
		StackFrameContext& operator=(const StackFrameContext&) = delete;
		StackFrameContext(Stack& thread_, const char* site_name_, std::string&& comment)
			: thread{thread_}
			, site_name{site_name_}
		{
			thread.enter_stack_frame(site_name, std::move(comment));
		}
		~StackFrameContext() {
			thread.exit_stack_frame(site_name);
		}
	};
} // namespace cpu_timer
