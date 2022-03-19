#pragma once

#include <memory_resource>

#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace memory {
	using Allocator = std::pmr::memory_resource;
	using Arena = std::pmr::monotonic_buffer_resource;

	Allocator* tmp();

	// make sure any container that uses tmp has been destructed before resetting
	void reset_tmp();

	inline static Allocator*
	default_allocator() {
		return std::pmr::get_default_resource();
	}

	inline static Allocator*
	set_default_allocator(Allocator* new_allocator) {
		const auto x = std::pmr::get_default_resource();
		std::pmr::set_default_resource(new_allocator);
		return x;
	}
}

template<typename T>
using Vec = std::pmr::vector<T>;

template<typename K, typename V>
using Map = std::pmr::unordered_map<K, V>;

template<typename K>
using Set = std::pmr::unordered_set<K>;

using Str = std::pmr::string;

using StrView = std::string_view;

inline static StrView
operator"" _str_lit(const char* s, size_t l) {
	return StrView(s, l);
}

template<typename T, size_t N>
using Arr = std::array<T, N>;
