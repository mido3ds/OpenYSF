#include <cstdio>
#include <cstdint>

// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <glad/glad.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

// don't export min/max/near/far definitions with windows.h otherwise other includes might break
#define NOMINMAX
#include <portable-file-dialogs.h>
#undef near
#undef far

#include <mn/Log.h>
#include <mn/Defer.h>
#include <mn/OS.h>
#include <mn/Thread.h>
#include <mn/Path.h>

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/gtc/type_ptr.hpp> // glm::value_ptr

namespace fmt {
	template<>
	struct formatter<glm::uvec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::uvec2 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::uvec2{{{}, {}}}", v.x, v.y);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec2 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec2{{{}, {}}}", v.x, v.y);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec3> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec3 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec3{{{}, {}, {}}}", v.x, v.y, v.z);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec4 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec4{{{}, {}, {}, {}}}", v.x, v.y, v.z, v.w);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::mat4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::mat4 &m, FormatContext &ctx) {
			return format_to(
				ctx.out(),
				"glm::mat4{{\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				"}}",
				m[0][0], m[0][1], m[0][2], m[0][3],
				m[1][0], m[1][1], m[1][2], m[1][3],
				m[2][0], m[2][1], m[2][2], m[2][3],
				m[3][0], m[3][1], m[3][2], m[3][3]
			);
		}
	};
}

// euclidean modulo (https://stackoverflow.com/a/52529440)
// always positive
constexpr
int mod(int a, int b) {
	const auto r = a % b;
	if (r < 0) {
		// return r + (b < 0) ? -b : b; // avoid this form: it is UB when b == INT_MIN
		return (b < 0) ? r - b : r + b;
	}
	return r;
}

static_assert(mod(+7, +3) == 1);
static_assert(mod(+7, -3) == 1);
static_assert(mod(-7, +3) == 2);
static_assert(mod(-7, -3) == 2);
static_assert(mod(0-1, 5) == 4);

mn::Str smaller_str(const mn::Str& s) {
	auto s2 = mn::str_clone(s, mn::memory::tmp());
	if (s2.count > 90) {
		mn::str_resize(s2, 90);
		mn::str_push(s2, "....");
	}
	return s2;
}

float token_float(mn::Str& s) {
	if (s.count == 0) {
		mn::panic("end of str");
	} else if (!(::isdigit(s[0]) || s[0] == '-')) {
		mn::panic("doesn't start with digit or -");
	}

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
		mn::panic("doesn't start with digit, {}", smaller_str(s));
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

int32_t token_i32(mn::Str& s) {
	if (s.count == 0) {
		mn::panic("end of str");
	} else if (!(::isdigit(s[0]) || s[0] == '-')) {
		mn::panic("doesn't start with digit, {}", smaller_str(s));
	}

	char* pos;
	const long d = strtol(s.ptr, &pos, 10);
	if (s.ptr == pos) {
		mn::panic("no long found");
	}
	if (d < INT32_MIN || d > INT32_MAX) {
		mn::panic("number is not i32");
	}
	const int32_t diff = pos - s.ptr;
	s.ptr = pos;
	s.count -= diff;
	return d;
}

uint8_t token_u8(mn::Str& s) {
	const uint64_t b = token_u64(s);
	if (b > UINT8_MAX) {
		mn::panic("number is not a byte");
	}
	return (uint8_t) b;
}

mn::Str token_str(mn::Str& s, mn::Allocator allocator = mn::allocator_top()) {
	const char* a = s.ptr;
	while (s.count > 0 && s[0] != ' ' && s[0] != '\n') {
		s.ptr++;
		s.count--;
	}
	return mn::str_from_substr(a, s.ptr, allocator);
}

bool peek(const mn::Str& s, char c) {
	if (s.count == 0) {
		return false;
	}
	return *s.ptr == c;
}

bool peek(const mn::Str& s1, const char* s2) {
	const size_t len = ::strlen(s2);
	if (len > s1.count) {
		return false;
	}
	return ::memcmp(s1.ptr, s2, len) == 0;
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
		mn::panic("failed to find '{}' in '{}'", s2, smaller_str(s1));
	}
}

void skip_after(mn::Str& s, char c) {
	const size_t index = mn::str_find(s, c, 0);
	if (index == SIZE_MAX) {
		mn::panic("failed to find '{}' in '{}'", c, smaller_str(s));
	}
	s.ptr += index + 1;
	s.count -= index + 1;
}

struct Face {
	mn::Buf<uint32_t> vertices_ids;
	glm::vec4 color;
	glm::vec3 center, normal;
};

void face_free(Face& self) {
	mn::destruct(self.vertices_ids);
}

void destruct(Face& self) {
	face_free(self);
}

namespace fmt {
	template<>
	struct formatter<Face> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Face &c, FormatContext &ctx) {
			return format_to(ctx.out(), "Face{{vertices_ids: {}, color: {}, "
				"center: {}, normal: {}}}", c.vertices_ids, c.color, c.center, c.normal);
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
			case AnimationClass::AIRCRAFT_LANDING_GEAR /*| AnimationClass::GROUND_DEFAULT*/:
				s = mn::str_lit("(AIRCRAFT_LANDING_GEAR||GROUND_DEFAULT)");
				break;
			case AnimationClass::AIRCRAFT_VARIABLE_GEOMETRY_WING /*| AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_VARIABLE_GEOMETRY_WING||GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT /*| AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_AFTERBURNER_REHEAT||GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_SPINNER_PROPELLER /*| AnimationClass::GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_SPINNER_PROPELLER||GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_AIRBRAKE /*| AnimationClass::GROUND_SAM_LAUNCHER_VERTICAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_AIRBRAKE||GROUND_SAM_LAUNCHER_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_FLAPS /*| AnimationClass::GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_FLAPS||GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_ELEVATOR /*| AnimationClass::GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING*/:
				s = mn::str_lit("(AIRCRAFT_ELEVATOR||GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING)");
				break;
			case AnimationClass::AIRCRAFT_VTOL_NOZZLE /*| AnimationClass::GROUND_SPINNING_RADAR_SLOW*/:
				s = mn::str_lit("(AIRCRAFT_VTOL_NOZZLE||GROUND_SPINNING_RADAR_SLOW)");
				break;
			case AnimationClass::AIRCRAFT_THRUST_REVERSE /*| AnimationClass::GROUND_SPINNING_RADAR_FAST*/:
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
// animation as 50% of the way between the translation of the two STAs.
// STA's can also be used to keep certain elements of the aircraft model from
// being rendered when not being used. This reduces the lag that can be
// generated by detailed models. Toggling the visible, non-visible option will keep the srf at that STA visible, or invisible.
// See https://ysflightsim.fandom.com/wiki/STA
struct MeshState {
	glm::vec3 translation;
	glm::vec3 rotation; // roll, pitch, yaw
	bool visible;
};

namespace fmt {
	template<>
	struct formatter<MeshState> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const MeshState &s, FormatContext &ctx) {
			return format_to(ctx.out(), "MeshState{{pos: {}, rotation: {}, visible: {}}}", s.translation, s.rotation, s.visible);
		}
	};
}

// SURF
struct Mesh {
	bool is_light_source = false;
	AnimationClass animation_type = AnimationClass::UNKNOWN;

	// CNT = contra-position, see https://forum.ysfhq.com/viewtopic.php?p=94793&sid=837b2845906af55fe13e82afcc183d2f#p94793
	// Basically for modders: you can make your full model with all parts in the place where they are located on the plane.
	// for instance you draw the left main gear at -1,45 meters on the x-axis, and -1 meter to the back on z-axis (in Gepolyx).
	//
	// Then you cut the part from the mesh and save it.
	// In DNM now, you add the gear SRF, but it rotates on 0,0,0 middle point, which gives a wrong animation.
	// So, you enter the exact coordinates of the SRF you just made in the CNT line (I think it means Counter or contra-location), in above example x=-1,45 y=0 z=-1
	// In DNM viewer the part has now moved to the middle of the plane.
	//
	// Then you locate the part again on the place where it should be.
	// Result, the animation is seamless.
	//
	// especially with geardoors and bombdoors this is very important as they close exact and you wont see any cracks.
	// Flaps and ailerons and the like are also easily made (in the wing) and they move much better.
	glm::vec3 cnt;

	mn::Str name; // name in SRF not FIL
	mn::Buf<glm::vec3> vertices;
	mn::Buf<bool> vertices_has_smooth_shading; // ???
	mn::Buf<Face> faces;
	mn::Buf<uint64_t> gfs; // ???
	mn::Buf<uint64_t> zls; // ids of faces to create a sprite at the center of (???)
	mn::Buf<uint64_t> zzs; // ???
	mn::Buf<mn::Str> children; // refers to FIL name not SRF (don't compare against Mesh::name)
	mn::Buf<MeshState> animation_states; // STA

	// POS
	MeshState initial_state; // should be kepts const after init

	struct {
		GLuint vao, vbo;
		size_t array_count;
	} gpu;

	// physics
	glm::mat4 transformation;
	MeshState current_state;

	bool render_pos_axis;
	bool render_cnt_axis;
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

void mesh_unload_from_gpu(Mesh& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};
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
			", zzs: {}, initial_state:{}, animation_states: {}, cnt: {}, children: {}, is_light_source: {}}}",
				s.name, s.animation_type, s.vertices, s.vertices_has_smooth_shading,
				s.faces, s.gfs, s.zls, s.zzs, s.initial_state, s.animation_states, s.cnt, s.children, s.is_light_source);
		}
	};
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

// region R = { (x, y, z) | min.x<=x<=max.x, min.y<=y<=max.y, min.z<=z<=max.z }
struct AABB {
	glm::vec3 min, max;
};

namespace fmt {
	template<>
	struct formatter<AABB> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const AABB &s, FormatContext &ctx) {
			return format_to(ctx.out(), "AABB{{max: {}, min: {}}}", s.min, s.max);
		}
	};
}

// no intersection if separated along an axis
// overlapping on all axes means AABBs are intersecting
bool aabbs_intersect(const AABB& a, const AABB& b) {
	return glm::all(glm::greaterThanEqual(a.max, b.min)) && glm::all(glm::greaterThanEqual(b.max, a.min));
}

struct Box {
	glm::vec3 translation, scale, color;
};

Box aabb_to_box(const AABB& aabb) {
	return Box {
		.translation = aabb.min,
		.scale = aabb.max - aabb.min,
	};
}

void test_aabbs_intersection() {
	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, 3.0f},
			.max={3.0f, 3.0f, 4.0f},
		};
		mn_assert(aabbs_intersect(x, y));
	}

	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, -3.0f},
			.max={3.0f, 3.0f, -4.0f},
		};
		mn_assert(aabbs_intersect(x, y) == false);
	}

	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, -3.0f},
			.max={3.0f, 3.0f, 4.0f},
		};
		mn_assert(aabbs_intersect(x, y));
	}

	mn::log_debug("test_aabbs_intersection: all passed");
}

// DNM See https://ysflightsim.fandom.com/wiki/DynaModel_Files
struct Model {
	mn::Str file_abs_path;
	bool should_open_dnm_file;
	bool should_load_model;
	bool enable_rotating_around;

	mn::Map<mn::Str, Mesh> meshes;
	mn::Buf<size_t> root_meshes_indices;

	AABB initial_aabb;
	AABB current_aabb;
	bool render_aabb;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		bool visible = true;
	} current_state;
};

void model_load_to_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes.values) {
		struct Stride {
			glm::vec3 vertex;
			glm::vec4 color;
			glm::vec3 normal;
		};
		auto buffer = mn::buf_with_allocator<Stride>(mn::memory::tmp());
		for (const auto& face : mesh.faces) {
			for (size_t i = 0; i < face.vertices_ids.count; i++) {
				mn::buf_push(buffer, Stride {
					.vertex=mesh.vertices[face.vertices_ids[i]],
					.color=face.color,
					.normal=face.normal,
				});
			}
		}
		mesh.gpu.array_count = buffer.count;

		glGenVertexArrays(1, &mesh.gpu.vao);
		glBindVertexArray(mesh.gpu.vao);
			glGenBuffers(1, &mesh.gpu.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, mesh.gpu.vbo);
			glBufferData(GL_ARRAY_BUFFER, buffer.count * sizeof(Stride), buffer.ptr, GL_STATIC_DRAW);

			size_t offset = 0;

			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				0,              /*index*/
				3,              /*#components*/
				GL_FLOAT,       /*type*/
				GL_FALSE,       /*normalize*/
				sizeof(Stride), /*stride bytes*/
				(void*)offset   /*offset*/
			);
			offset += sizeof(Stride::vertex);

			glEnableVertexAttribArray(1);
			glVertexAttribPointer(
				1,              /*index*/
				4,              /*#components*/
				GL_FLOAT,       /*type*/
				GL_FALSE,       /*normalize*/
				sizeof(Stride), /*stride bytes*/
				(void*)offset   /*offset*/
			);
			offset += sizeof(Stride::color);

			glEnableVertexAttribArray(2);
			glVertexAttribPointer(
				2,              /*index*/
				3,              /*#components*/
				GL_FLOAT,       /*type*/
				GL_FALSE,       /*normalize*/
				sizeof(Stride), /*stride bytes*/
				(void*)offset   /*offset*/
			);
			offset += sizeof(Stride::normal);
		glBindVertexArray(0);

		GL_CATCH_ERRS();
	}
}

