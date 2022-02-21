#pragma once

#include <mn/Str.h>
#include <mn/Log.h>
#include <mn/Defer.h>
#include <mn/Assert.h>

struct Parser {
	mn::Str str;
	size_t pos; // index in string
	size_t curr_line; // 0 is first line
};

void parser_free(Parser& self) {
	mn::str_free(self.str);
}

void destruct(Parser& self) {
	parser_free(self);
}

Parser parser_from_str(const mn::Str& str, mn::Allocator allocator = mn::allocator_top()) {
	auto str_clone = mn::str_clone(str, allocator);
	mn::str_replace(str_clone, "\r\n", "\n");
	return Parser {
		.str = str_clone,
	};
}

bool parser_peek(const Parser& self, char c) {
	if ((self.pos + 1) > self.str.count) {
		return false;
	}

	if (self.str[self.pos] != c) {
		return false;
	}

	return true;
}

bool parser_peek(const Parser& self, const char* s) {
	const size_t len = ::strlen(s);
	if ((self.pos + len) > self.str.count) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (self.str[i+self.pos] != s[i]) {
			return false;
		}
	}
	return true;
}

bool parser_accept(Parser& self, char c) {
	if (parser_peek(self, c)) {
		self.pos++;
		if (c == '\n') {
			self.curr_line++;
		}

		return true;
	}
	return false;
}

bool parser_accept(Parser& self, const char* s) {
	const size_t len = ::strlen(s);
	if ((self.pos + len) > self.str.count) {
		return false;
	}

	size_t lines = 0;
	for (size_t i = 0; i < len; i++) {
		if (self.str[i+self.pos] != s[i]) {
			return false;
		}
		if (self.str[i+self.pos] == '\n') {
			lines++;
		}
	}

	self.pos += len;
	self.curr_line += lines;
	return true;
}

mn::Str parser_summary(const Parser& self) {
	auto s = mn::str_clone(self.str, mn::memory::tmp());
	if (s.count > 90) {
		mn::str_resize(s, 90);
		mn::str_push(s, "....");
	}
	mn::str_replace(s, "\n", "\\n");
	return s;
}

void _parser_panic(const Parser& self, const mn::Str& err_msg) {
	mn::panic("{}: {}, parser.str='{}', parser.pos={}", self.curr_line+1, err_msg, parser_summary(self), self.pos);
}

template<typename T>
void parser_expect(Parser& self, T s) {
	if (!parser_accept(self, s)) {
		mn::panic("failed to find '{}' in '{}'", s, parser_summary(self));
	}
}

void parser_skip_after(Parser& self, char c) {
	size_t lines = 0;
	for (size_t i = self.pos; i < self.str.count; i++) {
		if (self.str[i] == '\n') {
			lines++;
		}
		if (self.str[i] == c) {
			self.pos = i + 1;
			self.curr_line += lines;
			return;
		}
	}
}

void parser_skip_after(Parser& self, const char* s) {
	const auto s2 = mn::str_lit(s);
	const size_t index = mn::str_find(self.str, s2, self.pos);
	if (index == SIZE_MAX) {
		_parser_panic(self, mn::str_tmpf("failed to find '{}'", s2));
	}

	for (size_t i = self.pos; i < index+s2.count; i++) {
		if (self.str[i] == '\n') {
			self.curr_line++;
		}
	}
	self.pos = index + s2.count;
}

float parser_token_float(Parser& self) {
	if (self.pos >= self.str.count) {
		_parser_panic(self, mn::str_lit("can't find float at end of str"));
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		_parser_panic(self, mn::str_lit("can't find float, string doesn't start with digit or -"));
	}

	char* pos = nullptr;
	const float d = strtod(mn::begin(self.str)+self.pos, &pos);
	if (mn::begin(self.str)+self.pos == pos) {
		_parser_panic(self, mn::str_lit("failed to parse float"));
	}

	const size_t float_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += float_str_len;

	return d;
}

