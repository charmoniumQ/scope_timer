#include "gtest/gtest.h"
#include "charmonium/scope_timer.hpp"
#include <algorithm>
#include <deque>
#include <list>
#include <mutex>
#include <ostream>
#include <unordered_map>
#include <unordered_set>

namespace ch_sc = charmonium::scope_timer;

void trace1() {
	SCOPE_TIMER();
	// test crossing object-file boundary
	void trace2();
	trace2();
}

void verify_thread_main(const ch_sc::Timers&, const ch_sc::Timer& frame) {
	EXPECT_EQ(0, frame.get_caller_index()) << "Caller of thread_main should be 0";
	EXPECT_EQ(0, frame.get_index()) << "Index of thread_main should be 0";
	EXPECT_EQ(std::string{""}, frame.get_source_loc().get_function_name()) << "Name of thread_main should be empty";
}

void verify_preorder(const ch_sc::Timers& trace) {
	// trace should already be in preorder
	auto preorder_trace = ch_sc::Timers{trace.cbegin(), trace.cend()};
	std::sort(preorder_trace.begin(), preorder_trace.end(), [](const ch_sc::Timer& f1, const ch_sc::Timer& f2) {
		return f1.get_index() < f2.get_index();
	});

	verify_thread_main(preorder_trace, preorder_trace.at(0));
	for (size_t i = 0; i < preorder_trace.size(); ++i) {
		auto& frame = preorder_trace.at(i);
		EXPECT_EQ(frame.get_index(), i) << "All `index`es from 0..n should be used exactly once";

		if (!frame.is_leaf()) {
			size_t child_index = frame.get_youngest_callee_index();
			const ch_sc::Timer* child = nullptr;
			do {
				child =  &preorder_trace.at(child_index);
				EXPECT_EQ(child->get_caller_index(), frame.get_index()) << "youngest_callee and siblings refer to same parent";
				child_index = child->get_prev_index();
			} while (child->has_prev());
		}

		EXPECT_LE(frame.get_start_cpu(), frame.get_stop_cpu()) << "Frame starts before stop";
		EXPECT_LE(frame.get_start_wall(), frame.get_stop_wall()) << "Frame starts before stop";

		if (i > 0) {
			auto& prev_frame = preorder_trace.at(i-1);

			if (frame.get_start_cpu() != ch_sc::CpuNs{0}) {
				EXPECT_LT(prev_frame.get_start_cpu (), frame.get_start_cpu ()) << "In preorder, prior frames should have started earlier";
			}
			if (frame.get_start_wall() != ch_sc::WallNs{0}) {
				EXPECT_LT(prev_frame.get_start_wall(), frame.get_start_wall()) << "In preorder, prior frames should have started earlier";
			}

			EXPECT_LT(frame.get_caller_index(), frame.get_index()) << "Caller of this frame should have started before this frame";
			// This also proves that the trace digraph is a tree rooted at the frames[0] record.
			// Assume for induction i > 0 and frames[0:i] are reachable from the frames[0] record.
			// If frames[i]'s parent is one of frames[0:i], then frames[i] is also reachable from the 0th record, the inductive step.
			// Base case is that frames[0] is in the tree rooted at frames[0] (tautology).

			// This asserts that this frame is on the linked-list pointed to by its parent.
			size_t sibling_index = preorder_trace.at(frame.get_caller_index()).get_youngest_callee_index();
			const ch_sc::Timer* sibling = nullptr;
			bool found = false;
			do {
				sibling = &preorder_trace.at(sibling_index);
				if (sibling == &frame) {
					found = true;
					break;
				}
				sibling_index = sibling->get_prev_index();
			} while (sibling->has_prev());
			EXPECT_TRUE(found) << "Should be a sibling of the parent's youngest child";
		}
	}
}

