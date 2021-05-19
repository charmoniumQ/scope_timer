#pragma once // NOLINT(llvm-header-guard)
#include "compiler_specific.hpp"
#include "os_specific.hpp"
#include "thread.hpp"
#include "process.hpp"
#include "util.hpp"
#include <memory>
#include <fstream>
#include <string>
#include <thread>

namespace charmonium::scope_timer::detail {

	/*
	  I want to hold a process with a static lifetime.
	  I don't want anyone to access it directly.
	  This gives me the possibility of lazy-loading.
	  Therefore, I will construct this at load-time, and call get_process at use-time.
	 */
	static class ProcessContainer {
	private:
		std::string filename;
		std::shared_ptr<Process> process;

		/*
		  This may need to lookup the address of the process,
		  so it can't return a shared_ptr; it has to mutate the container.
		 */
		void create_or_lookup_process() {
			std::ifstream infile {filename};
			if (CHARMONIUM_SCOPE_TIMER_LIKELY(infile.good())) {
				uintptr_t intptr = 0;
				infile >> intptr;
				// std::cerr << "ProcessContainer::create_or_lookup_process() lookup got " << reinterpret_cast<void*>(intptr) << " from " << filename << "\n";
				assert(intptr);
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				process = *reinterpret_cast<std::shared_ptr<Process>*>(intptr);
			} else {
				infile.close();
				process = std::make_shared<Process>();
				std::ofstream outfile {filename};
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				uintptr_t intptr = reinterpret_cast<uintptr_t>(&process);
				// std::cerr << "ProcessContainer::create_or_lookup_process() create put " << reinterpret_cast<void*>(intptr) << " into " << filename << "\n";
				outfile << intptr;
			}
		}

	public:
		ProcessContainer()
			: filename{tmp_path(std::to_string(get_pid()) + "_" + std::to_string(get_pid_uniquifier()))}
		{
			// std::cerr << "ProcessContainer::ProcessContainer()\n";
		}
		~ProcessContainer() {
			// std::cerr << "ProcessContainer::~ProcessContainer()\n";
			if (process.unique()) {
				[[maybe_unused]] auto rc = std::remove(filename.c_str());
				assert(rc == 0);
			}
		}
		Process& get_process() {
			if (CHARMONIUM_SCOPE_TIMER_UNLIKELY(!process)) {
				create_or_lookup_process();
			}
			return *process;
		}
	} process_container;

	/*
	  I want the Thread to be in thread-local storage, so each thread
	  can cheaply access their own (cheaper than looking up in a map
	  from thread::id -> Thread, I think).

	  However, I can't just `static thread_local Thread`, because the
	  parameters for creation depend on the process. Therefore I will
	  `static thread_local OBJECT`, where the sole responsibility is
	  to construct and hold a Thread.
	*/
	static thread_local class ThreadContainer {
	private:
		std::thread::id id;
		Process& process;
		Thread& thread;

	public:
		ThreadContainer()
			: id{std::this_thread::get_id()}
			, process{process_container.get_process()}
			, thread{process.create_thread(id, get_tid(), get_thread_name())}
		{ }

		~ThreadContainer() {
			// std::cerr << "ThreadContainer::~ThreadContainer: " << id << std::endl;
			process.delete_thread(id);
		}

		Thread& get_thread() { return thread; }
	} thread_container;

} // namespace scope_timer::detail
