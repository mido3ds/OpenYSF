#include <stdio.h>
#include <stdint.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <GL/gl.h>

#include <cstdio>

#include <mn/Log.h>
#include <mn/Defer.h>
#include <mn/OS.h>

#define WinWidth 400
#define WinHeight 400

//
// void _skip_white_space(mn::Str& s) {
// 	size_t i = 0;
// 	while (i < s.count && ::isspace(s[i])) {
// 		i++;
// 	}
// 	s.ptr += i;
// 	s.count -= i;
// }

//
// void _skip_till(mn::Str& s, char s2) {
// 	size_t i;
// 	for (i = 0; i < s.count && s[i] != s2; i++) { }
// 	s.ptr += i;
// 	s.count -= i;
// }

//
// mn::Str _token_str(mn::Str& s) {
// 	const auto pos = s.ptr;
// 	_skip_till(s, ' ');
// 	return mn::str_from_substr(pos, s.ptr, mn::memory::tmp());
// }

double token_double(mn::Str& s) {
	char* pos;
	double d = strtod(s.ptr, &pos);
	if (s.ptr == pos)
		mn::panic("no double found");
	if (errno == ERANGE)
		mn::panic("double out of range");
	const size_t diff = pos - s.ptr;
	s.ptr = pos;
	s.count -= diff;
	return d;
}

int64_t token_long(mn::Str& s) {
	char* pos;
	const int64_t d = strtol(s.ptr, &pos, 10);
	if (s.ptr == pos)
		mn::panic("no int64_t found");
	if (errno == ERANGE)
		mn::panic("int64_t out of range");
	const size_t diff = pos - s.ptr;
	s.ptr = pos;
	s.count -= diff;
	return d;
}

uint8_t token_byte(mn::Str& s) {
	const int64_t i = token_long(s);
	if (i < 0 || i > 255) {
		mn::panic("number is not a byte");
	}
	return (uint8_t) i;
}

// size_t peek(const mn::Str& s1, const char* s2) {
// 	const size_t len = ::strlen(s2);
// 	if (len > s1.count+1) {
// 		return false;
// 	}
// 	if (::memcmp(s1.ptr+1, s2, len) == 0) {
// 		return len;
// 	}
// 	return 0;
// }

// size_t peek(const mn::Str& s, char c) {
// 	if (s.count > 1 && s[1] == c) {
// 		return 1;
// 	}

// 	return 0;
// }

bool accept(mn::Str& s1, const char* s2) {
	const size_t len = ::strlen(s2);
	if (len > s1.count) {
		return false;
	}
	if (::memcmp(s1.ptr, s2, len) == 0) {
		s1.ptr += len;
		s1.count -= len;
		return true;
	}
	return false;
}

bool accept(mn::Str& s, char c) {
	if (s.count > 0 && s[0] == c) {
		s.ptr++;
		s.count--;
		return true;
	}

	return false;
}

template<typename T>
void expect(mn::Str& s1, T s2) {
	if (!accept(s1, s2)) {
		mn::panic("failed to find '{}' in '{}'", s2, s1);
	}
}

// skip comments and whitespaces
void skip_cws(mn::Str& s) {
	while (s.count > 0) {
		if (s[0] == ' ' || s[0] == '\t' || s[0] == '\n') {
			s.ptr++;
			s.count--;
		} else if (s[0] == '#' | accept(s, "REM ")) {
			while (s.count > 0 && s[0] != '\n') {
				s.ptr++;
				s.count--;
			}
		} else {
			return;
		}
	}
}

struct Vec3 {
	double x, y, z;
};

namespace fmt {
	template<>
	struct formatter<Vec3> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Vec3 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "Vec3{{x: {}, y: {}, z: {}}}", v.x, v.y, v.z);
		}
	};
}

struct Vertex {
	Vec3 pos;
	bool smooth_shading;
};

namespace fmt {
	template<>
	struct formatter<Vertex> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Vertex &v, FormatContext &ctx) {
			return format_to(ctx.out(), "Vertex{{pos: {}, smooth_shading: {}}}", v.pos, v.smooth_shading);
		}
	};
}

