// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>

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

#include "imgui.hpp"
#include "gpu.hpp"
#include "parser.hpp"
#include "math.hpp"

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

// from YSFLIGHT SCENERY EDITOR 2009
// ???
enum class FieldID {
	NONE=0,
	RUNWAY=1,
	TAXIWAY=2,
	AIRPORT_AREA=4,
	ENEMY_TANK_GENERATOR=6,
	FRIENDLY_TANK_GENERATOR=7,
	TOWER=10, // ???? not sure (from small.fld)
	VIEW_POINT=20,
};

namespace fmt {
	template<>
	struct formatter<FieldID> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const FieldID &v, FormatContext &ctx) {
			switch (v) {
			case FieldID::NONE:                    return format_to(ctx.out(), "FieldID::NONE");
			case FieldID::RUNWAY:                  return format_to(ctx.out(), "FieldID::RUNWAY");
			case FieldID::TAXIWAY:                 return format_to(ctx.out(), "FieldID::TAXIWAY");
			case FieldID::AIRPORT_AREA:            return format_to(ctx.out(), "FieldID::AIRPORT_AREA");
			case FieldID::ENEMY_TANK_GENERATOR:    return format_to(ctx.out(), "FieldID::ENEMY_TANK_GENERATOR");
			case FieldID::FRIENDLY_TANK_GENERATOR: return format_to(ctx.out(), "FieldID::FRIENDLY_TANK_GENERATOR");
			case FieldID::TOWER:                   return format_to(ctx.out(), "FieldID::TOWER");
			case FieldID::VIEW_POINT:              return format_to(ctx.out(), "FieldID::VIEW_POINT");
			default: mn::log_error("found unknown ID = {}", (int) v);
			}
			return format_to(ctx.out(), "FieldID::????");
		}
	};
}

// SURF
struct Mesh {
	FieldID id;

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

void mesh_load_to_gpu(Mesh& self) {
	struct Stride {
		glm::vec3 vertex;
		glm::vec4 color;
		glm::vec3 normal;
	};
	auto buffer = mn::buf_with_allocator<Stride>(mn::memory::tmp());
	for (const auto& face : self.faces) {
		for (size_t i = 0; i < face.vertices_ids.count; i++) {
			mn::buf_push(buffer, Stride {
				.vertex=self.vertices[face.vertices_ids[i]],
				.color=face.color,
				.normal=face.normal,
			});
		}
	}
	self.gpu.array_count = buffer.count;

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

	gpu_check_errors();
}

void mesh_unload_from_gpu(Mesh& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};
}

struct StartInfo {
	mn::Str name;
	glm::vec3 position;
	glm::vec3 attitude;
	float speed;
	float throttle;
	bool landing_gear_is_out = true;
};

void start_info_free(StartInfo& self) {
	mn::str_free(self.name);
}

void destruct(StartInfo& self) {
	start_info_free(self);
}

float _token_distance(Parser& parser) {
	const float x = parser_token_float(parser);

	if (parser_accept(parser, 'm')) {
		return x;
	} else if (parser_accept(parser, "ft")) {
		return x / 3.281f;
	}

	parser_panic(parser, "expected either m or ft");
	return x;
}

float _token_angle(Parser& parser) {
	const float x = parser_token_float(parser);
	parser_expect(parser, "deg");
	return x / DEGREES_MAX * RADIANS_MAX;
}

bool _token_bool(Parser& parser) {
	const auto x = parser_token_str(parser, mn::memory::tmp());
	if (x == "TRUE") {
		return true;
	} else if (x == "FALSE") {
		return false;
	}
	parser_panic(parser, "expected either TRUE or FALSE, found='{}'", x);
	return false;
}

mn::Buf<StartInfo> start_info_from_stp_file(const mn::Str& stp_file_abs_path) {
	auto parser = parser_from_file(stp_file_abs_path, mn::memory::tmp());

	auto start_infos = mn::buf_new<StartInfo>();

	while (parser_finished(parser) == false) {
		StartInfo start_info {};

		parser_expect(parser, "N ");
		start_info.name = parser_token_str(parser);
		parser_expect(parser, '\n');

		while (parser_accept(parser, "C ")) {
			if (parser_accept(parser, "POSITION ")) {
				start_info.position.x = _token_distance(parser);
				parser_expect(parser, ' ');
				start_info.position.y = -_token_distance(parser);
				parser_expect(parser, ' ');
				start_info.position.z = _token_distance(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "ATTITUDE ")) {
				start_info.attitude.x = _token_angle(parser);
				parser_expect(parser, ' ');
				start_info.attitude.y = _token_angle(parser);
				parser_expect(parser, ' ');
				start_info.attitude.z = _token_angle(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "INITSPED ")) {
				start_info.speed = parser_token_float(parser);
				parser_expect(parser, "MACH\n");
			} else if (parser_accept(parser, "CTLTHROT ")) {
				start_info.throttle = parser_token_float(parser);
				parser_expect(parser, '\n');
				if (start_info.throttle > 1 || start_info.throttle < 0) {
					mn::panic("throttle={} out of bounds [0,1]", start_info.throttle);
				}
			} else if (parser_accept(parser, "CTLLDGEA ")) {
				start_info.landing_gear_is_out = _token_bool(parser);
				parser_expect(parser, '\n');
			} else {
				parser_panic(parser, "unrecognized type");
			}

			while (parser_accept(parser, '\n')) {}
		}

		mn::buf_push(start_infos, start_info);
	}

	return start_infos;
}

// DNM See https://ysflightsim.fandom.com/wiki/DynaModel_Files
struct Model {
	mn::Str file_abs_path;
	bool should_select_file;
	bool should_load_file;
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
		float speed;
		float throttle;
		bool landing_gear_is_out = true;
	} current_state;
};

void model_load_to_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes.values) {
		mesh_load_to_gpu(mesh);
	}
}

void model_unload_from_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes.values) {
		mesh_unload_from_gpu(mesh);
	}
}

void model_set_start(Model& self, StartInfo& start_info) {
	self.current_state.translation = start_info.position;
	self.current_state.rotation = start_info.attitude;
	self.current_state.landing_gear_is_out = start_info.landing_gear_is_out;
	self.current_state.throttle = start_info.throttle;
	self.current_state.speed = start_info.speed;
}

