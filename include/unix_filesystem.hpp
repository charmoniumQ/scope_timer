#pragma once // NOLINT(llvm-header-guard)
#include "branch_prediction.hpp"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * A miniature port of a subset of the C++ filesystem stdlib specialized for POSIX.
 *
 * I take no joy in supporting antiquated systems.
 */
namespace cpu_timer {
namespace detail {
namespace filesystem {

	/**
	 * @brief Replace all instances of @p from to @p to in @p str
	 *
	 * See https://stackoverflow.com/questions/3418231/replace-part-of-a-string-with-another-string
	 */
	static void replace_all(std::string& str, const std::string& from, const std::string& to) {
		if (from.empty()) {
			return;
		}
		size_t start_pos = 0;
		while((start_pos = str.find(from, start_pos)) != std::string::npos) {
			str.replace(start_pos, from.length(), to);
			start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
		}
	}

	class path {
	public:
		using value_type = char;
		using string_type = std::basic_string<value_type>;
		explicit path(const value_type* val_) : val{val_} { replace_all(val, "/", "\\/"); }
		explicit path(string_type&& val_) : val{std::move(val_)} { replace_all(val, "/", "\\/"); }
		explicit path(const string_type& val_) : val{std::move(val_)} { replace_all(val, "/", "\\/"); }
		path operator/(path other_path) const { return {val + "/" + other_path.val, true}; }
		path operator+(path other_path) const { return {val + other_path.val, true}; }
		const char* c_str() const { return val.c_str(); }
		std::string string() const { return val; }
	private:
		path(string_type&& val_, bool) : val{std::move(val_)} {}
		string_type val;
	};