struct Color {
	uint8_t r, g, b;
};

namespace fmt {
	template<>
	struct formatter<Color> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Color &c, FormatContext &ctx) {
			return format_to(ctx.out(), "Color{{r: {}, g: {}, b: {}}}", c.r, c.g, c.b);
		}
	};
}

struct Face {
	mn::Buf<size_t> vertices_ids;
	Color color;
	Vec3 center, normal;
	bool unshaded_light_source;
};

void face_free(Face &self) {
	mn::destruct(self.vertices_ids);
}

namespace fmt {
	template<>
	struct formatter<Face> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Face &c, FormatContext &ctx) {
			return format_to(ctx.out(), "Face{{vertices_ids: {}, color: {}, "
				"center: {}, normal: {}, unshaded_light_source: {}}}", c.vertices_ids, c.color, c.center, c.normal, c.unshaded_light_source);
		}
	};
}

struct SURF {
	mn::Buf<Vertex> vertices;
	mn::Buf<Face> faces;
};

void surf_free(SURF &self) {
	mn::destruct(self.vertices);
	mn::destruct(self.faces);
}

namespace fmt {
	template<>
	struct formatter<SURF> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const SURF &s, FormatContext &ctx) {
			return format_to(ctx.out(), "SURF{{vertices: {}, faces: {}}}", s.vertices, s.faces);
		}
	};
}

SURF surf_parse(const char* in) {
	SURF surf {};
	auto s = mn::str_lit(in);

	expect(s, "SURF");
	skip_cws(s);

	// V {x} {y} {z}[ R]
	while (accept(s, "V ")) {
		Vertex v {};

		v.pos.x = token_double(s);
		expect(s, ' ');
		v.pos.y = token_double(s);
		expect(s, ' ');
		v.pos.z = token_double(s);
		v.smooth_shading = accept(s, " R");
		skip_cws(s);

		mn::buf_push(surf.vertices, v);
	}
	if (surf.vertices.count == 0) {
		mn::panic("must have at least one vertex");
	}

	while (!accept(s, 'E')) {
		Face face {};
		bool parsed_color = false,
			parsed_normal = false,
			parsed_vertices = false;

		expect(s, 'F');
		skip_cws(s);
		while (!accept(s, 'E')) {
			if (accept(s, "C ")) {
				if (parsed_color) {
					mn::panic("found more than one color");
				}
				parsed_color = true;

				face.color.r = token_byte(s);
				expect(s, ' ');
				face.color.g = token_byte(s);
				expect(s, ' ');
				face.color.b = token_byte(s);
				skip_cws(s);
			}

			if (accept(s, "N ")) {
				if (parsed_normal) {
					mn::panic("found more than one normal");
				}
				parsed_normal = true;

				face.center.x = token_double(s);
				expect(s, ' ');
				face.center.y = token_double(s);
				expect(s, ' ');
				face.center.z = token_double(s);
				expect(s, ' ');

				face.normal.x = token_double(s);
				expect(s, ' ');
				face.normal.y = token_double(s);
				expect(s, ' ');
				face.normal.z = token_double(s);
				skip_cws(s);
			}

			// V {x}...
			if (accept(s, 'V')) {
				if (parsed_vertices) {
					mn::panic("found more than one vertices line");
				}
				parsed_vertices = true;

				while (accept(s, ' ')) {
					int64_t id = token_long(s);
					if (id < 0) {
						mn::panic("id={} is negative", id);
					}
					if (id >= surf.vertices.count) {
						mn::panic("id={} out of bounds={}", id, surf.vertices.count);
					}
					mn::buf_push(face.vertices_ids, (size_t) id);
				}
				skip_cws(s);
			}
			if (face.vertices_ids.count < 3) {
				mn::panic("face has count of ids={}, it must be >= 3", face.vertices_ids.count);
			}

			if (accept(s, 'B')) {
				if (face.unshaded_light_source) {
					mn::panic("found more than 1 B");
				}
				face.unshaded_light_source = true;
				skip_cws(s);
			}
		}
		skip_cws(s);

		if (!parsed_color) {
			mn::panic("face has no color");
		}
		if (!parsed_normal) {
			mn::panic("face has no normal");
		}
		if (!parsed_vertices) {
			mn::panic("face has no vertices");
		}

		mn::buf_push(surf.faces, face);
	}

	return surf;
}