Mesh mesh_from_srf_str(Parser& parser, const mn::Str& name, size_t dnm_version = 1) {
	// aircraft/cessna172r.dnm has Surf instead of SURF (and .fld files use Surf)
	if (parser_accept(parser, "SURF\n") == false) {
		parser_expect(parser, "Surf\n");
	}

	Mesh mesh { .name=mn::str_clone(name) };

	// V {x} {y} {z}[ R]\n
	while (parser_accept(parser, "V ")) {
		glm::vec3 v {};

		v.x = parser_token_float(parser);
		parser_expect(parser, ' ');
		v.y = -parser_token_float(parser);
		parser_expect(parser, ' ');
		v.z = parser_token_float(parser);
		bool smooth_shading = parser_accept(parser, " R");

		// aircraft/cessna172r.dnm has spaces after end if V
		while (parser_accept(parser, " ")) {}

		parser_expect(parser, '\n');

		mn::buf_push(mesh.vertices, v);
		mn::buf_push(mesh.vertices_has_smooth_shading, smooth_shading);
	}
	if (mesh.vertices.count == 0) {
		mn::log_error("'{}': doesn't have any vertices!", name);
	}

	// <Face>+
	auto faces_unshaded_light_source = mn::buf_with_allocator<bool>(mn::memory::tmp());
	while (parser_accept(parser, "F\n")) {
		Face face {};
		bool parsed_color = false,
			parsed_normal = false,
			parsed_vertices = false,
			is_light_source = false;

		while (!parser_accept(parser, "E\n")) {
			if (parser_accept(parser, "C ")) {
				if (parsed_color) {
					mn::panic("'{}': found more than one color", name);
				}
				parsed_color = true;
				face.color.a = 1.0f;

				const auto num = parser_token_u64(parser);

				if (parser_accept(parser, ' ')) {
					face.color.r = num / 255.0f;
					face.color.g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					face.color.b = parser_token_u8(parser) / 255.0f;

					// aircraft/cessna172r.dnm allows alpha value in color
					// otherwise, maybe overwritten in ZA line
					if (parser_accept(parser, ' ')) {
						face.color.a = parser_token_u8(parser) / 255.0f;
					}
				} else {
					union {
						uint32_t as_long;
						struct { uint8_t r, b, g, padding; } as_struct;
					} packed_color;
					static_assert(sizeof(packed_color) == sizeof(uint32_t));
					packed_color.as_long = num;

					face.color.r = packed_color.as_struct.r / 255.0f;
					face.color.g = packed_color.as_struct.g / 255.0f;
					face.color.b = packed_color.as_struct.b / 255.0f;
					mn_assert(packed_color.as_struct.padding == 0);
				}

				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "N ")) {
				if (parsed_normal) {
					mn::panic("'{}': found more than one normal", name);
				}
				parsed_normal = true;

				face.center.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.center.y = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.center.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				face.normal.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.normal.y = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.normal.z = parser_token_float(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, 'V')) {
				// V {x}...
				auto polygon_vertices_ids = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
				while (parser_accept(parser, ' ')) {
					auto id = parser_token_u64(parser);
					if (id >= mesh.vertices.count) {
						mn::panic("'{}': id={} out of bounds={}", name, id, mesh.vertices.count);
					}
					mn::buf_push(polygon_vertices_ids, (uint32_t) id);
				}
				parser_expect(parser, '\n');

				if (parsed_vertices) {
					mn::log_error("'{}': found more than one vertices line, ignore others", name);
				} else {
					parsed_vertices = true;

					if (polygon_vertices_ids.count < 3) {
						mn::log_error("'{}': face has count of ids={}, it should be >= 3", name, polygon_vertices_ids.count);
					}

					face.vertices_ids = polygons_to_triangles(mesh.vertices, polygon_vertices_ids, face.center);
					if (face.vertices_ids.count % 3 != 0) {
						auto orig_vertices = mn::buf_with_allocator<glm::vec3>(mn::memory::tmp());
						for (auto id : polygon_vertices_ids) {
							mn::buf_push(orig_vertices, mesh.vertices[id]);
						}
						auto new_vertices = mn::buf_with_allocator<glm::vec3>(mn::memory::tmp());
						for (auto id : face.vertices_ids) {
							mn::buf_push(new_vertices, mesh.vertices[id]);
						}
						mn::log_error("{}:{}: num of vertices_ids must have been divisble by 3 to be triangles, but found {}, original vertices={}, new vertices={}", name, parser.curr_line+1,
							face.vertices_ids.count, orig_vertices, new_vertices);
					}
				}
			} else if (parser_accept(parser, "B\n")) {
				if (is_light_source) {
					mn::log_error("'{}': found more than 1 B for same face", name);
				}
				is_light_source = true;
			} else {
				parser_panic(parser, "'{}': unexpected line", name);
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
		mn::buf_push(mesh.faces, face);
	}

	size_t zl_count = 0;
	size_t zz_count = 0;
	while (true) {
		if (parser_accept(parser, '\n')) {
			// nothing
		} else if (parser_accept(parser, "GE") || parser_accept(parser, "ZE") || parser_accept(parser, "GL")) {
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "GF")) { // [GF< {u64}>+\n]+
			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.count) {
					mn::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.count);
				}
				mn::buf_push(mesh.gfs, id);
			}
			parser_expect(parser, '\n');
		} else if (parser_accept(parser, "ZA")) { // [ZA< {u64} {u8}>+\n]+
			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.count) {
					mn::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.count);
				}
				parser_expect(parser, ' ');
				mesh.faces[id].color.a = (255 - parser_token_u8(parser)) / 255.0f;
				// because alpha came as: 0 -> obaque, 255 -> clear
				// we revert it so it becomes: 1 -> obaque, 0 -> clear
			}
			parser_expect(parser, '\n');
		} else if (parser_accept(parser, "ZL")) { // [ZL< {u64}>+\n]
			zl_count++;
			if (dnm_version == 1) {
				if (zl_count > 1) {
					mn::panic("'{}': found {} > 1 ZLs", name, zl_count);
				}
			}

			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.count) {
					mn::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.count);
				}
				mn::buf_push(mesh.zls, id);
			}
			parser_expect(parser, '\n');
		} else if  (parser_accept(parser, "ZZ")) { // [ZZ< {u64}>+\n]
			zz_count++;
			if (zz_count > 1) {
				mn::panic("'{}': found {} > 1 ZZs", name, zz_count);
			}

			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.count) {
					mn::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.count);
				}
				mn::buf_push(mesh.zzs, id);
			}
			parser_expect(parser, '\n');
		} else {
			break;
		}
	}

	size_t unshaded_count = 0;
	for (auto unshaded : faces_unshaded_light_source) {
		if (unshaded) {
			unshaded_count++;
		}
	}
	mesh.is_light_source = (unshaded_count == faces_unshaded_light_source.count);

	return mesh;
}

