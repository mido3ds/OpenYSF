#include <cstdio>
#include <cstdint>

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
auto mod(auto a, auto b) {
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
	glm::vec3 rotation; // (degrees) roll, pitch, yaw
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
	glm::vec3 cnt; // ???

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

	// GPU
	struct {
		GLuint vao, vbo, ebo, color_texture;
	} gpu_handles;
	size_t indices_count; // sum(face.vertices_ids.count for face in faces)

	// physics
	glm::mat4 transformation;
	MeshState current_state;
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
	glDeleteVertexArrays(1, &self.gpu_handles.vao);
	glDeleteBuffers(1, &self.gpu_handles.vbo);
	glDeleteBuffers(1, &self.gpu_handles.ebo);
	glDeleteTextures(1, &self.gpu_handles.color_texture);

	self.gpu_handles = {};
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

// DNM See https://ysflightsim.fandom.com/wiki/DynaModel_Files
struct Model {
	mn::Map<mn::Str, Mesh> tree;
	mn::Buf<size_t> root_meshes_indices;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // (degrees) roll, pitch, yaw
		bool visible = true;
	} current_state;
};

void model_load_to_gpu(Model& self) {
	for (auto& [_, mesh] : self.tree.values) {
		// buffers
		glGenVertexArrays(1, &mesh.gpu_handles.vao);
		glBindVertexArray(mesh.gpu_handles.vao);
			glGenBuffers(1, &mesh.gpu_handles.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, mesh.gpu_handles.vbo);
			glBufferData(GL_ARRAY_BUFFER, mesh.vertices.count * sizeof(glm::vec3) * sizeof(float), mesh.vertices.ptr, GL_STATIC_DRAW);

			glGenBuffers(1, &mesh.gpu_handles.ebo);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.gpu_handles.ebo);
			auto indices = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
			for (const auto& face : mesh.faces) {
				mn::buf_concat(indices, face.vertices_ids);
			}
			mesh.indices_count = indices.count;
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices_count * sizeof(GLuint), indices.ptr, GL_STATIC_DRAW);

			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
			glEnableVertexAttribArray(0);
		glBindVertexArray(0);

		GL_CATCH_ERRS();

		// faces colors
		// glEnable(GL_TEXTURE_1D); // crashes with gl3.3
		glGenTextures(1, &mesh.gpu_handles.color_texture);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, mesh.gpu_handles.color_texture);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			auto colors = mn::buf_with_allocator<glm::vec4>(mn::memory::tmp());
			for (const auto& face : mesh.faces) {
				mn::buf_push(colors, face.color);
			}
			glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, colors.count, 0, GL_RGBA, GL_FLOAT, colors.ptr);
		glBindTexture(GL_TEXTURE_1D, 0);

		GL_CATCH_ERRS();
	}
}

void model_unload_from_gpu(Model& self) {
	for (auto& [_, mesh] : self.tree.values) {
		mesh_unload_from_gpu(mesh);
	}
}

bool lines_intersect(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
	const auto ab = b-a;
	const auto ac = c-a;
	const auto ad = d-a;
	return glm::normalize(glm::cross(ab, ac)) == (- glm::normalize(glm::cross(ab, ad)));
}

