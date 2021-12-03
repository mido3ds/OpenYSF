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

double token_double(mn::Str& s) {
	char* pos;
	const double d = strtod(s.ptr, &pos);
	if (s.ptr == pos) {
		mn::panic("no double found");
	}
	const size_t diff = pos - s.ptr;
	s.ptr = pos;
	s.count -= diff;
	return d;
}

uint64_t token_u64(mn::Str& s) {
	if (s.count == 0) {
		mn::panic("end of str");
	} else if (!::isdigit(s[0])) {
		mn::panic("doesn't start with digit");
	}

	char* pos;
	const uint64_t d = strtoull(s.ptr, &pos, 10);
	if (s.ptr == pos) {
		mn::panic("no uint64_t found");
	}
	const uint64_t diff = pos - s.ptr;
	s.ptr = pos;
	s.count -= diff;
	return d;
}

uint8_t token_u8(mn::Str& s) {
	const int64_t i = token_u64(s);
	if (i > 255) {
		mn::panic("number is not a byte");
	}
	return (uint8_t) i;
}

mn::Str token_str(mn::Str& s) {
	const char* a = s.ptr;
	while (s.count > 0 && s[0] != ' ' && s[0] != '\n') {
		s.ptr++;
		s.count--;
	}
	return mn::str_from_substr(a, s.ptr);
}

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

// // skip comments and whitespaces
// void skip_cws(mn::Str& s) {
// 	while (s.count > 0) {
// 		if (s[0] == ' ' || s[0] == '\t' || s[0] == '\n') {
// 			s.ptr++;
// 			s.count--;
// 		} else if (s[0] == '#' | accept(s, "REM ")) {
// 			while (s.count > 0 && s[0] != '\n') {
// 				s.ptr++;
// 				s.count--;
// 			}
// 		} else {
// 			return;
// 		}
// 	}
// }

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
	uint8_t transparency; // 0=obaque, 255=clear
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
				"center: {}, normal: {}, unshaded_light_source: {}, transparency: {}}}", c.vertices_ids, c.color, c.center, c.normal,
				c.unshaded_light_source, (uint64_t) c.transparency);
		}
	};
}

// class of animation
enum class CLA {
	LANDING_GEAR = 0,
	VARIABLE_GEOMETRY_WING = 1,
	AFTERBURNER_REHEAT = 2,
	SPINNER_PROPELLER = 3,
	AIRBRAKE = 4,
	FLAPS = 5,
	ELEVATOR = 6,
	AILERONS = 7,
	RUDDER = 8,
	BOMB_BAY_DOORS = 9,
	VTOL_NOZZLE = 10,
	THRUST_REVERSE = 11,
	THRUST_VECTOR_ANIMATION_LONG = 12, // long time delay (a.k.a. TV-interlock)
	THRUST_VECTOR_ANIMATION_SHORT = 13, // short time delay (a.k.a. High-speed TV-interlock)
	GEAR_DOORS_TRANSITION = 14, // open only for transition, close when gear down
	INSIDE_GEAR_BAY = 15, // shows only when gear is down
	BRAKE_ARRESTER = 16,
	GEAR_DOORS = 17, // open when down
	LOW_THROTTLE = 18, // static object (a.k.a low speed propeller)
	HIGH_THROTTLE = 20, // static object (a.k.a high speed propeller)
	TURRET_OBJECTS = 21,
	ROTATING_WHEELS = 22,
	STEERING = 23,
	NAV_LIGHTS = 30,
	ANTI_COLLISION_LIGHTS = 31,
	STROBE_LIGHTS = 32,
	LANDING_LIGHTS = 33,
	LANDING_GEAR_LIGHTS = 34, // off with gear up

	LEN
};