Model model_from_dnm_file(const mn::Str& dnm_file_abs_path) {
	auto parser = parser_from_file(dnm_file_abs_path, mn::memory::tmp());

	parser_expect(parser, "DYNAMODEL\nDNMVER ");
	const uint8_t dnm_version = parser_token_u8(parser);
	if (dnm_version > 2) {
		mn::panic("unsupported version {}", dnm_version);
	}
	parser_expect(parser, '\n');

	auto meshes = mn::map_new<mn::Str, Mesh>();
	while (parser_accept(parser, "PCK ")) {
		auto name = parser_token_str(parser, mn::memory::tmp());
		parser_expect(parser, ' ');
		const auto pck_expected_no_lines = parser_token_u64(parser);
		parser_expect(parser, '\n');

		const auto pck_first_lineno = parser.curr_line;

		auto subparser = parser_fork(parser, pck_expected_no_lines);
		const auto mesh = mesh_from_srf_str(subparser, name);
		while (parser_accept(parser, "\n")) {}

		const auto current_lineno = parser.curr_line;
		const auto pck_found_linenos = current_lineno - pck_first_lineno - 1;
		if (pck_found_linenos != pck_expected_no_lines) {
			mn::log_error("'{}':{} expected {} lines in PCK, found {}", name, current_lineno, pck_expected_no_lines, pck_found_linenos);
		}

		mn::map_insert(meshes, name, mesh);
	}

	while (parser_accept(parser, "SRF ")) {
		auto name = parser_token_str(parser);
		if (!(mn::str_prefix(name, "\"") && mn::str_suffix(name, "\""))) {
			mn::panic("name must be in \"\" found={}", name);
		}
		mn::str_trim(name, "\"");
		parser_expect(parser, '\n');

		parser_expect(parser, "FIL ");
		auto fil = parser_token_str(parser, mn::memory::tmp());
		parser_expect(parser, '\n');
		auto surf = mn::map_lookup(meshes, fil);
		if (!surf) {
			mn::panic("'{}': line referenced undeclared surf={}", name, fil);
		}
		mn::str_free(surf->value.name);
		surf->value.name = name;

		parser_expect(parser, "CLA ");
		auto animation_type = parser_token_u8(parser);
		surf->value.animation_type = (AnimationClass) animation_type;
		parser_expect(parser, '\n');

		parser_expect(parser, "NST ");
		auto num_stas = parser_token_u64(parser);
		mn::buf_reserve(surf->value.animation_states, num_stas);
		parser_expect(parser, '\n');

		for (size_t i = 0; i < num_stas; i++) {
			parser_expect(parser, "STA ");

			MeshState sta {};
			sta.translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			sta.translation.y = -parser_token_float(parser);
			parser_expect(parser, ' ');
			sta.translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
			sta.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			sta.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			sta.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');

			uint8_t visible = parser_token_u8(parser);
			if (visible == 1 || visible == 0) {
				sta.visible = (visible == 1);
			} else {
				mn::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
			}
			parser_expect(parser, '\n');

			mn::buf_push(surf->value.animation_states, sta);
		}

		bool read_pos = false, read_cnt = false, read_rel_dep = false, read_nch = false;
		while (true) {
			if (parser_accept(parser, "POS ")) {
				read_pos = true;

				surf->value.initial_state.translation.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->value.initial_state.translation.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->value.initial_state.translation.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
				surf->value.initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf->value.initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf->value.initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;

				// aircraft/cessna172r.dnm is the only file with no visibility
				if (parser_accept(parser, ' ')) {
					uint8_t visible = parser_token_u8(parser);
					if (visible == 1 || visible == 0) {
						surf->value.initial_state.visible = (visible == 1);
					} else {
						mn::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
					}
				} else {
					surf->value.initial_state.visible = true;
				}

				parser_expect(parser, '\n');

				surf->value.current_state = surf->value.initial_state;
			} else if (parser_accept(parser, "CNT ")) {
				read_cnt = true;

				surf->value.cnt.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->value.cnt.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->value.cnt.z = parser_token_float(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "PAX")) {
				parser_skip_after(parser, '\n');
			} else if (parser_accept(parser, "REL DEP\n")) {
				read_rel_dep = true;
			} else if (parser_accept(parser, "NCH ")) {
				read_nch = true;

				const auto num_children = parser_token_u64(parser);
				parser_expect(parser, '\n');
				mn::buf_reserve(surf->value.children, num_children);

				for (size_t i = 0; i < num_children; i++) {
					parser_expect(parser, "CLD ");
					auto child_name = parser_token_str(parser);
					if (!(mn::str_prefix(child_name, "\"") && mn::str_suffix(child_name, "\""))) {
						mn::panic("'{}': child_name must be in \"\" found={}", name, child_name);
					}
					mn::str_trim(child_name, "\"");
					mn::buf_push(surf->value.children, child_name);
					parser_expect(parser, '\n');
				}
			} else {
				break;
			}
		}

		if (read_pos == false) {
			parser_panic(parser, "failed to find POS");
		}
		if (read_cnt == false) {
			parser_panic(parser, "failed to find CNT");
		}
		if (read_rel_dep == false) {
			// aircraft/cessna172r.dnm doesn't have REL DEP
			mn::log_error("'{}':{} failed to find REL DEP", name, parser.curr_line+1);
		}
		if (read_nch == false) {
			parser_panic(parser, "failed to find NCH");
		}

		// reinsert with name instead of FIL
		surf = mn::map_insert(meshes, mn::str_clone(name), surf->value);
		if (!mn::map_remove(meshes, fil)) {
			parser_panic(parser, "must be able to remove {} from meshes", name, fil);
		}

		parser_expect(parser, "END\n");
	}
	// aircraft/cessna172r.dnm doesn't have final END
	if (parser_finished(parser) == false) {
		parser_expect(parser, "END\n");
	}

	// check children exist
	for (const auto [_, srf] : meshes.values) {
		for (const auto child : srf.children) {
			auto srf2 = mn::map_lookup(meshes, child);
			if (srf2 == nullptr) {
				mn::panic("SURF {} contains child {} that doesn't exist", srf.name, child);
			} else if (srf2->value.name == srf.name) {
				mn::log_warning("SURF {} references itself", child);
			}
		}
	}

	auto model = Model {
		.file_abs_path = mn::str_clone(dnm_file_abs_path),
		.meshes = meshes,
		.initial_aabb = AABB {
			.min={+FLT_MAX, +FLT_MAX, +FLT_MAX},
			.max={-FLT_MAX, -FLT_MAX, -FLT_MAX},
		},
	};

	// top level nodes = nodes without parents
	auto surfs_wth_parents = mn::set_with_allocator<mn::Str>(mn::memory::tmp());
	for (const auto& [_, surf] : meshes.values) {
		for (const auto& child : surf.children) {
			mn::set_insert(surfs_wth_parents, child);
		}
	}
	for (size_t i = 0; i < meshes.values.count; i++) {
		if (mn::set_lookup(surfs_wth_parents, meshes.values[i].key) == nullptr) {
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

constexpr auto WND_TITLE        = "OpenYSF";
constexpr int  WND_INIT_WIDTH   = 1028;
constexpr int  WND_INIT_HEIGHT  = 680;
constexpr Uint32 WND_FLAGS      = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
constexpr float IMGUI_WNDS_BG_ALPHA = 0.8f;
constexpr glm::vec3 CORNFLOWER_BLU_COLOR {0.392f, 0.584f, 0.929f};

constexpr auto GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_CORE;
constexpr int  GL_CONTEXT_MAJOR = 3;
constexpr int  GL_CONTEXT_MINOR = 3;
constexpr auto GL_DOUBLE_BUFFER = SDL_TRUE;

struct Camera {
	struct {
		bool identity           = false;
		float movement_speed    = 1000.0f;
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
		float near         = 0.1f;
		float far          = 100000;
		float fovy         = 45.0f / DEGREES_MAX * RADIANS_MAX;
		float aspect       = (float) WND_INIT_WIDTH / WND_INIT_HEIGHT;
		bool custom_aspect = false;
	} projection;
};

glm::mat4 camera_get_view_matrix(const Camera& self) {
	if (self.view.identity) {
		return glm::identity<glm::mat4>();
	}
	return glm::lookAt(self.view.pos, self.view.pos + self.view.front, self.view.up);
}

glm::mat4 camera_get_projection_matrix(const Camera& self) {
	return glm::perspective(
		self.projection.fovy,
		self.projection.aspect,
		self.projection.near,
		self.projection.far
	);
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

struct Block {
	enum { RIGHT=0, LEFT } orientation;
	glm::vec4 faces_color[2];
};

struct TerrMesh {
	mn::Str name, tag;
	FieldID id;

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
		bool visible = true;
	} current_state, initial_state;
};

void terr_mesh_free(TerrMesh& self) {
	mn::str_free(self.name);
	mn::str_free(self.tag);
	mn::destruct(self.nodes_height);
	mn::destruct(self.blocks);
}

void destruct(TerrMesh& self) {
	terr_mesh_free(self);
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

	gpu_check_errors();
}

void terr_mesh_unload_from_gpu(TerrMesh& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};
}

struct Primitive2D {
	enum class Kind {
		POINTS,                // PST
		LINES,                 // LSQ
		LINE_SEGMENTS,         // PLL
		TRIANGLES,             // TRI
		QUAD_STRIPS,           // QST
		GRADATION_QUAD_STRIPS, // GQS
		QUADRILATERAL,         // QDR
		POLYGON,               // PLG
	} kind;

	glm::vec3 color;
	glm::vec3 color2; // only for kind=GRADATION_QUAD_STRIPS

	// (X,Z), y=0
	mn::Buf<glm::vec2> vertices;

	struct {
		GLuint vao, vbo;

		// possible values?
		// GL_POINTS
		// GL_LINES
		// GL_LINE_LOOP
		// GL_LINE_STRIP
		// GL_TRIANGLES
		// GL_TRIANGLE_STRIP
		// GL_TRIANGLE_FAN
		GLenum primitive_type;

		size_t array_count;
	} gpu;
};

void prmitive2d_free(Primitive2D& self) {
	mn::buf_free(self.vertices);
}

void primitive2d_load_to_gpu(Primitive2D& self) {
	auto vertices = mn::buf_with_allocator<glm::vec2>(mn::memory::tmp());
	switch (self.kind) {
	case Primitive2D::Kind::POINTS:
		self.gpu.primitive_type = GL_POINTS;
		vertices = self.vertices;
		break;
	case Primitive2D::Kind::LINES:
		self.gpu.primitive_type = GL_LINES;
		vertices = self.vertices;
		break;
	case Primitive2D::Kind::LINE_SEGMENTS:
		self.gpu.primitive_type = GL_LINE_STRIP;
		vertices = self.vertices;
		break;
	case Primitive2D::Kind::TRIANGLES:
		self.gpu.primitive_type = GL_TRIANGLES;
		vertices = self.vertices;
		break;
	case Primitive2D::Kind::QUADRILATERAL:
		self.gpu.primitive_type = GL_TRIANGLES;
		for (int i = 0; i < (int)self.vertices.count - 3; i += 4) {
			mn::buf_push(vertices, self.vertices[i]);
			mn::buf_push(vertices, self.vertices[i+3]);
			mn::buf_push(vertices, self.vertices[i+2]);

			mn::buf_push(vertices, self.vertices[i]);
			mn::buf_push(vertices, self.vertices[i+2]);
			mn::buf_push(vertices, self.vertices[i+1]);
		}
		break;
	case Primitive2D::Kind::GRADATION_QUAD_STRIPS: // same as QUAD_STRIPS but with extra color
	case Primitive2D::Kind::QUAD_STRIPS:
		self.gpu.primitive_type = GL_TRIANGLES;
		for (int i = 0; i < (int)self.vertices.count - 2; i += 2) {
			mn::buf_push(vertices, self.vertices[i]);
			mn::buf_push(vertices, self.vertices[i+1]);
			mn::buf_push(vertices, self.vertices[i+3]);

			mn::buf_push(vertices, self.vertices[i]);
			mn::buf_push(vertices, self.vertices[i+2]);
			mn::buf_push(vertices, self.vertices[i+3]);
		}
		break;
	case Primitive2D::Kind::POLYGON:
	{
		self.gpu.primitive_type = GL_TRIANGLES;
		auto indices = polygons2d_to_triangles(self.vertices, mn::memory::tmp());
		for (auto& index : indices) {
			mn::buf_push(vertices, self.vertices[index]);
		}
		break;
	}
	default: mn_unreachable();
	}
	self.gpu.array_count = vertices.count;

	// load vertices to gpu
	glGenVertexArrays(1, &self.gpu.vao);
	glBindVertexArray(self.gpu.vao);
		glGenBuffers(1, &self.gpu.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.gpu.vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.count * sizeof(glm::vec2), vertices.ptr, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,                 /*index*/
			2,                 /*#components*/
			GL_FLOAT,          /*type*/
			GL_FALSE,          /*normalize*/
			sizeof(glm::vec2), /*stride bytes*/
			(void*)0           /*offset*/
		);
	glBindVertexArray(0);

	gpu_check_errors();
}

void prmitive2d_unload_from_gpu(Primitive2D& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};
}

void destruct(Primitive2D& self) {
	prmitive2d_free(self);
}

struct Picture2D {
	mn::Str name;
	FieldID id;

	mn::Buf<Primitive2D> primitives;
	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		glm::vec3 scale;
		bool visible = true;
	} current_state, initial_state;
};

