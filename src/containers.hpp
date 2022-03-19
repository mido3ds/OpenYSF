#pragma once

#include <memory_resource>

#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>
#include <fmt/ostream.h>

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

template<class T>
void vec_remove_unordered(typename Vec<T>& v, int i) {
    auto last = v.rbegin();
    if (i != v.size()-1) {
        v[i] = std::move(*last);
    }
    v.pop_back();
}

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

namespace fmt {
	template<typename T>
	struct formatter<Vec<T>> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Vec<T> &self, FormatContext &ctx) {
			format_to(ctx.out(), "Vec[{}]{{", self.size());
			for (int i = 0; i < self.size(); i++) {
				format_to(ctx.out(), "[{}]={}{}", i, self[i], (i == self.size()-1? "":", "));
			}
			return format_to(ctx.out(), "}}");
		}
	};
}

template<typename T>
bool operator==(const Vec<T>& aa, const Vec<T>& bb) {
	if (aa.size() != bb.size()) {
		return false;
	}
	for (int i = 0; i < aa.size(); i++) {
		if (aa[i] != bb[i]) {
			return false;
		}
	}
	return true;
}

template<typename T>
bool operator!=(const Vec<T>& aa, const Vec<T>& bb) {
	return (aa == bb) == false;
}