void verify_postorder(const ch_sc::Timers& postorder_trace) {
	// auto postorder_trace = ch_sc::Timers{trace.cbegin(), trace.cend()};
	// std::sort(postorder_trace.begin(), postorder_trace.end(), [](const Timer& f1, const Timer& f2) {
	// 	return f1.get_stop_index() < f2.get_stop_index();
	// });
	verify_thread_main(postorder_trace, postorder_trace.at(postorder_trace.size() - 1));
	for (size_t i = 0; i < postorder_trace.size(); ++i) {
		auto frame = postorder_trace.at(i);

		if (i > 0) {
			auto prev_frame = postorder_trace.at(i-1);

			if (frame.get_stop_cpu() != ch_sc::CpuNs{0}) {
				EXPECT_LT(prev_frame.get_stop_cpu (), frame.get_stop_cpu ()) << "In postorder, prior frames finished earlier";
			}
			if (frame.get_stop_wall() != ch_sc::WallNs{0}) {
				EXPECT_LT(prev_frame.get_stop_wall(), frame.get_stop_wall()) << "In postorder, prior frames finished earlier";
			}
		}
	}
}

void verify_general(const ch_sc::Timers& trace) {
	verify_preorder (trace);
	verify_postorder(trace);
}

void verify_trace1(const ch_sc::Timers& trace) {
	EXPECT_NE(trace.at(0).get_source_loc().get_line(), trace.at(1).get_source_loc().get_line());
	EXPECT_STREQ("trace4", trace.at(0).get_source_loc().get_function_name());
	EXPECT_EQ(2          , trace.at(0).get_caller_index());
	EXPECT_STREQ("trace4", trace.at(1).get_source_loc().get_function_name());
	EXPECT_EQ(2          , trace.at(1).get_caller_index());
	EXPECT_STREQ("trace2", trace.at(2).get_source_loc().get_function_name());
	EXPECT_STREQ("hello" , ch_sc::extract_type_eraser<std::string>(trace.at(2).get_info()).c_str());
	EXPECT_EQ(1          , trace.at(2).get_caller_index());
	EXPECT_STREQ("trace1", trace.at(3).get_source_loc().get_function_name());
	EXPECT_EQ(0          , trace.at(3).get_caller_index());
	EXPECT_EQ(""         , trace.at(4).get_source_loc().get_function_name());
	EXPECT_EQ(0          , trace.at(4).get_caller_index());
}

void verify_trace3(ch_sc::Timers trace) {
	EXPECT_NE(trace.at(0).get_source_loc().get_line(), trace.at(1).get_source_loc().get_line());
	EXPECT_STREQ("trace4", trace.at(0).get_source_loc().get_function_name());
	EXPECT_EQ(1          , trace.at(0).get_caller_index());
	EXPECT_STREQ("trace4", trace.at(1).get_source_loc().get_function_name());
	EXPECT_EQ(1          , trace.at(1).get_caller_index());
	EXPECT_STREQ("trace3", trace.at(2).get_source_loc().get_function_name());
	EXPECT_EQ(0          , trace.at(2).get_caller_index());
	EXPECT_EQ(""         , trace.at(3).get_source_loc().get_function_name());
	EXPECT_EQ(0          , trace.at(3).get_caller_index());
}

class ErrCallback : public ch_sc::CallbackType {
public:
	void thread_start(ch_sc::Thread&) override { }
	void thread_in_situ(ch_sc::Thread&) override { ADD_FAILURE(); }
	void thread_stop(ch_sc::Thread&) override { ADD_FAILURE(); }
};

void verify_trace_1_or_3(const ch_sc::Timers& trace) {
	if (trace.at(trace.size() - 2).get_source_loc().get_function_name() == std::string{"trace1"}) {
		verify_trace1(trace);
	} else {
		verify_trace3(trace);
	}
}