void model_unload_from_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes.values) {
		mesh_unload_from_gpu(mesh);
	}
}

// margin of error
constexpr double EPS = 0.001;

bool
almost_equal(const glm::vec3& a, const glm::vec3& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS && ::fabs(c.z) < EPS;
}

bool
almost_equal(const glm::vec2& a, const glm::vec2& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS;
}

bool
almost_equal(const glm::vec4& a, const glm::vec4& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS && ::fabs(c.z) < EPS && ::fabs(c.w) < EPS;
}

bool
almost_equal(const float& a, const float& b) {
	return ::fabs(a - b) < EPS;
}

// http://paulbourke.net/geometry/pointlineplane/
// http://paulbourke.net/geometry/pointlineplane/lineline.c
bool lines_intersect(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4) {
	const glm::vec3 p43 = p4 - p3;
	if (almost_equal(p43, {0,0,0})) {
		return false;
	}

	const glm::vec3 p21 = p2 - p1;
	if (almost_equal(p21, {0,0,0})) {
		return false;
	}

	const glm::vec3 p13 = p1 - p3;
	const double d1343 = glm::dot(p13, p43);
	const double d4321 = glm::dot(p43, p21);
	const double d1321 = glm::dot(p13, p21);
	const double d4343 = glm::dot(p43, p43);
	const double d2121 = glm::dot(p21, p21);

	const double denom = d2121 * d4343 - d4321 * d4321;
	if (almost_equal(denom, 0)) {
		return false;
	}
	const double numer = d1343 * d4321 - d1321 * d4343;

	const double mua = numer / denom;
	const double mub = (d1343 + d4321 * (mua)) / d4343;

	return mua >= 0 && mua <= 1 && mub >= 0 && mub <= 1;
}

mn::Buf<uint32_t>
polygons_to_triangles(const mn::Buf<glm::vec3>& vertices, const mn::Buf<uint32_t>& orig_indices, const glm::vec3& center) {
	// dbl_indices -> orig_indices -> vertices
	// vertex = vertices[orig_indices[dbl_indices[i]]]
	// indices to indices to vertices
	// sort dbl_indices from farthest from center to nearst
	auto dbl_indices = mn::buf_with_allocator<size_t>(mn::memory::tmp());
	for (size_t i = 0; i < orig_indices.count; i++) {
		mn::buf_push(dbl_indices, i);
	}
	auto dist_from_center = mn::buf_with_allocator<double>(mn::memory::tmp());
	for (const auto& v : vertices) {
		mn::buf_push(dist_from_center, glm::distance(center, v));
	}
	std::sort(mn::begin(dbl_indices), mn::end(dbl_indices), [&](size_t a, size_t b) {
		return dist_from_center[orig_indices[a]] > dist_from_center[orig_indices[b]];
	});

	mn::Buf<uint32_t> out {};
	auto indices = mn::buf_clone(orig_indices, mn::memory::tmp());

	// limit no of iterations to avoid inf loop
	size_t k = indices.count + 1;
	while (k > 0 && indices.count > 3) {
		k--;

		for (size_t j = 0; j < dbl_indices.count; j++) {
			auto i = dbl_indices[j];

			// indices
			const uint32_t iv0 = indices[mod(i-1, indices.count)];
			const uint32_t iv2 = indices[mod(i+1, indices.count)];

			bool is_ear = true;

			// segment: (v0, v2) must not intersect with any other edge in polygon
			// for edge in edges:
			//   if not share_vertex(segment, edge):
			//     if intersects(segment, edge): return false
			for (size_t j = 0; j < indices.count; j++) {
				// edge
				const uint32_t jv0 = indices[j];
				const uint32_t jv1 = indices[mod(j+1, indices.count)];

				// don't test the edge if it shares a vertex with it
				if ((jv0 != iv0 && jv0 != iv2) && (jv1 != iv0 && jv1 != iv2)) {
					if (lines_intersect(vertices[jv0], vertices[jv1], vertices[iv0], vertices[iv2])) {
						is_ear = false;
						break;
					}
				}
			}

			if (is_ear) {
				mn::buf_push(out, indices[mod(i-1, indices.count)]);
				mn::buf_push(out, indices[i]);
				mn::buf_push(out, indices[mod(i+1, indices.count)]);

				mn::buf_remove_ordered(indices, i);
				mn::buf_remove_ordered(dbl_indices, j);

				for (auto& id : dbl_indices) {
					if (id > i) {
						id--;
					}
				}

				// exit the loop so that we check again the first vertex of the loop, maybe it became now a convex one
				break;
			}
		}
	}

	if (indices.count != 3) {
		mn::log_error("failed to tesselate");
	}
	mn::buf_concat(out, indices);
	return out;
}

template<typename T>
bool buf_equal(const mn::Buf<T>& a, const mn::Buf<T> b) {
	if (a.count != b.count) {
		return false;
	}
	for (size_t i = 0; i < a.count; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

void test_polygons_to_triangles() {
	mn::allocator_push(mn::memory::tmp());
	mn_defer(mn::allocator_pop());

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4})));
	}

	{
		// shouldn't intersect
		glm::vec3 a {2,4,0};
		glm::vec3 b {4,4,0};
		glm::vec3 c {4,3,0};
		glm::vec3 d {3,2,0};

		mn_assert(lines_intersect(a, b, c, d) == false);
	}

	{
		// shouldn't intersect
		glm::vec3 a {1.311345,  0.627778,  1.068002};
		glm::vec3 b {1.311345, -0.000053, -1.472697};
		glm::vec3 c {1.311345, -0.000053,  1.717336};
		glm::vec3 d {1.311345,  0.512254,  2.414495};

		mn_assert(lines_intersect(a, b, c, d) == false);
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{4,4,0},
			{5,3,0},
			{4,2,0},
			{3,3,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3});
		const glm::vec3 center {4, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({3, 0, 1, 1, 2, 3})));
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4})));
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{0.19, -0.77, 0.82},
			{0.23, -0.75, 0.68},
			{0.20, -0.75, 0.00},
			{0.32, -0.71, 0.00},
			{0.31, -0.73, 0.96},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {0.25, -0.742, 0.492};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({2, 3, 4, 1, 2, 4, 0, 1, 4})));
	}

	mn::log_debug("test_polygons_to_triangles: all passed");
}

size_t get_line_no(const mn::Str& str1, const mn::Str& str2) {
	size_t all_lines = 0;
	for (auto c : str1) {
		if (c == '\n') {
			all_lines++;
		}
	}

	size_t partial_lines = 0;
	for (auto c : str2) {
		if (c == '\n') {
			partial_lines++;
		}
	}

	return all_lines - partial_lines + 1;
}

// YS angle format, degrees(0->360): YS(0x0000->0xFFFF), extracted from ys blender scripts
constexpr float YS_MAX      = 0xFFFF;
constexpr float RADIANS_MAX = 6.283185307179586f;
constexpr float DEGREES_MAX = 360.0f;

