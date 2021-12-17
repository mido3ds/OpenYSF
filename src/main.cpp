#include <cstdio>
#include <cstdint>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <glad/glad.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include <mn/Log.h>
#include <mn/Defer.h>
#include <mn/OS.h>

float token_float(mn::Str& s) {
	char* pos;
	const float d = strtod(s.ptr, &pos);
	if (s.ptr == pos) {
		mn::panic("no float found");
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

uint32_t token_u32(mn::Str& s) {
	const uint64_t u = token_u64(s);
	if (u > UINT32_MAX) {
		mn::panic("number is not u32");
	}
	return (uint32_t) u;
}

uint8_t token_u8(mn::Str& s) {
	const uint64_t b = token_u64(s);
	if (b > UINT8_MAX) {
		mn::panic("number is not a byte");
	}
	return (uint8_t) b;
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
	float x, y, z;
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

struct Color {
	uint8_t r, g, b, a;
	// alpha: 0 -> obaque, 255 -> clear
};

namespace fmt {
	template<>
	struct formatter<Color> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Color &c, FormatContext &ctx) {
			return format_to(ctx.out(), "Color{{r: {}, g: {}, b: {}, a: {}}}", c.r, c.g, c.b, c.a);
		}
	};
}

struct Face {
	mn::Buf<uint32_t> vertices_ids;
	Color color;
	Vec3 center, normal;
	bool unshaded_light_source; // ???
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
				"center: {}, normal: {}, unshaded_light_source: {}}}", c.vertices_ids, c.color, c.center, c.normal,
				c.unshaded_light_source);
		}
	};
}

// CLA: class of animation (aircraft or ground object or player controled ground vehicles)
// The CLA animation, possibly standing for class,
// defines what aircraft system an .srf is animated to
// (for example, landing gear, or thrust reverser).
// There are two different animation channel types, aircraft, and ground objects.
// Aircraft animations include all flight oriented animations, as well as lights and turrets.
// Ground object animations are far more simplistic as there are far fewer visual tasks ground objects perform.
// The CLA is applied to the .srf by the "CLA" line in the .dnm footer.
// See https://ysflightsim.fandom.com/wiki/CLA
enum class AnimationClass {
	AIRCRAFT_LANDING_GEAR = 0,
	AIRCRAFT_VARIABLE_GEOMETRY_WING = 1,
	AIRCRAFT_AFTERBURNER_REHEAT = 2,
	AIRCRAFT_SPINNER_PROPELLER = 3,
	AIRCRAFT_AIRBRAKE = 4,
	AIRCRAFT_FLAPS = 5,
	AIRCRAFT_ELEVATOR = 6,
	AIRCRAFT_AILERONS = 7,
	AIRCRAFT_RUDDER = 8,
	AIRCRAFT_BOMB_BAY_DOORS = 9,
	AIRCRAFT_VTOL_NOZZLE = 10,
	AIRCRAFT_THRUST_REVERSE = 11,
	AIRCRAFT_THRUST_VECTOR_ANIMATION_LONG = 12, // long time delay (a.k.a. TV-interlock)
	AIRCRAFT_THRUST_VECTOR_ANIMATION_SHORT = 13, // short time delay (a.k.a. High-speed TV-interlock)
	AIRCRAFT_GEAR_DOORS_TRANSITION = 14, // open only for transition, close when gear down
	AIRCRAFT_INSIDE_GEAR_BAY = 15, // shows only when gear is down
	AIRCRAFT_BRAKE_ARRESTER = 16,
	AIRCRAFT_GEAR_DOORS = 17, // open when down
	AIRCRAFT_LOW_THROTTLE = 18, // static object (a.k.a low speed propeller)
	AIRCRAFT_HIGH_THROTTLE = 20, // static object (a.k.a high speed propeller)
	AIRCRAFT_TURRET_OBJECTS = 21,
	AIRCRAFT_ROTATING_WHEELS = 22,
	AIRCRAFT_STEERING = 23,
	AIRCRAFT_NAV_LIGHTS = 30,
	AIRCRAFT_ANTI_COLLISION_LIGHTS = 31,
	AIRCRAFT_STROBE_LIGHTS = 32,
	AIRCRAFT_LANDING_LIGHTS = 33,
	AIRCRAFT_LANDING_GEAR_LIGHTS = 34, // off with gear up

