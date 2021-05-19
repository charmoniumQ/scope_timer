# `charmonium::scope_timer`

This library helps measure time at a "scope" granularity.

## Usage

Copy `charmonium` into your project's include directory.

For each block you want to time, write

```cpp
{
    // Time this scope
    SCOPE_TIMER();

   // Do comuptation
   // ...
}
```

All of the finished timers will get collected and sent to a callback, which
can be configured as such:

```cpp
class Callback : public scope_timer::CallbackType {
public:
    void thread_start(scope_timer::Thread&) override { }
    void thread_in_situ(scope_timer::Thread&) override { }
    void thread_stop(scope_timer::Thread&) override { }
};

int main() {
    auto& proc = scope_timer::get_process();
    proc.emplace_callback<Callback>();
    proc.set_enabled(true);

    // ...
    // Do program
}
```

Note that this callback _process-global_. You can include scope_timer in
dynamically and statically linked libraries, and they will all feed into the
same callback.

See [`./example/main.cpp`][3] for more example usage.

### Motivation

While perf exists, it is Linux-specific (doesn't even work in Docker) and it
can't record dynamic information.

Recording dynamic information is important when the functions compute time is
input-dependent. One would needs to know not only the time taken, but what
the input was.

Scope timer involves instrumenting the source code. This makes timing "opt
in" rather than "opt out." You can ignore code that gets timed a lot
(e.g. `malloc()`) but time the caller of it. It uses RAII, so just have to
add one line time a block.

This timer not only times the scope itself, it connects it the other timers
(i.e. a call tree). This way, you can time the whole and parts of the whole,
without double-counting.

Unlike perf and prof/gprof, this timer collects wall time in addition to CPU
time.

## Overhead

These timers have a ~400ns overhead (check clocks + storing frame overhead)
per frame timed on my system. Run `./test.sh` to check on yours.

I use clock_gettime with `CLOCK_THREAD_CPUTIME_ID` (cpu time) and
`CLOCK_MONOTONIC` (wall time). rdtsc won't track CPU time if the thread
gets interrupted [2], and I *need* the extra work that
`clock_gettime(CLOCK_MONOTIC)` does to convert tsc into a wall time. The
VDSO interface mitigates sycall overhead. In some cases, clock_gettime
is faster [1].

## Developing

I use Nix to standardize my development environment. To run tests on this
library, install `nix` and run `test.sh`.

[1]: https://stackoverflow.com/questions/7935518/is-clock-gettime-adequate-for-submicrosecond-timing
[2]: https://stackoverflow.com/questions/42189976/calculate-system-time-using-rdtsc
[3]: https://github.com/charmoniumQ/scope_timer/tree/main/example/main.cpp