Model model_from_dnm(const mn::Str& dnm_file_abs_path) {
	const auto dnm_file = mn::file_content_str(dnm_file_abs_path, mn::memory::tmp());
	auto s = mn::str_clone(dnm_file, mn::memory::tmp());
	mn::str_replace(s, "\r\n", "\n");

	expect(s, "DYNAMODEL\nDNMVER ");
	const uint8_t dnm_version = token_u8(s);
	if (dnm_version > 2) {
		mn::panic("unsupported version {}", dnm_version);
	}
	expect(s, '\n');

	auto surfs = mn::map_new<mn::Str, Mesh>();
	while (accept(s, "PCK ")) {
		auto name = token_str(s, mn::memory::tmp());
		expect(s, ' ');
		const auto pck_expected_linenos = token_u64(s);
		expect(s, '\n');

		const auto pck_first_lineno = get_line_no(dnm_file, s);

		// aircraft/cessna172r.dnm has Surf instead of SURF
		if ((accept(s, "SURF\n") || accept(s, "Surf\n")) == false) {
			mn::panic("'{}':{} failed to find SURF/Surf for start of mesh", name, get_line_no(dnm_file, s));
		}
		Mesh surf {};

		// V {x} {y} {z}[ R]\n
		while (accept(s, "V ")) {
			glm::vec3 v {};

			v.x = token_float(s);
			expect(s, ' ');
			v.y = -token_float(s);
			expect(s, ' ');
			v.z = token_float(s);
			bool smooth_shading = accept(s, " R");

			// aircraft/cessna172r.dnm has spaces after end if V
			while (accept(s, " ")) {}

			expect(s, '\n');

			mn::buf_push(surf.vertices, v);
			mn::buf_push(surf.vertices_has_smooth_shading, smooth_shading);
		}
		if (surf.vertices.count == 0) {
			mn::log_error("'{}': doesn't have any vertices!", name);
		}

		// <Face>+
		auto faces_unshaded_light_source = mn::buf_with_allocator<bool>(mn::memory::tmp());
		while (accept(s, "F\n")) {
			Face face {};
			bool parsed_color = false,
				parsed_normal = false,
				parsed_vertices = false,
				is_light_source = false;

			while (!accept(s, "E\n")) {
				if (accept(s, "C ")) {
					if (parsed_color) {
						mn::panic("'{}': found more than one color", name);
					}
					parsed_color = true;

					face.color.r = token_u8(s) / 255.0f;
					expect(s, ' ');
					face.color.g = token_u8(s) / 255.0f;
					expect(s, ' ');
					face.color.b = token_u8(s) / 255.0f;

					// aircraft/cessna172r.dnm allows alpha value in color
					if (accept(s, ' ')) {
						face.color.a = token_u8(s) / 255.0f;
					} else {
						face.color.a = 1.0f; // maybe overwritten in ZA line
					}

					expect(s, '\n');
				} else if (accept(s, "N ")) {
					if (parsed_normal) {
						mn::panic("'{}': found more than one normal", name);
					}
					parsed_normal = true;

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
				} else if (accept(s, 'V')) {
					// V {x}...
					auto polygon_vertices_ids = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
					while (accept(s, ' ')) {
						uint32_t id = token_u32(s);
						if (id >= surf.vertices.count) {
							mn::panic("'{}': id={} out of bounds={}", name, id, surf.vertices.count);
						}
						mn::buf_push(polygon_vertices_ids, (uint32_t) id);
					}
					expect(s, '\n');

					if (parsed_vertices) {
						mn::log_error("'{}': found more than one vertices line, ignore others", name);
					} else {
						parsed_vertices = true;

						if (polygon_vertices_ids.count < 3) {
							mn::log_error("'{}': face has count of ids={}, it should be >= 3, {}", name, polygon_vertices_ids.count, smaller_str(s));
						}

						face.vertices_ids = polygons_to_triangles(surf.vertices, polygon_vertices_ids, face.center);
						if (face.vertices_ids.count % 3 != 0) {
							auto orig_vertices = mn::buf_with_allocator<glm::vec3>(mn::memory::tmp());
							for (auto id : polygon_vertices_ids) {
								mn::buf_push(orig_vertices, surf.vertices[id]);
							}
							auto new_vertices = mn::buf_with_allocator<glm::vec3>(mn::memory::tmp());
							for (auto id : face.vertices_ids) {
								mn::buf_push(new_vertices, surf.vertices[id]);
							}
							mn::log_error("{}:{}: num of vertices_ids must have been divisble by 3 to be triangles, but found {}, original vertices={}, new vertices={}", name, get_line_no(dnm_file, s),
								face.vertices_ids.count, orig_vertices, new_vertices);
						}
					}
				} else if (accept(s, "B\n")) {
					if (is_light_source) {
						mn::log_error("'{}': found more than 1 B for same face", name);
					}
					is_light_source = true;
				} else {
					mn::panic("'{}': unexpected line, '{}'", name, smaller_str(s));
				}
			}

			if (!parsed_color) {
				mn::log_error("'{}': face has no color", name);
			}
			if (!parsed_normal) {
				mn::log_error("'{}': face has no normal", name);
			}
			if (!parsed_vertices) {
				mn::log_error("'{}': face has no vertices", name);
			}

			mn::buf_push(faces_unshaded_light_source, is_light_source);
			mn::buf_push(surf.faces, face);
		}

		size_t zl_count = 0;
		size_t zz_count = 0;
		while (true) {
			if (accept(s, '\n')) {
				// nothing
			} else if (accept(s, "GE") || accept(s, "ZE") || accept(s, "GL")) {
				skip_after(s, '\n');
			} else if (accept(s, "GF")) { // [GF< {u64}>+\n]+
				while (accept(s, ' ')) {
					auto id = token_u64(s);
					if (id >= surf.faces.count) {
						mn::panic("'{}': out of range faceid={}, range={}", name, id, surf.faces.count);
					}
					mn::buf_push(surf.gfs, id);
				}
				expect(s, '\n');
			} else if (accept(s, "ZA")) { // [ZA< {u64} {u8}>+\n]+
				while (accept(s, ' ')) {
					auto id = token_u64(s);
					if (id >= surf.faces.count) {
						mn::panic("'{}': out of range faceid={}, range={}", name, id, surf.faces.count);
					}
					expect(s, ' ');
					surf.faces[id].color.a = (255 - token_u8(s)) / 255.0f;
					// because alpha came as: 0 -> obaque, 255 -> clear
					// we revert it so it becomes: 1 -> obaque, 0 -> clear
				}
				expect(s, '\n');
			} else if (accept(s, "ZL")) { // [ZL< {u64}>+\n]
				zl_count++;
				if (dnm_version == 1) {
					if (zl_count > 1) {
						mn::panic("'{}': found {} > 1 ZLs", name, zl_count);
					}
				}

				while (accept(s, ' ')) {
					auto id = token_u64(s);
					if (id >= surf.faces.count) {
						mn::panic("'{}': out of range faceid={}, range={}", name, id, surf.faces.count);
					}
					mn::buf_push(surf.zls, id);
				}
				expect(s, '\n');
			} else if  (accept(s, "ZZ")) { // [ZZ< {u64}>+\n]
				zz_count++;
				if (zz_count > 1) {
					mn::panic("'{}': found {} > 1 ZZs", name, zz_count);
				}

				while (accept(s, ' ')) {
					auto id = token_u64(s);
					if (id >= surf.faces.count) {
						mn::panic("'{}': out of range faceid={}, range={}", name, id, surf.faces.count);
					}
					mn::buf_push(surf.zzs, id);
				}
				expect(s, '\n');
			} else {
				break;
			}
		}

		// last line
		{
			const auto current_lineno = get_line_no(dnm_file, s);
			const auto pck_found_linenos = current_lineno - pck_first_lineno - 1;
			if (pck_found_linenos != pck_expected_linenos) {
				mn::log_error("'{}':{} expected {} lines in PCK, found {}", name, current_lineno, pck_expected_linenos, pck_found_linenos);
			}
		}

		size_t unshaded_count = 0;
		for (auto unshaded : faces_unshaded_light_source) {
			if (unshaded) {
				unshaded_count++;
			}
		}
		surf.is_light_source = (unshaded_count == faces_unshaded_light_source.count);

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
		auto fil = token_str(s, mn::memory::tmp());
		expect(s, '\n');
		auto surf = mn::map_lookup(surfs, fil);
		if (!surf) {
			mn::panic("'{}': line referenced undeclared surf={}", name, fil);
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
			expect(s, "STA ");

			MeshState sta {};
			sta.translation.x = token_float(s);
			expect(s, ' ');
			sta.translation.y = -token_float(s);
			expect(s, ' ');
			sta.translation.z = token_float(s);
			expect(s, ' ');

			// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
			sta.rotation.x = token_float(s) / YS_MAX * RADIANS_MAX;
			expect(s, ' ');
			sta.rotation.y = token_float(s) / YS_MAX * RADIANS_MAX;
			expect(s, ' ');
			sta.rotation.z = token_float(s) / YS_MAX * RADIANS_MAX;
			expect(s, ' ');

			uint8_t visible = token_u8(s);
			if (visible == 1 || visible == 0) {
				sta.visible = (visible == 1);
			} else {
				mn::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, get_line_no(dnm_file, s), visible);
			}
			expect(s, '\n');

			mn::buf_push(surf->value.animation_states, sta);
		}

		bool read_pos = false, read_cnt = false, read_rel_dep = false, read_nch = false;
		while (true) {
			if (accept(s, "POS ")) {
				read_pos = true;

				surf->value.initial_state.translation.x = token_float(s);
				expect(s, ' ');
				surf->value.initial_state.translation.y = -token_float(s);
				expect(s, ' ');
				surf->value.initial_state.translation.z = token_float(s);
				expect(s, ' ');

				// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
				surf->value.initial_state.rotation.x = token_float(s) / YS_MAX * RADIANS_MAX;
				expect(s, ' ');
				surf->value.initial_state.rotation.y = token_float(s) / YS_MAX * RADIANS_MAX;
				expect(s, ' ');
				surf->value.initial_state.rotation.z = token_float(s) / YS_MAX * RADIANS_MAX;

				// aircraft/cessna172r.dnm is the only file with no visibility
				if (accept(s, ' ')) {
					uint8_t visible = token_u8(s);
					if (visible == 1 || visible == 0) {
						surf->value.initial_state.visible = (visible == 1);
					} else {
						mn::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, get_line_no(dnm_file, s), visible);
					}
				} else {
					surf->value.initial_state.visible = true;
				}

				expect(s, '\n');

				surf->value.current_state = surf->value.initial_state;
			} else if (accept(s, "CNT ")) {
				read_cnt = true;

				surf->value.cnt.x = token_float(s);
				expect(s, ' ');
				surf->value.cnt.y = -token_float(s);
				expect(s, ' ');
				surf->value.cnt.z = token_float(s);
				expect(s, '\n');
			} else if (accept(s, "PAX")) {
				skip_after(s, '\n');
			} else if (accept(s, "REL DEP\n")) {
				read_rel_dep = true;
			} else if (accept(s, "NCH ")) {
				read_nch = true;

				const auto num_children = token_u64(s);
				expect(s, '\n');
				mn::buf_reserve(surf->value.children, num_children);

				for (size_t i = 0; i < num_children; i++) {
					expect(s, "CLD ");
					auto child_name = token_str(s);
					if (!(mn::str_prefix(child_name, "\"") && mn::str_suffix(child_name, "\""))) {
						mn::panic("'{}': child_name must be in \"\" found={}", name, child_name);
					}
					mn::str_trim(child_name, "\"");
					mn::buf_push(surf->value.children, child_name);
					expect(s, '\n');
				}
			} else {
				break;
			}
		}

		if (read_pos == false) {
			mn::panic("'{}':{} failed to find POS", name, get_line_no(dnm_file, s));
		}
		if (read_cnt == false) {
			mn::panic("'{}':{} failed to find CNT", name, get_line_no(dnm_file, s));
		}
		if (read_rel_dep == false) {
			// aircraft/cessna172r.dnm doesn't have REL DEP
			mn::log_error("'{}':{} failed to find REL DEP", name, get_line_no(dnm_file, s));
		}
		if (read_nch == false) {
			mn::panic("'{}':{} failed to find NCH", name, get_line_no(dnm_file, s));
		}

		// reinsert with name instead of FIL
		surf = mn::map_insert(surfs, mn::str_clone(name), surf->value);
		if (!mn::map_remove(surfs, fil)) {
			mn::panic("'{}': must be able to remove {} from meshes", name, fil);
		}

		expect(s, "END\n");
	}
	// aircraft/cessna172r.dnm doesn't have final END
	if (s.count > 0) {
		expect(s, "END\n");
	}

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

	auto model = Model {
		.file_abs_path = mn::str_clone(dnm_file_abs_path),
		.should_load_model = true,
		.meshes = surfs,
		.initial_aabb = AABB {
			.min={+FLT_MAX, +FLT_MAX, +FLT_MAX},
			.max={-FLT_MAX, -FLT_MAX, -FLT_MAX},
		},
	};

	// top level nodes = nodes without parents
	auto surfs_wth_parents = mn::set_with_allocator<mn::Str>(mn::memory::tmp());
	for (const auto& [_, surf] : surfs.values) {
		for (const auto& child : surf.children) {
			mn::set_insert(surfs_wth_parents, child);
		}
	}
	for (size_t i = 0; i < surfs.values.count; i++) {
		if (mn::set_lookup(surfs_wth_parents, surfs.values[i].key) == nullptr) {
			mn::buf_push(model.root_meshes_indices, i);
		}
	}

	// for each mesh: vertex -= mesh.CNT, mesh.children.each.cnt += mesh.cnt
	auto meshes_stack = mn::buf_with_allocator<Mesh*>(mn::memory::tmp());
	for (auto i : model.root_meshes_indices) {
		Mesh* mesh = &model.meshes.values[i].value;
		mesh->transformation = glm::identity<glm::mat4>();
		mn::buf_push(meshes_stack, mesh);
	}
	while (meshes_stack.count > 0) {
		Mesh* mesh = mn::buf_top(meshes_stack);
		mn_assert(mesh);
		mn::buf_pop(meshes_stack);

		for (auto& v : mesh->vertices) {
			v -= mesh->cnt;

			// apply mesh transformation to get model space vertex
			mesh->transformation = glm::translate(mesh->transformation, mesh->current_state.translation);
			mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[2], glm::vec3{0, 0, 1});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[1], glm::vec3{1, 0, 0});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[0], glm::vec3{0, 1, 0});
			const auto model_v = mesh->transformation * glm::vec4(v, 1.0);

			// update AABB
			for (int i = 0; i < 3; i++) {
				if (model_v[i] < model.initial_aabb.min[i]) {
					model.initial_aabb.min[i] = model_v[i];
				}
				if (model_v[i] > model.initial_aabb.max[i]) {
					model.initial_aabb.max[i] = model_v[i];
				}
			}
		}

		for (const mn::Str& child_name : mesh->children) {
			auto* kv = mn::map_lookup(model.meshes, child_name);
			mn_assert(kv);

			Mesh* child_mesh = &kv->value;
			child_mesh->cnt += mesh->cnt;

			mn::buf_push(meshes_stack, child_mesh);
		}
	}

	// to sphere
	// model.initial_aabb.min = glm::vec3(glm::min(glm::min(model.initial_aabb.min[0], model.initial_aabb.min[1]), model.initial_aabb.min[2]));
	// model.initial_aabb.max = glm::vec3(glm::max(glm::max(model.initial_aabb.max[0], model.initial_aabb.max[1]), model.initial_aabb.max[2]));

	model.current_aabb = model.initial_aabb;

	return model;
}

void model_free(Model& self) {
	mn::str_free(self.file_abs_path);
	mn::destruct(self.meshes);
	mn::destruct(self.root_meshes_indices);
}

void destruct(Model& self) {
	model_free(self);
}

constexpr auto WND_TITLE        = "JFS";
constexpr int  WND_INIT_WIDTH   = 1028;
constexpr int  WND_INIT_HEIGHT  = 680;
constexpr Uint32 WND_FLAGS      = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
constexpr float IMGUI_WNDS_BG_ALPHA = 0.8f;
constexpr glm::vec3 BG_COLOR {0.392f, 0.584f, 0.929f};

constexpr auto GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_CORE;
constexpr int  GL_CONTEXT_MAJOR = 3;
constexpr int  GL_CONTEXT_MINOR = 3;
constexpr auto GL_DOUBLE_BUFFER = SDL_TRUE;

enum PROJECTION_KIND { PROJECTION_KIND_IDENTITY, PROJECTION_KIND_ORTHO, PROJECTION_KIND_PERSPECTIVE };

struct Camera {
	struct {
		bool identity           = false;
		float movement_speed    = 2.5f;
		float mouse_sensitivity = 1.4;

		glm::vec3 pos      = glm::vec3{0.0f, 0.0f, 3.0f};
		glm::vec3 front    = glm::vec3{0.0f, 0.0f, -1.0f};
		glm::vec3 world_up = glm::vec3{0.0f, -1.0f, 0.0f};
		glm::vec3 right    = glm::vec3{1.0f, 0.0f, 0.0f};
		glm::vec3 up       = world_up;

		float yaw   = -90.0f / DEGREES_MAX * RADIANS_MAX;
		float pitch = 0.0f / DEGREES_MAX * RADIANS_MAX;
	} view;

	struct {
		PROJECTION_KIND kind = PROJECTION_KIND_PERSPECTIVE;

		struct {
			float near   = 0.1f;
			float far    = 1000.0f;
			float left   = -1.0f;
			float right  = +1.0f;
			float bottom = -1.0f;
			float top    = +1.0f;
		} ortho; // orthographic

		struct {
			float near         = 0.1f;
			float far          = 1000.0f;
			float fovy         = 45.0f / DEGREES_MAX * RADIANS_MAX;
			float aspect       = (float) WND_INIT_WIDTH / WND_INIT_HEIGHT;
			bool custom_aspect = false;
		} pers; // perspective
	} proj; // projection
};

glm::mat4 camera_get_view_matrix(const Camera& self) {
	if (self.view.identity) {
		return glm::identity<glm::mat4>();
	}
	return glm::lookAt(self.view.pos, self.view.pos + self.view.front, self.view.up);
}