bool vertex_is_ear(const mn::Buf<glm::vec3> &vertices, const mn::Buf<uint32_t> &indices, int i) {
	// indices
	const uint32_t iv0 = indices[mod(i-2, indices.count)];
	const uint32_t iv1 = indices[mod(i-1, indices.count)];
	const uint32_t iv2 = indices[i];
	const uint32_t iv3 = indices[mod(i+1, indices.count)];
	const uint32_t iv4 = indices[mod(i+2, indices.count)];

	// vertices
	const glm::vec3 v1v0 = glm::normalize(vertices[iv0] - vertices[iv1]);
	const glm::vec3 v1v2 = glm::normalize(vertices[iv2] - vertices[iv1]);
	const glm::vec3 v1v3 = glm::normalize(vertices[iv3] - vertices[iv1]);
	const glm::vec3 v3v2 = glm::normalize(vertices[iv2] - vertices[iv3]);
	const glm::vec3 v3v4 = glm::normalize(vertices[iv4] - vertices[iv3]);
	const glm::vec3 v3v1 = -v1v3;

	// v1v3 must be in Sector(v1v2, v1v0)
	{
		const float phi = glm::acos(glm::dot(v1v2, v1v0));
		if (glm::acos(glm::dot(v1v2, v1v3)) >= phi || glm::acos(glm::dot(v1v0, v1v3)) >= phi) {
			return false;
		}
	}

	// v3v1 must be in Sector(v3v4, v3v2)
	{
		const float phi = glm::acos(glm::dot(v3v4, v3v2));
		if (glm::acos(glm::dot(v3v4, v3v1)) >= phi || glm::acos(glm::dot(v3v2, v3v1)) >= phi) {
			return false;
		}
	}

	// edge_i: (v1, v3) must not intersect with any other edge in polygon

	// for edge_j in edges:
	//   if not share_vertex(edge_i, edge_j):
	//     if intersects(edge_i, edge_j): return false
	for (size_t j = 0; j < indices.count; j++) {
		// edge_j
		const uint32_t jv0 = indices[j];
		const uint32_t jv1 = indices[mod(j+1, indices.count)];

		// don't test the edge if it shares a vertex with it
		if ((jv0 != iv1 && jv0 != iv3) && (jv1 != iv1 && jv1 != iv3)) {
			// intersects(jv0 -> jv1, iv2 -> iv3)?
			const auto ab = vertices[jv1] - vertices[jv0];
			// TODO: probably wrong algorithm
			if (glm::cross(ab, (vertices[iv2] - vertices[jv0])) == -1.0f * glm::cross(ab, (vertices[iv3] - vertices[jv0]))) {
				return false;
			}
		}
	}

	return true;
}

mn::Buf<uint32_t>
polygons_to_triangles(const mn::Buf<glm::vec3>& vertices, const mn::Buf<uint32_t>& orig_indices) {
	mn::Buf<uint32_t> out {};

	auto indices = mn::buf_clone(orig_indices, mn::memory::tmp());
	size_t k = 0;
	while (indices.count > 3) {
		k++;
		// mn_assert_msg(k <= orig_indices.count, "can't tesselate");
		if (k > orig_indices.count) {
			mn::log_error("can't tesselate");
			return orig_indices;
		}

		for (size_t i = 0; i < indices.count; i++) {
			if (vertex_is_ear(vertices, indices, i)) {
				mn::buf_push(out, indices[mod(i-1, indices.count)]);
				mn::buf_push(out, indices[i]);
				mn::buf_push(out, indices[mod(i+1, indices.count)]);

				mn::buf_remove_ordered(indices, i);

				// exit the loop so that we check again the first vertex of the loop, maybe it became now a convex one
				break;
			}
		}
	}

	mn_assert(indices.count == 3);
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

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,3,0},
			glm::vec3{3,2,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == true);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,3,0},
			glm::vec3{3,-200,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == true);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,3,0},
			glm::vec3{3,200,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == false);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,2,0},
			glm::vec3{3,2,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == true);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,1,0},
			glm::vec3{3,2,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == false);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,3,0},
			glm::vec3{3,2,0},
			glm::vec3{4,2,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == true);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,3,0},
			glm::vec3{3,2,0},
			glm::vec3{4,1,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(vertex_is_ear(vertices, indices, 2) == false);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{4,4,0},
			glm::vec3{5,3,0},
			glm::vec3{4,2,0},
			glm::vec3{3,3,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3});
		mn_assert(vertex_is_ear(vertices, indices, 0) == true);
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{4,4,0},
			glm::vec3{5,3,0},
			glm::vec3{4,2,0},
			glm::vec3{3,3,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3});
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices), mn::buf_lit<uint32_t>({3, 0, 1, 1, 2, 3})));
	}

	{
		auto vertices = mn::buf_lit({
			glm::vec3{2,4,0},
			glm::vec3{2,2,0},
			glm::vec3{3,2,0},
			glm::vec3{4,3,0},
			glm::vec3{4,4,0},
		});
		auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices), mn::buf_lit<uint32_t>({0, 1, 2, 4, 0, 2, 2, 3, 4})));
	}
}