class StoreCallback : public ch_sc::CallbackType {
private:
	std::mutex mutex;
	std::unordered_set<std::thread::id> thread_starts;
	std::unordered_map<std::thread::id, std::list<ch_sc::Timers>> thread_in_situs;
	std::unordered_map<std::thread::id, ch_sc::Timers> thread_stops;
public:
	std::unordered_set<std::thread::id> threads() const { return thread_starts; }
	size_t num_thread_starts() const { return thread_starts.size(); }
	size_t num_thread_in_situs(std::thread::id id) const { return thread_in_situs.count(id) != 0 ? thread_in_situs.at(id).size() : 0; }
	size_t num_thread_stops(std::thread::id id) const { return thread_stops.at(id).size(); }
	void thread_start(ch_sc::Thread& stack) override {
		std::lock_guard<std::mutex> lock{mutex};
		thread_starts.insert(stack.get_id());
	}
	void thread_in_situ(ch_sc::Thread& stack) override {
		std::lock_guard<std::mutex> lock{mutex};
		EXPECT_EQ(thread_starts.count(stack.get_id()), 1);
		thread_in_situs[stack.get_id()].push_back(stack.drain_finished());
	}
	void thread_stop(ch_sc::Thread& stack) override {
		std::lock_guard<std::mutex> lock{mutex};
		EXPECT_EQ(thread_starts.count(stack.get_id()), 1);
		EXPECT_EQ(thread_stops.count(stack.get_id()), 0);
		thread_stops[stack.get_id()] = stack.drain_finished();
	}
	ch_sc::Timers get_all_frames(std::thread::id id) {
		std::lock_guard<std::mutex> lock{mutex};
		EXPECT_EQ(thread_starts.count(id), 1);
		EXPECT_EQ(thread_stops.count(id), 1);
		ch_sc::Timers all_frames;
		for (const auto& frames : thread_in_situs[id]) {
			all_frames.insert(all_frames.cend(), frames.cbegin(), frames.cend());
		}
		all_frames.insert(all_frames.cend(), thread_stops.at(id).cbegin(), thread_stops.at(id).cend());
		return all_frames;
	}
};


// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cert-err58-cpp,misc-unused-parameters)
TEST(CpuTimerTest, TraceCorrectness) {
	auto& proc = ch_sc::get_process();
	proc.set_callback(std::unique_ptr<ch_sc::CallbackType>{new ErrCallback});
	for (bool batched : {true, false}) {
		if (batched) {
			proc.callback_once();
		} else {
			proc.callback_every();
		}
		proc.set_callback(std::unique_ptr<ch_sc::CallbackType>{new StoreCallback});
		proc.set_enabled(true);
		std::thread th {trace1};
		th.join();
		auto& sc = proc.get_callback<StoreCallback>();
		EXPECT_EQ(sc.num_thread_starts(), 2);
		std::cout
			<< "{\n"
			<< "  \"batched\": " << (batched ? "true" : "false") << ",\n" ;
		for (const std::thread::id id : sc.threads()) {
			if (batched) {
				EXPECT_EQ(sc.num_thread_in_situs(id), 0) << "Batched implies no in situ calls";
				EXPECT_GT(sc.num_thread_stops(id), 1) << "Batched implies many frames at thread_stop";
			} else {
				EXPECT_GT(sc.num_thread_in_situs(id), 1) << "Unbatched implies many in situ calls";
				EXPECT_EQ(sc.num_thread_stops(id), 0) << "Unbatched implies we should be done before thread_stop";
			}
			auto frames = sc.get_all_frames(id);
			std::cout << "  \"thread_id\": " << id << ",\n";
			std::cout << "  \"frames\": [\n";
			for (const auto& frame : frames) {
				std::cout << "    \"" << frame << "\",\n";
			}
			std::cout
				<< "  ],\n"
				<< "}" << std::endl;
			verify_general(frames);
			verify_trace_1_or_3(frames);
		}
		proc.set_callback(std::unique_ptr<ch_sc::CallbackType>{new ErrCallback});
	}
}
