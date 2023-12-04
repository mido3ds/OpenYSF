#pragma once

#include <mu/utils.h>

struct Parser {
	mu::Str str;
	mu::Str file_path;
	size_t pos; // index in string
	size_t curr_line; // 0 is first line
};

Parser parser_from_str(mu::StrView str, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	mu::Str str_clone(str, allocator);
	mu::str_replace(str_clone, "\r\n", "\n");
	return Parser { .str = str_clone };
}

Parser parser_from_file(mu::StrView file_path, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	auto str = mu::file_content_str(file_path.data(), mu::memory::tmp());
	mu::str_replace(str, "\r\n", "\n");
	return Parser {
		.str = str,
		.file_path = mu::Str(file_path, allocator),
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

bool parser_peek(const Parser& self, mu::StrView s) {
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

bool parser_accept(Parser& self, mu::StrView s) {
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

// return multiplier that converts to standard unit (maybe 1 if no unit or standard unit)
// standard units: m, g, m/s, HP, degrees
double parser_accept_unit(Parser& self) {
	if (parser_accept(self, "ft"))   { return 0.3048;    }
	if (parser_accept(self, "kt"))   { return 0.514444;  }
	if (parser_accept(self, "km/h")) { return 0.277778;  }
	if (parser_accept(self, "MACH")) { return 340.29;    }
	if (parser_accept(self, "kg"))   { return 1000;      }
	if (parser_accept(self, "t"))    { return 1000*1000; }
	if (parser_accept(self, "%"))    { return .01; }

	// accept those units to avoid crashing on them later
	if (parser_accept(self, "deg"))  { return 1;    }
	if (parser_accept(self, "HP"))   { return 1;    }
	if (parser_accept(self, "m^2"))  { return 1;    }
	if (parser_accept(self, "m/s"))  { return 1;    }
	if (parser_accept(self, "m"))    { return 1;    }

	return 1;
}

template<typename ... Args>
void parser_panic(const Parser& self, mu::StrView err_msg, const Args& ... args) {
	mu::Str summary(self.str, mu::memory::tmp());
	if (summary.size() > 90) {
		summary.resize(90);
		summary += "....";
	}
	mu::str_replace(summary, "\n", "\\n");

	auto file_path = self.file_path.empty() ? "%memory%" : self.file_path;
	mu::panic("{}:{}: {}, parser.str='{}', parser.pos={}", file_path, self.curr_line+1, mu::str_tmpf(err_msg.data(), args...), summary, self.pos);
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

void parser_skip_after(Parser& self, mu::StrView s) {
	const size_t index = self.str.find(s, self.pos);
	if (index == mu::Str::npos) {
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

template<typename Function>
mu::Str parser_token_str_with(Parser& self, Function predicate, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	auto a = &self.str[self.pos];
	while (self.pos < self.str.size() && predicate(self.str[self.pos])) {
		self.pos++;
	}
	return mu::Str(a, &self.str[self.pos], allocator);
}

mu::Str parser_token_str(Parser& self, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	return parser_token_str_with(self, [](char c){ return !::isspace(c); }, allocator);
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
	Parser parser = parser_from_str("hello world \r\n m", mu::memory::tmp());
	auto orig = parser;

	mu_assert(parser_peek(parser, "hello"));
	mu_assert(parser_peek(parser, "ello") == false);

	{
		parser = orig;
		mu_assert(parser_accept(parser, "hello"));
		mu_assert(parser_accept(parser, ' '));
		mu_assert(parser_finished(parser) == false);
		mu_assert(parser.curr_line == 0);
		mu_assert(parser_accept(parser, "world \n"));
		mu_assert(parser.curr_line == 1);
		mu_assert(parser_accept(parser, " m"));
		mu_assert(parser_finished(parser));

		parser = orig;
		mu_assert(parser_accept(parser, "ello") == false);
	}

	{
		parser = orig;
		parser_expect(parser, "hello");
		parser_expect(parser, ' ');
		mu_assert(parser.curr_line == 0);
		parser_expect(parser, "world \n");
		mu_assert(parser.curr_line == 1);
		parser_expect(parser, " m");
	}

	parser = parser_from_str("5\n-1.4\nhello 1%", mu::memory::tmp());
	mu_assert(parser_token_u64(parser) == 5);
	parser_expect(parser, '\n');
	mu_assert(parser_token_float(parser) == -1.4f);
	parser_expect(parser, '\n');
	mu_assert(parser_token_str(parser, mu::memory::tmp()) == "hello");
	parser_expect(parser, ' ');
	mu_assert(parser_token_u64(parser) == 1);
	parser_expect(parser, '%');
	mu_assert(parser.curr_line == 2);

	parser = parser_from_str("0deg 0.2ft 15HP 1.2 2%", mu::memory::tmp());
	mu_assert(parser_token_float(parser) == 0);
	mu_assert(parser_accept_unit(parser) == 1);
	parser_expect(parser, ' ');
	mu_assert(almost_equal(parser_token_float(parser) * parser_accept_unit(parser), 0.06096f));
	parser_expect(parser, ' ');
	mu_assert(almost_equal((float)parser_token_i64(parser) * parser_accept_unit(parser), 15.0f));
	parser_expect(parser, ' ');
	mu_assert(almost_equal(parser_token_float(parser) * parser_accept_unit(parser), 1.2));
	parser_expect(parser, ' ');
	mu_assert(almost_equal(parser_token_float(parser) * parser_accept_unit(parser), 0.02));
	mu_assert(parser_finished(parser));

	mu::log_debug("test_parser: all tests pass");
}