constexpr auto SURF_EXAMPLE = R"(SURF
V 0.000 4.260 0.370 R
V 0.000 4.260 0.370 R
V 0.000 0.020 0.350 R
V 0.000 0.000 4.290 R
V 0.000 4.290 4.290 R
V 4.940 4.258 0.375 R
V 4.940 0.017 0.355 R
V 4.940 -0.003 4.295 R
V 4.940 4.287 4.295 R
F
C 127 127 127
N 0.000 2.143 2.325 -1.000 0.001 -0.001
V 4 3 2 0
E
F
V 0 2 6 5
C 121 127 122
N 2.470 2.139 0.363 0.001 0.005 -1.000
E
F
V 2 3 7 6
C 0 127 127
N 2.470 0.009 2.322 -0.001 -1.000 -0.005
E
F
C 127 2 127
V 3 4 8 7
N 2.470 2.144 4.293 -0.001 0.000 1.000
E
F
N 2.470 4.274 2.332 0.001 1.000 -0.008
V 4 0 5 8
C 127 127 1
B
E
F
V 5 6 7 8
N 4.940 2.140 2.330 1.000 0.000 0.000
C 6 66 127
E
E)";

int main() {
	// float f[3] = {0, 0, 0};
	// char c[2] = {0, 0};
	// mn::log_debug("{} {} {} {} {}", ::sscanf("V 0.000 4.260 -0.370 R\nX 5.12 1.2\n", "V %f %f %f %[R]", &f[0], &f[1], &f[2], c), f[0], f[1], f[2], c);

	// auto s = mn::str_lit(SURF_EXAMPLE);
	// expect(s, "SURF\n");
	// // mn_assert(!accept(s, "sadfwe"));
	// mn_assert(accept(s, "V "));
	// mn_assert(token_double(s) == 0);
	// // mn_assert(_token_str(s) == "0.000");
	// // _skip_white_space(s);
	// // mn_assert(!accept(s, "w23r"));
	// // mn_assert(_token_str(s) == "4.260");
	// expect(s, ' ');
	// mn_assert(token_double(s) == 4.260);
	// // _skip_white_space(s);
	// // mn_assert(!accept(s, "2eri9h"));
	// // mn_assert(_token_str(s) == "0.370");
	// expect(s, ' ');
	// mn_assert(token_double(s) == 0.370);
	// // _skip_white_space(s);
	// mn_assert(accept(s, " R\n"));
	// // _skip_white_space(s);
	// mn_assert(accept(s, "V "));
	// mn::log_debug("ok");

	auto surf = surf_parse(SURF_EXAMPLE);
	mn_defer(surf_free(surf));
	mn::log_debug("surf={}", surf);
	return 0;

	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		mn::log_error("Failed to init video, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_Quit());

	auto wnd = SDL_CreateWindow(
		"JFS",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WinWidth, WinHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
	);
	if (!wnd) {
		mn::log_error("Faield to create window, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_DestroyWindow(wnd));
	SDL_SetWindowBordered(wnd, SDL_TRUE);

	auto cxt = SDL_GL_CreateContext(wnd);
	mn_defer(SDL_GL_DeleteContext(cxt));

	bool running = true;
	bool fullscreen = false;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					running = 0;
					break;
				case 'f':
					fullscreen = !fullscreen;
					if (fullscreen) {
						SDL_SetWindowFullscreen(wnd, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
					} else {
						SDL_SetWindowFullscreen(wnd, SDL_WINDOW_OPENGL);
					}
					break;
				default:
					break;
				}
			} else if (event.type == SDL_QUIT) {
				running = 0;
			}
		}

		glViewport(0, 0, WinWidth, WinHeight);
		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT);

		SDL_GL_SwapWindow(wnd);
	}

	return 0;
}