void picture2d_free(Picture2D& self) {
	mn::str_free(self.name);
	mn::destruct(self.primitives);
}

void destruct(Picture2D& self) {
	picture2d_free(self);
}

void picture2d_load_to_gpu(Picture2D& self) {
	for (auto& primitive : self.primitives) {
		primitive2d_load_to_gpu(primitive);
	}
}

void picture2d_unload_from_gpu(Picture2D& self) {
	for (auto& primitive : self.primitives) {
		prmitive2d_unload_from_gpu(primitive);
	}
}

enum class AreaKind {
	NOAREA=0,
	LAND,
	WATER,
};

namespace fmt {
	template<>
	struct formatter<AreaKind> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const AreaKind &v, FormatContext &ctx) {
			switch (v) {
			case AreaKind::NOAREA: return format_to(ctx.out(), "AreaKind::NOAREA");
			case AreaKind::LAND:   return format_to(ctx.out(), "AreaKind::LAND");
			case AreaKind::WATER:  return format_to(ctx.out(), "AreaKind::WATER");
			default: mn_unreachable();
			}
			return format_to(ctx.out(), "????????");
		}
	};
}

// runway or viewpoint
struct FieldRegion {
	// (X,Z) y=0
	glm::vec2 min, max;
	glm::mat4 transformation;
	FieldID id;
	mn::Str tag;
};

void region_free(FieldRegion& self) {
	mn::str_free(self.tag);
}

void destruct(FieldRegion& self) {
	region_free(self);
}

struct Field {
	mn::Str name;
	FieldID id;

	AreaKind default_area;
	glm::vec3 ground_color, sky_color;
	bool ground_specular; // ????

	mn::Buf<TerrMesh> terr_meshes;
	mn::Buf<Picture2D> pictures;
	mn::Buf<FieldRegion> regions;
	mn::Buf<Field> subfields;
	mn::Buf<Mesh> meshes;

	mn::Str file_abs_path;
	bool should_select_file, should_load_file;

	glm::mat4 transformation;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		bool visible = true;
	} current_state, initial_state;
};

void field_free(Field& self) {
	mn::str_free(self.name);
	mn::destruct(self.terr_meshes);
	mn::destruct(self.pictures);
	mn::destruct(self.regions);
	mn::destruct(self.subfields);
	mn::destruct(self.meshes);
	mn::str_free(self.file_abs_path);
}

void destruct(Field& self) {
	field_free(self);
}

