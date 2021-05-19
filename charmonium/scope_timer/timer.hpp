#pragma once // NOLINT(llvm-header-guard)
#include "clock.hpp"
#include "type_eraser.hpp"
#include "source_loc.hpp"
#include "util.hpp"
#include <deque>
#include <cassert>

namespace charmonium::scope_timer::detail {

	static constexpr bool use_fences = true;

	using IndexNo = size_t;

	class Thread;

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

	CHARMONIUM_SCOPE_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const Timer& frame) {
		return os
			<< "frame[" << frame.get_index() << "] = "
			<< frame.get_source_loc()
			<< " called by frame[" << frame.get_caller_index() << "]"
			;
	}

	using Timers = std::deque<Timer>;
} // namespace charmonium::scope_timer::detail