namespace fmt {
	template<>
	struct formatter<CLA> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const CLA &c, FormatContext &ctx) {
			mn::Str s {};
			switch (c) {
				case CLA::LANDING_GEAR: s = mn::str_lit("CLA::LANDING_GEAR"); break;
				case CLA::VARIABLE_GEOMETRY_WING: s = mn::str_lit("CLA::VARIABLE_GEOMETRY_WING"); break;
				case CLA::AFTERBURNER_REHEAT: s = mn::str_lit("CLA::AFTERBURNER_REHEAT"); break;
				case CLA::SPINNER_PROPELLER: s = mn::str_lit("CLA::SPINNER_PROPELLER"); break;
				case CLA::AIRBRAKE: s = mn::str_lit("CLA::AIRBRAKE"); break;
				case CLA::FLAPS: s = mn::str_lit("CLA::FLAPS"); break;
				case CLA::ELEVATOR: s = mn::str_lit("CLA::ELEVATOR"); break;
				case CLA::AILERONS: s = mn::str_lit("CLA::AILERONS"); break;
				case CLA::RUDDER: s = mn::str_lit("CLA::RUDDER"); break;
				case CLA::BOMB_BAY_DOORS: s = mn::str_lit("CLA::BOMB_BAY_DOORS"); break;
				case CLA::VTOL_NOZZLE: s = mn::str_lit("CLA::VTOL_NOZZLE"); break;
				case CLA::THRUST_REVERSE: s = mn::str_lit("CLA::THRUST_REVERSE"); break;
				case CLA::THRUST_VECTOR_ANIMATION_LONG: s = mn::str_lit("CLA::THRUST_VECTOR_ANIMATION_LONG"); break;
				case CLA::THRUST_VECTOR_ANIMATION_SHORT: s = mn::str_lit("CLA::THRUST_VECTOR_ANIMATION_SHORT"); break;
				case CLA::GEAR_DOORS_TRANSITION: s = mn::str_lit("CLA::GEAR_DOORS_TRANSITION"); break;
				case CLA::INSIDE_GEAR_BAY: s = mn::str_lit("CLA::INSIDE_GEAR_BAY"); break;
				case CLA::BRAKE_ARRESTER: s = mn::str_lit("CLA::BRAKE_ARRESTER"); break;
				case CLA::GEAR_DOORS: s = mn::str_lit("CLA::GEAR_DOORS"); break;
				case CLA::LOW_THROTTLE: s = mn::str_lit("CLA::LOW_THROTTLE"); break;
				case CLA::HIGH_THROTTLE: s = mn::str_lit("CLA::HIGH_THROTTLE"); break;
				case CLA::TURRET_OBJECTS: s = mn::str_lit("CLA::TURRET_OBJECTS"); break;
				case CLA::ROTATING_WHEELS: s = mn::str_lit("CLA::ROTATING_WHEELS"); break;
				case CLA::STEERING: s = mn::str_lit("CLA::STEERING"); break;
				case CLA::NAV_LIGHTS: s = mn::str_lit("CLA::NAV_LIGHTS"); break;
				case CLA::ANTI_COLLISION_LIGHTS: s = mn::str_lit("CLA::ANTI_COLLISION_LIGHTS"); break;
				case CLA::STROBE_LIGHTS: s = mn::str_lit("CLA::STROBE_LIGHTS"); break;
				case CLA::LANDING_LIGHTS: s = mn::str_lit("CLA::LANDING_LIGHTS"); break;
				case CLA::LANDING_GEAR_LIGHTS: s = mn::str_lit("CLA::LANDING_GEAR_LIGHTS"); break;
				default: mn_unreachable();
			}
			return format_to(ctx.out(), "{}", s);
		}
	};
}

struct STA {
	Vec3 pos, rotation;
	uint8_t visibility;
};

namespace fmt {
	template<>
	struct formatter<STA> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const STA &s, FormatContext &ctx) {
			return format_to(ctx.out(), "STA{{pos: {}, rotation: {}, visibility: {}}}", s.pos, s.rotation, s.visibility);
		}
	};
}

struct SURF {
	mn::Str name; // from SRF not FIL
	mn::Buf<Vertex> vertices;
	mn::Buf<Face> faces;
	mn::Buf<uint64_t> gfs, zls, zzs;
	CLA cla;
	mn::Buf<STA> stas; // last one is POS
	Vec3 cnt;
	mn::Buf<mn::Str> children;
};

void surf_free(SURF &self) {
	mn::str_free(self.name);
	mn::destruct(self.vertices);
	mn::destruct(self.faces);
	mn::destruct(self.gfs);
	mn::destruct(self.zls);
	mn::destruct(self.zzs);
	mn::destruct(self.stas);
	mn::destruct(self.children);
}

namespace fmt {
	template<>
	struct formatter<SURF> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const SURF &s, FormatContext &ctx) {
			return format_to(ctx.out(), "SURF{{name: {}, cla: {}, vertices: {}, faces: {}, gfs: {}, zls: {}, zzs: {}, stas: {}, cnt: {}, children: {}}}",
				s.name, s.cla, s.vertices, s.faces, s.gfs, s.zls, s.zzs, s.stas, s.cnt, s.children);
		}
	};
}

template<typename T>
struct Option {
	T val;
	bool ok;
};

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
E
)";

