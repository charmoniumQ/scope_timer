#include <string>
#include <random>
#include <atomic>
#include <cstdlib>

namespace util {
	static std::string random_hex_string(size_t n = 16) {
		std::mt19937 rng {std::random_device{}()};
		std::uniform_int_distribution<unsigned int> dist {0, 15};
		std::string ret (n, ' ');
		for (char& ch : ret) {
			unsigned int r = dist(rng);
			ch = (r < 10 ? '0' + r : 'a' + r - 10);
		}
		return ret;
	}

	/**
	 * @brief if var is env-var return it, else default_
	 */
	static std::string
	getenv_or(std::string var, std::string default_) {
		if (std::getenv(var.c_str())) {
			return {std::getenv(var.c_str())};
		} else {
			return default_;
		}
	}

	static void fence() {
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	template <typename Map, typename RevMap>
	typename Map::mapped_type lookup(Map& map, RevMap& reverse_map, typename Map::key_type word) {
		auto it = map.find(word);
		if (it != map.end()) {
			assert(reverse_map[it->second] == it->first);
			return it->second;
		} else {
			auto val = map.size();
			map[word] = val;
			reverse_map.push_back(word);
			return val;
		}
	}
}