	GROUND_DEFAULT = 0,
	GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING = 1, // i.e. the turret
	GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING = 2, // i.e. the barrel
	GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING = 3,
	GROUND_SAM_LAUNCHER_VERTICAL_TRACKING = 4,
	GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING = 5, // e.g. those default ground object tanks will shoot at other objects, this is the turret
	GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING = 6,
	GROUND_SPINNING_RADAR_SLOW = 10, // 3 seconds per revolution
	GROUND_SPINNING_RADAR_FAST = 11, // 2 seconds per revolution

	PLAYER_GROUND_LEFT_DOOR = 40,
	PLAYER_GROUND_RIGHT_DOOR = 41,
    PLAYER_GROUND_REAR_DOOR = 42,
    PLAYER_GROUND_CARGO_DOOR = 43,

	UNKNOWN,
};

namespace fmt {
	template<>
	struct formatter<AnimationClass> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const AnimationClass &c, FormatContext &ctx) {
			mn::Str s {};
			switch (c) {
			case AnimationClass::AIRCRAFT_LANDING_GEAR:
			case AnimationClass::GROUND_DEFAULT:
				s = mn::str_lit("(AIRCRAFT_LANDING_GEAR||GROUND_DEFAULT)");
				break;
			case AnimationClass::AIRCRAFT_VARIABLE_GEOMETRY_WING:
			case AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_VARIABLE_GEOMETRY_WING||GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT:
			case AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_AFTERBURNER_REHEAT||GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_SPINNER_PROPELLER:
			case AnimationClass::GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_SPINNER_PROPELLER||GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_AIRBRAKE:
			case AnimationClass::GROUND_SAM_LAUNCHER_VERTICAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_AIRBRAKE||GROUND_SAM_LAUNCHER_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_FLAPS:
			case AnimationClass::GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_FLAPS||GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_ELEVATOR:
			case AnimationClass::GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING:
				s = mn::str_lit("(AIRCRAFT_ELEVATOR||GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_VTOL_NOZZLE:
			case AnimationClass::GROUND_SPINNING_RADAR_SLOW:
				s = mn::str_lit("(AIRCRAFT_VTOL_NOZZLE||GROUND_SPINNING_RADAR_SLOW)");
				break;
			case AnimationClass::AIRCRAFT_THRUST_REVERSE:
			case AnimationClass::GROUND_SPINNING_RADAR_FAST:
				s = mn::str_lit("(AIRCRAFT_THRUST_REVERSE||GROUND_SPINNING_RADAR_FAST)");
				break;

			case AnimationClass::AIRCRAFT_AILERONS: s = mn::str_lit("AIRCRAFT_AILERONS"); break;
			case AnimationClass::AIRCRAFT_RUDDER: s = mn::str_lit("AIRCRAFT_RUDDER"); break;
			case AnimationClass::AIRCRAFT_BOMB_BAY_DOORS: s = mn::str_lit("AIRCRAFT_BOMB_BAY_DOORS"); break;
			case AnimationClass::AIRCRAFT_THRUST_VECTOR_ANIMATION_LONG: s = mn::str_lit("AIRCRAFT_THRUST_VECTOR_ANIMATION_LONG"); break;
			case AnimationClass::AIRCRAFT_THRUST_VECTOR_ANIMATION_SHORT: s = mn::str_lit("AIRCRAFT_THRUST_VECTOR_ANIMATION_SHORT"); break;
			case AnimationClass::AIRCRAFT_GEAR_DOORS_TRANSITION: s = mn::str_lit("AIRCRAFT_GEAR_DOORS_TRANSITION"); break;
			case AnimationClass::AIRCRAFT_INSIDE_GEAR_BAY: s = mn::str_lit("AIRCRAFT_INSIDE_GEAR_BAY"); break;
			case AnimationClass::AIRCRAFT_BRAKE_ARRESTER: s = mn::str_lit("AIRCRAFT_BRAKE_ARRESTER"); break;
			case AnimationClass::AIRCRAFT_GEAR_DOORS: s = mn::str_lit("AIRCRAFT_GEAR_DOORS"); break;
			case AnimationClass::AIRCRAFT_LOW_THROTTLE: s = mn::str_lit("AIRCRAFT_LOW_THROTTLE"); break;
			case AnimationClass::AIRCRAFT_HIGH_THROTTLE: s = mn::str_lit("AIRCRAFT_HIGH_THROTTLE"); break;
			case AnimationClass::AIRCRAFT_TURRET_OBJECTS: s = mn::str_lit("AIRCRAFT_TURRET_OBJECTS"); break;
			case AnimationClass::AIRCRAFT_ROTATING_WHEELS: s = mn::str_lit("AIRCRAFT_ROTATING_WHEELS"); break;
			case AnimationClass::AIRCRAFT_STEERING: s = mn::str_lit("AIRCRAFT_STEERING"); break;
			case AnimationClass::AIRCRAFT_NAV_LIGHTS: s = mn::str_lit("AIRCRAFT_NAV_LIGHTS"); break;
			case AnimationClass::AIRCRAFT_ANTI_COLLISION_LIGHTS: s = mn::str_lit("AIRCRAFT_ANTI_COLLISION_LIGHTS"); break;
			case AnimationClass::AIRCRAFT_STROBE_LIGHTS: s = mn::str_lit("AIRCRAFT_STROBE_LIGHTS"); break;
			case AnimationClass::AIRCRAFT_LANDING_LIGHTS: s = mn::str_lit("AIRCRAFT_LANDING_LIGHTS"); break;
			case AnimationClass::AIRCRAFT_LANDING_GEAR_LIGHTS: s = mn::str_lit("AIRCRAFT_LANDING_GEAR_LIGHTS"); break;

			case AnimationClass::PLAYER_GROUND_LEFT_DOOR: s = mn::str_lit("PLAYER_GROUND_LEFT_DOOR"); break;
			case AnimationClass::PLAYER_GROUND_RIGHT_DOOR: s = mn::str_lit("PLAYER_GROUND_RIGHT_DOOR"); break;
			case AnimationClass::PLAYER_GROUND_REAR_DOOR: s = mn::str_lit("PLAYER_GROUND_REAR_DOOR"); break;
			case AnimationClass::PLAYER_GROUND_CARGO_DOOR: s = mn::str_lit("PLAYER_GROUND_CARGO_DOOR"); break;

			case AnimationClass::UNKNOWN: s = mn::str_lit("UNKNOWN"); break;

			default: mn_unreachable();
			}
			return format_to(ctx.out(), "AnimationClass::{}", s);
		}
	};
}