uint64_t parser_token_u64(Parser& self) {
	if (self.pos >= self.str.count) {
		_parser_panic(self, mn::str_lit("can't find u64 at end of str"));
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		_parser_panic(self, mn::str_lit("can't find u64, string doesn't start with digit or -"));
	}

	char* pos = nullptr;
	const uint64_t d = strtoull(mn::begin(self.str)+self.pos, &pos, 10);
	if (mn::begin(self.str)+self.pos == pos) {
		_parser_panic(self, mn::str_lit("failed to parse u64"));
	}

	const size_t int_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += int_str_len;

	return d;
}

uint32_t parser_token_u32(Parser& self) {
	const uint64_t u = parser_token_u64(self);
	if (u > UINT32_MAX) {
		_parser_panic(self, mn::str_tmpf("out of range number, {} > {}", u, UINT32_MAX));
	}
	return (uint32_t) u;
}

int32_t parser_token_i32(Parser& self) {
	if (self.pos >= self.str.count) {
		_parser_panic(self, mn::str_lit("can't find i32 at end of str"));
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		_parser_panic(self, mn::str_lit("can't find i32, string doesn't start with digit or -"));
	}

	char* pos = nullptr;
	const uint64_t d = strtoull(mn::begin(self.str)+self.pos, &pos, 10);
	if (mn::begin(self.str)+self.pos == pos) {
		_parser_panic(self, mn::str_lit("failed to parse i32"));
	}
	if (d < INT32_MIN || d > INT32_MAX) {
		_parser_panic(self, mn::str_tmpf("out of range number, must be {} < {} < {}", INT32_MIN, d, INT32_MAX));
	}

	const size_t int_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += int_str_len;

	return d;
}

uint8_t parser_token_u8(Parser& self) {
	const uint64_t b = parser_token_u64(self);
	if (b > UINT8_MAX) {
		_parser_panic(self, mn::str_tmpf("out of range number, {} > {}", b, UINT8_MAX));
	}
	return (uint8_t) b;
}

mn::Str parser_token_str(Parser& self, mn::Allocator allocator = mn::allocator_top()) {
	const char* a = mn::begin(self.str)+self.pos;
	while (self.pos < self.str.count && self.str[self.pos] != ' ' && self.str[self.pos] != '\n') {
		self.pos++;
	}
	return mn::str_from_substr(a, mn::begin(self.str)+self.pos, allocator);
}

bool parser_token_bool(Parser& self) {
	const auto x = parser_token_str(self, mn::memory::tmp());
	if (x == "TRUE") {
		return true;
	} else if (x == "FALSE") {
		return false;
	}
	_parser_panic(self, mn::str_tmpf("expected either TRUE or FALSE, found='{}'", x));
	return false;
}

void test_parser() {
	Parser parser = parser_from_str(mn::str_lit("hello world \r\n m"), mn::memory::tmp());
	auto orig = parser;

	mn_assert(parser_peek(parser, "hello"));
	mn_assert(parser_peek(parser, "ello") == false);

	{
		parser = orig;
		mn_assert(parser_accept(parser, "hello"));
		mn_assert(parser_accept(parser, ' '));
		mn_assert(parser.curr_line == 0);
		mn_assert(parser_accept(parser, "world \n"));
		mn_assert(parser.curr_line == 1);
		mn_assert(parser_accept(parser, " m"));

		parser = orig;
		mn_assert(parser_accept(parser, "ello") == false);
	}

	{
		parser = orig;
		parser_expect(parser, "hello");
		parser_expect(parser, ' ');
		mn_assert(parser.curr_line == 0);
		parser_expect(parser, "world \n");
		mn_assert(parser.curr_line == 1);
		parser_expect(parser, " m");
	}

	parser = parser_from_str(mn::str_lit("5\n-1.4\nhello 1%"), mn::memory::tmp());
	mn_assert(parser_token_u32(parser) == 5);
	parser_expect(parser, '\n');
	mn_assert(parser_token_float(parser) == -1.4f);
	parser_expect(parser, '\n');
	mn_assert(parser_token_str(parser, mn::memory::tmp()) == "hello");
	parser_expect(parser, ' ');
	mn_assert(parser_token_u64(parser) == 1);
	parser_expect(parser, '%');
	mn_assert(parser.curr_line == 2);

	mn::log_debug("test_parser: all tests pass");
}

/*
TODO:
- parser_from_file()?
	- store file path in parser?
	- read from disk?
*/