Model model_from_dnm(const char* dnm_file) {
	auto s = mn::str_tmp(dnm_file);
	mn::str_replace(s, "\r\n", "\n");

	expect(s, "DYNAMODEL\nDNMVER 1\n");

	auto surfs = mn::map_new<mn::Str, Mesh>();
	while (accept(s, "PCK ")) {
		auto name = token_str(s, mn::memory::tmp());
		expect(s, ' ');
		const auto expected_lines = token_u64(s);
		auto lines = expected_lines;
		expect(s, '\n');

		expect(s, "SURF\n");
		Mesh surf {};

		// V {x} {y} {z}[ R]\n
		while (accept(s, "V ")) {
			glm::vec3 v {};

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
		auto faces_unshaded_light_source = mn::buf_with_allocator<bool>(mn::memory::tmp());
		while (!accept(s, '\n')) {
			Face face {};
			bool parsed_color = false,
				parsed_normal = false,
				parsed_vertices = false,
				is_light_source = false;

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

					face.color.r = token_u8(s) / 255.0f;
					expect(s, ' ');
					face.color.g = token_u8(s) / 255.0f;
					expect(s, ' ');
					face.color.b = token_u8(s) / 255.0f;
					expect(s, '\n');
					face.color.a = 1.0f; // maybe overwritten in ZA line
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				} else if (accept(s, "N ")) {
					if (parsed_normal) {
						mn::panic("found more than one normal");
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
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				} else if (accept(s, 'V')) {
					// V {x}...
					if (parsed_vertices) {
						mn::panic("found more than one vertices line");
					}
					parsed_vertices = true;

					auto polygon_vertices_ids = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
					while (accept(s, ' ')) {
						uint32_t id = token_u32(s);
						if (id >= surf.vertices.count) {
							mn::panic("id={} out of bounds={}", id, surf.vertices.count);
						}
						mn::buf_push(polygon_vertices_ids, (uint32_t) id);
					}
					expect(s, '\n');
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}

					if (polygon_vertices_ids.count < 3) {
						mn::log_warning("face has count of ids={}, it should be >= 3, {}", polygon_vertices_ids.count, smaller_str(s));
					}

					// if (polygon_vertices_ids.count == 4) {
					// 	mn::buf_push(polygon_vertices_ids, polygon_vertices_ids[3]);
					// 	polygon_vertices_ids[3] = polygon_vertices_ids[2];
					// 	mn::buf_push(polygon_vertices_ids, polygon_vertices_ids[0]);
					// }
					face.vertices_ids = polygons_to_triangles(surf.vertices, polygon_vertices_ids);
					if (face.vertices_ids.count % 3 != 0) {
						mn::log_error("must have been divisble by 3, but found {}", face.vertices_ids.count);
					}
				} else if (accept(s, "B\n")) {
					if (is_light_source) {
						mn::panic("found more than 1 B");
					}
					is_light_source = true;
					if (lines-- == 0) {
						mn::panic("expected {} lines, found more", expected_lines);
					}
				} else {
					mn::panic("unexpected line, '{}'", smaller_str(s));
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

			mn::buf_push(faces_unshaded_light_source, is_light_source);
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
				surf.faces[id].color.a = (255 - token_u8(s)) / 255.0f;
				// because alpha came as: 0 -> obaque, 255 -> clear
				// we revert it so it becomes: 1 -> obaque, 0 -> clear
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
			mn::log_error("expected {} lines, found {}", expected_lines, expected_lines - lines);
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
		// YS angle format to degrees, extracted from ys blender scripts
		constexpr double YSA_TO_DEGREES = 0.005493164;
		for (size_t i = 0; i < num_stas; i++) {
			expect(s, "STA ");

			MeshState sta {};
			sta.translation.x = token_float(s);
			expect(s, ' ');
			sta.translation.y = token_float(s);
			expect(s, ' ');
			sta.translation.z = token_float(s);
			expect(s, ' ');

			sta.rotation.x = token_i32(s) * YSA_TO_DEGREES;
			expect(s, ' ');
			sta.rotation.y = token_i32(s) * YSA_TO_DEGREES;
			expect(s, ' ');
			sta.rotation.z = token_i32(s) * YSA_TO_DEGREES;
			expect(s, ' ');

			sta.visible = token_u8(s) == 1;
			expect(s, '\n');

			mn::buf_push(surf->value.animation_states, sta);
		}

		expect(s, "POS ");
		surf->value.initial_state.translation.x = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.translation.y = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.translation.z = token_float(s);
		expect(s, ' ');
		surf->value.initial_state.rotation.x = token_i32(s) * YSA_TO_DEGREES;
		expect(s, ' ');
		surf->value.initial_state.rotation.y = token_i32(s) * YSA_TO_DEGREES;
		expect(s, ' ');
		surf->value.initial_state.rotation.z = token_i32(s) * YSA_TO_DEGREES;
		expect(s, ' ');
		surf->value.initial_state.visible = token_u8(s) == 1;
		expect(s, '\n');
		surf->value.current_state = surf->value.initial_state;

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
		surf = mn::map_insert(surfs, mn::str_clone(name), surf->value);
		if (!mn::map_remove(surfs, fil)) {
			mn::panic("must be able to remove {} from tree", fil);
		}

		expect(s, "END\n");
	}
	expect(s, "END\n");

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
		.tree = surfs,
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

	return model;
}

void model_free(Model& self) {
	mn::destruct(self.tree);
	mn::destruct(self.root_meshes_indices);
}

void destruct(Model& self) {
	model_free(self);
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
PCK habal.srf 24
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
PCK triangle.srf 11
SURF
V 0.500 0.500 0.000
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
PCK rectangle.srf 17
SURF
V 0.500 0.500 0.000
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



PCK rectangle2.srf 18
SURF
V 0.500 0.500 0.000
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
NCH 1
CLD "rectangle2"
END
SRF "rectangle2"
FIL rectangle2.srf
CLA 2
NST 0
POS 0.6000 0.4000 0.0000 0 0 0 1
CNT 0.0000 0.0000 0.0000
REL DEP
NCH 0
END
END
)";

constexpr auto BOX_DNM = R"(DYNAMODEL
DNMVER 1
PCK box.srf 71
SURF
V -0.500 -0.500 -0.500
V 0.500 -0.500 -0.500
V 0.500 0.500 -0.500
V -0.500 0.500 -0.500
V -0.500 -0.500 0.500
V 0.500 -0.500 0.500
V 0.500 0.500 0.500
V -0.500 0.500 0.500
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 0 1 2
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 2 3 0
E
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 4 5 6
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 6 7 4
E
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 7 3 0
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 0 4 7
E
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 6 2 1
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 1 5 6
E
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 0 1 5
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 5 4 0
E
F
C 255 0 0
N 0.000 0.000 0.000 0.000 0.000 0.000
V 3 2 6
E
F
C 255 255 255
N 0.000 0.000 0.000 0.000 0.000 0.000
V 6 7 3
E



SRF "box"
FIL box.srf
CLA 2
NST 0
POS 0.0000 0.0000 0.0000 0 0 0 1
CNT 0.0000 0.0000 0.0000
REL DEP
NCH 0
END
END
)";

// smaller ground/vasi.dnm
constexpr auto VASI_DNM = R"(DYNAMODEL
DNMVER 1
PCK 00000006.srf 69
SURF
V -3.525 0 -0.625
V -2.475 0 -0.625
V -2.475 1.05 -0.625
V -3.525 1.05 -0.625
V 3.525 0 -0.625
V 2.475 0 -0.625
V 2.475 1.05 -0.625
V 3.525 1.05 -0.625
V -3.525 1.05 0.375
V -3.525 0 0.375
V -2.475 0 0.375
V -2.475 1.05 0.375
V 2.475 1.05 0.375
V 2.475 0 0.375
V 3.525 0 0.375
V 3.525 1.05 0.375
F
V 1 0 3 2
N -3 0.525 -0.625 -0 -0 -1
C 255 255 0
E
F
V 6 7 4 5
N 3 0.525 -0.625 -0 -0 -1
C 255 255 0
E
F
V 3 0 9 8
N -3.525 0.525 -0.125 -1 -0 -0
C 255 255 0
E
F
V 0 1 10 9
N -3 0 -0.125 0 -1 0
C 255 255 0
E
F
V 1 2 11 10
N -2.475 0.525 -0.125 1 0 0
C 255 255 0
E
F
V 2 3 8 11
N -3 1.05 -0.125 0 1 0
C 255 255 0
E
F
V 6 5 13 12
N 2.475 0.525 -0.125 -1 -0 -0
C 255 255 0
E
F
V 5 4 14 13
N 3 0 -0.125 0 -1 0
C 255 255 0
E
F
V 4 7 15 14
N 3.525 0.525 -0.125 1 0 0
C 255 255 0
E
F
V 7 6 12 15
N 3 1.05 -0.125 0 1 0
C 255 255 0
E



SRF "Boxes"
FIL 00000006.srf
CLA 0
NST 0
POS 0.0000 0.0000 0.0000 0 0 0 1
CNT 0.0000 0.0000 0.0000
REL DEP
NCH 0
END
END
)";

