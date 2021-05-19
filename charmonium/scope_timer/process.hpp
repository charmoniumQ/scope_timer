#pragma once // NOLINT(llvm-header-guard)

#include "thread.hpp"
#include <memory>
#include <thread>
#include <unordered_map>

namespace charmonium::scope_timer::detail {

	class Process;
	class ScopeTimer;

	/**
	 * @brief All threads in the current process.
	 *
	 * This calls callback with one thread's batches of Frames, periodically not sooner than callback_period, in the thread whose functions are in the batch.
	 */
	class Process {
	private:
		friend class Thread;
		friend class ScopeTimer;

		// std::atomic<bool> enabled;
		bool enabled {false};
		// std::mutex config_mutex;
		const WallTime start;
		CpuTime callback_period {0}; // locked by config_mutex
		std::unique_ptr<CallbackType> callback; // locked by config_mutex
		// Actually, I don't need to lock the config
		// If two threads race to modify the config, the "winner" is already non-deterministic
		// The callers should synchronize themselves.
		// If this thread writes while someone else reads, there is no guarantee they hadn't "already read" the values.
		// The caller should synchronize with the readers.
		std::unordered_map<std::thread::id, Thread> threads; // locked by threads_mutex
		std::unordered_map<std::thread::id, size_t> thread_use_count; // locked by threads_mutex
		mutable std::recursive_mutex threads_mutex;

	public:

		explicit Process()
			: start{wall_now()}
			, callback{new CallbackType}
		{ }

		WallTime get_start() { return start; }

		/**
		 * @brief Create or get the thread.
		 *
		 * For efficiency, the caller should cache this in thread_local storage.
		 */
		Thread& create_thread(
				std::thread::id thread,
				std::thread::native_handle_type native_handle,
				std::string&& thread_name
		) {
			std::lock_guard<std::recursive_mutex> threads_lock {threads_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			// Could have also been set up by the caller.
			if (threads.count(thread) == 0) {
				threads.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(thread),
					std::forward_as_tuple(*this, thread, native_handle, std::move(thread_name))
				);
			}
			thread_use_count[thread]++;
			return threads.at(thread);
		}

		/**
		 * @brief Call when a thread is disposed.
		 *
		 * This is neccessary because the OS can reuse old thread IDs.
		 */
		void delete_thread(std::thread::id thread) {
			// std::cerr << "Process::delete_thread: " << thread << std::endl;
			std::lock_guard<std::recursive_mutex> threads_lock {threads_mutex};
			// This could be the same thread, just a different static context (i.e. different obj-file or lib)
			if (threads.count(thread) != 0) {
				thread_use_count[thread]--;
				if (thread_use_count.at(thread) == 0) {
					threads.erase(thread);
				}
			}
		}

		/**
		 * @brief Sets @p enabled for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_enabled(bool enabled_) {
			enabled = enabled_;
		}

		/**
		 * @brief Sets @p callback_period for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		void set_callback_period(CpuTime callback_period_) {
			// std::lock_guard<std::recursive_mutex> config_lock {config_mutex};
			callback_period = callback_period_;
		}

		/**
		 * @brief Calls callback after every frame.
		 *
		 * This is usually too inefficient.
		 */
		void callback_every() { set_callback_period(CpuTime{1}); }

		/**
		 * @brief Call callback in destructor.
		 *
		 * This is the most efficient, putting the entire lifetime of
		 * each thread into one batch and calling the callback.
		 */
		void callback_once() { set_callback_period(CpuTime{0}); }

		bool is_enabled() const {
			return enabled;
		}

		/**
		 * @brief Sets @p callback for future threads.
		 *
		 * All in-progress threads will complete with the prior value.
		 */
		template <typename YourCallbackType>
		void set_callback(std::unique_ptr<YourCallbackType>&& callback_) {
			// std::lock_guard<std::mutex> config_lock {config_mutex};
			callback = std::unique_ptr<CallbackType>{static_cast<CallbackType*>(callback_.release())};
		}


		template <typename YourCallbackType, typename... Args>
		void emplace_callback(Args&&... args) {
			// In C++17, we can write:
			// set_callback(std::make_unique<YourCallbackType>(std::forward<Args>(args)...));
			set_callback(std::unique_ptr<YourCallbackType>{new YourCallbackType{std::forward<Args>(args)...}});
		}

		template <typename YourCallbackType>
		YourCallbackType& get_callback() {
			return dynamic_cast<YourCallbackType&>(*callback);
		}


		// get_thread() returns pointers into this, so it should not be copied or moved.
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
		Process(const Process&&) = delete;
		Process& operator=(const Process&&) = delete;
		~Process() {
			// std::cout << "Process::~Process" << std::endl;
			for (const auto& pair : threads) {
				std::cerr << pair.first << " is still around. Going to kick their logs out.\n";
			}
		}
	};

	inline CallbackType& Thread::get_callback() const { return *process.callback; }

	inline CpuTime Thread::get_callback_period() const { return process.callback_period; }

	inline WallTime Thread::get_process_start() const { return process.start; }

	// TODO(grayson5): Figure out which threads the callback could be called from.
	// thread_in_situ will always be called from the target thread.
	// I believe thread_local ThreadContainer call create_thread and delete_thread, so I think they will always be called from the target thread.
	// But the main thread might get deleted by the destructor of its containing map, in the main thread.

} // namespace charmonium::scope_timer::detail
