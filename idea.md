- I have a new way of tracking of performance, that I think is better than what we currently do, and is reusable past ILLIXR. I haven't written any code, but I've been whiteboarding the ideas.

- There are two fundamental problems with existing solutions:

  - They report _too much_ data. I need to aggregate the data they produce to produce the graphs that we care about in ILLIXR.
	- This is a non-problem if it is feasible to write a script to aggregate the data. I will look into the feasibility of that.

  - They report _too little_ data. They account time to a function-call. However, there may be _dynamic_ data that we need in order to make sense of the performance numbers

	- For example, OpenCV uses _higher-order functions_ to implement their parallel jobserver; The caller passes a pointer to the function containing the workload gets as a parameter to the jobserver, and the jobserver calls that function. Thus a singler caller (the jobserver's main function) is running code which we want to attribute to multiple accounts. Which account we want to attribute the duration to is only known dynamically, not statically, and perf only accounts based on statically knowable accounts (i.e. function names).

```python
# Loose Python-y pseudocode

class worker_thread:
    def run(self):
        while True:
    	    work, arg = self.workqueue.pop()
            work(arg)

worker_threads = Launch n copies of worker_thread.

def run_in_jobserver(func, arg):
    for thread in worker_threads:
        thread.workqueue.push(func, arg)

def foo():
    run_in_jobserver(convert_image, "foo_image.jpg")

def bar():
    run_in_jobserver(convert_image, "bar_image.jpg")

foo()
bar()
... # other code that might call convert_image
```

```text
Profile results:
foo              : little CPU time, (the _true_ cost shows up in worker_thread)
bar              : little CPU time, (the _true_ cost shows up in worker_thread)
worker_thread.run: much CPU time
convert_image    : much CPU time

Was convert_image("foo_image.jpg") slower or faster than convert_image("bar_image.jpg")?
Idk.

In addition to the static information (the callee, caller, convert_image in this case), the dynamic information (the argument) matters.
```
- (continued)
  -
    - For example, suppose f calls g, n times a loop. We want to know which iterations of g correspond to which iterations of f. It is unclear if variance in f is due to variance in g or variance in n. This can only be resovled with dynamic data (i.e. knowing n for each iteration of f, knowing the iteration of f for each iteration of g).

```
def f():
    for i in range(n()):
        g()
```

  - More incidental problems include:
	- Intel VTune only works for Intel processors.
	- NVIDIA NSight only works for CUDA applications.
	- I can't figure out how to export gprof data into a format externally usable.

- No existing solutions I know of (NVIDIO NSight, Intel VTune, Linux perf) can record _dynamic_ information, which I believe is important in the cases above (although I have **not** done a complete search).
  - I think this is a fundamental limit with _sampling_ profilers.
  - By annotating a small subset of functions that get called at a low frequency, one can overcome this limitation.
  - This is kind of what we did with the ILLIXR timers, but in an ad-hoc kludge.
	- Any hierarchical timers (g is timed, f is timed, and g calls f) have to be manually subtracted to get a correct total time.

- My idea is to instrument the source-code with timers instead of using interrupt-based sampling (as we have already done with our ad-hoc ILLIXR timers).
  - The timer interface would utilize [RAII](https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization), so it can easily time blocks of C++, including functions, with one line of code.
  - However, it is "opt-in", so you don't end up with too much overhead. For example, one probably should not put a timer inside `malloc`, since that is called at such a high frequency.
  - They could all be buffered and flushed post-realtime, so they don't impact the actual performance.
    - They could also be aggregated in real time?
  - This could be used in microservice datacenters.
  - It has a first-class concept of loop-iterations. You can put an `iteration_no` in a timer.
  - Hierachy is handled automatically, in the common case.
	- Each timer sets a thread-specific global-variable with its ID and its `iteration_no`.
	- Each subtimer logs the parent ID and the parent `iteration_no`.
	- This creates a DAG of timer-records.
	   - Each node is a duration of some dynamic block of code.
	   - An edge exists from A to B if A's block of code called B's block of code.
	   - This preserves which iteration each block is responsible for (!)
	- However, in some cases this has to be maintained manually.
	  - When a new thread is spawned, the thread-specific global-variable will be empty, so the spawning thread must pass a copy of it's global-var into the child thread.
	  - When a higher-order function executes a job that is going to a different account, the account should be set manually.
  - If we embrace this hierarchical approach, then it becomes much easier to programatically test if we have "missing time". The `main` method would have a timer, and if we subtract its children, we can easily globally determine missing time.

- It gets even better: If we implement a system like this, we essentially have a trace. In parallel programs it is not easy to determine the impact of speeding up one component on the whole system. If we have the logs generated by my idea, then we might have enough information to _replay_ the trace, but tweak the relative speed of components.

- I still need to search more, if it is possible to log dynamic data in existing tools, what the state-of-the-art is (what those tools do _right_), and if traces from exisitng tools can be aggregated into the data I care about.
