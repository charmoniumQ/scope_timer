#include "gtest/gtest.h"
#include "include/cpu_timer.hpp"
#include <algorithm>
#include <deque>
#include <mutex>
#include <ostream>
#include <unordered_map>

using Frame = cpu_timer::StackFrame;
using Frames = std::deque<cpu_timer::StackFrame>;

void trace1() {
	CPU_TIMER_TIME_FUNCTION();
	// test crossing object-file boundary
	void trace2();
	trace2();
}

void verify_thread_main(const Frames& trace, const Frame& frame) {
	EXPECT_EQ(0, frame.get_caller_start_index());
	EXPECT_EQ(0, frame.get_start_index());
	EXPECT_EQ(trace.size() - 1, frame.get_stop_index());
	EXPECT_EQ(std::string{"thread_main"}, frame.get_function_name());
}

void verify_tree(const Frames&) {
	// TODO(sam):
}

void verify_preorder(const Frames& trace) {
	// trace should already be in preorder
	auto preorder_trace = Frames{trace.cbegin(), trace.cend()};
	std::sort(preorder_trace.begin(), preorder_trace.end(), [](const Frame& f1, const Frame& f2) {
		return f1.get_start_index() < f2.get_start_index();
	});
	verify_thread_main(preorder_trace, preorder_trace.at(0));
	for (size_t i = 0; i < preorder_trace.size(); ++i) {
		auto frame = preorder_trace.at(i);
		// All `start_index`es from 0..n should be used, so the sort should be "dense"
		EXPECT_EQ(frame.get_start_index(), i);

		if (i > 0) {
			auto prev_frame = preorder_trace.at(i-1);
			// In preorder, prior frames started earlier
			EXPECT_LT(prev_frame.get_start_cpu (), frame.get_start_cpu ());
			EXPECT_LT(prev_frame.get_start_wall(), frame.get_start_wall());

			// Check if sorted
			EXPECT_EQ(prev_frame.get_start_index(), frame.get_start_index() - 1);

			// Our caller must have started before us, except for thread_main, which is a loop
			EXPECT_LT(frame.get_caller_start_index(), frame.get_start_index());
		}
	}
}

void verify_postorder(const Frames& postorder_trace) {
	// auto postorder_trace = Frames{trace.cbegin(), trace.cend()};
	// std::sort(postorder_trace.begin(), postorder_trace.end(), [](const Frame& f1, const Frame& f2) {
	// 	return f1.get_stop_index() < f2.get_stop_index();
	// });
	verify_thread_main(postorder_trace, postorder_trace.at(postorder_trace.size() - 1));
	for (size_t i = 0; i < postorder_trace.size(); ++i) {
		auto frame = postorder_trace.at(i);

		// All `start_index`es from 0..n should be used, so the sort should eb "dense"
		EXPECT_EQ(frame.get_stop_index(), i);

		if (i > 0) {
			auto prev_frame = postorder_trace.at(i-1);

			// In postorder, prior frames finished earlier
			EXPECT_LT(prev_frame.get_stop_cpu (), frame.get_stop_cpu ());
			EXPECT_LT(prev_frame.get_stop_wall(), frame.get_stop_wall());

			// Check if sorted
			EXPECT_EQ(prev_frame.get_stop_index(), frame.get_stop_index() - 1);
		}
	}
}

void verify_trace1(const Frames& trace) {
	EXPECT_NE(trace.at(0).get_line(), trace.at(1).get_line());
	EXPECT_EQ(std::string{"trace4"}, trace.at(0).get_function_name());
	EXPECT_EQ(2                    , trace.at(0).get_caller_start_index());
	EXPECT_EQ(std::string{"trace4"}, trace.at(1).get_function_name());
	EXPECT_EQ(2                    , trace.at(1).get_caller_start_index());
	EXPECT_EQ(std::string{"trace2"}, trace.at(2).get_function_name());
	EXPECT_EQ(std::string{"hello"} , trace.at(2).get_comment());
	EXPECT_EQ(1                    , trace.at(2).get_caller_start_index());
	EXPECT_EQ(std::string{"trace1"}, trace.at(3).get_function_name());
	EXPECT_EQ(0                    , trace.at(3).get_caller_start_index());
	EXPECT_EQ(std::string{"thread_main"}, trace.at(4).get_function_name());
	EXPECT_EQ(0                    , trace.at(4).get_caller_start_index());
}

void verify_trace3(Frames trace) {
	EXPECT_NE(trace.at(0).get_line(), trace.at(1).get_line());
	EXPECT_EQ(std::string{"trace4"}, trace.at(0).get_function_name());
	EXPECT_EQ(1                    , trace.at(0).get_caller_start_index());
	EXPECT_EQ(std::string{"trace4"}, trace.at(1).get_function_name());
	EXPECT_EQ(1                    , trace.at(1).get_caller_start_index());
	EXPECT_EQ(std::string{"trace3"}, trace.at(2).get_function_name());
	EXPECT_EQ(0                    , trace.at(2).get_caller_start_index());
	EXPECT_EQ(std::string{"thread_main"}, trace.at(3).get_function_name());
	EXPECT_EQ(0                    , trace.at(3).get_caller_start_index());
}

void verify_general(const Frames& trace) {
	verify_tree(trace);
	verify_preorder (trace);
	verify_postorder(trace);
}

void err_callback(std::thread::id, Frames&&, const Frames&) {
	ADD_FAILURE();
}

class Globals {
public:
	Globals() {
		cpu_timer::make_process(true, cpu_timer::CpuTime{0}, &err_callback);
	}
	~Globals() {
		cpu_timer::get_process().set_callback(cpu_timer::CallbackType{});
	}
};

static Globals globals;

void verify_any_trace(const Frames& trace) {
	verify_general(trace);
	if (trace.at(trace.size() - 2).get_function_name() == std::string{"trace1"}) {
		verify_trace1(trace);
	} else {
		verify_trace3(trace);
	}
}


// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cert-err58-cpp)
TEST(CpuTimerTest, TraceCorrectBatched) {
	cpu_timer::get_process().set_callback([=](std::thread::id, Frames&& finished, const Frames& stack) {
		EXPECT_EQ(0, stack.size());
		verify_any_trace(finished);
	});
	std::thread th {trace1};
	th.join();
	cpu_timer::get_process().flush();
	cpu_timer::get_process().set_callback(&err_callback);
}

TEST(CpuTimerTest, TraceCorrectUnbatched) {
	std::mutex mutex;
	std::unordered_map<std::thread::id, size_t> count;
	std::unordered_map<std::thread::id, Frames> accumulated;
	cpu_timer::get_process().set_log_period(cpu_timer::CpuTime{1});
	cpu_timer::get_process().set_callback([&](std::thread::id thread, Frames&& finished, const Frames&) {
		std::lock_guard<std::mutex> lock{mutex};
		EXPECT_EQ(finished.size(), 1);
		count[thread]++;
		accumulated[thread].insert(accumulated[thread].end(), finished.cbegin(), finished.cend());
	});
	std::thread th {trace1};
	th.join();
	cpu_timer::get_process().set_callback(&err_callback);

	for (const auto& pair : accumulated) {
		verify_any_trace(pair.second);
		if (pair.second.at(pair.second.size() - 2).get_function_name() == std::string{"trace1"}) {
			EXPECT_EQ(count.at(pair.first), 5);
		} else {
			EXPECT_EQ(count.at(pair.first), 4);
		}
	}
}