glm::mat4 camera_get_projection_matrix(const Camera& self) {
	switch (self.proj.kind) {
	case PROJECTION_KIND_IDENTITY:
		return glm::identity<glm::mat4>();
	case PROJECTION_KIND_ORTHO:
		return glm::transpose(glm::ortho(
			self.proj.ortho.left,
			self.proj.ortho.right,
			self.proj.ortho.bottom,
			self.proj.ortho.top,
			self.proj.ortho.near, self.proj.ortho.far
		));
	case PROJECTION_KIND_PERSPECTIVE: {
		return glm::perspective(
			self.proj.pers.fovy,
			self.proj.pers.aspect,
			self.proj.pers.near,
			self.proj.pers.far
		);
	}
	default: mn_unreachable();
	}
	return {};
}

auto clamp(auto x, auto lower_limit, auto upper_limit) {
	if (x > upper_limit) {
		return upper_limit;
	}
	if (x < lower_limit) {
		return lower_limit;
	}
	return x;
}

void camera_update(Camera& self, float delta_time) {
	if (self.view.identity) {
		return;
	}

	// move with keyboard
	const Uint8 * key_pressed = SDL_GetKeyboardState(nullptr);
	const float velocity = self.view.movement_speed * delta_time;
	if (key_pressed[SDL_SCANCODE_W]) {
		self.view.pos += self.view.front * velocity;
	}
	if (key_pressed[SDL_SCANCODE_S]) {
		self.view.pos -= self.view.front * velocity;
	}
	if (key_pressed[SDL_SCANCODE_D]) {
		self.view.pos += self.view.right * velocity;
	}
	if (key_pressed[SDL_SCANCODE_A]) {
		self.view.pos -= self.view.right * velocity;
	}

	// move with mouse
	static glm::ivec2 mouse_before {};
	glm::ivec2 mouse_now;
	const auto buttons = SDL_GetMouseState(&mouse_now.x, &mouse_now.y);
	if ((buttons & SDL_BUTTON(SDL_BUTTON_RIGHT))) {
		self.view.yaw   += (mouse_now.x - mouse_before.x) * self.view.mouse_sensitivity / 1000;
		self.view.pitch -= (mouse_now.y - mouse_before.y) * self.view.mouse_sensitivity / 1000;

		// make sure that when pitch is out of bounds, screen doesn't get flipped
		constexpr float CAMERA_PITCH_MAX = 89.0f / DEGREES_MAX * RADIANS_MAX;
		self.view.pitch = clamp(self.view.pitch, -CAMERA_PITCH_MAX, CAMERA_PITCH_MAX);
	}
	mouse_before = mouse_now;

	// update front, right and up Vectors using the updated Euler angles
	self.view.front = glm::normalize(glm::vec3 {
		glm::cos(self.view.yaw) * glm::cos(self.view.pitch),
		glm::sin(self.view.pitch),
		glm::sin(self.view.yaw) * glm::cos(self.view.pitch),
	});

	// normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
	self.view.right = glm::normalize(glm::cross(self.view.front, self.view.world_up));
	self.view.up    = glm::normalize(glm::cross(self.view.right, self.view.front));
}

GLfloat _glGetFloat(GLenum e) {
	GLfloat out;
	glGetFloatv(e, &out);
	return out;
}

namespace MyImGui {
	template<typename T>
	void EnumsCombo(const char* label, T* p_enum, const std::initializer_list<std::pair<T, const char*>>& enums) {
		int var_i = -1;
		const char* preview = "- Invalid Value -";
		for (const auto& [type, type_str] : enums) {
			var_i++;
			if (type == *p_enum) {
				preview = type_str;
				break;
			}
		}

		if (ImGui::BeginCombo(label, preview)) {
			for (const auto& [type, type_str] : enums) {
				if (ImGui::Selectable(type_str,  type == *p_enum)) {
					*p_enum = type;
				}
			}

			ImGui::EndCombo();
		}
	}

	void SliderAngle(const char* label, float* radians, float angle_max) {
		float angle = *radians / RADIANS_MAX * angle_max;
		ImGui::DragFloat(label, &angle, 0.01f * angle_max, -angle_max, angle_max);
		*radians = angle / angle_max * RADIANS_MAX;
	}

	void SliderAngle3(const char* label, glm::vec3* radians, float angle_max) {
		glm::vec3 angle = *radians / RADIANS_MAX * angle_max;
		ImGui::DragFloat3(label, glm::value_ptr(angle), 0.01f * angle_max, -angle_max, angle_max);
		*radians = angle / angle_max * RADIANS_MAX;
	}
}

GLuint gpu_program_new(const char* vertex_shader_src, const char* fragment_shader_src) {
	// vertex shader
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
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
    const GLuint gpu_program = glCreateProgram();
    glAttachShader(gpu_program, vertex_shader);
    glAttachShader(gpu_program, fragment_shader);
    glLinkProgram(gpu_program);

	GLint shader_program_success;
    glGetProgramiv(gpu_program, GL_LINK_STATUS, &shader_program_success);
    if (!shader_program_success) {
    	char info_log[512];
        glGetProgramInfoLog(gpu_program, 512, NULL, info_log);
        mn::panic("failed to link vertex and fragment shaders, err: {}", info_log);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

	return gpu_program;
}

// from YSFLIGHT SCENERY EDITOR 2009
enum class RectRegionID {
	NONE=0,
	RUNWAY=1,
	TAXIWAY=2,
	AIRPORT_AREA=4,
	ENEMY_TANK_GENERATOR=6,
	FRIENDLY_TANK_GENERATOR=7,
	VIEW_POINT=20,
};

constexpr auto TER_EXAMPLE = R"(TerrMesh
NBL 3 3
TMS 20.00 20.00
CBE 0.00 15.70 46 112 10 194 169 92
BOT 0 255 0
RIG 255 255 255
TOP 200 77 86
LEF 128 128 255
BLO 0.00 L 1 193 168 91 1 46 112 10
BLO 0.00 L 1 194 169 92 1 194 169 92
BLO 10.00 R 1 194 169 92 1 132 145 57
BLO 0.00
BLO 0.00 R 1 192 168 91 1 194 169 92
BLO 17.06 L 1 194 169 92 1 194 169 92
BLO 17.12 L 1 191 168 90 1 194 169 92
BLO 0.00
BLO 0.00 R 1 46 112 10 1 192 168 91
BLO 17.00 L 1 191 168 90 1 194 169 92
BLO 16.87 L 1 46 112 10 1 191 168 90
BLO 0.00
BLO -10.00
BLO 0.00
BLO 0.00
BLO 0.00
END
)";

struct Block {
	enum { RIGHT=0, LEFT } orientation;
	glm::vec4 faces_color[2];
};

namespace fmt {
	template<>
	struct formatter<Block> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Block &v, FormatContext &ctx) {
			mn::Str orientation {};
			switch (v.orientation) {
			case Block::LEFT: orientation = mn::str_lit("LEFT"); break;
			case Block::RIGHT: orientation = mn::str_lit("RIGHT"); break;
			default: mn_unreachable();
			}

			return format_to(ctx.out(), "Block{{orientation: {}, faces_color[0]: {}, faces_color[1]: {}}}", orientation, v.faces_color[0], v.faces_color[1]);
		}
	};
}

struct TerrMesh {
	// [z][x] where (z=0,x=0) is bot-left most
	mn::Buf<mn::Buf<float>> nodes_height;
	mn::Buf<mn::Buf<Block>> blocks;

	struct {
		bool enabled;
		float bottom_y, top_y;
		glm::vec3 bottom_color, top_color;
	} gradiant;

	glm::vec4 top_side_color, bottom_side_color, right_side_color, left_side_color;

	struct {
		GLuint vao, vbo;
		size_t array_count;
	} gpu;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		glm::vec3 scale;
	} current_state, initial_state;
};

void terr_mesh_free(TerrMesh& self) {
	mn::destruct(self.nodes_height);
	mn::destruct(self.blocks);
}

void destruct(TerrMesh& self) {
	terr_mesh_free(self);
}

namespace fmt {
	template<>
	struct formatter<TerrMesh> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const TerrMesh &v, FormatContext &ctx) {
			return format_to(ctx.out(), "TerrMesh{{scale: {}, translation: {}, rotation: {}, "
				"gradiant: {{enabled: {}, bottom_y: {}, top_y: {}, bottom_color: {}, top_color: {}}}, nodes_height: {}, blocks: {}, "
				"sides: [top={}, bot={}, right={}, left={}]}}",
				v.current_state.scale, v.current_state.translation, v.current_state.rotation,
				v.gradiant.enabled, v.gradiant.bottom_y, v.gradiant.top_y, v.gradiant.bottom_color, v.gradiant.top_color,
				v.nodes_height, v.blocks, v.top_side_color, v.bottom_side_color, v.right_side_color, v.left_side_color);
		}
	};
}

TerrMesh terr_mesh_from_ter_file(const mn::Str& ter_file_content) {
	auto s = mn::str_clone(ter_file_content, mn::memory::tmp());
	mn::str_replace(s, "\r\n", "\n");

	expect(s, "TerrMesh\n");

	TerrMesh self {};

	expect(s, "NBL ");
	const auto num_blocks_x = token_u32(s);
	expect(s, ' ');
	const auto num_blocks_z = token_u32(s);
	expect(s, '\n');

	expect(s, "TMS ");
	// TODO: multiply by 10?
	self.initial_state.scale = {1, 1, 1};
	self.initial_state.scale.x = token_float(s); //* 10.0f;
	expect(s, ' ');
	self.initial_state.scale.z = token_float(s); //* 10.0f;
	expect(s, '\n');
	// mn::log_debug("self.scale.x={}, self.scale.y={}", self.scale.x, self.scale.y);

	self.current_state = self.initial_state;

	if (accept(s, "CBE ")) {
		self.gradiant.enabled = true;

		self.gradiant.top_y = token_float(s);
		expect(s, ' ');
		self.gradiant.bottom_y = -token_float(s);
		expect(s, ' ');

		self.gradiant.top_color.r = token_u8(s) / 255.0f;
		expect(s, ' ');
		self.gradiant.top_color.g = token_u8(s) / 255.0f;
		expect(s, ' ');
		self.gradiant.top_color.b = token_u8(s) / 255.0f;
		expect(s, ' ');

		self.gradiant.bottom_color.r = token_u8(s) / 255.0f;
		expect(s, ' ');
		self.gradiant.bottom_color.g = token_u8(s) / 255.0f;
		expect(s, ' ');
		self.gradiant.bottom_color.b = token_u8(s) / 255.0f;
		expect(s, '\n');
	}
	// if (self.gradiant.enabled) {
	// 	mn::log_debug("gradiant: bottom_y={}, top_y={}, bottom_color={}, top_color={}", self.gradiant.bottom_y, self.gradiant.top_y, self.gradiant.bottom_color, self.gradiant.top_color);
	// } else {
	// 	mn::log_debug("gradiant not enabled");
	// }

	// NOTE: assumed order in file
	for (auto [side_str, side] : {
		std::pair{"BOT ", &self.bottom_side_color},
		std::pair{"RIG ", &self.right_side_color},
		std::pair{"TOP ", &self.top_side_color},
		std::pair{"LEF ", &self.left_side_color},
	}) {
		if (accept(s, side_str)) {
			side->a = 1;
			side->r = token_u8(s) / 255.0f;
			expect(s, ' ');
			side->g = token_u8(s) / 255.0f;
			expect(s, ' ');
			side->b = token_u8(s) / 255.0f;
			expect(s, '\n');
		}

		// mn::log_debug("{}: {} and color={}", side_str, side->enabled, side->color);
	}

	// create blocks
	self.blocks = mn::buf_with_count<mn::Buf<Block>>(num_blocks_z);
	for (auto& row : self.blocks) {
		row = mn::buf_with_count<Block>(num_blocks_x);
	}

	// create nodes
	self.nodes_height = mn::buf_with_count<mn::Buf<float>>(num_blocks_z+1);
	for (auto& row : self.nodes_height) {
		row = mn::buf_with_count<float>(num_blocks_x+1);
	}

	// parse blocks and nodes
	for (size_t z = 0; z < self.nodes_height.count; z++) {
		for (size_t x = 0; x < self.nodes_height[z].count; x++) {
			expect(s, "BLO ");
			self.nodes_height[z][x] = token_float(s);

			// don't read rest of block if node is on edge/wedge
			if (z == self.nodes_height.count-1 || x == self.nodes_height[z].count-1) {
				skip_after(s, '\n');
				continue;
			}

			// from here the node has a block
			if (accept(s, '\n')) {
				continue;
			} else if (accept(s, " R ")) {
				self.blocks[z][x].orientation = Block::RIGHT;
			} else if (accept(s, " L ")) {
				self.blocks[z][x].orientation = Block::LEFT;
			} else {
				mn::panic("{}: expected either a new line or L or R, found='{}'", get_line_no(s, ter_file_content), smaller_str(s));
			}

			// face 0
			if (accept(s, "OFF ") || accept(s, "0 ")) {
				self.blocks[z][x].faces_color[0].a = 0;
			} else if (accept(s, "ON ") || accept(s, "1 ")) {
				self.blocks[z][x].faces_color[0].a = 1;
			} else {
				skip_after(s, ' ');
				self.blocks[z][x].faces_color[0].a = 1;
			}

			self.blocks[z][x].faces_color[0].r = token_u8(s) / 255.0f;
			expect(s, ' ');
			self.blocks[z][x].faces_color[0].g = token_u8(s) / 255.0f;
			expect(s, ' ');
			self.blocks[z][x].faces_color[0].b = token_u8(s) / 255.0f;
			expect(s, ' ');

			// face 1
			if (accept(s, "OFF ") || accept(s, "0 ")) {
				self.blocks[z][x].faces_color[1].a = 0;
			} else if (accept(s, "ON ") || accept(s, "1 ")) {
				self.blocks[z][x].faces_color[1].a = 1;
			} else {
				skip_after(s, ' ');
				self.blocks[z][x].faces_color[1].a = 1;
			}

			self.blocks[z][x].faces_color[1].r = token_u8(s) / 255.0f;
			expect(s, ' ');
			self.blocks[z][x].faces_color[1].g = token_u8(s) / 255.0f;
			expect(s, ' ');
			self.blocks[z][x].faces_color[1].b = token_u8(s) / 255.0f;
			expect(s, '\n');
		}
	}
	// mn::log_debug("{}", self.nodes_height);

	expect(s, "END\n");

	// mn::log_debug("{}", self);
	return self;
}