Field _field_from_fld_str(Parser& parser) {
	parser_expect(parser, "FIELD\n");

	Field field {};

	while (true) {
		if (parser_accept(parser, "FLDVERSION ")) {
			// TODO
			mn::log_warning("{}: found FLDVERSION, doesn't support it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "FLDNAME ")) {
			mn::log_warning("{}: found FLDNAME, doesn't support it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "TEXMAN")) {
			// TODO
			mn::log_warning("{}: found TEXMAN, doesn't support it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "TEXMAN ENDTEXTURE\n");
		} else {
			break;
		}
	}

	parser_expect(parser, "GND ");
	field.ground_color.r = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, ' ');
	field.ground_color.g = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, ' ');
	field.ground_color.b = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, '\n');

	parser_expect(parser, "SKY ");
	field.sky_color.r = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, ' ');
	field.sky_color.g = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, ' ');
	field.sky_color.b = parser_token_u8(parser) / 255.0f;
	parser_expect(parser, '\n');

	if (parser_accept(parser, "GNDSPECULAR ")) {
		field.ground_specular = _token_bool(parser);
		parser_expect(parser, '\n');
	}

	field.default_area = AreaKind::NOAREA;
	if (parser_accept(parser, "DEFAREA ")) {
		const auto default_area_str = parser_token_str(parser, mn::memory::tmp());
		parser_expect(parser, '\n');
		if (default_area_str == "NOAREA") {
			field.default_area = AreaKind::NOAREA;
		} else if (default_area_str == "LAND") {
			field.default_area = AreaKind::LAND;
		} else if (default_area_str == "WATER") {
			field.default_area = AreaKind::WATER;
		} else {
			parser_panic(parser, "unrecognized area '{}'", default_area_str);
		}
	}

	if (parser_accept(parser, "BASEELV ")) {
		// TODO
		mn::log_warning("{}: found BASEELV, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, '\n');
	}

	if (parser_accept(parser, "MAGVAR ")) {
		// TODO
		mn::log_warning("{}: found MAGVAR, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, '\n');
	}

	if (parser_accept(parser, "CANRESUME TRUE\n") || parser_accept(parser, "CANRESUME FALSE\n")) {
		// TODO
		mn::log_warning("{}: found CANRESUME, doesn't understand it, skip for now", parser.curr_line+1);
	}

	while (parser_accept(parser, "AIRROUTE\n")) {
		// TODO
		mn::log_warning("{}: found AIRROUTE, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, "ENDAIRROUTE\n");
	}

	while (parser_accept(parser, "PCK ")) {
		auto name = parser_token_str(parser);
		mn::str_trim(name, "\"");
		parser_expect(parser, ' ');

		const auto total_lines_count = parser_token_u64(parser);
		parser_expect(parser, '\n');

		const size_t first_line_no = parser.curr_line+1;
		if (parser_peek(parser, "FIELD\n")) {
			auto subparser = parser_fork(parser, total_lines_count);
			auto subfield = _field_from_fld_str(subparser);
			subfield.name = name;

			mn::buf_push(field.subfields, subfield);
		} else if (parser_accept(parser, "TerrMesh\n")) {
			TerrMesh terr_mesh { .name=name };

			if (parser_accept(parser, "SPEC TRUE\n") || parser_accept(parser, "SPEC FALSE\n")) {
				// TODO
				mn::log_warning("{}: found SPEC, doesn't understand it, skip for now", parser.curr_line+1);
			}

			if (parser_accept(parser, "TEX MAIN")) {
				// TODO
				mn::log_warning("{}: found TEX MAIN, doesn't understand it, skip for now", parser.curr_line+1);
				parser_skip_after(parser, "\n");
			}

			parser_expect(parser, "NBL ");
			const auto num_blocks_x = parser_token_u64(parser);
			parser_expect(parser, ' ');
			const auto num_blocks_z = parser_token_u64(parser);
			parser_expect(parser, '\n');

			parser_expect(parser, "TMS ");
			// TODO: multiply by 10?
			terr_mesh.initial_state.scale = {1, 1, 1};
			terr_mesh.initial_state.scale.x = parser_token_float(parser); //* 10.0f;
			parser_expect(parser, ' ');
			terr_mesh.initial_state.scale.z = parser_token_float(parser); //* 10.0f;
			parser_expect(parser, '\n');

			terr_mesh.current_state = terr_mesh.initial_state;

			if (parser_accept(parser, "CBE ")) {
				terr_mesh.gradiant.enabled = true;

				terr_mesh.gradiant.top_y = parser_token_float(parser);
				parser_expect(parser, ' ');
				terr_mesh.gradiant.bottom_y = -parser_token_float(parser);
				parser_expect(parser, ' ');

				terr_mesh.gradiant.top_color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradiant.top_color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradiant.top_color.b = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');

				terr_mesh.gradiant.bottom_color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradiant.bottom_color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradiant.bottom_color.b = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, '\n');
			}

			// NOTE: assumed order in file
			for (auto [side_str, side] : {
				std::pair{"BOT ", &terr_mesh.bottom_side_color},
				std::pair{"RIG ", &terr_mesh.right_side_color},
				std::pair{"TOP ", &terr_mesh.top_side_color},
				std::pair{"LEF ", &terr_mesh.left_side_color},
			}) {
				if (parser_accept(parser, side_str)) {
					side->a = 1;
					side->r = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					side->g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					side->b = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, '\n');
				}
			}

			// create blocks
			terr_mesh.blocks = mn::buf_with_count<mn::Buf<Block>>(num_blocks_z);
			for (auto& row : terr_mesh.blocks) {
				row = mn::buf_with_count<Block>(num_blocks_x);
			}

			// create nodes
			terr_mesh.nodes_height = mn::buf_with_count<mn::Buf<float>>(num_blocks_z+1);
			for (auto& row : terr_mesh.nodes_height) {
				row = mn::buf_with_count<float>(num_blocks_x+1);
			}

			// parse blocks and nodes
			for (size_t z = 0; z < terr_mesh.nodes_height.count; z++) {
				for (size_t x = 0; x < terr_mesh.nodes_height[z].count; x++) {
					parser_expect(parser, "BLO ");
					terr_mesh.nodes_height[z][x] = parser_token_float(parser);

					// don't read rest of block if node is on edge/wedge
					if (z == terr_mesh.nodes_height.count-1 || x == terr_mesh.nodes_height[z].count-1) {
						parser_skip_after(parser, '\n');
						continue;
					}

					// from here the node has a block
					if (parser_accept(parser, '\n')) {
						continue;
					} else if (parser_accept(parser, " R ")) {
						terr_mesh.blocks[z][x].orientation = Block::RIGHT;
					} else if (parser_accept(parser, " L ")) {
						terr_mesh.blocks[z][x].orientation = Block::LEFT;
					} else {
						parser_panic(parser, "expected either a new line or L or R");
					}

					// face 0
					if (parser_accept(parser, "OFF ") || parser_accept(parser, "0 ")) {
						terr_mesh.blocks[z][x].faces_color[0].a = 0;
					} else if (parser_accept(parser, "ON ") || parser_accept(parser, "1 ")) {
						terr_mesh.blocks[z][x].faces_color[0].a = 1;
					} else {
						parser_skip_after(parser, ' ');
						terr_mesh.blocks[z][x].faces_color[0].a = 1;
					}

					terr_mesh.blocks[z][x].faces_color[0].r = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					terr_mesh.blocks[z][x].faces_color[0].g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					terr_mesh.blocks[z][x].faces_color[0].b = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');

					// face 1
					if (parser_accept(parser, "OFF ") || parser_accept(parser, "0 ")) {
						terr_mesh.blocks[z][x].faces_color[1].a = 0;
					} else if (parser_accept(parser, "ON ") || parser_accept(parser, "1 ")) {
						terr_mesh.blocks[z][x].faces_color[1].a = 1;
					} else {
						parser_skip_after(parser, ' ');
						terr_mesh.blocks[z][x].faces_color[1].a = 1;
					}

					terr_mesh.blocks[z][x].faces_color[1].r = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					terr_mesh.blocks[z][x].faces_color[1].g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					terr_mesh.blocks[z][x].faces_color[1].b = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, '\n');
				}
			}

			parser_expect(parser, "END\n");

			mn::buf_push(field.terr_meshes, terr_mesh);
		} else if (parser_accept(parser, "Pict2\n")) {
			Picture2D picture { .name=name };

			picture.initial_state.scale = {1,1,1};
			picture.current_state = picture.initial_state;

			while (parser_accept(parser, "ENDPICT\n") == false) {
				Primitive2D permitive {};

				auto kind_str = parser_token_str(parser, mn::memory::tmp());
				parser_expect(parser, '\n');

				if (kind_str == "LSQ") {
					permitive.kind = Primitive2D::Kind::LINES;
				} else if (kind_str == "PLG") {
					permitive.kind = Primitive2D::Kind::POLYGON;
				} else if (kind_str == "PLL") {
					permitive.kind = Primitive2D::Kind::LINE_SEGMENTS;
				} else if (kind_str == "PST") {
					permitive.kind = Primitive2D::Kind::POINTS;
				} else if (kind_str == "QDR") {
					permitive.kind = Primitive2D::Kind::QUADRILATERAL;
				} else if (kind_str == "GQS") {
					permitive.kind = Primitive2D::Kind::GRADATION_QUAD_STRIPS;
				} else if (kind_str == "QST") {
					permitive.kind = Primitive2D::Kind::QUAD_STRIPS;
				} else if (kind_str == "TRI") {
					permitive.kind = Primitive2D::Kind::TRIANGLES;
				} else {
					mn::log_warning("{}: invalid pict2 kind={}, skip for now", parser.curr_line+1, kind_str);
					parser_skip_after(parser, "ENDO\n");
					continue;
				}

				if (parser_accept(parser, "DST ")) {
					// TODO
					mn::log_warning("{}: found DST, doesn't understand it, skip for now", parser.curr_line+1);
					parser_skip_after(parser, '\n');
				}

				parser_expect(parser, "COL ");
				permitive.color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				permitive.color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				permitive.color.b = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, '\n');

				if (permitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS) {
					parser_expect(parser, "CL2 ");
					permitive.color2.r = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					permitive.color2.g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					permitive.color2.b = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, '\n');
				}

				while (parser_accept(parser, "ENDO\n") == false) {
					if (parser_accept(parser, "SPEC TRUE\n") || parser_accept(parser, "SPEC FALSE\n")) {
						// TODO
						mn::log_warning("{}: found SPEC, doesn't understand it, skip for now", parser.curr_line+1);
						continue;
					}

					if (parser_accept(parser, "TXL")) {
						// TODO
						mn::log_warning("{}: found TXL, doesn't understand it, skip for now", parser.curr_line+1);
						parser_skip_after(parser, '\n');
						while (parser_accept(parser, "TXC")) {
							parser_skip_after(parser, '\n');
						}
						continue;
					}

					glm::vec2 vertex {};
					parser_expect(parser, "VER ");
					vertex.x = parser_token_float(parser);
					parser_expect(parser, ' ');
					vertex.y = parser_token_float(parser);
					parser_expect(parser, '\n');

					mn::buf_push(permitive.vertices, vertex);
				}

				if (permitive.vertices.count == 0) {
					parser_panic(parser, "{}: no vertices", parser.curr_line+1);
				} else if (permitive.kind == Primitive2D::Kind::TRIANGLES && permitive.vertices.count % 3 != 0) {
					parser_panic(parser, "{}: kind is triangle but num of vertices ({}) isn't divisible by 3", parser.curr_line+1, permitive.vertices.count);
				} else if (permitive.kind == Primitive2D::Kind::LINES && permitive.vertices.count % 2 != 0) {
					mn::log_error("{}: kind is line but num of vertices ({}) isn't divisible by 2, ignoring last vertex", parser.curr_line+1, permitive.vertices.count);
					mn::buf_pop(permitive.vertices);
				} else if (permitive.kind == Primitive2D::Kind::LINE_SEGMENTS && permitive.vertices.count == 1) {
					parser_panic(parser, "{}: kind is line but has one point", parser.curr_line+1);
				} else if (permitive.kind == Primitive2D::Kind::QUADRILATERAL && permitive.vertices.count % 4 != 0) {
					parser_panic(parser, "{}: kind is quadrilateral but num of vertices ({}) isn't divisible by 4", parser.curr_line+1, permitive.vertices.count);
				} else if (permitive.kind == Primitive2D::Kind::QUAD_STRIPS && (permitive.vertices.count >= 4 && permitive.vertices.count % 2 == 0) == false) {
					parser_panic(parser, "{}: kind is quad_strip but num of vertices ({}) isn't in (4,6,8,10,...)", parser.curr_line+1, permitive.vertices.count);
				}

				mn::buf_push(picture.primitives, permitive);
			}

			mn::buf_push(field.pictures, picture);
		} else if (parser_peek(parser, "Surf\n")) {
			auto subparser = parser_fork(parser, total_lines_count);
			auto mesh = mesh_from_srf_str(subparser, name);
			mn::str_free(name);

			mn::buf_push(field.meshes, mesh);
		} else {
			parser_panic(parser, "{}: invalid type '{}'", parser.curr_line+1, parser_token_str(parser, mn::memory::tmp()));
		}

		const size_t last_line_no = parser.curr_line+1;
		const size_t curr_lines_count = last_line_no - first_line_no;
		if (curr_lines_count != total_lines_count) {
			mn::log_error("{}: expected {} lines, found {}", last_line_no, total_lines_count, curr_lines_count);
		}

		parser_expect(parser, "\n\n");

		// aomori.fld contains more than 2 empty lines
		while (parser_accept(parser, '\n')) {}
	}

	while (parser_finished(parser) == false) {
		if (parser_accept(parser, "FLD\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mn::memory::tmp());
			mn::str_trim(name, "\"");
			parser_expect(parser, '\n');

			Field* subfield = nullptr;
			for (auto& sf : field.subfields) {
				if (sf.name == name) {
					subfield = &sf;
					break;
				}
			}
			if (subfield == nullptr) {
				parser_panic(parser, "{}: didn't find FLD with name='{}'", parser.curr_line+1, name);
			}

			parser_expect(parser, "POS ");
			subfield->initial_state.translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			subfield->initial_state.translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			subfield->initial_state.translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			subfield->initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			subfield->initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			subfield->initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');
			subfield->current_state = subfield->initial_state;

			parser_expect(parser, "ID ");
			subfield->id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, "\nEND\n");
		} else if (parser_accept(parser, "TER\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mn::memory::tmp());
			mn::str_trim(name, "\"");
			parser_expect(parser, '\n');

			TerrMesh* terr_mesh = nullptr;
			for (auto& terr : field.terr_meshes) {
				if (terr.name == name) {
					terr_mesh = &terr;
					break;
				}
			}
			if (terr_mesh == nullptr) {
				parser_panic(parser, "{}: didn't find TER with name='{}'", parser.curr_line+1, name);
			}

			parser_expect(parser, "POS ");
			terr_mesh->initial_state.translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			terr_mesh->initial_state.translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			terr_mesh->initial_state.translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			terr_mesh->initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			terr_mesh->initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			terr_mesh->initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');
			terr_mesh->current_state = terr_mesh->initial_state;

			parser_expect(parser, "ID ");
			terr_mesh->id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, '\n');

			if (parser_accept(parser, "TAG ")) {
				terr_mesh->tag = parser_token_str(parser);
				mn::str_trim(terr_mesh->tag, "\"");
				parser_expect(parser, '\n');
			}

			parser_expect(parser, "END\n");
		} else if (parser_accept(parser, "PC2\n") || parser_accept(parser, "PLT\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mn::memory::tmp());
			mn::str_trim(name, "\"");
			parser_expect(parser, '\n');

			Picture2D* picture = nullptr;
			for (auto& pict : field.pictures) {
				if (pict.name == name) {
					picture = &pict;
					break;
				}
			}
			if (picture == nullptr) {
				parser_panic(parser, "{}: didn't find TER with name='{}'", parser.curr_line+1, name);
			}

			parser_expect(parser, "POS ");
			picture->initial_state.translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			picture->initial_state.translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			picture->initial_state.translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			picture->initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			picture->initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			picture->initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');
			picture->current_state = picture->initial_state;

			parser_expect(parser, "ID ");
			picture->id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, "\nEND\n");
		} else if (parser_accept(parser, "RGN\n")) {
			FieldRegion region {};

			parser_expect(parser, "ARE ");
			while (parser_accept(parser, ' ')) {}
			region.min.x = parser_token_float(parser);
			while (parser_accept(parser, ' ')) {}
			region.min.y = parser_token_float(parser);
			while (parser_accept(parser, ' ')) {}
			region.max.x = parser_token_float(parser);
			while (parser_accept(parser, ' ')) {}
			region.max.y = parser_token_float(parser);
			parser_expect(parser, '\n');

			if (parser_accept(parser, "SUB DEADLOCKFREEAP\n")) {
				// TODO
				mn::log_warning("{}: found SUB DEADLOCKFREEAP, doesn't understand it, skip for now", parser.curr_line+1);
			}

			parser_expect(parser, "POS ");
			glm::vec3 translation {};
			translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			glm::vec3 rotation {};
			rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');

			region.transformation = glm::identity<glm::mat4>();
			region.transformation = glm::translate(region.transformation, translation);
			region.transformation = glm::rotate(region.transformation, rotation[2], glm::vec3{0, 0, 1});
			region.transformation = glm::rotate(region.transformation, rotation[1], glm::vec3{1, 0, 0});
			region.transformation = glm::rotate(region.transformation, rotation[0], glm::vec3{0, 1, 0});

			parser_expect(parser, "ID ");
			region.id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, '\n');

			if (parser_accept(parser, "TAG ")) {
				region.tag = parser_token_str(parser);
				mn::str_trim(region.tag, "\"");
				parser_expect(parser, '\n');
			}

			parser_expect(parser, "END\n");

			mn::buf_push(field.regions, region);
		} else if (parser_accept(parser, "PST\n")) {
			// TODO
			mn::log_warning("{}: found PST, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "GOB\n")) {
			// TODO
			mn::log_warning("{}: found GOB, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "AOB\n")) {
			// TODO
			mn::log_warning("{}: found AOB, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "SRF\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mn::memory::tmp());
			mn::str_trim(name, "\"");
			parser_expect(parser, '\n');

			Mesh* mesh = nullptr;
			for (auto& m : field.meshes) {
				if (m.name == name) {
					mesh = &m;
					break;
				}
			}
			if (mesh == nullptr) {
				parser_panic(parser, "{}: didn't find TER with name='{}'", parser.curr_line+1, name);
			}

			parser_expect(parser, "POS ");
			mesh->initial_state.translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			mesh->initial_state.translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			mesh->initial_state.translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			mesh->initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			mesh->initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			mesh->initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');
			mesh->initial_state.visible = true;
			mesh->current_state = mesh->initial_state;

			parser_expect(parser, "ID ");
			mesh->id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, '\n');

			parser_expect(parser, "END\n");
		} else if (parser_accept(parser, '\n')) {
			// aomori.fld adds extra spaces
		} else {
			parser_panic(parser, "{}: found invalid type = '{}'", parser.curr_line+1, parser_token_str(parser, mn::memory::tmp()));
		}
	}

	return field;
}

