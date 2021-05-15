// Include scope_timer from any file in your project.
// Everything is inline, so no link-time duplicates.
#include "include/scope_timer.hpp"

// Define a Callback.
// ScopeTimer sends you timing data through this.
class Callback : public scope_timer::CallbackType {
public:
    void thread_start(scope_timer::Thread&) override;
    void thread_in_situ(scope_timer::Thread&) override;
    void thread_stop(scope_timer::Thread&) override;
};

void foo();

int main() {
    // Configure the timer at a process-level
    // Only ONE thread should do this.
    auto& proc = scope_timer::get_process();
    proc.emplace_callback<Callback>();

    // callback_once means all timers get sent at program termination.
    // This impacts performance the least.
    proc.callback_once();

    // callback_every is the opposite; it sends every timer as soon as it is finished.
    // This could impact performance if the callback is expensiv.e
    //proc.callback_every()

    // set_callback_period sends completed timers if they are older than 10ns.
    // This is a compromise between callback_once and callback_every.
    //proc.set_callback_period(std::chrono::nanoseconds{10});

    // Enable the timer. While disabled, the tiemr overhead is very small.
    // Note that this only effects scope timers that *haven't* started yet.
    proc.set_enabled(true);

    // Execute your program normally
    foo();
}

void foo() {
    // For functions that you want time, use this macro.
    // It uses RAII to start a timer at this line, and stop it when the variable goes out of scope, like std::lock_guard.
    SCOPE_TIMER();

    // If you want to time parts of the function, put SCOPE_TIMER in curlies.
    // You can reuse the same curly braces from control-flow such as if, while, and for.
    {
        SCOPE_TIMER();
    }

    // Extra options can be provided within parentheses.
    // See `scope_timer_internal.hpp:ScopeTimerArgs`.
    SCOPE_TIMER(.set_name("foo"));

    // You can attach arbitrary information to a frame using `info` of type `TypeEraser`,
    auto info = std::vector<std::string>{"hello", "world"};
    auto type_erased_info = scope_timer::make_type_eraser<std::vector<std::string>>(info);

    SCOPE_TIMER(
        .set_name("foo")
        .set_info(std::move(type_erased_info))
    );
}

void Callback::thread_start(scope_timer::Thread& thread) {
    // See `scope_internal_timer.hpp:Thread`
    std::cout << thread.get_id() << thread.get_native_handle() << std::endl;
}
void Callback::thread_in_situ(scope_timer::Thread& thread) {
    // `process.callback_once()` says to never call in_situ.
    // `process.callback_every()` says to call in_situ every time a timer finishes.
    // `process.set_callback_period(std::chrono::nanoseconds{1000})` says to call in_situ in batches of 1000ns.

    // If you call `thread.drain_finished()`, you get the finished timers, and they are removed from the `Thread`.
    // If you don't, finished timers remain in the `Thread`, and you can access them the _next_ time you call `thread.drain_finished()`.

    for (const auto& timer : thread.drain_finished()) {
        // See `scope_internal_timer.hpp:Timer`.

		// We have both wall time and CPU time of the timer.


        // Note that these `Timer`s form a tree.
        // This tree can be navigated by `index`.
        // `caller_index` points "up" (to the parent).
        //     For the root of the tree, `root.caller_index == root.index`.
        // `prev_index` points "left" (older sibling).
        //     For the oldest sibling, `frame.prev_index == 0`.
        // `youngest_child_index` points "down" (to the youngest child).
        //     For leaf nodes, `leaf.youngest_child == 0`.

        auto type_erased_info = timer.get_info();
        if (type_erased_info) {
            [[maybe_unused]] auto& info = scope_timer::extract_type_eraser<std::vector<std::string>>(type_erased_info);
        }
    }
}
void Callback::thread_stop(scope_timer::Thread& thread) {
    for ([[maybe_unused]] const auto& timer : thread.drain_finished()) {
        // Similar to `Callback::thread_in_situ`, but called (unconditionally) when threads stop.
    }
}
