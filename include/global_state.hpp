#pragma once // NOLINT(llvm-header-guard)
#include "branch_prediction.hpp"
#include "cpu_timer_internal.hpp"
#include "os_specific.hpp"
#include "util.hpp"
#include <memory>
#include <fstream>
#include <string>
#include <thread>

namespace cpu_timer {
namespace detail {

	static std::shared_ptr<Process> process;

	static thread_local Stack* stack = nullptr;

	static std::string get_filename() {
		return tmp_path(std::to_string(get_pid()));
	}

	static std::shared_ptr<Process> lookup_process() {
		std::ifstream infile {get_filename()};
		if (infile.good()) {
			intptr_t intptr = 0;
			infile >> intptr;
			assert(intptr);
			return *reinterpret_cast<std::shared_ptr<Process>*>(intptr); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		}
		error("Must call make_process(...) in the main thread before any other cpu_timer actions.");
		return std::shared_ptr<Process>();
	}

	static void make_process(CpuTime log_period, CallbackType callback) {
		bool is_enabled = std::stoi(getenv_or("CPU_TIMER_DISABLE", "0")) != 0;
		process = std::make_shared<Process>(is_enabled, log_period, callback);
		std::ofstream outfile {get_filename()};
		outfile << reinterpret_cast<intptr_t>(&process); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	static Process& get_process() {
		if (bool_unlikely(!process)) {
			process = lookup_process();
		}
		return *process;
	}

	static Stack& get_stack() {
		if (bool_unlikely(!stack)) {
			stack = &get_process().get_stack(std::this_thread::get_id());
		}
		return *stack;
	}

} // namespace detail
} // namespace cpu_timer