constexpr auto WND_TITLE        = "JFS";
constexpr int  WND_INIT_WIDTH   = 1028;
constexpr int  WND_INIT_HEIGHT  = 680;
constexpr float IMGUI_WNDS_BG_ALPHA = 0.8f;
constexpr glm::vec3 BG_COLOR {0.392f, 0.584f, 0.929f};

constexpr auto GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_CORE;
constexpr int  GL_CONTEXT_MAJOR = 3;
constexpr int  GL_CONTEXT_MINOR = 3;
constexpr auto GL_DOUBLE_BUFFER = SDL_TRUE;

const char* PROJECTION_KIND_STR[] { "PROJECTION_KIND_IDENTITY", "PROJECTION_KIND_ORTHO", "PROJECTION_KIND_PERSPECTIVE" };
enum PROJECTION_KIND { PROJECTION_KIND_IDENTITY, PROJECTION_KIND_ORTHO, PROJECTION_KIND_PERSPECTIVE };

struct Camera {
	struct {
		bool identity           = false;
		float movement_speed    = 90.5f;
		float mouse_sensitivity = 0.1f;

		glm::vec3 pos      = glm::vec3{0.0f, 0.0f, 3.0f};
		glm::vec3 front    = glm::vec3{0.0f, 0.0f, -1.0f};
		glm::vec3 world_up = glm::vec3{0.0f, 1.0f, 0.0f};
		glm::vec3 right    = glm::vec3{1.0f, 0.0f, 0.0f};
		glm::vec3 up       = world_up;

