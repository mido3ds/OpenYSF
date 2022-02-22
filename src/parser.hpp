#pragma once

#include <mn/Str.h>
#include <mn/Log.h>
#include <mn/Defer.h>
#include <mn/Assert.h>

struct Parser {
	mn::Str str;
	mn::Str file_path;
	size_t pos; // index in string
	size_t curr_line; // 0 is first line
};

void parser_free(Parser& self) {
	mn::str_free(self.str);
	mn::str_free(self.file_path);
}

void destruct(Parser& self) {
	parser_free(self);
}

Parser parser_from_str(const mn::Str& str, mn::Allocator allocator = mn::allocator_top()) {
	auto str_clone = mn::str_clone(str, allocator);
	mn::str_replace(str_clone, "\r\n", "\n");
	return Parser {
		.str = str_clone,
		.file_path = mn::str_from_c("%memory%", allocator),
	};
}

Parser parser_from_file(const mn::Str& file_path, mn::Allocator allocator = mn::allocator_top()) {
	auto str = mn::file_content_str(file_path, allocator);
	mn::str_replace(str, "\r\n", "\n");
	return Parser {
		.str = str,
		.file_path = mn::str_clone(file_path, allocator),
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

template<typename ... Args>
void parser_panic(const Parser& self, const char* err_msg, const Args& ... args) {
	auto summary = mn::str_clone(self.str, mn::memory::tmp());
	if (summary.count > 90) {
		mn::str_resize(summary, 90);
		mn::str_push(summary, "....");
	}
	mn::str_replace(summary, "\n", "\\n");

	mn::panic("{}:{}: {}, parser.str='{}', parser.pos={}", self.file_path, self.curr_line+1, mn::str_tmpf(err_msg, args...), summary, self.pos);
}

template<typename T>
void parser_expect(Parser& self, T s) {
	if (!parser_accept(self, s)) {
		parser_panic(self, "failed to find '{}'", s);
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
		parser_panic(self, "failed to find '{}'", s2);
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
		parser_panic(self, "can't find float at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find float, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const float d = strtod(mn::begin(self.str)+self.pos, &pos);
	if (mn::begin(self.str)+self.pos == pos) {
		parser_panic(self, "failed to parse float");
	}

	const size_t float_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += float_str_len;

	return d;
}

uint64_t parser_token_u64(Parser& self) {
	if (self.pos >= self.str.count) {
		parser_panic(self, "can't find u64 at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find u64, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const uint64_t d = strtoull(mn::begin(self.str)+self.pos, &pos, 10);
	if (mn::begin(self.str)+self.pos == pos) {
		parser_panic(self, "failed to parse u64");
	}

	const size_t int_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += int_str_len;

	return d;
}

uint8_t parser_token_u8(Parser& self) {
	const uint64_t b = parser_token_u64(self);
	if (b > UINT8_MAX) {
		parser_panic(self, "out of range number, {} > {}", b, UINT8_MAX);
	}
	return (uint8_t) b;
}

int64_t parser_token_i64(Parser& self) {
	if (self.pos >= self.str.count) {
		parser_panic(self, "can't find i64 at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find i64, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const int64_t d = strtoll(mn::begin(self.str)+self.pos, &pos, 10);
	if (mn::begin(self.str)+self.pos == pos) {
		parser_panic(self, "failed to parse i64");
	}

	const size_t int_str_len = pos - (mn::begin(self.str)+self.pos);
	self.pos += int_str_len;

	return d;
}

mn::Str parser_token_str(Parser& self, mn::Allocator allocator = mn::allocator_top()) {
	const char* a = mn::begin(self.str)+self.pos;
	while (self.pos < self.str.count && self.str[self.pos] != ' ' && self.str[self.pos] != '\n') {
		self.pos++;
	}
	return mn::str_from_substr(a, mn::begin(self.str)+self.pos, allocator);
}

bool parser_finished(Parser& self) {
	return self.pos >= self.str.count;
}

Parser parser_fork(Parser& self, size_t lines) {
	auto other = self;

	size_t i = other.pos;
	size_t lines_in_other = 0;
	for (;i < other.str.count && lines_in_other < lines; i++) {
		if (self.str[i] == '\n') {
			lines_in_other++;
		}
	}
	if (lines_in_other != lines) {
		parser_panic(self, "failed to fork parser, can't find {} lines in str", lines);
	}

	other.str.count = other.str.cap = i+1;
	self.pos = i;	 
	self.curr_line += lines;

	return other;
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
		mn_assert(parser_finished(parser) == false);
		mn_assert(parser.curr_line == 0);
		mn_assert(parser_accept(parser, "world \n"));
		mn_assert(parser.curr_line == 1);
		mn_assert(parser_accept(parser, " m"));
		mn_assert(parser_finished(parser));

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
	mn_assert(parser_token_u64(parser) == 5);
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
- read from disk?
*/