void terr_mesh_load_to_gpu(TerrMesh& self) {
	struct Stride {
		glm::vec3 vertex;
		glm::vec4 color;
	};
	auto buffer = mn::buf_with_allocator<Stride>(mn::memory::tmp());

	// main triangles
	for (size_t z = 0; z < self.blocks.count; z++) {
		for (size_t x = 0; x < self.blocks[z].count; x++) {
			if (self.blocks[z][x].orientation == Block::RIGHT) {
				// face 1
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[0],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});

				// face 2
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[1],
				});
			} else {
				// face 1
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[0],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});

				// face 2
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[1],
				});
				mn::buf_push(buffer, Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[1],
				});
			}
		}
	}
	self.gpu.array_count = buffer.count;

	// load buffer to gpu
	glGenVertexArrays(1, &self.gpu.vao);
	glBindVertexArray(self.gpu.vao);
		glGenBuffers(1, &self.gpu.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.gpu.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.count * sizeof(Stride), buffer.ptr, GL_STATIC_DRAW);

		size_t offset = 0;

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,              /*index*/
			3,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			sizeof(Stride), /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(Stride::vertex);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(
			1,              /*index*/
			4,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			sizeof(Stride), /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(Stride::color);
	glBindVertexArray(0);

	GL_CATCH_ERRS();
}

void terr_mesh_unload_from_gpu(TerrMesh& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};
}

constexpr auto PICT2_EXAMPLE = R"(Pict2
PLG
COL 0 87 0
VER -230.00 300.00
VER 490.00 -1500.00
VER 2260.00 -2440.00
VER 4430.00 -2440.00
VER 6410.00 -1620.00
VER 8000.00 1480.00
VER 8870.00 3940.00
VER 8940.00 5010.00
VER 8070.00 8170.00
VER 7000.00 8760.00
VER 5870.00 8730.00
VER 3250.00 7600.00
VER 1330.00 6300.00
VER 390.00 5040.00
VER 190.00 3060.00
VER 580.00 2900.00
VER 680.00 2330.00
VER 440.00 1940.00
VER 50.00 1670.00
ENDO
PLG
COL 0 106 53
VER 12200.00 27760.00
VER 10810.00 29410.00
VER 10890.00 30990.00
VER 11730.00 31180.00
VER 13040.00 30220.00
VER 13230.00 28570.00
ENDO
ENDPICT
)";

struct Permitive {
	enum class Kind {
		LINE,
		POLYGON,
		LINE_SEGMENT,
		POINTS,
		QUAD_STRIP,
		GRADATION_QUAD_STRIP,
		QUADRILATERAL,
		TRIANGLE,
	} kind;

	glm::vec3 color;
	glm::vec3 color2; // only for kind=GRADATION_QUAD_STRIP
	mn::Buf<glm::vec2> vertices;
};

namespace fmt {
	template<>
	struct formatter<Permitive::Kind> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Permitive::Kind &v, FormatContext &ctx) {
			switch (v) {
			case Permitive::Kind::LINE:                    return format_to(ctx.out(), "Permitive::Kind::LINE");
			case Permitive::Kind::POLYGON:                 return format_to(ctx.out(), "Permitive::Kind::POLYGON");
			case Permitive::Kind::LINE_SEGMENT:            return format_to(ctx.out(), "Permitive::Kind::LINE_SEGMENT");
			case Permitive::Kind::POINTS:                  return format_to(ctx.out(), "Permitive::Kind::POINTS");
			case Permitive::Kind::QUADRILATERAL:           return format_to(ctx.out(), "Permitive::Kind::QUADRILATERAL");
			case Permitive::Kind::QUAD_STRIP:              return format_to(ctx.out(), "Permitive::Kind::QUAD_STRIP");
			case Permitive::Kind::GRADATION_QUAD_STRIP:    return format_to(ctx.out(), "Permitive::Kind::GRADATION_QUAD_STRIP");
			case Permitive::Kind::TRIANGLE:                return format_to(ctx.out(), "Permitive::Kind::TRIANGLE");
			default: mn_unreachable();
			}
			return format_to(ctx.out(), "????????");
		}
	};

	template<>
	struct formatter<Permitive> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const Permitive &v, FormatContext &ctx) {
			return format_to(ctx.out(), "Permitive{{kind: {}, color: {}, color2: {}, vertices: {}}}", v.kind, v.color, v.color2, v.vertices);
		}
	};
}

void pict2permitive_free(Permitive& self) {
	mn::buf_free(self.vertices);
}

void destruct(Permitive& self) {
	pict2permitive_free(self);
}

mn::Buf<Permitive> permitives_from_pc2_file(const mn::Str& pc2_file_content) {
	auto s = mn::str_clone(pc2_file_content, mn::memory::tmp());
	mn::str_replace(s, "\r\n", "\n");

	expect(s, "Pict2\n");

	mn::Buf<Permitive> permitives {};

	while (accept(s, "ENDPICT\n") == false) {
		Permitive permitive {};

		auto kind_str = token_str(s, mn::memory::tmp());
		expect(s, '\n');

		if (kind_str == "LSQ") {
			permitive.kind = Permitive::Kind::LINE;
		} else if (kind_str == "PLG") {
			permitive.kind = Permitive::Kind::POLYGON;
		} else if (kind_str == "PLL") {
			permitive.kind = Permitive::Kind::LINE_SEGMENT;
		} else if (kind_str == "PST") {
			permitive.kind = Permitive::Kind::POINTS;
		} else if (kind_str == "QDR") {
			permitive.kind = Permitive::Kind::QUADRILATERAL;
		} else if (kind_str == "GQS") {
			permitive.kind = Permitive::Kind::GRADATION_QUAD_STRIP;
		} else if (kind_str == "QST") {
			permitive.kind = Permitive::Kind::QUAD_STRIP;
		} else if (kind_str == "TRI") {
			permitive.kind = Permitive::Kind::TRIANGLE;
		} else {
			mn::panic("{}: invalid pict2 kind={}", get_line_no(s, pc2_file_content), kind_str);
		}
		// mn::log_debug("kind_str='{}', kind={}", kind_str, permitive.kind);

		expect(s, "COL ");
		permitive.color.r = token_u8(s) / 255.0f;
		expect(s, ' ');
		permitive.color.g = token_u8(s) / 255.0f;
		expect(s, ' ');
		permitive.color.b = token_u8(s) / 255.0f;
		expect(s, '\n');
		// mn::log_debug("color={}", permitive.color);

		if (permitive.kind == Permitive::Kind::GRADATION_QUAD_STRIP) {
			expect(s, "CL2 ");
			permitive.color2.r = token_u8(s) / 255.0f;
			expect(s, ' ');
			permitive.color2.g = token_u8(s) / 255.0f;
			expect(s, ' ');
			permitive.color2.b = token_u8(s) / 255.0f;
			expect(s, '\n');
			// mn::log_debug("color2={}", permitive.color2);
		}

		while (accept(s, "ENDO\n") == false) {
			glm::vec2 vertex {};
			expect(s, "VER ");
			vertex.x = token_float(s);
			expect(s, ' ');
			vertex.y = token_float(s);
			expect(s, '\n');

			if (permitive.kind == Permitive::Kind::TRIANGLE && permitive.vertices.count >= 3) {
				mn::panic("{}: kind is triangle but found more than 3 vertices", get_line_no(s, pc2_file_content));
			}

			// mn::log_debug("ver: (x={}, y={})", x, y);
			mn::buf_push(permitive.vertices, vertex);
		}
		// mn::log_debug("vertices={}", permitive.vertices);

		if (permitive.vertices.count == 0) {
			mn::panic("{}: no vertices", get_line_no(s, pc2_file_content));
		} else if (permitive.kind == Permitive::Kind::LINE && permitive.vertices.count % 2 != 0) {
			mn::log_error("{}: kind is line but num of vertices ({}) isn't divisible by 2, ignoring last vertex", get_line_no(s, pc2_file_content), permitive.vertices.count);
			mn::buf_pop(permitive.vertices);
		} else if (permitive.kind == Permitive::Kind::LINE_SEGMENT && permitive.vertices.count == 1) {
			mn::panic("{}: kind is line but has one point", get_line_no(s, pc2_file_content));
		} else if (permitive.kind == Permitive::Kind::QUADRILATERAL && permitive.vertices.count % 4 != 0) {
			mn::panic("{}: kind is quadrilateral but num of vertices ({}) isn't divisible by 4", get_line_no(s, pc2_file_content), permitive.vertices.count);
		} else if (permitive.kind == Permitive::Kind::QUAD_STRIP && (permitive.vertices.count >= 4 && permitive.vertices.count % 2 == 0) == false) {
			mn::panic("{}: kind is quad_strip but num of vertices ({}) isn't in (4,6,8,10,...)", get_line_no(s, pc2_file_content), permitive.vertices.count);
		}

		// mn::log_debug("{}", permitive);
		mn::buf_push(permitives, permitive);
	}
	// mn::log_debug("{}", permitives);

	return permitives;
}