// STA
// STA's provide boundary conditions for animations to function in YS Flight.
// For most animations they provide minimum and maximum positions for
// srf files to be in based on inputs from the user.
// For example, the flaps require two STAs. One for up, and the other for fully deployed.
// If the user deploys the flaps to 50%, then YS Flight will display the
// animation as 50% of the way between the position of the two STAs.
// STA's can also be used to keep certain elements of the aircraft model from
// being rendered when not being used. This reduces the lag that can be
// generated by detailed models. Toggling the visible, non-visible option will keep the srf at that STA visible, or invisible.
// See https://ysflightsim.fandom.com/wiki/STA
struct MeshState {
	Vec3 pos, rotation;
	bool visible;
};

namespace fmt {
	template<>
	struct formatter<MeshState> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const MeshState &s, FormatContext &ctx) {
			return format_to(ctx.out(), "MeshState{{pos: {}, rotation: {}, visible: {}}}", s.pos, s.rotation, s.visible);
		}
	};
}

// SURF
struct Mesh {
	AnimationClass animation_type = AnimationClass::UNKNOWN;
	Vec3 cnt; // ???

	mn::Str name; // name in SRF not FIL
	mn::Buf<Vec3> vertices;
	mn::Buf<bool> vertices_has_smooth_shading; // ???
	mn::Buf<Face> faces;
	mn::Buf<uint64_t> gfs; // ???
	mn::Buf<uint64_t> zls; // ids of faces to create a sprite at the center of (???)
	mn::Buf<uint64_t> zzs; // ???
	mn::Buf<mn::Str> children; // refers to FIL name not SRF (don't compare against Mesh::name)
	mn::Buf<MeshState> animation_states; // STA