constexpr auto DNM_EXAMPLE = R"(DYNAMODEL
DNMVER 1
PCK habal.srf 25
SURF
V 0.000 4.260 0.370 R
V 0.000 4.260 0.370 R
V 0.000 0.020 0.350 R
F
C 127 127 127
N 0.000 2.143 2.325 -1.000 0.001 -0.001
V 0 1 2
E
F
C 107 127 127
N 0.000 1.143 2.325 -1.000 0.001 -0.001
V 0 1 1
E

GF 0 0 0
GF 1
GF 0 0 1
GF 0 0

ZA 0 200 0 200 1 100 0 200 0 200 0 200
ZA 0 200 0 200 0 250 1 200 0 200
ZL 0 1 0 1
ZZ 0 0 1

SRF "Habal"
FIL habal.srf
CLA 2
NST 2
STA 0.0000 0.0000 0.0000 -32768 -15915 32768 0
STA 0.0000 0.0000 0.0000 0 0 0 1
POS 1.1000 -0.8500 -1.8500 32736 0 0 1
CNT 0.0000 0.0000 0.0000
REL DEP
NCH 1
CLD "Habal"
END
END
)";

using DNM = mn::Map<mn::Str, SURF>;
DNM expect_dnm(mn::Str& s) {
	expect(s, "DYNAMODEL\nDNMVER 1\n");

	auto surfs = mn::map_new<mn::Str, SURF>();
	while (accept(s, "PCK ")) {
		auto name = token_str(s);
		expect(s, ' ');
		const auto expected_lines = token_u64(s);
		auto lines = expected_lines;
		expect(s, '\n');

		expect(s, "SURF\n");
		if (lines-- == 0) {
			mn::panic("expected {} lines, found more", expected_lines);
		}

		SURF surf {};

		// V {x} {y} {z}[ R]\n
		while (accept(s, "V ")) {
			Vertex v {};

			v.pos.x = token_double(s);
			expect(s, ' ');
			v.pos.y = token_double(s);
			expect(s, ' ');
			v.pos.z = token_double(s);
			v.smooth_shading = accept(s, " R");
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}

			mn::buf_push(surf.vertices, v);
		}
		if (surf.vertices.count == 0) {
			mn::panic("must have at least one vertex");
		}

		// <Face>+
		while (!accept(s, '\n')) {
			Face face {};
			bool parsed_color = false,
				parsed_normal = false,
				parsed_vertices = false;

			expect(s, "F\n");
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}
			while (!accept(s, "E\n")) {
				if (accept(s, "C ")) {
					if (parsed_color) {
						mn::panic("found more than one color");
					}
					parsed_color = true;

					face.color.r = token_u8(s);
					expect(s, ' ');
					face.color.g = token_u8(s);
					expect(s, ' ');
					face.color.b = token_u8(s);
					expect(s, '\n');
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
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
					expect(s, '\n');
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				}

				// V {x}...
				if (accept(s, 'V')) {
					if (parsed_vertices) {
						mn::panic("found more than one vertices line");
					}
					parsed_vertices = true;

					while (accept(s, ' ')) {
						uint64_t id = token_u64(s);
						if (id >= surf.vertices.count) {
							mn::panic("id={} out of bounds={}", id, surf.vertices.count);
						}
						mn::buf_push(face.vertices_ids, (size_t) id);
					}
					expect(s, '\n');
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				}
				if (face.vertices_ids.count < 3) {
					mn::panic("face has count of ids={}, it must be >= 3", face.vertices_ids.count);
				}

				if (accept(s, "B\n")) {
					if (face.unshaded_light_source) {
						mn::panic("found more than 1 B");
					}
					face.unshaded_light_source = true;
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				}
			}
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}

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
		if (lines-- == 0) {
			mn::panic("expected {} lines, found more", expected_lines);
		}

		// [GF< {u64}>+\n]+
		while (accept(s, "GF")) {
			while (accept(s, ' ')) {
				auto id = token_u64(s);
				if (id >= surf.faces.count) {
					mn::panic("out of range faceid={}, range={}", id, surf.faces.count);
				}
				mn::buf_push(surf.gfs, id);
			}
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}
		}
		expect(s, '\n');
		if (lines-- == 0) {
			mn::panic("expected {} lines, found more", expected_lines);
		}

		// [ZA< {u64} {u8}>+\n]+
		while (accept(s, "ZA")) {
			while (accept(s, ' ')) {
				auto id = token_u64(s);
				if (id >= surf.faces.count) {
					mn::panic("out of range faceid={}, range={}", id, surf.faces.count);
				}
				expect(s, ' ');
				surf.faces[id].transparency = token_u8(s);
			}
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}
		}

		// [ZL< {u64}>+\n]
		size_t zl_count = 0;
		while (accept(s, "ZL")) {
			zl_count++;
			if (zl_count > 1) {
				mn::panic("found {} > 1 ZLs", zl_count);
			}

			while (accept(s, ' ')) {
				auto id = token_u64(s);
				if (id >= surf.faces.count) {
					mn::panic("out of range faceid={}, range={}", id, surf.faces.count);
				}
				mn::buf_push(surf.zls, id);
			}
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}
		}

		// [ZZ< {u64}>+\n]
		size_t zz_count = 0;
		while (accept(s, "ZZ")) {
			zz_count++;
			if (zz_count > 1) {
				mn::panic("found {} > 1 ZZs", zz_count);
			}

			while (accept(s, ' ')) {
				auto id = token_u64(s);
				if (id >= surf.faces.count) {
					mn::panic("out of range faceid={}, range={}", id, surf.faces.count);
				}
				mn::buf_push(surf.zzs, id);
			}
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}
		}

		expect(s, '\n');
		// last line
		lines--;
		if (lines > 0) {
			mn::panic("expected {} lines, found less", expected_lines);
		}

		mn::map_insert(surfs, name, surf);
	}

	while (accept(s, "SRF ")) {
		auto name = token_str(s);
		if (!(mn::str_prefix(name, "\"") && mn::str_suffix(name, "\""))) {
			mn::panic("name must be in \"\" found={}", name);
		}
		mn::str_trim(name, "\"");
		expect(s, '\n');

		expect(s, "FIL ");
		auto fil = token_str(s);
		expect(s, '\n');
		auto surf = mn::map_lookup(surfs, fil);
		if (!surf) {
			mn::panic("line referenced undeclared surf={}", fil);
		}
		surf->value.name = name;

		expect(s, "CLA ");
		auto cla = token_u8(s);
		if (cla >= (uint8_t) CLA::LEN) {
			mn::panic("invalid CLA={}", cla);
		}
		surf->value.cla = (CLA) cla;
		expect(s, '\n');

		expect(s, "NST ");
		auto num_stas = token_u64(s);
		mn::buf_reserve(surf->value.stas, num_stas);
		expect(s, '\n');
		for (size_t i = 0; i < num_stas+1; i++) {
			if (i == num_stas) {
				expect(s, "POS ");
			} else {
				expect(s, "STA ");
			}

			STA sta {};
			sta.pos.x = token_double(s);
			expect(s, ' ');
			sta.pos.y = token_double(s);
			expect(s, ' ');
			sta.pos.z = token_double(s);
			expect(s, ' ');

			sta.rotation.x = token_double(s);
			expect(s, ' ');
			sta.rotation.y = token_double(s);
			expect(s, ' ');
			sta.rotation.z = token_double(s);
			expect(s, ' ');

			sta.visibility = token_u8(s);
			expect(s, '\n');

			mn::buf_push(surf->value.stas, sta);
		}

		expect(s, "CNT ");
		surf->value.cnt.x = token_double(s);
		expect(s, ' ');
		surf->value.cnt.y = token_double(s);
		expect(s, ' ');
		surf->value.cnt.z = token_double(s);
		expect(s, '\n');

		expect(s, "REL DEP\n");

		expect(s, "NCH ");
		auto num_children = token_u64(s);
		expect(s, '\n');
		mn::buf_reserve(surf->value.children, num_children);
		for (size_t i = 0; i < num_children; i++) {
			expect(s, "CLD ");
			auto child_name = token_str(s);
			if (!(mn::str_prefix(child_name, "\"") && mn::str_suffix(child_name, "\""))) {
				mn::panic("child_name must be in \"\" found={}", child_name);
			}
			mn::str_trim(child_name, "\"");
			mn::buf_push(surf->value.children, child_name);
			expect(s, '\n');
		}
		// reinsert with name instead of FIL
		surf = mn::map_insert(surfs, name, surf->value);
		mn::map_remove(surfs, fil);

		// check children exist
		for (const auto [_, srf] : surfs.values) {
			for (const auto child : srf.children) {
				auto srf2 = mn::map_lookup(surfs, child);
				if (srf2 == nullptr) {
					mn::panic("SURF {} contains child {} that doesn't exist", srf.name, child);
				} else if (srf2->value.name == srf.name) {
					mn::log_warning("SURF {} references itself", child);
				}
			}
		}

		expect(s, "END\n");
	}
	expect(s, "END\n");

	return surfs;
}

int main() {
	auto s = mn::str_lit(DNM_EXAMPLE);
	auto dnm = expect_dnm(s);
	mn_defer(mn::destruct(dnm));
	mn::log_debug("dnm={}", dnm);

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