int main() {
	auto permitives = permitives_from_pc2_file(mn::str_lit(PICT2_EXAMPLE));
	mn_defer(mn::destruct(permitives));
	mn::log_debug("{}", permitives);

	return 0;
	test_aabbs_intersection();
	test_polygons_to_triangles();

	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		mn::panic(SDL_GetError());
	}
	mn_defer(SDL_Quit());

	auto sdl_window = SDL_CreateWindow(
		WND_TITLE,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WND_INIT_WIDTH, WND_INIT_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | WND_FLAGS
	);
	if (!sdl_window) {
		mn::panic(SDL_GetError());
	}
	mn_defer(SDL_DestroyWindow(sdl_window));
	SDL_SetWindowBordered(sdl_window, SDL_TRUE);

	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_PROFILE)) { mn::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_CONTEXT_MAJOR))  { mn::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_CONTEXT_MINOR))  { mn::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_DOUBLE_BUFFER))           { mn::panic(SDL_GetError()); }

	auto gl_context = SDL_GL_CreateContext(sdl_window);
	if (!gl_context) {
		mn::panic(SDL_GetError());
	}
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
	ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL2_InitForOpenGL(sdl_window, gl_context)) {
		mn::panic("failed to init imgui implementation for SDL2");
	}
	mn_defer(ImGui_ImplSDL2_Shutdown());
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
		mn::panic("failed to init imgui implementation for OpenGL3");
	}
	mn_defer(ImGui_ImplOpenGL3_Shutdown());

	const GLuint meshes_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec3 attr_position;
			layout (location = 1) in vec4 attr_color;
			layout (location = 2) in vec3 attr_normal;

			// TODO: pass MVP instead of 3 uniforms for perf
			uniform mat4 projection, view, model;

			out float vs_vertex_y;
			out vec4 vs_color;
			out vec3 vs_normal;

			void main() {
				gl_Position = projection * view * model * vec4(attr_position, 1.0);
				vs_color = attr_color;
				vs_normal = attr_normal;
				vs_vertex_y = attr_position.y;
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core
			in float vs_vertex_y;
			in vec4 vs_color;
			in vec3 vs_normal;

			out vec4 out_fragcolor;

			uniform bool is_light_source;

			uniform bool gradient_enabled;
			uniform float gradient_bottom_y, gradient_top_y;
			uniform vec3 gradient_bottom_color, gradient_top_color;

			void main() {
				if (vs_color.a == 0) {
					discard;
				} else if (gradient_enabled) {
					float alpha = (vs_vertex_y - gradient_bottom_y) / (gradient_top_y - gradient_bottom_y);
					out_fragcolor = vec4(mix(gradient_bottom_color, gradient_top_color, alpha), 1.0f);
				} else {
					out_fragcolor = vs_color;
				}
			}
		)GLSL"
	);
	mn_defer(glDeleteProgram(meshes_gpu_program));

	// models
	constexpr int NUM_MODELS = 2;
	Model models[NUM_MODELS] = {
		model_from_dnm(mn::str_lit("C:\\Users\\User\\dev\\JFS\\build\\Ysflight\\aircraft\\ys11.dnm")),
		model_from_dnm(mn::str_lit("C:\\Users\\User\\dev\\JFS\\build\\Ysflight\\aircraft\\ys11.dnm")),
	};
	mn_defer({
		for (int i = 0; i < NUM_MODELS; i++) {
			model_unload_from_gpu(models[i]);
			model_free(models[i]);
		}
	});

	Camera camera {};

	bool wnd_size_changed = true;
	bool running = true;
	bool fullscreen = false;

	Uint32 time_millis = SDL_GetTicks();
	double delta_time; // seconds since previous frame

	bool should_limit_fps = true;
	int fps_limit = 60;
	int millis_till_render = 0;

	auto logs_arena = mn::allocator_arena_new();
	mn_defer(mn::allocator_free(logs_arena));
	auto logs = mn::buf_with_allocator<mn::Str>(logs_arena);
	bool logs_auto_scrolling = true;
	bool logs_wrapped = false;
	float logs_last_scrolled_line = 0;
	mn::log_debug("logs will be copied to logs window");
	auto old_log_interface = mn::log_interface_set(mn::Log_Interface{
		// pointer to user data
		.self = &logs,
		.debug = +[](void* self, const char* msg) {
			auto logs = (mn::Buf<mn::Str>*) self;
			auto formatted = mn::strf(logs->allocator, "> {}\n", msg);
			mn::buf_push(*logs, formatted);
			::fprintf(stdout, "%s", formatted.ptr);
		},
		.info = +[](void* self, const char* msg) {
			auto logs = (mn::Buf<mn::Str>*) self;
			auto formatted = mn::strf(logs->allocator, "[info] {}\n", msg);
			mn::buf_push(*logs, formatted);
			::fprintf(stdout, "%s", formatted.ptr);
		},
		.warning = +[](void* self, const char* msg) {
			auto logs = (mn::Buf<mn::Str>*) self;
			auto formatted = mn::strf(logs->allocator, "[warning] {}\n", msg);
			mn::buf_push(*logs, formatted);
			::fprintf(stderr, "%s", formatted.ptr);
		},
		.error = +[](void* self, const char* msg) {
			auto logs = (mn::Buf<mn::Str>*) self;
			auto formatted = mn::strf(logs->allocator, "[error] {}\n", msg);
			mn::buf_push(*logs, formatted);
			::fprintf(stderr, "%s", formatted.ptr);
		},
		.critical = +[](void* self, const char* msg) {
			mn::panic("{}", msg);
		},
	});

	struct {
		bool transpose_view       = false;
		bool transpose_projection = false;
		bool transpose_model      = false;

		bool smooth_lines = true;
		GLfloat line_width = 3.0f;
		GLfloat point_size = 3.0f;

		GLenum regular_primitives_type = GL_TRIANGLES;
		GLenum light_primitives_type   = GL_TRIANGLES;
		GLenum polygon_mode            = GL_FILL;

		// NOTE: some meshes are open from outside and culling would break them (hide sides as in ground/vasi.dnm)
		bool culling_enabled = false;
		GLenum culling_face_type = GL_BACK;
		GLenum culling_front_face_type = GL_CCW;
	} rendering {};

	const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = _glGetFloat(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
	const GLfloat POINT_SIZE_GRANULARITY        = _glGetFloat(GL_POINT_SIZE_GRANULARITY);

	struct {
		bool enabled = true;

		float check_rate_secs = 1.0f;
		float last_check_time = 0; // when checked file
		int64_t last_write_time = 0; // when file was written to (by some other progrem)
	} dnm_hotreload;

	float imgui_angle_max = DEGREES_MAX;

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 5.0f;
		bool on_top = true;
	} axis_rendering;
	mn_defer({
		glDeleteBuffers(1, &axis_rendering.vbo);
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &axis_rendering.vao);
	});
	{
		struct Stride {
			glm::vec3 vertex;
			glm::vec4 color;
		};
		mn::allocator_push(mn::memory::tmp());
		const auto buffer = mn::buf_lit({
			Stride {{0, 0, 0}, {1, 0, 0, 1}}, // X
			Stride {{1, 0, 0}, {1, 0, 0, 1}},
			Stride {{0, 0, 0}, {0, 1, 0, 1}}, // Y
			Stride {{0, 1, 0}, {0, 1, 0, 1}},
			Stride {{0, 0, 0}, {0, 0, 1, 1}}, // Z
			Stride {{0, 0, 1}, {0, 0, 1, 1}},
		});
		mn::allocator_pop();
		axis_rendering.points_count = buffer.count;

		glGenVertexArrays(1, &axis_rendering.vao);
		glBindVertexArray(axis_rendering.vao);
			glGenBuffers(1, &axis_rendering.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, axis_rendering.vbo);
			glBufferData(GL_ARRAY_BUFFER, buffer.count * sizeof(Stride), buffer.ptr, GL_STATIC_DRAW);

			size_t offset = 0;

			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				0,              /*index*/
				3,              /*#components*/
				GL_FLOAT,       /*type*/
				GL_FALSE,       /*normalize*/
				sizeof(Stride), /*stride bytes*/
				(void*)offset   /*offset*/
			);
			offset += sizeof(Stride::vertex);

			glEnableVertexAttribArray(1);
			glVertexAttribPointer(
				1,              /*index*/
				4,              /*#components*/
				GL_FLOAT,       /*type*/
				GL_FALSE,       /*normalize*/
				sizeof(Stride), /*stride bytes*/
				(void*)offset   /*offset*/
			);
			offset += sizeof(Stride::color);
		glBindVertexArray(0);

		GL_CATCH_ERRS();
	}

	const GLuint lines_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec3 attr_position;
			uniform mat4 model_view_projection;
			void main() {
				gl_Position = model_view_projection * vec4(attr_position, 1.0);
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core
			uniform vec3 color;
			out vec4 out_fragcolor;
			void main() {
				out_fragcolor = vec4(color, 1.0f);
			}
		)GLSL"
	);
	mn_defer(glDeleteProgram(lines_gpu_program));

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 1.0f;
	} box_rendering;
	mn_defer({
		glDeleteBuffers(1, &box_rendering.vbo);
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &box_rendering.vao);
	});
	{
		mn::allocator_push(mn::memory::tmp());
		const auto buffer = mn::buf_lit<glm::vec3>({
			{0, 0, 0}, // face x0
			{0, 1, 0},
			{0, 1, 1},
			{0, 0, 1},
			{0, 0, 0},
			{1, 0, 0}, // face x1
			{1, 1, 0},
			{1, 1, 1},
			{1, 0, 1},
			{1, 0, 0},
			{0, 0, 0}, // face y0
			{1, 0, 0},
			{1, 0, 1},
			{0, 0, 1},
			{0, 0, 0},
			{0, 1, 0}, // face y1
			{1, 1, 0},
			{1, 1, 1},
			{0, 1, 1},
			{0, 1, 0},
			{0, 0, 0}, // face z0
			{1, 0, 0},
			{1, 1, 0},
			{0, 1, 0},
			{0, 0, 0},
			{0, 0, 1}, // face z1
			{1, 0, 1},
			{1, 1, 1},
			{0, 1, 1},
			{0, 0, 1},
		});
		mn::allocator_pop();
		box_rendering.points_count = buffer.count;

		glGenVertexArrays(1, &box_rendering.vao);
		glBindVertexArray(box_rendering.vao);
			glGenBuffers(1, &box_rendering.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, box_rendering.vbo);
			glBufferData(GL_ARRAY_BUFFER, buffer.count * sizeof(glm::vec3), buffer.ptr, GL_STATIC_DRAW);

			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				0,                 /*index*/
				3,                 /*#components*/
				GL_FLOAT,          /*type*/
				GL_FALSE,          /*normalize*/
				sizeof(glm::vec3), /*stride bytes*/
				(void*)0           /*offset*/
			);
		glBindVertexArray(0);

		GL_CATCH_ERRS();
	}

	auto terr_mesh = terr_mesh_from_ter_file(mn::str_lit(TER_EXAMPLE));
	mn_defer(terr_mesh_free(terr_mesh));
	terr_mesh_load_to_gpu(terr_mesh);
	mn_defer(terr_mesh_unload_from_gpu(terr_mesh));

	struct {
		bool enabled = true;
		float landing_gear_alpha = 0; // 0 -> DOWN, 1 -> UP

		float throttle = 0;
		float propoller_max_angle_speed = 5.0f / DEGREES_MAX * RADIANS_MAX;

		bool afterburner_reheat_enabled = false;
		float afterburner_throttle_threshold = 0.80f;
	} animation_config;

	while (running) {
		mn::memory::tmp()->clear_all();

		auto overlay_text = mn::str_tmp();

		// time
		{
			Uint32 delta_time_millis = SDL_GetTicks() - time_millis;
			time_millis += delta_time_millis;

			if (should_limit_fps) {
				int millis_diff = (1000 / fps_limit) - delta_time_millis;
				millis_till_render = clamp(millis_till_render - millis_diff, 0, 1000);
				if (millis_till_render > 0) {
					mn::thread_sleep(2);
					continue;
				} else {
					millis_till_render = 1000 / fps_limit;
					delta_time = 1.0f/ fps_limit;
				}
			} else {
				delta_time = (double) delta_time_millis / 1000;
			}

			if (delta_time < 0.0001f) {
				delta_time = 0.0001f;
			}
		}
		overlay_text = mn::strf(overlay_text, "fps: {:.2f}\n", 1.0f/delta_time);

		camera_update(camera, delta_time);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					running = false;
					break;
				case 'f':
					fullscreen = !fullscreen;
					wnd_size_changed = true;
					if (fullscreen) {
						if (SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
							mn::panic(SDL_GetError());
						}
					} else {
						if (SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL)) {
							mn::panic(SDL_GetError());
						}
					}
					break;
				default:
					break;
				}
			} else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				wnd_size_changed = true;
			} else if (event.type == SDL_QUIT) {
				running = false;
			}
		}

		if (wnd_size_changed) {
			wnd_size_changed = false;

			int w, h;
			SDL_GetWindowSize(sdl_window, &w, &h);

			glViewport(0, 0, w, h);

			if (!camera.proj.pers.custom_aspect) {
				camera.proj.pers.aspect = (float) w / h;
			}
		}

		if (dnm_hotreload.enabled) {
			dnm_hotreload.last_check_time += delta_time;
			if (dnm_hotreload.last_check_time >= dnm_hotreload.check_rate_secs) {
				dnm_hotreload.last_check_time = 0;

				for (int i = 0; i < NUM_MODELS; i++)
				{
					const int64_t write_time = mn::file_last_write_time(models[i].file_abs_path);
					if (write_time > dnm_hotreload.last_write_time) {
						dnm_hotreload.last_write_time = write_time;
						models[i].should_load_model = true;
						mn::log_debug("Model[{}]: file '{}' changed, will reload", i, models[i].file_abs_path);
					}
				}
			}
		}

		for (int i = 0; i < NUM_MODELS; i++) {
			if (models[i].should_open_dnm_file) {
				models[i].should_open_dnm_file = false;

				auto result = pfd::open_file("Select DNM", "", {"DNM Files", "*.dnm", "All Files", "*"}).result();
				if (result.size() == 1) {
					mn::str_free(models[i].file_abs_path);
					mn::str_push(models[i].file_abs_path, result[0].c_str());
					mn::log_debug("loading '{}'", models[i].file_abs_path);
					models[i].should_load_model = true;
				}
			}

			if (models[i].should_load_model) {
				auto model = model_from_dnm(models[i].file_abs_path);
				model_load_to_gpu(model);

				// so we don't hot reload it again
				dnm_hotreload.last_write_time = mn::file_last_write_time(models[i].file_abs_path);

				model_unload_from_gpu(models[i]);
				model_free(models[i]);
				models[i] = model;

				mn::log_debug("loaded '{}'", models[i].file_abs_path);
				models[i].should_load_model = false;
			}
		}

		glEnable(GL_DEPTH_TEST);
		glClearDepth(1);
		glClearColor(BG_COLOR.x, BG_COLOR.y, BG_COLOR.z, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (rendering.smooth_lines) {
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(rendering.line_width);
		} else {
			glDisable(GL_LINE_SMOOTH);
		}
		glPointSize(rendering.point_size);
		glPolygonMode(GL_FRONT_AND_BACK, rendering.polygon_mode);

		if (rendering.culling_enabled) {
			glCullFace(rendering.culling_face_type);
			glFrontFace(rendering.culling_front_face_type);
			glEnable(GL_CULL_FACE);
		} else {
			glDisable(GL_CULL_FACE);
		}

		// test intersection
		auto box_instances = mn::buf_with_allocator<Box>(mn::memory::tmp());
		for (int i = 0; i < NUM_MODELS-1; i++) {
			if (models[i].current_state.visible == false) {
				overlay_text = mn::strf(overlay_text, "model[{}] invisible and won't intersect\n", i);
				continue;
			}

			glm::vec3 i_color {0, 0, 1};

			for (int j = i+1; j < NUM_MODELS; j++) {
				if (models[j].current_state.visible == false) {
					overlay_text = mn::strf(overlay_text, "model[{}] invisible and won't intersect\n", j);
					continue;
				}

				glm::vec3 j_color {0, 0, 1};

				if (aabbs_intersect(models[i].current_aabb, models[j].current_aabb)) {
					overlay_text = mn::strf(overlay_text, "model[{}] intersects model[{}]\n", i, j);
					j_color = i_color = {1, 0, 0};
				} else {
					overlay_text = mn::strf(overlay_text, "model[{}] doesn't intersect model[{}]\n", i, j);
				}

				if (models[j].render_aabb) {
					auto box = aabb_to_box(models[j].current_aabb);
					box.color = j_color;
					mn::buf_push(box_instances, box);
				}
			}

			if (models[i].render_aabb) {
				auto box = aabb_to_box(models[i].current_aabb);
				box.color = i_color;
				mn::buf_push(box_instances, box);
			}
		}

		auto axis_instances = mn::buf_with_allocator<glm::mat4>(mn::memory::tmp());

		glUseProgram(meshes_gpu_program);
		glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "view"), 1, rendering.transpose_view, glm::value_ptr(camera_get_view_matrix(camera)));
		glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection"), 1, rendering.transpose_projection, glm::value_ptr(camera_get_projection_matrix(camera)));

		// render terrain
		{
			auto model_transformation = glm::identity<glm::mat4>();
			model_transformation = glm::scale(model_transformation, terr_mesh.current_state.scale);
			model_transformation = glm::translate(model_transformation, terr_mesh.current_state.translation);
			model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[2], glm::vec3{0, 0, 1});
			model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[1], glm::vec3{1, 0, 0});
			model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[0], glm::vec3{0, 1, 0});
			glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "model"), 1, rendering.transpose_model, glm::value_ptr(model_transformation));

			glUniform1i(glGetUniformLocation(meshes_gpu_program, "gradient_enabled"), (GLint) terr_mesh.gradiant.enabled);
			if (terr_mesh.gradiant.enabled) {
				glUniform1f(glGetUniformLocation(meshes_gpu_program, "gradient_bottom_y"), terr_mesh.gradiant.bottom_y);
				glUniform1f(glGetUniformLocation(meshes_gpu_program, "gradient_top_y"), terr_mesh.gradiant.top_y);
				glUniform3fv(glGetUniformLocation(meshes_gpu_program, "gradient_bottom_color"), 1, glm::value_ptr(terr_mesh.gradiant.bottom_color));
				glUniform3fv(glGetUniformLocation(meshes_gpu_program, "gradient_top_color"), 1, glm::value_ptr(terr_mesh.gradiant.top_color));
			}

			glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), (GLint) false);
			glBindVertexArray(terr_mesh.gpu.vao);
			glDrawArrays(rendering.regular_primitives_type, 0, terr_mesh.gpu.array_count);

			glUniform1i(glGetUniformLocation(meshes_gpu_program, "gradient_enabled"), (GLint) false);
		}

		// render models
		for (int i = 0; i < NUM_MODELS; i++) {
			Model& model = models[i];
			overlay_text = mn::strf(overlay_text, "models[{}]: '{}'\n", i, model.file_abs_path);

			if (model.current_state.visible) {
				if (model.enable_rotating_around) {
					model.current_state.rotation.x += (7 * delta_time) / DEGREES_MAX * RADIANS_MAX;
					model.current_state.rotation.y = -21.0f / DEGREES_MAX * RADIANS_MAX;
				}

				if (animation_config.afterburner_reheat_enabled && animation_config.throttle < animation_config.afterburner_throttle_threshold) {
					animation_config.throttle = animation_config.afterburner_throttle_threshold;
				}

				// apply model transformation
				auto model_transformation = glm::identity<glm::mat4>();
				model_transformation = glm::translate(model_transformation, model.current_state.translation);
				model_transformation = glm::rotate(model_transformation, model.current_state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, model.current_state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, model.current_state.rotation[0], glm::vec3{0, 1, 0});
				// overlay_text = mn::strf(overlay_text, "{}\n", model_transformation);

				// transform AABB (estimate new AABB after rotation)
				{
					// translate AABB
					model.current_aabb.min = model.current_aabb.max = model.current_state.translation;

					// new rotated AABB (no translation)
					const auto model_rotation = glm::mat3(model_transformation);
					const auto rotated_min = model_rotation * model.initial_aabb.min;
					const auto rotated_max = model_rotation * model.initial_aabb.max;
					const AABB rotated_aabb {
						.min = glm::min(rotated_min, rotated_max),
						.max = glm::max(rotated_min, rotated_max),
					};

					// for all three axes
					for (int i = 0; i < 3; i++) {
						// form extent by summing smaller and larger terms respectively
						for (int j = 0; j < 3; j++) {
							const float e = model_rotation[j][i] * rotated_aabb.min[j];
							const float f = model_rotation[j][i] * rotated_aabb.max[j];
							if (e < f) {
								model.current_aabb.min[i] += e;
								model.current_aabb.max[i] += f;
							} else {
								model.current_aabb.min[i] += f;
								model.current_aabb.max[i] += e;
							}
						}
					}
				}

				// start with root meshes
				auto meshes_stack = mn::buf_with_allocator<Mesh*>(mn::memory::tmp());
				for (auto i : model.root_meshes_indices) {
					Mesh* mesh = &model.meshes.values[i].value;
					mesh->transformation = model_transformation;
					mn::buf_push(meshes_stack, mesh);
				}

				while (meshes_stack.count > 0) {
					Mesh* mesh = mn::buf_top(meshes_stack);
					mn_assert(mesh);
					mn::buf_pop(meshes_stack);

					if (animation_config.enabled) {
						if (mesh->animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER) {
							mesh->current_state.rotation.x += animation_config.throttle * animation_config.propoller_max_angle_speed;
						}

						if (mesh->animation_type == AnimationClass::AIRCRAFT_LANDING_GEAR && mesh->animation_states.count > 1) {
							// ignore 3rd STA, it should always be 0 (TODO are they always 0??)
							const MeshState& state_up   = mesh->animation_states[0];
							const MeshState& state_down = mesh->animation_states[1];
							const auto& alpha = animation_config.landing_gear_alpha;

							mesh->current_state.translation = mesh->initial_state.translation + state_down.translation * (1-alpha) +  state_up.translation * alpha;
							mesh->current_state.rotation = glm::eulerAngles(glm::slerp(glm::quat(mesh->initial_state.rotation), glm::quat(state_up.rotation), alpha));// ???

							float visibilty = (float) state_down.visible * (1-alpha) + (float) state_up.visible * alpha;
							mesh->current_state.visible = visibilty > 0.05;;
						}
					}

					const bool enable_high_throttle = almost_equal(animation_config.throttle, 1.0f);
					if (mesh->animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE && enable_high_throttle == false) {
						continue;
					}
					if (mesh->animation_type == AnimationClass::AIRCRAFT_LOW_THROTTLE && enable_high_throttle) {
						continue;
					}

					if (mesh->animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
						if (animation_config.afterburner_reheat_enabled == false) {
							continue;
						}

						if (animation_config.throttle < animation_config.afterburner_throttle_threshold) {
							continue;
						}
					}

					if (mesh->current_state.visible) {
						if (mesh->render_cnt_axis) {
							mn::buf_push(axis_instances, glm::translate(glm::identity<glm::mat4>(), mesh->cnt));
						}

						// apply mesh transformation
						mesh->transformation = glm::translate(mesh->transformation, mesh->current_state.translation);
						mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[2], glm::vec3{0, 0, 1});
						mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[1], glm::vec3{1, 0, 0});
						mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[0], glm::vec3{0, 1, 0});

						if (mesh->render_pos_axis) {
							mn::buf_push(axis_instances, mesh->transformation);
						}

						// upload transofmation model
						glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "model"), 1, rendering.transpose_model, glm::value_ptr(mesh->transformation));

						glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), (GLint) mesh->is_light_source);

						glBindVertexArray(mesh->gpu.vao);
						glDrawArrays(mesh->is_light_source? rendering.light_primitives_type : rendering.regular_primitives_type, 0, mesh->gpu.array_count);

						for (const mn::Str& child_name : mesh->children) {
							auto* kv = mn::map_lookup(model.meshes, child_name);
							mn_assert(kv);
							Mesh* child_mesh = &kv->value;
							child_mesh->transformation = mesh->transformation;
							mn::buf_push(meshes_stack, child_mesh);
						}
					}
				}
			}
		}

		// render axis
		if (axis_instances.count > 0) {
			if (axis_rendering.on_top) {
				glDisable(GL_DEPTH_TEST);
			} else {
				glEnable(GL_DEPTH_TEST);
			}
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(axis_rendering.line_width);
			glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), 0);
			glBindVertexArray(axis_rendering.vao);
			for (const auto& transformation : axis_instances) {
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "model"), 1, rendering.transpose_model, glm::value_ptr(transformation));
				glDrawArrays(GL_LINES, 0, axis_rendering.points_count);
			}
		}

		// render boxes
		if (box_instances.count > 0) {
			glUseProgram(lines_gpu_program);
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(box_rendering.line_width);
			glBindVertexArray(box_rendering.vao);

			const auto projection_view = camera_get_projection_matrix(camera) * camera_get_view_matrix(camera);

			for (const auto& box : box_instances) {
				auto transformation = glm::translate(glm::identity<glm::mat4>(), box.translation);
				transformation = glm::scale(transformation, box.scale);
				const auto projection_view_model = projection_view * transformation;
				glUniformMatrix4fv(glGetUniformLocation(lines_gpu_program, "model_view_projection"), 1, rendering.transpose_model, glm::value_ptr(projection_view_model));

				glUniform3fv(glGetUniformLocation(lines_gpu_program, "color"), 1, glm::value_ptr(box.color));

				glDrawArrays(GL_LINE_LOOP, 0, box_rendering.points_count);
			}
		}

		// imgui
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::TreeNode("DNM Hotreload")) {
				ImGui::Checkbox("Enabled", &dnm_hotreload.enabled);
				ImGui::InputFloat("Check Rate (secs)", &dnm_hotreload.check_rate_secs, 0.5f, 2.0f);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Window")) {
				ImGui::Checkbox("Limit FPS", &should_limit_fps);
				ImGui::BeginDisabled(!should_limit_fps); {
					ImGui::InputInt("FPS", &fps_limit, 1, 5);
				}
				ImGui::EndDisabled();

				int size[2];
				SDL_GetWindowSize(sdl_window, &size[0], &size[1]);
				const bool width_changed = ImGui::InputInt("Width", &size[0]);
				const bool height_changed = ImGui::InputInt("Height", &size[1]);
				if (width_changed || height_changed) {
					SDL_SetWindowSize(sdl_window, size[0], size[1]);
				}

				MyImGui::EnumsCombo("Angle Max", &imgui_angle_max, {
					{DEGREES_MAX, "DEGREES_MAX"},
					{RADIANS_MAX, "RADIANS_MAX"},
					{YS_MAX,      "YS_MAX"},
				});

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Projection")) {
				if (ImGui::Button("Reset")) {
					camera.proj = {};
					wnd_size_changed = true;
				}

				MyImGui::EnumsCombo("Type", &camera.proj.kind, {
					{PROJECTION_KIND_IDENTITY,    "Identity"},
					{PROJECTION_KIND_ORTHO,       "Ortho"},
					{PROJECTION_KIND_PERSPECTIVE, "Perpective"},
				});

				switch (camera.proj.kind) {
				case PROJECTION_KIND_IDENTITY: break;
				case PROJECTION_KIND_ORTHO:
					ImGui::InputFloat("near", &camera.proj.ortho.near, 1, 10);
					ImGui::InputFloat("far", &camera.proj.ortho.far, 1, 10);
					ImGui::InputFloat("left", &camera.proj.ortho.left, 1, 10);
					ImGui::InputFloat("right", &camera.proj.ortho.right, 1, 10);
					ImGui::InputFloat("bottom", &camera.proj.ortho.bottom, 1, 10);
					ImGui::InputFloat("top", &camera.proj.ortho.top, 1, 10);
					break;
				case PROJECTION_KIND_PERSPECTIVE:
					ImGui::InputFloat("near", &camera.proj.pers.near, 1, 10);
					ImGui::InputFloat("far", &camera.proj.pers.far, 1, 10);
					ImGui::DragFloat("fovy (1/zoom)", &camera.proj.pers.fovy, 1, 1, 45);

					if (ImGui::Checkbox("custom aspect", &camera.proj.pers.custom_aspect) && !camera.proj.pers.custom_aspect) {
						wnd_size_changed = true;
					}
					ImGui::BeginDisabled(!camera.proj.pers.custom_aspect);
						ImGui::InputFloat("aspect", &camera.proj.pers.aspect, 1, 10);
					ImGui::EndDisabled();

					break;
				default: mn_unreachable();
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("View")) {
				if (ImGui::Button("Reset")) {
					camera.view = {};
				}

				ImGui::Checkbox("Identity", &camera.view.identity);

				ImGui::BeginDisabled(camera.view.identity);
					ImGui::DragFloat("movement_speed", &camera.view.movement_speed, 0.5, 1, 10);
					ImGui::DragFloat("mouse_sensitivity", &camera.view.mouse_sensitivity, 1, 0.5, 10);
					ImGui::SliderAngle("yaw", &camera.view.yaw);
					ImGui::SliderAngle("pitch", &camera.view.pitch, -89, 89);

					ImGui::DragFloat3("front", glm::value_ptr(camera.view.front), 0.1, -1, 1);
					ImGui::DragFloat3("pos", glm::value_ptr(camera.view.pos), 1, -100, 100);
					ImGui::DragFloat3("world_up", glm::value_ptr(camera.view.world_up), 1, -100, 100);
					ImGui::BeginDisabled();
						ImGui::DragFloat3("right", glm::value_ptr(camera.view.right), 1, -100, 100);
						ImGui::DragFloat3("up", glm::value_ptr(camera.view.up), 1, -100, 100);
					ImGui::EndDisabled();
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Rendering")) {
				if (ImGui::Button("Reset")) {
					rendering = {};
				}

				ImGui::Checkbox("Transpose View", &rendering.transpose_view);
				ImGui::Checkbox("Transpose Projection", &rendering.transpose_projection);
				ImGui::Checkbox("Transpose Model", &rendering.transpose_model);

				MyImGui::EnumsCombo("Polygon Mode", &rendering.polygon_mode, {
					{GL_POINT, "GL_POINT"},
					{GL_LINE,  "GL_LINE"},
					{GL_FILL,  "GL_FILL"},
				});

				MyImGui::EnumsCombo("Regular Mesh Primitives", &rendering.regular_primitives_type, {
					{GL_POINTS,          "GL_POINTS"},
					{GL_LINES,           "GL_LINES"},
					{GL_LINE_LOOP,       "GL_LINE_LOOP"},
					{GL_LINE_STRIP,      "GL_LINE_STRIP"},
					{GL_TRIANGLES,       "GL_TRIANGLES"},
					{GL_TRIANGLE_STRIP,  "GL_TRIANGLE_STRIP"},
					{GL_TRIANGLE_FAN,    "GL_TRIANGLE_FAN"},
				});

				MyImGui::EnumsCombo("Light Mesh Primitives", &rendering.light_primitives_type, {
					{GL_POINTS,          "GL_POINTS"},
					{GL_LINES,           "GL_LINES"},
					{GL_LINE_LOOP,       "GL_LINE_LOOP"},
					{GL_LINE_STRIP,      "GL_LINE_STRIP"},
					{GL_TRIANGLES,       "GL_TRIANGLES"},
					{GL_TRIANGLE_STRIP,  "GL_TRIANGLE_STRIP"},
					{GL_TRIANGLE_FAN,    "GL_TRIANGLE_FAN"},
				});

				ImGui::Checkbox("Smooth Lines", &rendering.smooth_lines);
				ImGui::BeginDisabled(!rendering.smooth_lines);
					ImGui::DragFloat("Line Width", &rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::EndDisabled();

				ImGui::DragFloat("Point Size", &rendering.point_size, POINT_SIZE_GRANULARITY, 0.5, 100);

				ImGui::Checkbox("Culling", &rendering.culling_enabled);
				ImGui::BeginDisabled(!rendering.culling_enabled);
					MyImGui::EnumsCombo("Face To Cull", &rendering.culling_face_type, {
						{GL_FRONT,          "GL_FRONT"},
						{GL_BACK,           "GL_BACK"},
						{GL_FRONT_AND_BACK, "GL_FRONT_AND_BACK"},
					});
					MyImGui::EnumsCombo("Front Face Orientation", &rendering.culling_front_face_type, {
						{GL_CCW, "GL_CCW"},
						{GL_CW,  "GL_CW"},
					});
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Axis Rendering")) {
				ImGui::Checkbox("On Top", &axis_rendering.on_top);
				ImGui::DragFloat("Line Width", &axis_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("AABB Rendering")) {
				ImGui::DragFloat("Line Width", &box_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Animation")) {
				ImGui::Checkbox("Enabled", &animation_config.enabled);
				if (ImGui::Button("Reset")) {
					animation_config = {};
				}

				ImGui::BeginDisabled(animation_config.enabled == false);
					ImGui::DragFloat("Landing Gear", &animation_config.landing_gear_alpha, 0.01, 0, 1);
					if (ImGui::SliderFloat("Throttle", &animation_config.throttle, 0.0f, 1.0f)) {
						if (animation_config.throttle < animation_config.afterburner_throttle_threshold) {
							animation_config.afterburner_reheat_enabled = false;
						}
					}
					MyImGui::SliderAngle("Propoller Max Speed", &animation_config.propoller_max_angle_speed, imgui_angle_max);

					ImGui::NewLine();
					ImGui::Text("Afterburner Reheat");
					ImGui::Checkbox("Enable", &animation_config.afterburner_reheat_enabled);
					ImGui::SliderFloat("Threshold", &animation_config.afterburner_throttle_threshold, 0.0f, 1.0f);
				ImGui::EndDisabled();
				ImGui::TreePop();
			}

			for (int i = 0; i < NUM_MODELS; i++) {
				Model& model = models[i];
				if (ImGui::TreeNode(mn::str_tmpf("Model {}", i).ptr)) {
					models[i].should_open_dnm_file = ImGui::Button("Load DNM");
					models[i].should_load_model = ImGui::Button("Reload");

					if (ImGui::Button("Reset State")) {
						model.current_state = {};
					}
					if (ImGui::Button("Reset All")) {
						model.current_state = {};
						for (auto& [_, mesh] : model.meshes.values) {
							mesh.current_state = mesh.initial_state;
						}
					}

					ImGui::Checkbox("Rotate Around", &model.enable_rotating_around);

					ImGui::Checkbox("visible", &model.current_state.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(model.current_state.translation));
					MyImGui::SliderAngle3("rotation", &model.current_state.rotation, imgui_angle_max);

					ImGui::Checkbox("Render AABB", &model.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(model.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(model.current_aabb.max));

					size_t light_sources_count = 0;
					for (const auto& [_, mesh] : model.meshes.values) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
					}

					ImGui::BulletText(mn::str_tmpf("Meshes: (total: {}, root: {}, light: {})", model.meshes.count,
						model.root_meshes_indices.count, light_sources_count).ptr);

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&model, &render_mesh_ui, imgui_angle_max, animation_config](Mesh& mesh) {
						if (ImGui::TreeNode(mn::str_tmpf("{}", mesh.name).ptr)) {
							if (ImGui::Button("Reset")) {
								mesh.current_state = mesh.initial_state;
							}

							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::BeginDisabled(animation_config.enabled);
								ImGui::Checkbox("visible", &mesh.current_state.visible);
							ImGui::EndDisabled();

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::BeginDisabled(animation_config.enabled);
								ImGui::DragFloat3("translation", glm::value_ptr(mesh.current_state.translation));
								MyImGui::SliderAngle3("rotation", &mesh.current_state.rotation, imgui_angle_max);
							ImGui::EndDisabled();

							ImGui::Text(mn::str_tmpf("{}", mesh.animation_type).ptr);

							ImGui::BulletText(mn::str_tmpf("Children: ({})", mesh.children.count).ptr);
							ImGui::Indent();
							for (const auto& child_name : mesh.children) {
								auto kv = mn::map_lookup(model.meshes, child_name);
								mn_assert(kv);
								render_mesh_ui(kv->value);
							}
							ImGui::Unindent();

							if (ImGui::TreeNode(mn::str_tmpf("Faces: ({})", mesh.faces.count).ptr)) {
								for (size_t i = 0; i < mesh.faces.count; i++) {
									if (ImGui::TreeNode(mn::str_tmpf("{}", i).ptr)) {
										ImGui::TextWrapped("Vertices: %s", mn::str_tmpf("{}", mesh.faces[i].vertices_ids).ptr);

										bool changed = false;
										changed = changed || ImGui::DragFloat3("center", glm::value_ptr(mesh.faces[i].center), 0.1, -1, 1);
										changed = changed || ImGui::DragFloat3("normal", glm::value_ptr(mesh.faces[i].normal), 0.1, -1, 1);
										changed = changed || ImGui::ColorEdit4("color", glm::value_ptr(mesh.faces[i].color));
										if (changed) {
											model_unload_from_gpu(model);
											model_load_to_gpu(model);
										}

										ImGui::TreePop();
									}
								}

								ImGui::TreePop();
							}

							ImGui::TreePop();
						}
					};

					ImGui::Indent();
					for (auto i : model.root_meshes_indices) {
						render_mesh_ui(model.meshes.values[i].value);
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}

			if (ImGui::TreeNode("TerrMesh")) {
				if (ImGui::Button("Reset State")) {
					terr_mesh.current_state = terr_mesh.initial_state;
				}

				if (ImGui::Button("Reload")) {
					terr_mesh_unload_from_gpu(terr_mesh);
					terr_mesh_load_to_gpu(terr_mesh);
				}

				ImGui::DragFloat3("Scale", glm::value_ptr(terr_mesh.current_state.scale), 0.2f);
				ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.current_state.translation));
				MyImGui::SliderAngle3("Rotation", &terr_mesh.current_state.rotation, imgui_angle_max);

				ImGui::TreePop();
			}
		}
		ImGui::End();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Logs")) {
			ImGui::Checkbox("Auto-Scroll", &logs_auto_scrolling);
			ImGui::SameLine();
			ImGui::Checkbox("Wrapped", &logs_wrapped);
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				mn::destruct(logs);
			}

			if (ImGui::BeginChild("logs child", {}, false, logs_wrapped? 0:ImGuiWindowFlags_HorizontalScrollbar)) {
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0, 0});
				ImGuiListClipper clipper(logs.count);
				while (clipper.Step()) {
					for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
						if (logs_wrapped) {
							ImGui::TextWrapped("%s", logs[i].ptr);
						} else {
							ImGui::TextUnformatted(mn::begin(logs[i]), mn::end(logs[i]));
						}
					}
				}
				ImGui::PopStyleVar();

				// scroll
				if (logs_auto_scrolling) {
					if (logs_last_scrolled_line != logs.count) {
						logs_last_scrolled_line = logs.count;
						ImGui::SetScrollHereY();
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();

		{
			const float PAD = 10.0f;
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
			const ImVec2 window_pos {
				work_pos.x + viewport->WorkSize.x - PAD,
				work_pos.y + PAD,
			};
			const ImVec2 window_pos_pivot { 1.0f, 0.0f };
			ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
			ImGui::SetNextWindowSize(ImVec2 {220, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.35f);
		}
		if (ImGui::Begin("Overlay Info", nullptr, ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove)) {
			ImGui::TextWrapped(overlay_text.ptr);
		}
		ImGui::End();

		// ImGui::ShowDemoWindow();

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(sdl_window);

		GL_CATCH_ERRS();
	}

	return 0;
}
/*
bugs:
- tornado.dnm/f1.dnm: strobe lights and landing-gears not in their expected positions
- viggen.dnm: right wheel doesn't rotate right
- cessna172r propoller doesn't rotate
- rotation of terrmesh doesn't work nice with scale (y,z)

TODO:
- what's PAX in dnmver 2?
- what are GE and ZE in hurricane.dnm?
- what are GL in cessna172r.dnm?
- what do if REL DEP not in dnm?

- Scenery files
	- terrmesh
		- side color
	- pict2
		- render
- refactor rendering
	- primitives?
- AABB for each mesh?
- AABB -> OBB?
- use coll.dnm files?
- figure out how to IPO the landing gear (angles in general), no it's not slerp or lerp
- move from animation_config to Model
- animate landing gear transition in real time (no alpha)
- axis
	- better shader
		- no normals
		- no colors
		- refactor shaders
		- geometry shaders
	- 3d axis (camera)
	- select mesh/object
	- move selected (translation of axis)
	- show axis angels
	- rotate selected
	- move internal DNMs to resources dir
- tracking camera (copy from jet-simulator)
- struct parser (track line no, file, peek)
- all rotations as quaternions
- view normals (geometry shader)
- strict integers tokenization
- (optimization): store stack of nodes instead of tree in model
*/
