#pragma once

#include <memory_resource>

#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include <fmt/format.h>
#include <fmt/ostream.h>

template<typename T>
using Box = std::unique_ptr<T>;

template <class T, class... Types, std::enable_if_t<!std::is_array_v<T>, int> = 0>
[[nodiscard]] Box<T>
box_new(Types&&... _Args) {
    return Box<T>(new T(std::forward<Types>(_Args)...));
}

template<typename T, size_t N>
using Arr = std::array<T, N>;

using Str = std::pmr::string;
using StrView = std::string_view;

template<typename T>
using Vec = std::pmr::vector<T>;

template<typename K, typename V>
using Map = std::pmr::unordered_map<K, V>;

template<typename T>
using Set = std::pmr::unordered_set<T>;

namespace memory {
	using Allocator = std::pmr::memory_resource;

	class Arena : public Allocator {
		Box<std::pmr::monotonic_buffer_resource> _mbr = box_new<std::pmr::monotonic_buffer_resource>();

		virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override {
			return _mbr->allocate(bytes, alignment);
		}

		virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override { }

		virtual bool do_is_equal(const Allocator& other) const noexcept override {
			return this == &other;
		}
	};

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

template<class T>
void vec_remove_unordered(typename Vec<T>& v, int i) {
    auto last = v.rbegin();
    if (i != v.size()-1) {
        v[i] = std::move(*last);
    }
    v.pop_back();
}

inline static StrView
operator"" _str_lit(const char* s, size_t l) {
	return StrView(s, l);
}

inline static void
str_replace(Str& self, StrView search, StrView replace) {
    size_t pos = 0;
    while ((pos = self.find(search, pos)) != Str::npos) {
         self.replace(pos, search.length(), replace);
         pos += replace.length();
    }
}

inline static bool
str_prefix(StrView self, StrView search) {
	return self.find_first_of(search) == 0;
}

inline static bool
str_suffix(StrView self, StrView search) {
	return self.find_last_of(search) == self.size()-1;
}

// appends the formatted string to the end of self
template<typename ... Args>
inline static void
str_push(Str& self, StrView format_str, const Args& ... args) {
	fmt::format_to(std::back_inserter(self), format_str, args...);
}

template<typename ... Args>
inline static Str
str_format(memory::Allocator* allocator, StrView format_str, const Args& ... args) {
	Str self(allocator);
	fmt::format_to(std::back_inserter(self), format_str, args...);
	return std::move(self);
}

template<typename ... Args>
inline static Str
str_format(StrView format_str, const Args& ... args) {
	return std::move(str_format(memory::default_allocator(), format_str, args...));
}

template<typename ... Args>
inline static Str
str_tmpf(StrView format_str, const Args& ... args) {
	return std::move(str_format(memory::tmp(), format_str, args...));
}

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
