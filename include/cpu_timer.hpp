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
 *
 *
 * TODO:
 * - Add record for thread_caller.
 * - Log process start somewhere (in thread_caller)?
 * - Remove the mutex in ThreadContext.
 * - Make util headers not exported.
 */

#include "util.hpp"
#include "clock.hpp"
#include "cpu_timer_internal.hpp"
#include "filesystem.hpp"
#include <iostream>
#include <fstream>

namespace cpu_timer {
	/*
	 * These global variables should ONLY be used by the macros.
	 *
	 * The rest of the code should not depend on global state.
	 */

	class GlobalState {
	private:
		bool is_enabled_var;
		filesystem::path output_dir;
		Process process;
		thread_local static Stack* current_thread;
		static myclock::WallTime set_or_lookup_start_time([[maybe_unused]] const filesystem::path& output_dir) {
			// std::ifstream start_time_ifile {start_time_path.string()};
			// size_t start_time_int;
			// start_time_ifile >> start_time_int;
			// return time_point{seconds{start_time_int}};
			return std::chrono::nanoseconds{0};
		}
		static bool initialize_is_enabled() {
			if (!!std::stoi(util::getenv_or("CPU_TIMER_ENABLE", "0"))) {
#ifdef CPU_TIMER_DISABLE
				// TODO: find a library for colors.
				std::cerr <<
					"\e[0;31m[WARNING] "
					"You set CPU_TIMER_ENABLE=1 at runtime, but defined CPU_TIMER_DISABLE at compile-time. "
					"cpu_timer3 remains disabled.\e[0m\n"
					;
				return false;
#else
				return true;
#endif
			} else {
				return false;
			}
		}
	public:
		void initialize() {
			filesystem::remove_all(output_dir);
			filesystem::create_directory(output_dir);
			// fence();
			// std::ofstream start_time_ofile {start_time_path.string()};
			// size_t start_time_int = std::chrono::duration_cast<std::chrono::seconds>(wall_now().time_since_epoch()).count();
			// start_time_ofile << start_time_int;
		}
		GlobalState()
			: is_enabled_var{initialize_is_enabled()}
			  // TODO: disable the following constructor
			, output_dir{util::getenv_or("CPU_TIMER3_PATH", ".cpu_timer3")}
			, process{set_or_lookup_start_time(output_dir)}
		{ }
		~GlobalState() {
			if (is_enabled()) {
				serialize();
			}
		}
		filesystem::path serialize() {
			filesystem::path output_file_path {output_dir / util::random_hex_string() + std::string{"_data.csv"}};
			std::ofstream output_file {output_file_path.string()};
			assert(output_file.good());
			output_file
				<< "#{\"version\": \"3.2\", \"pandas_kwargs\": {\"dtype\": {\"comment\": \"str\"}, \"keep_default_na\": false, \"index_col\": [0, 1], \"comment\": \"#\"}}\n"
				<< "thread_id,frame_id,function_id,caller_frame_id,cpu_time_start,cpu_time,wall_time_start,wall_time,function_name,comment\n"
				;
			process.serialize(output_file);
			std::cerr << "Serialized to " << output_file_path.c_str() << "\n";
			return output_file_path;
		}
		Stack& get_current_stack() {
			return *(current_thread ? current_thread : (current_thread = make_stack()));
		}
		void set_current_stack(Stack& other) { current_thread = &other; }
		Stack* make_stack() { return process.make_stack(); }
		bool is_enabled() { return is_enabled_var; }
	};

	static GlobalState __state;
}
int foo() { return 532; }
