#pragma once // NOLINT(llvm-header-guard)

#if defined (__unix__)

#include <unistd.h>
// #include <sys/types.h>

namespace cpu_timer {
namespace detail {

	using ProcessId = size_t;
	static ProcessId get_pid() {
		return ::getpid();
	}

	// using ThreadId = size_t;
	// static ThreadId get_tid() {
	// 	return gettid();
	// }

	static std::string tmp_path(std::string data) {
		return "/tmp/cpu_timer_" + data;
	}

} // namespace detail
} // namespace cpu_timer

#else

#error "os_specicic.hpp is not supported for your platform"

#endif
