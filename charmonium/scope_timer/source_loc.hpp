#pragma once // NOLINT(llvm-header-guard)
#include <iostream>

namespace charmonium::scope_timer::detail {

	class SourceLoc {
	public:
		explicit SourceLoc(
						   const char* function_name_,
						   const char* file_name_,
						   size_t line_
		)
			: function_name{function_name_}
			, file_name{file_name_}
			, line{line_}
		{ }

		explicit SourceLoc()
			: SourceLoc{"", "", 0}
		{ }

		const char* get_function_name() const { return function_name; }
		const char* get_file_name() const { return file_name; }
		size_t get_line() const { return line; }
		operator bool() const { return function_name || file_name || line; }

	private:
		const char* function_name;
		const char* file_name;
		size_t line;
	};

	CHARMONIUM_SCOPE_TIMER_UNUSED static std::ostream& operator<<(std::ostream& os, const SourceLoc& source_loc) {
		return os
			<< source_loc.get_file_name()
			<< ':'
			<< source_loc.get_line()
			<< ':'
			<< source_loc.get_function_name();
	}

}; // namespace charmonium::scope_timer::detail

#define CHARMONIUM_SCOPE_TIMER_SOURCE_LOC() \
	(charmonium::scope_timer::detail::SourceLoc {__func__, __FILE__, __LINE__})
#define CHARMONIUM_SCOPE_TIMER_UNIQUE_NAME() \
	CHARMONIUM_SCOPE_TIMER_TOKENPASTE(__scope_timer__, __LINE__)
