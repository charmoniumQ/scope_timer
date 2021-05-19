#include "charmonium/scope_timer.hpp"

namespace ch_sc = charmonium::scope_timer;

void trace4() {
	// test time block
	{
		SCOPE_TIMER(.set_name("trace4"));
	}

	// test siblings with same name
	// test event
	SCOPE_TIMER();
}

void trace3() {
	SCOPE_TIMER();

	// test diamond stack
	trace4();
}

void trace2() {
	// test comment
	SCOPE_TIMER(.set_info(ch_sc::make_type_eraser<std::string>(std::string{"hello"})));
	std::thread th {[] {
		// test crossing thread boundary
		trace3();
	}};
	th.join();

	// test diamond stack
	trace4();
}