	class filesystem_error : public std::system_error {
	public:
		filesystem_error(const std::string& what_arg, const path& p1, const path& p2, int ec)
			: filesystem_error(what_arg, p1, p2, std::make_error_code(std::errc(ec)))
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, const path& p2, std::error_code ec)
			: system_error{ec, what_arg + " " + p1.string() + " " + p2.string() + ": " + std::to_string(ec.value())}
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, std::error_code ec)
			: system_error{ec, what_arg + " " + p1.string() + ": " + std::to_string(ec.value())}
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, int ec)
			: filesystem_error(what_arg, p1, std::make_error_code(std::errc(ec))) { }
	};

	class directory_iterator;
	class directory_entry {
	private:
		path this_path;
		bool _is_directory;
		bool _exists;
	public:
		directory_entry(path this_path_, std::error_code ec) : this_path{std::move(this_path_)} { refresh(ec); }
		directory_entry(path this_path_) : this_path{std::move(this_path_)} { refresh(); }
		void refresh(std::error_code& ec) {
			errno = 0;
			struct stat buf {};
#ifdef FILESYSTEM_DEBUG
			std::cerr << "stat " << this_path.string() << "\n";
#endif
			if (stat(this_path.c_str(), &buf)) {
				int errno_ = errno; errno = 0;
				ec = std::make_error_code(std::errc(errno_));
				_is_directory = false;
				_exists = false;
			} else {
				ec.clear();
				_is_directory = S_ISDIR(buf.st_mode);
				_exists = true;
			}
		}
		void refresh() {
			std::error_code ec;
			refresh(ec);
			if (ec) {
				if (ec.value() != ENOENT) {
					throw filesystem_error{std::string{"stat"}, this_path, ec};
				}
			}
		}
		path path() const { return this_path; }
		bool is_directory() const { return _is_directory; }
		bool exists() const { return _exists; }
	};

	class directory_iterator {
	private:
		std::deque<directory_entry> dir_entries;
	public:
		explicit directory_iterator(const path& dir_path) {
			errno = 0;
#ifdef FILESYSTEM_DEBUG
			std::cerr << "ls " << dir_path.string() << "\n";
#endif
			DIR* dir = opendir(dir_path.c_str());
			if (bool_likely(dir != nullptr)) {
				struct dirent* dir_entry = readdir(dir);
				while (bool_likely(dir_entry != nullptr)) {
					bool skip = strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0;
#ifdef FILESYSTEM_DEBUG
					std::cerr << "ls " << dir_path.string() << " -> " << dir_entry->d_name << " skip=" << skip << "\n";
#endif
					if (!skip) {
						dir_entries.emplace_back(dir_path / path{std::string{dir_entry->d_name}});
					}
					dir_entry = readdir(dir);
				}
				if (bool_unlikely(errno != 0)) {
					int errno_ = errno; errno = 0;
					throw filesystem_error{std::string{"readdir"}, dir_path, errno_};
				}
				if (bool_unlikely(closedir(dir) != 0)) {
					int errno_ = errno; errno = 0;
					throw filesystem_error{std::string{"closedir"}, dir_path, errno_};
				}
			} else {
				int errno_ = errno; errno = 0;
				throw filesystem_error{std::string{"opendir"}, dir_path, errno_};
			}
		}
		directory_iterator() { }
		bool operator==(const directory_iterator& other) {
			if (dir_entries.size() == other.dir_entries.size()) {
				for (auto dir_entry0 = dir_entries.cbegin(), dir_entry1 = other.dir_entries.cbegin();
					 dir_entry0 != dir_entries.cend();
					 ++dir_entry0, ++dir_entry1) {
					if (dir_entry0 != dir_entry1) {
						return false;
					}
				}
				return true;
			}
			return false;
		}
		bool operator!=(const directory_iterator& other) { return !(*this == other); }
		using value_type = directory_entry;
		using difference_type = std::ptrdiff_t;
		using pointer = const directory_entry*;
		using reference = const directory_entry&;
		using iterator_category = std::input_iterator_tag;
		reference operator*() { assert(!dir_entries.empty()); return dir_entries.back(); }
		reference operator->() { return **this; }
		directory_iterator& operator++() {
			assert(!dir_entries.empty());
			dir_entries.pop_back();
			return *this;
		}
		const directory_iterator operator++(int) {
			auto ret = *this;
			++*this;
			return ret;
		}
	};

	[[maybe_unused]] static std::deque<directory_entry> post_order(const path& this_path) {
		std::deque<directory_entry> ret;
		std::deque<directory_entry> stack;
		directory_entry this_dir_ent {this_path};
		if (this_dir_ent.exists()) {
			stack.push_back(this_dir_ent);
		}
		while (!stack.empty()) {
			directory_entry current {stack.front()};
			stack.pop_front();
			ret.push_front(current.path());
			if (current.is_directory()) {
				for (directory_iterator it {current.path()}; it != directory_iterator{}; ++it) {
					stack.push_back(*it);
				}
				// std::copy(directory_iterator{current.path()}, directory_iterator{}, stack.end() - 1);
			}
		}
		return ret;
	}

	static std::uintmax_t remove_all(const path& this_path) {
		errno = 0;
		std::uintmax_t i = 0;
#ifdef FILESYSTEM_DEBUG
		std::cerr << "rm -rf " << this_path.string() << "\n";
#endif
		// I use a post-order traversal here, so that I am removing a dir AFTER removing its children.
		for (const directory_entry& descendent : post_order(this_path)) {
			++i;
			if (descendent.is_directory()) {
#ifdef FILESYSTEM_DEBUG
				std::cerr << "rmdir " << descendent.path().string() << "\n";
#endif
				if (bool_unlikely(rmdir(descendent.path().c_str()))) {
					int errno_ = errno; errno = 0;
					throw filesystem_error(std::string{"rmdir"}, descendent.path(), errno_);
				}
			} else {
#ifdef FILESYSTEM_DEBUG
				std::cerr << "unlink " << descendent.path().string() << "\n";
#endif
				if (bool_unlikely(unlink(descendent.path().c_str()) != 0)) {
					int errno_ = errno; errno = 0;
					throw filesystem_error(std::string{"unlink"}, descendent.path(), errno_);
				}
			}
		}
		return i;
	}

	static bool create_directory(const path& this_path) {
		std::error_code ec;
		directory_entry this_dir_ent{this_path, ec};
		if (!this_dir_ent.exists()) {
			mode_t umask_default = umask(0);
			umask(umask_default);
#ifdef FILESYSTEM_DEBUG
			std::cerr << "mkdir " << this_path.string() << "\n";
#endif
			int ret = mkdir(this_path.c_str(), 0777 & ~umask_default);
			if (ret == -1) {
				int errno_ = errno; errno = 0;
				throw filesystem_error(std::string{"mkdir"}, this_path, errno_);
			}
			return true;
		}
		return false;
	}

} // namespace filesystem
} // namespace detail
} // namespace cpu_timer
