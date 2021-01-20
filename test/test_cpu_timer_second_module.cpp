#include "include/cpu_timer.hpp"

void trace4() {
	// test time block
	{
		CPU_TIMER_TIME_BLOCK("trace4");
	}
	// test siblings with same name
	{
		CPU_TIMER_TIME_BLOCK("trace4");
	}
}

void trace3() {
	CPU_TIMER_TIME_FUNCTION();

	// test diamond stack
	trace4();
}

void trace2() {
	// test comment
	CPU_TIMER_TIME_FUNCTION_COMMENT(std::string{"hello"});
	std::thread th {[] {
		// test crossing thread boundary
		trace3();
	}};
	th.join();

	// test diamond stack
	trace4();
}