	// POS
	// TODO: sure it's initial state???
	MeshState initial_state;
};

void mesh_free(Mesh &self) {
	mn::str_free(self.name);
	mn::destruct(self.vertices);
	mn::destruct(self.vertices_has_smooth_shading);
	mn::destruct(self.faces);
	mn::destruct(self.gfs);
	mn::destruct(self.zls);
	mn::destruct(self.zzs);
	mn::destruct(self.animation_states);
	mn::destruct(self.children);
}

void destruct(Mesh &self) {
	mesh_free(self);
}

namespace fmt {
	template<>
	struct formatter<Mesh> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Mesh &s, FormatContext &ctx) {
			return format_to(ctx.out(), "Mesh{{name: {}, animation_type: {}, vertices: {}"
			", vertices_has_smooth_shading: {}, faces: {}, gfs: {}, zls: {}"
			", zzs: {}, initial_state:{}, animation_states: {}, cnt: {}, children: {}}}",
				s.name, s.animation_type, s.vertices, s.vertices_has_smooth_shading,
				s.faces, s.gfs, s.zls, s.zzs, s.initial_state, s.animation_states, s.cnt, s.children);
		}
	};
}

// See https://ysflightsim.fandom.com/wiki/DynaModel_Files
mn::Map<mn::Str, Mesh> expect_dnm(mn::Str& s) {
	expect(s, "DYNAMODEL\nDNMVER 1\n");

	auto surfs = mn::map_new<mn::Str, Mesh>();
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

		Mesh surf {};

		// V {x} {y} {z}[ R]\n
		while (accept(s, "V ")) {
			Vec3 v {};

			v.x = token_float(s);
			expect(s, ' ');
			v.y = token_float(s);
			expect(s, ' ');
			v.z = token_float(s);
			bool smooth_shading = accept(s, " R");
			expect(s, '\n');
			if (lines-- == 0) {
				mn::panic("expected {} lines, found more", expected_lines);
			}

			mn::buf_push(surf.vertices, v);
			mn::buf_push(surf.vertices_has_smooth_shading, smooth_shading);
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
				// either C or N or V or B, panic on anything else
				bool parsed = false;

				if (accept(s, "C ")) {
					if (parsed_color) {
						mn::panic("found more than one color");
					}
					parsed_color = true;
					parsed = true;

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
					parsed = true;

					face.center.x = token_float(s);
					expect(s, ' ');
					face.center.y = token_float(s);
					expect(s, ' ');
					face.center.z = token_float(s);
					expect(s, ' ');

					face.normal.x = token_float(s);
					expect(s, ' ');
					face.normal.y = token_float(s);
					expect(s, ' ');
					face.normal.z = token_float(s);
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
					parsed = true;

					while (accept(s, ' ')) {
						uint32_t id = token_u32(s);
						if (id >= surf.vertices.count) {
							mn::panic("id={} out of bounds={}", id, surf.vertices.count);
						}
						mn::buf_push(face.vertices_ids, (uint32_t) id);
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
					parsed = true;
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				}

				if (!parsed) {
					mn::panic("unexpected line, '{}'", s);
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
				surf.faces[id].color.a = token_u8(s);
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
			mn::panic("expected {} lines, found {}", expected_lines, expected_lines - lines);
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
		auto animation_type = token_u8(s);
		surf->value.animation_type = (AnimationClass) animation_type;
		expect(s, '\n');

		expect(s, "NST ");
		auto num_stas = token_u64(s);
		mn::buf_reserve(surf->value.animation_states, num_stas);
		expect(s, '\n');
		for (size_t i = 0; i < num_stas; i++) {
			expect(s, "POS ");

			MeshState sta {};
			sta.pos.x = token_float(s);
			expect(s, ' ');
			sta.pos.y = token_float(s);
			expect(s, ' ');
			sta.pos.z = token_float(s);
			expect(s, ' ');

			sta.rotation.x = token_float(s);
			expect(s, ' ');
			sta.rotation.y = token_float(s);
			expect(s, ' ');
			sta.rotation.z = token_float(s);
			expect(s, ' ');

			sta.visible = token_u8(s) == 1;
			expect(s, '\n');

			mn::buf_push(surf->value.animation_states, sta);
		}

		expect(s, "POS ");
		surf->value.initial_state.pos.x = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.pos.y = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.pos.z = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.rotation.x = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.rotation.y = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.rotation.z = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.visible = token_u8(s) == 1;
		expect(s, '\n');

		expect(s, "CNT ");
		surf->value.cnt.x = token_float(s);
		expect(s, ' ');
		surf->value.cnt.y = token_float(s);
		expect(s, ' ');
		surf->value.cnt.z = token_float(s);
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

constexpr auto TRIANGLE_DNM = R"(DYNAMODEL
DNMVER 1
PCK triangle.srf 12
SURF
V 0.500  0.500 0.000
V 0.500 -0.500 0.000
V -0.500 -0.500 0.000
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 0 1 2
E



END
)";

constexpr auto RECTANGLE_DNM = R"(DYNAMODEL
DNMVER 1
PCK rectangle.srf 18
SURF
V 0.500  0.500 0.000
V 0.500 -0.500 0.000
V -0.500 -0.500 0.000
V -0.500 0.500 0.000
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 0 1 3
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 1 2 3
E



SRF "rectangle"
FIL rectangle.srf
CLA 2
NST 0
POS 0.0000 0.0000 0.0000 0 0 0 1
CNT 0.0000 0.0000 0.0000
REL DEP
NCH 0
END
END
)";

constexpr int WIN_INIT_WIDTH   = 600;
constexpr int WIN_INIT_HEIGHT  = 500;
constexpr Vec3 BG_COLOR {0.392f, 0.584f, 0.929f};

void update_viewport(SDL_Window *sdl_window) {
	int w, h;
	SDL_GetWindowSize(sdl_window, &w, &h);
	int d = w<h? w:h;
	int x = (w - d) / 2;
	int y = (h - d) / 2;
	glViewport(x, y, d, d);
}

#ifdef NDEBUG
#	define GL_CATCH_ERRS() ((void)0)
#else
void glCheckError_(const char *file, int line) {
	GLenum err_code;
	int errors = 0;
    while ((err_code = glGetError()) != GL_NO_ERROR) {
        std::string error;
        switch (err_code) {
		case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
		case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
		case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
		case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
		case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        }
		mn::log_error("GL::{} at {}:{}\n", error, file, line);
		errors++;
    }
	if (errors > 0) {
		mn::panic("found {} opengl errors");
	}
}
#	define GL_CATCH_ERRS() glCheckError_(__FILE__, __LINE__)
#endif

int main() {
	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		mn::log_error("Failed to init video, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_Quit());

	auto sdl_window = SDL_CreateWindow(
		"JFS",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIN_INIT_WIDTH, WIN_INIT_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	);
	if (!sdl_window) {
		mn::log_error("Faield to create window, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_DestroyWindow(sdl_window));
	SDL_SetWindowBordered(sdl_window, SDL_TRUE);

	auto gl_context = SDL_GL_CreateContext(sdl_window);
	mn_defer(SDL_GL_DeleteContext(gl_context));

	// glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		mn::panic("failed to load GLAD function pointers");
	}

	// setup imgui
	IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
		mn::panic("failed to create imgui context");
	}
	mn_defer(ImGui::DestroyContext());

    ImGuiIO& io = ImGui::GetIO(); (void)io;
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // need not to dispatch keyboard controls to app???
	ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL2_InitForOpenGL(sdl_window, gl_context)) {
		mn::panic("failed to init imgui implementation for SDL2");
	}
	mn_defer(ImGui_ImplSDL2_Shutdown());
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
		mn::panic("failed to init imgui implementation for OpenGL3");
	}
	mn_defer(ImGui_ImplOpenGL3_Shutdown());

    // vertex shader
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	constexpr auto vertex_shader_src = R"GLSL(
		#version 330 core
		layout (location = 0) in vec3 attr_pos;
		uniform vec3 position;
		void main() {
			gl_Position = vec4(attr_pos + position, 1.0);
		}
	)GLSL";
    glShaderSource(vertex_shader, 1, &vertex_shader_src, NULL);
    glCompileShader(vertex_shader);

    GLint vertex_shader_success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_shader_success);
    if (!vertex_shader_success) {
    	char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, info_log);
        mn::panic("failed to compile vertex shader, err: {}", info_log);
    }

    // fragment shader
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	constexpr auto fragment_shader_src = R"GLSL(
		#version 330 core
		out vec4 out_fragcolor;
		uniform sampler1D faces_colors;
		void main() {
			float color_index = (gl_PrimitiveID + 0.5f) / textureSize(faces_colors, 0);
			out_fragcolor = texture(faces_colors, color_index);
		}
	)GLSL";
    glShaderSource(fragment_shader, 1, &fragment_shader_src, NULL);
    glCompileShader(fragment_shader);

	GLint fragment_shader_success;
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_shader_success);
    if (!fragment_shader_success) {
    	char info_log[512];
        glGetShaderInfoLog(fragment_shader, 512, NULL, info_log);
        mn::panic("failed to compile fragment shader, err: {}", info_log);
    }

    // link shaders
    const GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

	GLint shader_program_success;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &shader_program_success);
    if (!shader_program_success) {
    	char info_log[512];
        glGetProgramInfoLog(shader_program, 512, NULL, info_log);
        mn::panic("failed to linke vertex and fragment shaders, err: {}", info_log);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
	mn_defer(glDeleteProgram(shader_program));

	// buffers
	auto s = mn::str_lit(RECTANGLE_DNM);
	auto dnm = expect_dnm(s);
	auto srf = mn::map_lookup(dnm, mn::str_lit("rectangle"));
	mn_defer(mn::destruct(srf));
	mn_assert(srf);

	GLuint vao;
    glGenVertexArrays(1, &vao);
	mn_defer(glDeleteVertexArrays(1, &vao));
	glBindVertexArray(vao);
		GLuint vbo;
		glGenBuffers(1, &vbo);
		mn_defer(glDeleteBuffers(1, &vbo));
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, srf->value.vertices.count * sizeof(Vec3) * sizeof(float), srf->value.vertices.ptr, GL_STATIC_DRAW);

		GLuint ebo;
		glGenBuffers(1, &ebo);
		mn_defer(glDeleteBuffers(1, &ebo));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		auto indices = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
		for (const auto& face : srf->value.faces) {
			mn::buf_concat(indices, face.vertices_ids);
		}
		const size_t indices_num_bytes = indices.count * sizeof(uint32_t);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_num_bytes, indices.ptr, GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	// faces colors
	glEnable(GL_TEXTURE_1D);
	GLuint color_texture;
	glGenTextures(1, &color_texture);
	glBindTexture(GL_TEXTURE_1D, color_texture);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	auto colors = mn::buf_with_allocator<Color>(mn::memory::tmp());
	for (const auto& face : srf->value.faces) {
		mn::buf_push(colors, face.color);
	}
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, colors.count, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.ptr);
	// bind in shader
	glUseProgram(shader_program);
	glUniform1i(glGetUniformLocation(shader_program, "faces_colors"), 0);

	GL_CATCH_ERRS();

	update_viewport(sdl_window);

	bool running = true;
	bool fullscreen = false;

	while (running) {
		mn::memory::tmp()->clear_all();

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case 'q':
				case SDLK_ESCAPE:
					running = false;
					break;
				case 'f':
					fullscreen = !fullscreen;
					if (fullscreen) {
						SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
					} else {
						SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL);
					}
					update_viewport(sdl_window);
					break;
				default:
					break;
				}
			} else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				update_viewport(sdl_window);
			} else if (event.type == SDL_QUIT) {
				running = false;
			}
		}

		glClearColor(BG_COLOR.x, BG_COLOR.y, BG_COLOR.z, 0.f);
		glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shader_program);
		glUniform3fv(glGetUniformLocation(shader_program, "position"), 1, (GLfloat*) &srf->value.initial_state.pos); // position (single srf)
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices_num_bytes, GL_UNSIGNED_INT, 0);

		// imgui
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

		if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::SliderFloat3("pos", (float*) &srf->value.initial_state.pos, -1, 1);

			ImGui::End();
		}

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(sdl_window);

		GL_CATCH_ERRS();
	}

	return 0;
}
/*
TODO:
- mesh rotation
*/