Field field_from_fld_file(const mn::Str& fld_file_abs_path) {
	auto parser = parser_from_file(fld_file_abs_path, mn::memory::tmp());

	auto field = _field_from_fld_str(parser);
	field.file_abs_path = mn::str_clone(fld_file_abs_path);
	if (field.name.count == 0) {
		field.name = mn::file_name(fld_file_abs_path);
	}

	return field;
}

void field_load_to_gpu(Field& self) {
	for (auto& terr_mesh : self.terr_meshes) {
		terr_mesh_load_to_gpu(terr_mesh);
	}
	for (auto& pict : self.pictures) {
		picture2d_load_to_gpu(pict);
	}
	for (auto& subfield : self.subfields) {
		field_load_to_gpu(subfield);
	}
	for (auto& mesh : self.meshes) {
		mesh_load_to_gpu(mesh);
	}
}

void field_unload_from_gpu(Field& self) {
	for (auto& subfield : self.subfields) {
		field_unload_from_gpu(subfield);
	}
	for (auto& terr_mesh : self.terr_meshes) {
		terr_mesh_unload_from_gpu(terr_mesh);
	}
	for (auto& pict : self.pictures) {
		picture2d_unload_from_gpu(pict);
	}
	for (auto& mesh : self.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

int main() {
	test_parser();
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
	auto _imgui_ini_file_path = mn::strf("{}/{}", mn::folder_config(mn::memory::tmp()), "open-ysf-imgui.ini");
	mn_defer(mn::str_free(_imgui_ini_file_path));

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

	ImGui::GetIO().IniFilename = _imgui_ini_file_path.ptr;

	auto meshes_gpu_program = gpu_program_new(
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
	mn_defer(gpu_program_free(meshes_gpu_program));

	// models
	constexpr int NUM_MODELS = 2;
	Model models[NUM_MODELS];
	for (auto& model : models) {
		model = model_from_dnm_file(mn::str_lit(ASSETS_DIR "/aircraft/ys11.dnm"));
		model_load_to_gpu(model);
	}
	mn_defer({
		for (auto& model : models) {
			model_unload_from_gpu(model);
			model_free(model);
		}
	});

	// field
	auto field = field_from_fld_file(mn::str_lit(ASSETS_DIR "/scenery/small.fld"));
	mn_defer(field_free(field));
	field_load_to_gpu(field);
	mn_defer(field_unload_from_gpu(field));

	// start infos
	auto start_infos = start_info_from_stp_file(mn::str_lit(ASSETS_DIR "/scenery/small.stp"));
	mn_defer(mn::destruct(start_infos));
	mn::buf_insert(start_infos, 0, StartInfo {
		.name=mn::str_from_c("-NULL-")
	});

	for (int i = 0; i < NUM_MODELS; i++) {
		model_set_start(models[i], start_infos[mod(i, start_infos.count)]);
	}

	Camera camera { .view { .pos = start_infos[1].position } };

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

	const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = gpu_get_float(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
	const GLfloat POINT_SIZE_GRANULARITY        = gpu_get_float(GL_POINT_SIZE_GRANULARITY);

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

		gpu_check_errors();
	}

	auto lines_gpu_program = gpu_program_new(
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
	mn_defer(gpu_program_free(lines_gpu_program));

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

		gpu_check_errors();
	}

	auto primitives2d_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec2 attr_position;

			uniform mat4 model_view_projection;
			uniform bool gradation_enabled;

			out float vs_color_index;

			void main() {
				gl_Position = model_view_projection * vec4(attr_position.x, 0.0, attr_position.y, 1.0);

				if (gradation_enabled) {
					switch (gl_VertexID % 6) {
					case 0: vs_color_index = 0; break;
					case 1: vs_color_index = 1; break;
					case 2: vs_color_index = 1; break;
					case 3: vs_color_index = 0; break;
					case 4: vs_color_index = 0; break;
					case 5: vs_color_index = 1; break;
					}
				}
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core

			in float vs_color_index;

			uniform vec3 primitive_color, primitive_color2;
			uniform bool gradation_enabled;

			out vec4 out_fragcolor;

			void main() {
				if (gradation_enabled) {
					out_fragcolor = vec4(mix(primitive_color, primitive_color2, vs_color_index), 1.0);
				} else {
					out_fragcolor = vec4(primitive_color, 1.0);
				}
			}
		)GLSL"
	);
	mn_defer(gpu_program_free(primitives2d_gpu_program));

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

			if (!camera.projection.custom_aspect) {
				camera.projection.aspect = (float) w / h;
			}
		}

		if (field.should_select_file) {
			field.should_select_file = false;

			auto result = pfd::open_file("Select FLD", "", {"FLD Files", "*.fld", "All Files", "*"}).result();
			if (result.size() == 1) {
				mn::str_free(field.file_abs_path);
				mn::str_push(field.file_abs_path, result[0].c_str());
				mn::log_debug("loading '{}'", field.file_abs_path);
				field.should_load_file = true;
			}
		}
		if (field.should_load_file) {
			field.should_load_file = false;

			Field new_field {};
			if (field.file_abs_path.count > 0) {
				new_field = field_from_fld_file(field.file_abs_path);
				field_load_to_gpu(new_field);
			}

			field_unload_from_gpu(field);
			field_free(field);
			field = new_field;
		}

		for (int i = 0; i < NUM_MODELS; i++) {
			if (models[i].should_select_file) {
				models[i].should_select_file = false;

				auto result = pfd::open_file("Select DNM", "", {"DNM Files", "*.dnm", "All Files", "*"}).result();
				if (result.size() == 1) {
					mn::str_free(models[i].file_abs_path);
					mn::str_push(models[i].file_abs_path, result[0].c_str());
					mn::log_debug("loading '{}'", models[i].file_abs_path);
					models[i].should_load_file = true;
				}
			}

			if (models[i].should_load_file) {
				auto model = model_from_dnm_file(models[i].file_abs_path);
				model_load_to_gpu(model);

				model_unload_from_gpu(models[i]);
				model_free(models[i]);
				models[i] = model;

				mn::log_debug("loaded '{}'", models[i].file_abs_path);
				models[i].should_load_file = false;
			}
		}

		// test intersection
		struct Box { glm::vec3 translation, scale, color; };
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
					auto aabb = models[j].current_aabb;
					mn::buf_push(box_instances, Box {
						.translation = aabb.min,
						.scale = aabb.max - aabb.min,
						.color = j_color,
					});
				}
			}

			if (models[i].render_aabb) {
				auto aabb = models[i].current_aabb;
				mn::buf_push(box_instances, Box {
					.translation = aabb.min,
					.scale = aabb.max - aabb.min,
					.color = i_color,
				});
			}
		}

		auto axis_instances = mn::buf_with_allocator<glm::mat4>(mn::memory::tmp());

		glEnable(GL_DEPTH_TEST);
		glClearDepth(1);
		glClearColor(field.sky_color.x, field.sky_color.y, field.sky_color.z, 0.0f);
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

		glUseProgram(meshes_gpu_program.handle);
		glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "view"), 1, rendering.transpose_view, glm::value_ptr(camera_get_view_matrix(camera)));
		glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "projection"), 1, rendering.transpose_projection, glm::value_ptr(camera_get_projection_matrix(camera)));

		// render 2d pictures
		auto fields_to_render = mn::buf_with_allocator<Field*>(mn::memory::tmp());
		field.transformation = glm::identity<glm::mat4>();
		mn::buf_push(fields_to_render, &field);
		glDisable(GL_DEPTH_TEST);
		while (fields_to_render.count > 0) {
			Field* field_to_render = mn::buf_top(fields_to_render);
			mn::buf_pop(fields_to_render);

			if (field_to_render->current_state.visible == false) {
				continue;
			}

			field_to_render->transformation = glm::translate(field_to_render->transformation, field_to_render->current_state.translation);
			field_to_render->transformation = glm::rotate(field_to_render->transformation, field_to_render->current_state.rotation[2], glm::vec3{0, 0, 1});
			field_to_render->transformation = glm::rotate(field_to_render->transformation, field_to_render->current_state.rotation[1], glm::vec3{1, 0, 0});
			field_to_render->transformation = glm::rotate(field_to_render->transformation, field_to_render->current_state.rotation[0], glm::vec3{0, 1, 0});

			for (int i = (int)field_to_render->subfields.count-1; i >= 0; i--) {
				field_to_render->subfields[i].transformation = field_to_render->transformation;
				mn::buf_push(fields_to_render, &field_to_render->subfields[i]);
			}

			glUseProgram(primitives2d_gpu_program.handle);
			const auto projection_view = camera_get_projection_matrix(camera) * camera_get_view_matrix(camera);
			for (auto& picture : field_to_render->pictures) {
				if (picture.current_state.visible == false) {
					continue;
				}

				auto model_transformation = field_to_render->transformation;
				model_transformation = glm::translate(model_transformation, picture.current_state.translation);
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[0], glm::vec3{0, 1, 0});
				model_transformation = glm::scale(model_transformation, picture.current_state.scale);

				const auto projection_view_model = projection_view * model_transformation;
				glUniformMatrix4fv(glGetUniformLocation(primitives2d_gpu_program.handle, "model_view_projection"), 1, rendering.transpose_model, glm::value_ptr(projection_view_model));

				for (auto& primitive : picture.primitives) {
					glUniform3fv(glGetUniformLocation(primitives2d_gpu_program.handle, "primitive_color"), 1, glm::value_ptr(primitive.color));

					const bool gradation_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS;
					glUniform1i(glGetUniformLocation(primitives2d_gpu_program.handle, "gradation_enabled"), (GLint) gradation_enabled);
					if (gradation_enabled) {
						glUniform3fv(glGetUniformLocation(primitives2d_gpu_program.handle, "primitive_color2"), 1, glm::value_ptr(primitive.color2));
					}

					glBindVertexArray(primitive.gpu.vao);
					glDrawArrays(primitive.gpu.primitive_type, 0, primitive.gpu.array_count);
				}
			}
		}
		glEnable(GL_DEPTH_TEST);

		// render terrains and meshes in field
		mn::buf_push(fields_to_render, &field);
		glUseProgram(meshes_gpu_program.handle);
		while (fields_to_render.count > 0) {
			Field* field_to_render = mn::buf_top(fields_to_render);
			mn::buf_pop(fields_to_render);

			if (field_to_render->current_state.visible == false) {
				continue;
			}

			for (int i = (int)field_to_render->subfields.count-1; i >= 0; i--) {
				mn::buf_push(fields_to_render, &field_to_render->subfields[i]);
			}

			// render terrains
			glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "is_light_source"), (GLint) false);
			for (auto& terr_mesh : field_to_render->terr_meshes) {
				if (terr_mesh.current_state.visible == false) {
					continue;
				}

				auto model_transformation = field_to_render->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.current_state.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[0], glm::vec3{0, 1, 0});
				model_transformation = glm::scale(model_transformation, terr_mesh.current_state.scale);
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "model"), 1, rendering.transpose_model, glm::value_ptr(model_transformation));

				glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "gradient_enabled"), (GLint) terr_mesh.gradiant.enabled);
				if (terr_mesh.gradiant.enabled) {
					glUniform1f(glGetUniformLocation(meshes_gpu_program.handle, "gradient_bottom_y"), terr_mesh.gradiant.bottom_y);
					glUniform1f(glGetUniformLocation(meshes_gpu_program.handle, "gradient_top_y"), terr_mesh.gradiant.top_y);
					glUniform3fv(glGetUniformLocation(meshes_gpu_program.handle, "gradient_bottom_color"), 1, glm::value_ptr(terr_mesh.gradiant.bottom_color));
					glUniform3fv(glGetUniformLocation(meshes_gpu_program.handle, "gradient_top_color"), 1, glm::value_ptr(terr_mesh.gradiant.top_color));
				}

				glBindVertexArray(terr_mesh.gpu.vao);
				glDrawArrays(rendering.regular_primitives_type, 0, terr_mesh.gpu.array_count);
			}
			glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "gradient_enabled"), (GLint) false);

			// meshes
			for (auto& mesh : field_to_render->meshes) {
				if (mesh.current_state.visible == false) {
					continue;
				}

				if (mesh.render_cnt_axis) {
					mn::buf_push(axis_instances, glm::translate(glm::identity<glm::mat4>(), mesh.cnt));
				}

				// apply mesh transformation
				mesh.transformation = field_to_render->transformation;
				mesh.transformation = glm::translate(mesh.transformation, mesh.current_state.translation);
				mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[2], glm::vec3{0, 0, 1});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[1], glm::vec3{1, 0, 0});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[0], glm::vec3{0, 1, 0});

				if (mesh.render_pos_axis) {
					mn::buf_push(axis_instances, mesh.transformation);
				}

				// upload transofmation model
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "model"), 1, rendering.transpose_model, glm::value_ptr(mesh.transformation));

				glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "is_light_source"), (GLint) mesh.is_light_source);

				glBindVertexArray(mesh.gpu.vao);
				glDrawArrays(mesh.is_light_source? rendering.light_primitives_type : rendering.regular_primitives_type, 0, mesh.gpu.array_count);
			}
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
						glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "model"), 1, rendering.transpose_model, glm::value_ptr(mesh->transformation));

						glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "is_light_source"), (GLint) mesh->is_light_source);

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
			glUniform1i(glGetUniformLocation(meshes_gpu_program.handle, "is_light_source"), 0);
			glBindVertexArray(axis_rendering.vao);
			for (const auto& transformation : axis_instances) {
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program.handle, "model"), 1, rendering.transpose_model, glm::value_ptr(transformation));
				glDrawArrays(GL_LINES, 0, axis_rendering.points_count);
			}
		}

		// render boxes
		if (box_instances.count > 0) {
			glUseProgram(lines_gpu_program.handle);
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(box_rendering.line_width);
			glBindVertexArray(box_rendering.vao);

			const auto projection_view = camera_get_projection_matrix(camera) * camera_get_view_matrix(camera);

			for (const auto& box : box_instances) {
				auto transformation = glm::translate(glm::identity<glm::mat4>(), box.translation);
				transformation = glm::scale(transformation, box.scale);
				const auto projection_view_model = projection_view * transformation;
				glUniformMatrix4fv(glGetUniformLocation(lines_gpu_program.handle, "model_view_projection"), 1, rendering.transpose_model, glm::value_ptr(projection_view_model));

				glUniform3fv(glGetUniformLocation(lines_gpu_program.handle, "color"), 1, glm::value_ptr(box.color));

				glDrawArrays(GL_LINE_LOOP, 0, box_rendering.points_count);
			}
		}

		// imgui
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
					camera.projection = {};
					wnd_size_changed = true;
				}

				ImGui::InputFloat("near", &camera.projection.near, 1, 10);
				ImGui::InputFloat("far", &camera.projection.far, 1, 10);
				ImGui::DragFloat("fovy (1/zoom)", &camera.projection.fovy, 1, 1, 45);

				if (ImGui::Checkbox("custom aspect", &camera.projection.custom_aspect) && !camera.projection.custom_aspect) {
					wnd_size_changed = true;
				}
				ImGui::BeginDisabled(!camera.projection.custom_aspect);
					ImGui::InputFloat("aspect", &camera.projection.aspect, 1, 10);
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("View")) {
				if (ImGui::Button("Reset")) {
					camera.view = {};
				}

				static size_t start_info_index = 0;
				if (ImGui::BeginCombo("Start Pos", start_infos[start_info_index].name.ptr)) {
					for (size_t j = 0; j < start_infos.count; j++) {
						if (ImGui::Selectable(start_infos[j].name.ptr, j == start_info_index)) {
							start_info_index = j;
							camera.view.pos = start_infos[j].position;
						}
					}

					ImGui::EndCombo();
				}

				ImGui::Checkbox("Identity", &camera.view.identity); // TODO: remove

				ImGui::BeginDisabled(camera.view.identity);
					ImGui::DragFloat("movement_speed", &camera.view.movement_speed, 5, 50, 1000);
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
					models[i].should_select_file = ImGui::Button("Load DNM");
					models[i].should_load_file = ImGui::Button("Reload");

					if (ImGui::Button("Reset State")) {
						model.current_state = {};
					}
					if (ImGui::Button("Reset All")) {
						model.current_state = {};
						for (auto& [_, mesh] : model.meshes.values) {
							mesh.current_state = mesh.initial_state;
						}
					}

					static size_t start_info_index = 0;
					if (ImGui::BeginCombo("Start Pos", start_infos[start_info_index].name.ptr)) {
						for (size_t j = 0; j < start_infos.count; j++) {
							if (ImGui::Selectable(start_infos[j].name.ptr, j == start_info_index)) {
								start_info_index = j;
								model_set_start(models[i], start_infos[start_info_index]);
							}
						}

						ImGui::EndCombo();
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

			std::function<void(Field&,bool)> render_field_imgui;
			render_field_imgui = [&render_field_imgui, &imgui_angle_max](Field& field, bool is_root) {
				if (ImGui::TreeNode(mn::str_tmpf("Field {}", field.name).ptr)) {
					if (is_root) {
						field.should_select_file = ImGui::Button("Open FLD");
						field.should_load_file = ImGui::Button("Reload");
					}

					if (ImGui::Button("Reset State")) {
						field.current_state = field.initial_state;
					}

					MyImGui::EnumsCombo("ID", &field.id, {
						{FieldID::NONE, "NONE"},
						{FieldID::RUNWAY, "RUNWAY"},
						{FieldID::TAXIWAY, "TAXIWAY"},
						{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
						{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
						{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
						{FieldID::TOWER, "TOWER"},
						{FieldID::VIEW_POINT, "VIEW_POINT"},
					});

					MyImGui::EnumsCombo("Default Area", &field.default_area, {
						{AreaKind::LAND, "LAND"},
						{AreaKind::WATER, "WATER"},
						{AreaKind::NOAREA, "NOAREA"},
					});
					ImGui::ColorEdit3("Sky Color", glm::value_ptr(field.sky_color));
					ImGui::ColorEdit3("GND Color", glm::value_ptr(field.ground_color));
					ImGui::Checkbox("GND Specular", &field.ground_specular);

					ImGui::Checkbox("Visible", &field.current_state.visible);

					ImGui::DragFloat3("Translation", glm::value_ptr(field.current_state.translation));
					MyImGui::SliderAngle3("Rotation", &field.current_state.rotation, imgui_angle_max);

					ImGui::BulletText("Sub Fields:");
					for (auto& subfield : field.subfields) {
						render_field_imgui(subfield, false);
					}

					ImGui::BulletText("TerrMesh: %d", (int)field.terr_meshes.count);
					for (auto& terr_mesh : field.terr_meshes) {
						if (ImGui::TreeNode(terr_mesh.name.ptr)) {
							if (ImGui::Button("Reset State")) {
								terr_mesh.current_state = terr_mesh.initial_state;
							}

							ImGui::Text("Tag: %s", terr_mesh.tag.ptr);

							MyImGui::EnumsCombo("ID", &terr_mesh.id, {
								{FieldID::NONE, "NONE"},
								{FieldID::RUNWAY, "RUNWAY"},
								{FieldID::TAXIWAY, "TAXIWAY"},
								{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
								{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
								{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
								{FieldID::TOWER, "TOWER"},
								{FieldID::VIEW_POINT, "VIEW_POINT"},
							});

							ImGui::Checkbox("Visible", &terr_mesh.current_state.visible);

							ImGui::DragFloat3("Scale", glm::value_ptr(terr_mesh.current_state.scale), 0.2f);
							ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.current_state.translation));
							MyImGui::SliderAngle3("Rotation", &terr_mesh.current_state.rotation, imgui_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Pict2: %d", (int)field.pictures.count);
					for (auto& picture : field.pictures) {
						if (ImGui::TreeNode(picture.name.ptr)) {
							if (ImGui::Button("Reset State")) {
								picture.current_state = picture.initial_state;
							}

							MyImGui::EnumsCombo("ID", &picture.id, {
								{FieldID::NONE, "NONE"},
								{FieldID::RUNWAY, "RUNWAY"},
								{FieldID::TAXIWAY, "TAXIWAY"},
								{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
								{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
								{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
								{FieldID::TOWER, "TOWER"},
								{FieldID::VIEW_POINT, "VIEW_POINT"},
							});

							ImGui::Checkbox("Visible", &picture.current_state.visible);

							ImGui::DragFloat3("Scale", glm::value_ptr(picture.current_state.scale), 0.01f);
							ImGui::DragFloat3("Translation", glm::value_ptr(picture.current_state.translation));
							MyImGui::SliderAngle3("Rotation", &picture.current_state.rotation, imgui_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Meshes: %d", (int)field.meshes.count);
					for (auto& mesh : field.meshes) {
						ImGui::Text("%s", mesh.name.ptr);
					}

					ImGui::TreePop();
				}
			};
			render_field_imgui(field, true);
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

		gpu_check_errors();
	}

	return 0;
}
/*
bugs:
- tornado.dnm/f1.dnm: strobe lights and landing-gears not in their expected positions
- viggen.dnm: right wheel doesn't rotate right
- cessna172r propoller doesn't rotate

TODO:
- what's PAX in dnmver 2?
- what are GE and ZE in hurricane.dnm?
- what are GL in cessna172r.dnm?
- what do if REL DEP not in dnm?

- Scenery files
	- read GOB at end of fld
	- add dnm GOBs to field
	- add srf GOBs to field (turn it into dnm?)
	- small.fld bugs:
		- biggest pic doesn't render/(tesselat?) correctly (from left side)
	- at end of FLD, what is PLT vs PC2? they both refer to Pict2 (!)
	- terrmesh sides colors
	- read PST at end of fld
	- aomori.fld:
		- what is BASEELV?
		- what is MAGVAR?
		- what is CANRESUME?
		- what is APL?
		- read TEXMAN
		- read TXL/TXC
		- render texture
		- what is AOB?
		- read AIRROUTE
		- failed to tesselate
	- what is DST in heathrow.fld pictures ?
	- hot reload file?
	- meshes imgui in field?
- render water
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
- all rotations as quaternions
- view normals (geometry shader)
- strict integers tokenization
- Mesh contains mn::Buf<Mesh> instead of names
- dnm hot realod per model

- optimization:
	- store stack of nodes instead of tree in model
	- use different kinds of opengl primitives (i.e. strip/fan)
*/