		float yaw   = -90.0f; // degrees
		float pitch = 0.0f; // degrees
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
			float fovy         = 45.0f; // degrees
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
			glm::radians(self.proj.pers.fovy),
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

void camera_update(Camera& self, const SDL_Event &event, float delta_time) {
	if (self.view.identity) {
		return;
	}

	if (event.type == SDL_KEYDOWN) {
		const float velocity = self.view.movement_speed * delta_time;
		switch (event.key.keysym.sym) {
		case 'w': self.view.pos += self.view.front * velocity; break;
		case 's': self.view.pos -= self.view.front * velocity; break;
		case 'd': self.view.pos += self.view.right * velocity; break;
		case 'a': self.view.pos -= self.view.right * velocity; break;
		default: break;
		}
	} else if (event.type == SDL_MOUSEMOTION && (SDL_GetMouseState(0,0) & SDL_BUTTON(SDL_BUTTON_RIGHT))) {
		if (SDL_GetModState() & KMOD_SHIFT) {
			self.proj.pers.fovy = clamp(self.proj.pers.fovy - event.motion.yrel * self.view.mouse_sensitivity, 1.0f, 45.0f);
		} else {
			self.view.yaw   -= event.motion.xrel * self.view.mouse_sensitivity;
			self.view.pitch += event.motion.yrel * self.view.mouse_sensitivity;

			// make sure that when pitch is out of bounds, screen doesn't get flipped
			self.view.pitch = clamp(self.view.pitch, -89.0f, 89.0f);
		}
	}

	// update front, right and up Vectors using the updated Euler angles
	glm::vec3 front;
	front.x = cos(glm::radians(self.view.yaw)) * cos(glm::radians(self.view.pitch));
	front.y = sin(glm::radians(self.view.pitch));
	front.z = sin(glm::radians(self.view.yaw)) * cos(glm::radians(self.view.pitch));
	self.view.front = glm::normalize(front);

	// normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
	self.view.right = glm::normalize(glm::cross(self.view.front, self.view.world_up));
	self.view.up    = glm::normalize(glm::cross(self.view.right, self.view.front));
}

GLfloat _glGetFloat(GLenum e) {
	GLfloat out;
	glGetFloatv(e, &out);
	return out;
}

int main() {
	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		mn::log_error("Failed to init video, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_Quit());

	auto sdl_window = SDL_CreateWindow(
		WND_TITLE,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WND_INIT_WIDTH, WND_INIT_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	);
	if (!sdl_window) {
		mn::log_error("Faield to create window, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_DestroyWindow(sdl_window));
	SDL_SetWindowBordered(sdl_window, SDL_TRUE);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_PROFILE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_CONTEXT_MAJOR);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_CONTEXT_MINOR);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_DOUBLE_BUFFER);

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
		uniform mat4 projection, view, model;
		void main() {
			gl_Position = projection * view * model * vec4(attr_pos, 1.0);
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
		uniform bool is_light_source;
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

	// model
	Model model = model_from_dnm(VASI_DNM);
	mn_defer(model_free(model));
	model_load_to_gpu(model);
	mn_defer(model_unload_from_gpu(model));

	bool should_load_model = false;
	bool should_reload_model = false;

	auto model_file_path = mn::str_from_c("Internal::Box");
	mn_defer(mn::str_free(model_file_path));

	Camera camera {};

	bool wnd_size_changed = true;
	bool running = true;
	bool fullscreen = false;

	Uint32 time_millis = SDL_GetTicks();
	double delta_time; // 1/seconds

	bool should_limit_fps = true;
	int fps_limit = 60;
	int millis_till_render = 0;

	mn::Str logs {};
	mn_defer(mn::str_free(logs));
	bool enable_window_logs = true;
	auto old_log_interface = mn::log_interface_set(mn::Log_Interface{
		// pointer to user data
		.self = &logs,
		.debug = +[](void* self, const char* msg) {
			auto logs = (mn::Str*) self;
			*logs = mn::strf(*logs, "> {}\n", msg);
		},
		.info = +[](void* self, const char* msg) {
			auto logs = (mn::Str*) self;
			*logs = mn::strf(*logs, "[info] {}\n", msg);
		},
		.warning = +[](void* self, const char* msg) {
			auto logs = (mn::Str*) self;
			*logs = mn::strf(*logs, "[warning] {}\n", msg);
		},
		.error = +[](void* self, const char* msg) {
			auto logs = (mn::Str*) self;
			*logs = mn::strf(*logs, "[error] {}\n", msg);
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
		GLenum light_primitives_type   = GL_LINES;
		GLenum polygon_mode            = GL_FILL;
	} rendering {};

	const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = _glGetFloat(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
	const GLfloat POINT_SIZE_GRANULARITY        = _glGetFloat(GL_POINT_SIZE_GRANULARITY);

	struct {
		bool enabled = true;

		float check_rate_secs = 1.0f;
		float last_check_time = 0; // when checked file
		int64_t last_write_time = 0; // when file was written to (by some other progrem)
	} dnm_hotreload;

	while (running) {
		mn::memory::tmp()->clear_all();
		mn::str_free(logs);

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

			mn::log_debug("fps: {:.2f}", 1.0f/delta_time);
		}

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			camera_update(camera, event, delta_time);
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
						SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
					} else {
						SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL);
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

		if (should_load_model) {
			should_load_model = false;

			auto result = pfd::open_file("Select DNM", "", {"DNM Files", "*.dnm", "All Files", "*"}).result();
			if (result.size() == 1) {
				mn::str_free(model_file_path);
				model_unload_from_gpu(model);
				model_free(model);

				model_file_path = mn::str_from_c(result[0].c_str());
				model = model_from_dnm(mn::file_content_str(model_file_path, mn::memory::tmp()).ptr);
				model_load_to_gpu(model);
			}
		}

		if (dnm_hotreload.enabled) {
			dnm_hotreload.last_check_time += delta_time;
			if (dnm_hotreload.last_check_time >= dnm_hotreload.check_rate_secs) {
				dnm_hotreload.last_check_time = 0;
				if (mn::str_prefix(model_file_path, "Internal::") == false) {
					int64_t write_time = mn::file_last_write_time(model_file_path);
					if (write_time > dnm_hotreload.last_write_time) {
						dnm_hotreload.last_write_time = write_time;
						should_reload_model = true;
					}
				}
			}
		}

		if (should_reload_model) {
			should_reload_model = false;

			model_unload_from_gpu(model);
			model_free(model);

			model = model_from_dnm(mn::file_content_str(model_file_path, mn::memory::tmp()).ptr);
			model_load_to_gpu(model);
		}

		glEnable(GL_DEPTH_TEST);
		glClearColor(BG_COLOR.x, BG_COLOR.y, BG_COLOR.z, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shader_program);
		glUniformMatrix4fv(glGetUniformLocation(shader_program, "view"), 1, rendering.transpose_view, glm::value_ptr(camera_get_view_matrix(camera)));
		glUniformMatrix4fv(glGetUniformLocation(shader_program, "projection"), 1, rendering.transpose_projection, glm::value_ptr(camera_get_projection_matrix(camera)));

		if (rendering.smooth_lines) {
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(rendering.line_width);
		} else {
			glDisable(GL_LINE_SMOOTH);
		}
		glPointSize(rendering.point_size);
		glPolygonMode(GL_FRONT_AND_BACK, rendering.polygon_mode);

		mn::log_debug("model: '{}'", model_file_path);

		if (model.current_state.visible) {
			// apply model transformation
			auto model_transformation = glm::identity<glm::mat4>();
			model_transformation = glm::translate(model_transformation, model.current_state.translation);
			model_transformation = glm::rotate(model_transformation, glm::radians(model.current_state.rotation[0]), glm::vec3(1, 0, 0));
			model_transformation = glm::rotate(model_transformation, glm::radians(model.current_state.rotation[1]), glm::vec3(0, 1, 0));
			model_transformation = glm::rotate(model_transformation, glm::radians(model.current_state.rotation[2]), glm::vec3(0, 0, 1));

			// start with root meshes
			auto meshes_stack = mn::buf_with_allocator<Mesh*>(mn::memory::tmp());
			for (auto i : model.root_meshes_indices) {
				Mesh* mesh = &model.tree.values[i].value;
				mesh->transformation = model_transformation;
				mn::buf_push(meshes_stack, mesh);
			}

			while (meshes_stack.count > 0) {
				Mesh* mesh = mn::buf_top(meshes_stack);
				mn_assert(mesh);
				mn::buf_pop(meshes_stack);

				if (mesh->current_state.visible) {
					// apply mesh transformation
					mesh->transformation = glm::translate(mesh->transformation, mesh->current_state.translation);
					mesh->transformation = glm::rotate(mesh->transformation, glm::radians(mesh->current_state.rotation[0]), glm::vec3(1, 0, 0));
					mesh->transformation = glm::rotate(mesh->transformation, glm::radians(mesh->current_state.rotation[1]), glm::vec3(0, 1, 0));
					mesh->transformation = glm::rotate(mesh->transformation, glm::radians(mesh->current_state.rotation[2]), glm::vec3(0, 0, 1));

					// upload transofmation model
					glUniformMatrix4fv(glGetUniformLocation(shader_program, "model"), 1, rendering.transpose_model, glm::value_ptr(mesh->transformation));

					glUniform1i(glGetUniformLocation(shader_program, "is_light_source"), (GLint) mesh->is_light_source);

					glBindVertexArray(mesh->gpu_handles.vao);

					if (mesh->is_light_source) {
						glDrawElements(rendering.light_primitives_type, mesh->indices_count * sizeof(GLuint), GL_UNSIGNED_INT, 0);
					} else {
						// upload texture
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_1D, mesh->gpu_handles.color_texture);

						// draw faces
						glDrawElements(rendering.regular_primitives_type, mesh->indices_count * sizeof(GLuint), GL_UNSIGNED_INT, 0);
					}

					for (const mn::Str& child_name : mesh->children) {
						auto* kv = mn::map_lookup(model.tree, child_name);
						mn_assert(kv);
						Mesh* child_mesh = &kv->value;
						child_mesh->transformation = mesh->transformation;
						mn::buf_push(meshes_stack, child_mesh);
					}
				}
			}
		}

		// imgui
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::Button("Load DNM")) {
				should_load_model = true;
			}
			if (ImGui::Button("Reload")) {
				should_reload_model = true;
			}

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

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Projection")) {
				if (ImGui::Button("Reset")) {
					camera.proj = {};
					wnd_size_changed = true;
				}

				if (ImGui::BeginCombo("Type", PROJECTION_KIND_STR[camera.proj.kind])) {
					for (auto type : {PROJECTION_KIND_IDENTITY, PROJECTION_KIND_ORTHO, PROJECTION_KIND_PERSPECTIVE}) {
						if (ImGui::Selectable(PROJECTION_KIND_STR[type], camera.proj.kind == type)) {
							camera.proj.kind = type;
						}
					}

					ImGui::EndCombo();
				}

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
					ImGui::DragFloat("movement_speed", &camera.view.movement_speed, 2, 0, 300);
					ImGui::DragFloat("mouse_sensitivity", &camera.view.mouse_sensitivity, 0.04, 0, 0.8);
					ImGui::DragFloat("yaw", &camera.view.yaw, 1, 0, 360);
					ImGui::DragFloat("pitch", &camera.view.pitch, 1, -89, 89);

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

				constexpr auto polygon_mode_to_str = +[](GLenum m) {
					switch (m) {
					case GL_POINT: return "GL_POINT";
					case GL_LINE: return "GL_LINE";
					case GL_FILL: return "GL_FILL";
					default: mn_unreachable();
					}
					return "????";
				};
				if (ImGui::BeginCombo("Polygon Mode", polygon_mode_to_str((int) rendering.polygon_mode))) {
					for (auto type : {GL_POINT, GL_LINE, GL_FILL}) {
						if (ImGui::Selectable(polygon_mode_to_str((int)type), rendering.polygon_mode == type)) {
							rendering.polygon_mode = type;
						}
					}

					ImGui::EndCombo();
				}

				for (auto [text, var] : {
					std::pair{"Regular Mesh Primitives", &rendering.regular_primitives_type},
					std::pair{"Light Mesh Primitives", &rendering.light_primitives_type},
				}) {
					const char* PRIMITIVE_TYPE_STR[] {
						"GL_POINTS",
						"GL_LINES",
						"GL_LINE_LOOP",
						"GL_LINE_STRIP",
						"GL_TRIANGLES",
						"GL_TRIANGLE_STRIP",
						"GL_TRIANGLE_FAN"
					};
					if (ImGui::BeginCombo(text, PRIMITIVE_TYPE_STR[(int) *var])) {
						for (auto type : {GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN}) {
							if (ImGui::Selectable(PRIMITIVE_TYPE_STR[(int)type], *var == type)) {
								*var = type;
							}
						}

						ImGui::EndCombo();
					}
				}

				ImGui::Checkbox("Smooth Lines", &rendering.smooth_lines);
				ImGui::BeginDisabled(!rendering.smooth_lines);
					ImGui::DragFloat("Line Width", &rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::EndDisabled();

				ImGui::DragFloat("Point Size", &rendering.point_size, POINT_SIZE_GRANULARITY, 0.5, 100);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Model")) {
				if (ImGui::Button("Reset State")) {
					model.current_state = {};
				}
				if (ImGui::Button("Reset All")) {
					model.current_state = {};
					for (auto& [_, mesh] : model.tree.values) {
						mesh.current_state = mesh.initial_state;
					}
				}

				ImGui::Checkbox("visible", &model.current_state.visible);
				ImGui::DragFloat3("translation", glm::value_ptr(model.current_state.translation), 0.1, -1, 1);
				ImGui::DragFloat3("rotation", glm::value_ptr(model.current_state.rotation), 5, 0, 180);

				size_t light_sources_count = 0;
				for (const auto& [_, mesh] : model.tree.values) {
					if (mesh.is_light_source) {
						light_sources_count++;
					}
				}

				ImGui::BulletText(mn::str_tmpf("Meshes: (total: {}, root: {}, light: {})", model.tree.count,
					model.root_meshes_indices.count, light_sources_count).ptr);

				std::function<void(Mesh&)> render_mesh_ui;
				render_mesh_ui = [&model, &render_mesh_ui](Mesh& mesh) {
					if (ImGui::TreeNode(mn::str_tmpf("{}", mesh.name).ptr)) {
						if (ImGui::Button("Reset")) {
							mesh.current_state = mesh.initial_state;
						}

						ImGui::Checkbox("light source", &mesh.is_light_source);
						ImGui::Checkbox("visible", &mesh.current_state.visible);

						ImGui::BeginDisabled();
							ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
						ImGui::EndDisabled();

						ImGui::DragFloat3("translation", glm::value_ptr(mesh.current_state.translation), 0.1, -1, 1);
						ImGui::DragFloat3("rotation", glm::value_ptr(mesh.current_state.rotation), 5, 0, 180);

						ImGui::BulletText(mn::str_tmpf("Children: ({})", mesh.children.count).ptr);
						ImGui::Indent();
						for (const auto& child_name : mesh.children) {
							auto kv = mn::map_lookup(model.tree, child_name);
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
					render_mesh_ui(model.tree.values[i].value);
				}
				ImGui::Unindent();

				ImGui::TreePop();
			}
		}
		ImGui::End();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Logs")) {
			if (ImGui::Checkbox("Enable", &enable_window_logs)) {
				old_log_interface = mn::log_interface_set(old_log_interface);
			}
			if (logs.count > 0) {
				ImGui::TextWrapped("%s", logs.ptr);
			}
		}
		ImGui::End();

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(sdl_window);

		GL_CATCH_ERRS();
	}

	return 0;
}
/*
TODO:
- fix tesselate
- fix positions
- what is CNT?
- don't clear logs
- overlay infos (fps, model path)

- strict integers tokenization
*/
