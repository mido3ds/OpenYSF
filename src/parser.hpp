#pragma once

#include "containers.hpp"

#include "log.hpp"
#include <mn/Assert.h>

struct Parser {
	Str str;
	Str file_path;
	size_t pos; // index in string
	size_t curr_line; // 0 is first line
};

Parser parser_from_str(StrView str, memory::Allocator* allocator = memory::default_allocator()) {
	Str str_clone(str, allocator);
	str_replace(str_clone, "\r\n", "\n");
	return Parser {
		.str = str_clone,
		.file_path = Str("%memory%", allocator),
	};
}

Parser parser_from_file(StrView file_path, memory::Allocator* allocator = memory::default_allocator()) {
	auto str = mn::file_content_str(file_path.data(), mn::memory::tmp());
	Str str_clone(str.ptr, allocator);
	str_replace(str_clone, "\r\n", "\n");
	return Parser {
		.str = str_clone,
		.file_path = Str(file_path, allocator),
	};
}

bool parser_peek(const Parser& self, char c) {
	if ((self.pos + 1) > self.str.size()) {
		return false;
	}

	if (self.str[self.pos] != c) {
		return false;
	}

	return true;
}

bool parser_peek(const Parser& self, StrView s) {
	if ((self.pos + s.size()) > self.str.size()) {
		return false;
	}
	for (size_t i = 0; i < s.size(); i++) {
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

bool parser_accept(Parser& self, StrView s) {
	if ((self.pos + s.size()) > self.str.size()) {
		return false;
	}

	size_t lines = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (self.str[i+self.pos] != s[i]) {
			return false;
		}
		if (self.str[i+self.pos] == '\n') {
			lines++;
		}
	}

	self.pos += s.size();
	self.curr_line += lines;
	return true;
}

template<typename ... Args>
void parser_panic(const Parser& self, StrView err_msg, const Args& ... args) {
	Str summary(self.str, memory::tmp());
	if (summary.size() > 90) {
		summary.resize(90);
		summary += "....";
	}
	str_replace(summary, "\n", "\\n");

	mn::panic("{}:{}: {}, parser.str='{}', parser.pos={}", self.file_path, self.curr_line+1, mn::str_tmpf(err_msg.data(), args...), summary, self.pos);
}

template<typename T>
void parser_expect(Parser& self, T s) {
	if (!parser_accept(self, s)) {
		parser_panic(self, "failed to find '{}'", s);
	}
}

void parser_skip_after(Parser& self, char c) {
	size_t lines = 0;
	for (size_t i = self.pos; i < self.str.size(); i++) {
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

void parser_skip_after(Parser& self, StrView s) {
	const size_t index = self.str.find(s, self.pos);
	if (index == Str::npos) {
		parser_panic(self, "failed to find '{}'", s);
	}

	for (size_t i = self.pos; i < index+s.size(); i++) {
		if (self.str[i] == '\n') {
			self.curr_line++;
		}
	}
	self.pos = index + s.size();
}

float parser_token_float(Parser& self) {
	if (self.pos >= self.str.size()) {
		parser_panic(self, "can't find float at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find float, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const float d = strtod(&self.str[self.pos], &pos);
	if (&self.str[self.pos] == pos) {
		parser_panic(self, "failed to parse float");
	}

	const size_t float_str_len = pos - (&self.str[self.pos]);
	self.pos += float_str_len;

	return d;
}

uint64_t parser_token_u64(Parser& self) {
	if (self.pos >= self.str.size()) {
		parser_panic(self, "can't find u64 at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find u64, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const uint64_t d = strtoull(&self.str[self.pos], &pos, 10);
	if (&self.str[self.pos] == pos) {
		parser_panic(self, "failed to parse u64");
	}

	const size_t int_str_len = pos - (&self.str[self.pos]);
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
	if (self.pos >= self.str.size()) {
		parser_panic(self, "can't find i64 at end of str");
	} else if (!(::isdigit(self.str[self.pos]) || self.str[self.pos] == '-')) {
		parser_panic(self, "can't find i64, string doesn't start with digit or -");
	}

	char* pos = nullptr;
	const int64_t d = strtoll(&self.str[self.pos], &pos, 10);
	if (&self.str[self.pos] == pos) {
		parser_panic(self, "failed to parse i64");
	}

	const size_t int_str_len = pos - (&self.str[self.pos]);
	self.pos += int_str_len;

	return d;
}

Str parser_token_str(Parser& self, memory::Allocator* allocator = memory::default_allocator()) {
	auto a = &self.str[self.pos];
	while (self.pos < self.str.size() && self.str[self.pos] != ' ' && self.str[self.pos] != '\n') {
		self.pos++;
	}
	return Str(a, &self.str[self.pos], allocator);
}

bool parser_finished(Parser& self) {
	return self.pos >= self.str.size();
}

Parser parser_fork(Parser& self, size_t lines) {
	auto other = self;

	size_t i = other.pos;
	size_t lines_in_other = 0;
	while (i < other.str.size() && lines_in_other < lines) {
		if (self.str[i] == '\n') {
			lines_in_other++;
		}
		i++;
	}
	if (lines_in_other != lines) {
		parser_panic(self, "failed to fork parser, can't find {} lines in str", lines);
	}

	other.str.resize(i+1);
	self.pos = i;
	self.curr_line += lines;

	return other;
}

void test_parser() {
	Parser parser = parser_from_str("hello world \r\n m", memory::tmp());
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

	parser = parser_from_str("5\n-1.4\nhello 1%", memory::tmp());
	mn_assert(parser_token_u64(parser) == 5);
	parser_expect(parser, '\n');
	mn_assert(parser_token_float(parser) == -1.4f);
	parser_expect(parser, '\n');
	mn_assert(parser_token_str(parser, memory::tmp()) == "hello");
	parser_expect(parser, ' ');
	mn_assert(parser_token_u64(parser) == 1);
	parser_expect(parser, '%');
	mn_assert(parser.curr_line == 2);

	log_debug("test_parser: all tests pass");
}

/*
TODO:
- read from disk?
*/
