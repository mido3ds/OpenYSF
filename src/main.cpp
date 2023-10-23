// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>

// don't export min/max/near/far definitions with windows.h otherwise other includes might break
#define NOMINMAX
#include <portable-file-dialogs.h>
#undef near
#undef far

#include "imgui.h"
#include "gpu.h"
#include "parser.h"
#include "math.h"
#include "audio.h"
#include <mu/utils.h>

#include <ft2build.h>
#include FT_FREETYPE_H

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

constexpr float PROPOLLER_MAX_ANGLE_SPEED = 10 * RADIANS_MAX;
constexpr float AFTERBURNER_THROTTLE_THRESHOLD = 0.80f;

constexpr float THROTTLE_SPEED = 0.4f;

constexpr float MIN_SPEED = 0.0f;
constexpr float MAX_SPEED = 50.0f;

constexpr float ZL_SCALE = 0.151f;

// 2 secs to flash anti collision lights
constexpr double ANTI_COLL_LIGHT_PERIOD = 4;

struct Face {
	mu::Vec<uint32_t> vertices_ids;
	glm::vec4 color;
	glm::vec3 center, normal;
};

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
	AIRCRAFT_SPINNER_PROPELLER_Z = 24, // rotates around Z instead of X (cessna172r.dnm)
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
			mu::StrView s {};
			switch (c) {
			case AnimationClass::AIRCRAFT_LANDING_GEAR /*| AnimationClass::GROUND_DEFAULT*/:
				s = "(AIRCRAFT_LANDING_GEAR||GROUND_DEFAULT)";
				break;
			case AnimationClass::AIRCRAFT_VARIABLE_GEOMETRY_WING /*| AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING*/:
				s = "(AIRCRAFT_VARIABLE_GEOMETRY_WING||GROUND_ANTI_AIRCRAFT_GUN_HORIZONTAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT /*| AnimationClass::GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING*/:
				s = "(AIRCRAFT_AFTERBURNER_REHEAT||GROUND_ANTI_AIRCRAFT_GUN_VERTICAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_SPINNER_PROPELLER /*| AnimationClass::GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING*/:
				s = "(AIRCRAFT_SPINNER_PROPELLER||GROUND_SAM_LAUNCHER_HORIZONTAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_AIRBRAKE /*| AnimationClass::GROUND_SAM_LAUNCHER_VERTICAL_TRACKING*/:
				s = "(AIRCRAFT_AIRBRAKE||GROUND_SAM_LAUNCHER_VERTICAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_FLAPS /*| AnimationClass::GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING*/:
				s = "(AIRCRAFT_FLAPS||GROUND_ANTI_GROUND_OBJECT_HORIZONTAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_ELEVATOR /*| AnimationClass::GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING*/:
				s = "(AIRCRAFT_ELEVATOR||GROUND_ANTI_GROUND_OBJECT_VERTICAL_TRACKING)";
				break;
			case AnimationClass::AIRCRAFT_VTOL_NOZZLE /*| AnimationClass::GROUND_SPINNING_RADAR_SLOW*/:
				s = "(AIRCRAFT_VTOL_NOZZLE||GROUND_SPINNING_RADAR_SLOW)";
				break;
			case AnimationClass::AIRCRAFT_THRUST_REVERSE /*| AnimationClass::GROUND_SPINNING_RADAR_FAST*/:
				s = "(AIRCRAFT_THRUST_REVERSE||GROUND_SPINNING_RADAR_FAST)";
				break;

			case AnimationClass::AIRCRAFT_AILERONS: s = "AIRCRAFT_AILERONS"; break;
			case AnimationClass::AIRCRAFT_RUDDER: s = "AIRCRAFT_RUDDER"; break;
			case AnimationClass::AIRCRAFT_BOMB_BAY_DOORS: s = "AIRCRAFT_BOMB_BAY_DOORS"; break;
			case AnimationClass::AIRCRAFT_THRUST_VECTOR_ANIMATION_LONG: s = "AIRCRAFT_THRUST_VECTOR_ANIMATION_LONG"; break;
			case AnimationClass::AIRCRAFT_THRUST_VECTOR_ANIMATION_SHORT: s = "AIRCRAFT_THRUST_VECTOR_ANIMATION_SHORT"; break;
			case AnimationClass::AIRCRAFT_GEAR_DOORS_TRANSITION: s = "AIRCRAFT_GEAR_DOORS_TRANSITION"; break;
			case AnimationClass::AIRCRAFT_INSIDE_GEAR_BAY: s = "AIRCRAFT_INSIDE_GEAR_BAY"; break;
			case AnimationClass::AIRCRAFT_BRAKE_ARRESTER: s = "AIRCRAFT_BRAKE_ARRESTER"; break;
			case AnimationClass::AIRCRAFT_GEAR_DOORS: s = "AIRCRAFT_GEAR_DOORS"; break;
			case AnimationClass::AIRCRAFT_LOW_THROTTLE: s = "AIRCRAFT_LOW_THROTTLE"; break;
			case AnimationClass::AIRCRAFT_HIGH_THROTTLE: s = "AIRCRAFT_HIGH_THROTTLE"; break;
			case AnimationClass::AIRCRAFT_TURRET_OBJECTS: s = "AIRCRAFT_TURRET_OBJECTS"; break;
			case AnimationClass::AIRCRAFT_ROTATING_WHEELS: s = "AIRCRAFT_ROTATING_WHEELS"; break;
			case AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z: s = "AIRCRAFT_SPINNER_PROPELLER_Z"; break;
			case AnimationClass::AIRCRAFT_STEERING: s = "AIRCRAFT_STEERING"; break;
			case AnimationClass::AIRCRAFT_NAV_LIGHTS: s = "AIRCRAFT_NAV_LIGHTS"; break;
			case AnimationClass::AIRCRAFT_ANTI_COLLISION_LIGHTS: s = "AIRCRAFT_ANTI_COLLISION_LIGHTS"; break;
			case AnimationClass::AIRCRAFT_STROBE_LIGHTS: s = "AIRCRAFT_STROBE_LIGHTS"; break;
			case AnimationClass::AIRCRAFT_LANDING_LIGHTS: s = "AIRCRAFT_LANDING_LIGHTS"; break;
			case AnimationClass::AIRCRAFT_LANDING_GEAR_LIGHTS: s = "AIRCRAFT_LANDING_GEAR_LIGHTS"; break;

			case AnimationClass::PLAYER_GROUND_LEFT_DOOR: s = "PLAYER_GROUND_LEFT_DOOR"; break;
			case AnimationClass::PLAYER_GROUND_RIGHT_DOOR: s = "PLAYER_GROUND_RIGHT_DOOR"; break;
			case AnimationClass::PLAYER_GROUND_REAR_DOOR: s = "PLAYER_GROUND_REAR_DOOR"; break;
			case AnimationClass::PLAYER_GROUND_CARGO_DOOR: s = "PLAYER_GROUND_CARGO_DOOR"; break;

			case AnimationClass::UNKNOWN:
			default:
				s = mu::str_tmpf("UNKNOWN({})", (int)c); break;
			}
			return fmt::format_to(ctx.out(), "AnimationClass::{}", s);
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
			case FieldID::NONE:                    return fmt::format_to(ctx.out(), "FieldID::NONE");
			case FieldID::RUNWAY:                  return fmt::format_to(ctx.out(), "FieldID::RUNWAY");
			case FieldID::TAXIWAY:                 return fmt::format_to(ctx.out(), "FieldID::TAXIWAY");
			case FieldID::AIRPORT_AREA:            return fmt::format_to(ctx.out(), "FieldID::AIRPORT_AREA");
			case FieldID::ENEMY_TANK_GENERATOR:    return fmt::format_to(ctx.out(), "FieldID::ENEMY_TANK_GENERATOR");
			case FieldID::FRIENDLY_TANK_GENERATOR: return fmt::format_to(ctx.out(), "FieldID::FRIENDLY_TANK_GENERATOR");
			case FieldID::TOWER:                   return fmt::format_to(ctx.out(), "FieldID::TOWER");
			case FieldID::VIEW_POINT:              return fmt::format_to(ctx.out(), "FieldID::VIEW_POINT");
			default: mu::log_error("found unknown ID = {}", (int) v);
			}
			return fmt::format_to(ctx.out(), "FieldID::????");
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

	mu::Str name; // name in SRF not FIL
	mu::Vec<glm::vec3> vertices;
	mu::Vec<bool> vertices_has_smooth_shading; // ???
	mu::Vec<Face> faces;
	mu::Vec<uint64_t> gfs; // ???
	mu::Vec<uint64_t> zls; // ids of faces to create a light sprite at the center of them
	mu::Vec<uint64_t> zzs; // ???
	mu::Vec<mu::Str> children; // refers to FIL name not SRF (don't compare against Mesh::name)
	mu::Vec<MeshState> animation_states; // STA

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

void mesh_load_to_gpu(Mesh& self) {
	struct Stride {
		glm::vec3 vertex;
		glm::vec4 color;
		glm::vec3 normal;
	};
	mu::Vec<Stride> buffer(mu::memory::tmp());
	for (const auto& face : self.faces) {
		for (size_t i = 0; i < face.vertices_ids.size(); i++) {
			buffer.push_back(Stride {
				.vertex=self.vertices[face.vertices_ids[i]],
				.color=face.color,
				.normal=face.normal,
			});
		}
	}
	self.gpu.array_count = buffer.size();

	glGenVertexArrays(1, &self.gpu.vao);
	glBindVertexArray(self.gpu.vao);
		glGenBuffers(1, &self.gpu.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.gpu.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(Stride), buffer.data(), GL_STATIC_DRAW);

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
	mu::Str name;
	glm::vec3 position;
	glm::vec3 attitude;
	float speed;
	float throttle;
	bool landing_gear_is_out = true;
};

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
	const auto x = parser_token_str(parser, mu::memory::tmp());
	if (x == "TRUE") {
		return true;
	} else if (x == "FALSE") {
		return false;
	}
	parser_panic(parser, "expected either TRUE or FALSE, found='{}'", x);
	return false;
}

mu::Vec<StartInfo> start_info_from_stp_file(mu::StrView stp_file_abs_path) {
	auto parser = parser_from_file(stp_file_abs_path, mu::memory::tmp());

	mu::Vec<StartInfo> start_infos;

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
					mu::panic("throttle={} out of bounds [0,1]", start_info.throttle);
				}
			} else if (parser_accept(parser, "CTLLDGEA ")) {
				start_info.landing_gear_is_out = _token_bool(parser);
				parser_expect(parser, '\n');
			} else {
				parser_panic(parser, "unrecognized type");
			}

			while (parser_accept(parser, '\n')) {}
		}

		start_infos.push_back(start_info);
	}

	return start_infos;
}

// DNM See https://ysflightsim.fandom.com/wiki/DynaModel_Files
struct Model {
	mu::Str file_abs_path;
	bool should_load_file, should_be_removed;

	mu::Map<mu::Str, Mesh> meshes;
	mu::Vec<mu::Str> root_meshes_names;

	AABB initial_aabb;
	AABB current_aabb;
	bool render_aabb;

	struct {
		glm::vec3 translation;
		LocalEulerAngles angles;
		bool visible = true;
		float speed;
	} current_state;

	struct {
		float landing_gear_alpha = 0; // 0 -> DOWN, 1 -> UP
		float throttle = 0;
		bool afterburner_reheat_enabled = false;
	} control;

	struct {
		bool visible = true;
		double time_left_secs = ANTI_COLL_LIGHT_PERIOD;
	} anti_coll_lights;

	Audio* engine_sound;
	bool has_propellers;
	bool has_afterburner;
	bool has_high_throttle_mesh;
};

void model_load_to_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes) {
		mesh_load_to_gpu(mesh);
	}
}

void model_unload_from_gpu(Model& self) {
	for (auto& [_, mesh] : self.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

void model_set_start(Model& self, StartInfo& start_info) {
	self.current_state.translation = start_info.position;
	self.current_state.angles = LocalEulerAngles::from_attitude(start_info.attitude);
	self.control.landing_gear_alpha = start_info.landing_gear_is_out? 0.0f : 1.0f;
	self.control.throttle = start_info.throttle;
	self.current_state.speed = start_info.speed;
}

Mesh mesh_from_srf_str(Parser& parser, mu::StrView name, size_t dnm_version = 1) {
	// aircraft/cessna172r.dnm has Surf instead of SURF (and .fld files use Surf)
	if (parser_accept(parser, "SURF\n") == false) {
		parser_expect(parser, "Surf\n");
	}

	Mesh mesh { .name = mu::Str(name) };

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

		mesh.vertices.push_back(v);
		mesh.vertices_has_smooth_shading.push_back(smooth_shading);
	}
	if (mesh.vertices.size() == 0) {
		mu::log_error("'{}': doesn't have any vertices!", name);
	}

	// <Face>+
	mu::Vec<bool> faces_unshaded_light_source(mu::memory::tmp());
	while (parser_accept(parser, "F\n")) {
		Face face {};
		bool parsed_color = false,
			parsed_normal = false,
			parsed_vertices = false,
			is_light_source = false;

		while (!parser_accept(parser, "E\n")) {
			if (parser_accept(parser, "C ")) {
				if (parsed_color) {
					mu::panic("'{}': found more than one color", name);
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
					my_assert(packed_color.as_struct.padding == 0);
				}

				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "N ")) {
				if (parsed_normal) {
					mu::panic("'{}': found more than one normal", name);
				}
				parsed_normal = true;

				face.center.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.center.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				face.center.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				face.normal.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				face.normal.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				face.normal.z = parser_token_float(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, 'V')) {
				// V {x}...
				mu::Vec<uint32_t> polygon_vertices_ids(mu::memory::tmp());
				while (parser_accept(parser, ' ')) {
					auto id = parser_token_u64(parser);
					if (id >= mesh.vertices.size()) {
						mu::panic("'{}': id={} out of bounds={}", name, id, mesh.vertices.size());
					}
					polygon_vertices_ids.push_back((uint32_t) id);
				}
				parser_expect(parser, '\n');

				if (parsed_vertices) {
					mu::log_error("'{}': found more than one vertices line, ignore others", name);
				} else {
					parsed_vertices = true;

					if (polygon_vertices_ids.size() < 3) {
						mu::log_error("'{}': face has count of ids={}, it should be >= 3", name, polygon_vertices_ids.size());
					}

					face.vertices_ids = polygons_to_triangles(mesh.vertices, polygon_vertices_ids, face.center);
					if (face.vertices_ids.size() % 3 != 0) {
						mu::Vec<glm::vec3> orig_vertices(mu::memory::tmp());
						for (auto id : polygon_vertices_ids) {
							orig_vertices.push_back(mesh.vertices[id]);
						}
						mu::Vec<glm::vec3> new_vertices(mu::memory::tmp());
						for (auto id : face.vertices_ids) {
							new_vertices.push_back(mesh.vertices[id]);
						}
						mu::log_error("{}:{}: num of vertices_ids must have been divisble by 3 to be triangles, but found {}, original vertices={}, new vertices={}", name, parser.curr_line+1,
							face.vertices_ids.size(), orig_vertices, new_vertices);
					}
				}
			} else if (parser_accept(parser, "B\n")) {
				if (is_light_source) {
					mu::log_error("'{}': found more than 1 B for same face", name);
				}
				is_light_source = true;
			} else {
				parser_panic(parser, "'{}': unexpected line", name);
			}
		}

		if (!parsed_color) {
			mu::log_error("'{}': face has no color", name);
		}
		if (!parsed_normal) {
			mu::log_error("'{}': face has no normal", name);
		}
		if (!parsed_vertices) {
			mu::log_error("'{}': face has no vertices", name);
		}

		faces_unshaded_light_source.push_back(is_light_source);
		mesh.faces.push_back(face);
	}

	size_t zz_count = 0;
	while (true) {
		if (parser_accept(parser, '\n')) {
			// nothing
		} else if (parser_accept(parser, "GE") || parser_accept(parser, "ZE") || parser_accept(parser, "GL")) {
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "GF")) { // [GF< {u64}>+\n]+
			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.size()) {
					mu::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.size());
				}
				mesh.gfs.push_back(id);
			}
			parser_expect(parser, '\n');
		} else if (parser_accept(parser, "ZA")) { // [ZA< {u64} {u8}>+\n]+
			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.size()) {
					mu::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.size());
				}
				parser_expect(parser, ' ');
				mesh.faces[id].color.a = (255 - parser_token_u8(parser)) / 255.0f;
				// because alpha came as: 0 -> obaque, 255 -> clear
				// we revert it so it becomes: 1 -> obaque, 0 -> clear
			}
			parser_expect(parser, '\n');
		} else if (parser_accept(parser, "ZL")) { // [ZL< {u64}>+\n]
			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.size()) {
					mu::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.size());
				}
				mesh.zls.push_back(id);
			}
			parser_expect(parser, '\n');
		} else if  (parser_accept(parser, "ZZ")) { // [ZZ< {u64}>+\n]
			zz_count++;
			if (zz_count > 1) {
				mu::panic("'{}': found {} > 1 ZZs", name, zz_count);
			}

			while (parser_accept(parser, ' ')) {
				auto id = parser_token_u64(parser);
				if (id >= mesh.faces.size()) {
					mu::panic("'{}': out of range faceid={}, range={}", name, id, mesh.faces.size());
				}
				mesh.zzs.push_back(id);
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
	mesh.is_light_source = (unshaded_count == faces_unshaded_light_source.size());

	return mesh;
}

inline static void
_str_unquote(mu::Str& s) {
	if (s.size() < 2) {
		return;
	}
	if (s[0] == s[s.size()-1] && s[0] == '\"') {
		s.pop_back();
		s.erase(s.begin());
	}
}

Model model_from_dnm_file(mu::StrView dnm_file_abs_path) {
	auto parser = parser_from_file(dnm_file_abs_path, mu::memory::tmp());
	Model model {
		.file_abs_path = mu::Str(dnm_file_abs_path),
		.initial_aabb = AABB {
			.min={+FLT_MAX, +FLT_MAX, +FLT_MAX},
			.max={-FLT_MAX, -FLT_MAX, -FLT_MAX},
		},
	};

	parser_expect(parser, "DYNAMODEL\nDNMVER ");
	const uint8_t dnm_version = parser_token_u8(parser);
	if (dnm_version > 2) {
		mu::panic("unsupported version {}", dnm_version);
	}
	parser_expect(parser, '\n');

	mu::Map<mu::Str, Mesh> meshes {};
	while (parser_accept(parser, "PCK ")) {
		auto name = parser_token_str(parser, mu::memory::tmp());
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
			mu::log_error("'{}':{} expected {} lines in PCK, found {}", name, current_lineno, pck_expected_no_lines, pck_found_linenos);
		}

		meshes[name] = mesh;
	}

	while (parser_accept(parser, "SRF ")) {
		auto name = parser_token_str(parser);
		if (!(mu::str_prefix(name, "\"") && mu::str_suffix(name, "\""))) {
			mu::panic("name must be in \"\" found={}", name);
		}
		_str_unquote(name);
		parser_expect(parser, '\n');

		parser_expect(parser, "FIL ");
		auto fil = parser_token_str(parser, mu::memory::tmp());
		parser_expect(parser, '\n');
		auto surf = meshes.find(fil);
		if (surf == meshes.end()) {
			mu::panic("'{}': line referenced undeclared surf={}", name, fil);
		}
		surf->second.name = name;

		parser_expect(parser, "CLA ");
		auto animation_type = parser_token_u8(parser);
		surf->second.animation_type = (AnimationClass) animation_type;
		if (surf->second.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER || surf->second.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z) {
			model.has_propellers = true;
		} else if (surf->second.animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
			model.has_afterburner = true;
		} else if (surf->second.animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE) {
			model.has_high_throttle_mesh = true;
		}
		parser_expect(parser, '\n');

		parser_expect(parser, "NST ");
		auto num_stas = parser_token_u64(parser);
		surf->second.animation_states.reserve(num_stas);
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
				mu::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
			}
			parser_expect(parser, '\n');

			surf->second.animation_states.push_back(sta);
		}

		bool read_pos = false, read_cnt = false, read_rel_dep = false, read_nch = false;
		while (true) {
			if (parser_accept(parser, "POS ")) {
				read_pos = true;

				surf->second.initial_state.translation.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->second.initial_state.translation.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->second.initial_state.translation.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
				surf->second.initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf->second.initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf->second.initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;

				// aircraft/cessna172r.dnm is the only file with no visibility
				if (parser_accept(parser, ' ')) {
					uint8_t visible = parser_token_u8(parser);
					if (visible == 1 || visible == 0) {
						surf->second.initial_state.visible = (visible == 1);
					} else {
						mu::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
					}
				} else {
					surf->second.initial_state.visible = true;
				}

				parser_expect(parser, '\n');

				surf->second.current_state = surf->second.initial_state;
			} else if (parser_accept(parser, "CNT ")) {
				read_cnt = true;

				surf->second.cnt.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->second.cnt.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf->second.cnt.z = parser_token_float(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "PAX")) {
				parser_skip_after(parser, '\n');
			} else if (parser_accept(parser, "REL DEP\n")) {
				read_rel_dep = true;
			} else if (parser_accept(parser, "NCH ")) {
				read_nch = true;

				const auto num_children = parser_token_u64(parser);
				parser_expect(parser, '\n');
				surf->second.children.reserve(num_children);

				for (size_t i = 0; i < num_children; i++) {
					parser_expect(parser, "CLD ");
					auto child_name = parser_token_str(parser);
					if (!(mu::str_prefix(child_name, "\"") && mu::str_suffix(child_name, "\""))) {
						mu::panic("'{}': child_name must be in \"\" found={}", name, child_name);
					}
					_str_unquote(child_name);
					surf->second.children.push_back(child_name);
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
			mu::log_error("'{}':{} failed to find REL DEP", name, parser.curr_line+1);
		}
		if (read_nch == false) {
			parser_panic(parser, "failed to find NCH");
		}

		// reinsert with name instead of FIL
		meshes[name] = surf->second;
		if (meshes.erase(fil) == 0) {
			parser_panic(parser, "must be able to remove {} from meshes", name, fil);
		}

		parser_expect(parser, "END\n");
	}
	// aircraft/cessna172r.dnm doesn't have final END
	if (parser_finished(parser) == false) {
		parser_expect(parser, "END\n");
	}

	// check children exist
	for (const auto [_, srf] : meshes) {
		for (const auto child : srf.children) {
			if (meshes.at(child).name == srf.name) {
				mu::log_warning("SURF {} references itself", child);
			}
		}
	}

	model.meshes = meshes;

	// top level nodes = nodes without parents
	mu::Set<mu::StrView> surfs_with_parents(mu::memory::tmp());
	for (const auto& [_, surf] : meshes) {
		for (const auto& child : surf.children) {
			surfs_with_parents.insert(child);
		}
	}
	for (const auto& [name, mesh] : meshes) {
		if (surfs_with_parents.find(name) == surfs_with_parents.end()) {
			model.root_meshes_names.push_back(name);
		}
	}

	// for each mesh: vertex -= mesh.CNT, mesh.children.each.cnt += mesh.cnt
	mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
	for (const auto& name : model.root_meshes_names) {
		Mesh& mesh = model.meshes.at(name);
		mesh.transformation = glm::identity<glm::mat4>();
		meshes_stack.push_back(&mesh);
	}
	while (meshes_stack.empty() == false) {
		Mesh* mesh = *meshes_stack.rbegin();
		meshes_stack.pop_back();

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

		for (const mu::Str& child_name : mesh->children) {
			auto& child_mesh = model.meshes.at(child_name);
			child_mesh.cnt += mesh->cnt;
			meshes_stack.push_back(&child_mesh);
		}
	}

	model.current_aabb = model.initial_aabb;

	return model;
}

struct PerspectiveProjection {
	float near         = 0.1f;
	float far          = 100000;
	float fovy         = 45.0f / DEGREES_MAX * RADIANS_MAX;
	float aspect       = (float) WND_INIT_WIDTH / WND_INIT_HEIGHT;
	bool custom_aspect = false;
};

struct Camera {
	Model* model;
	float distance_from_model = 50;

	float movement_speed    = 1000.0f;
	float mouse_sensitivity = 1.4;

	glm::vec3 position = glm::vec3{0.0f, 0.0f, 3.0f};
	glm::vec3 front    = glm::vec3{0.0f, 0.0f, -1.0f};
	glm::vec3 world_up = glm::vec3{0.0f, -1.0f, 0.0f};
	glm::vec3 right    = glm::vec3{1.0f, 0.0f, 0.0f};
	glm::vec3 up       = world_up;

	float yaw   = 15.0f / DEGREES_MAX * RADIANS_MAX;
	float pitch = 0.0f / DEGREES_MAX * RADIANS_MAX;

	glm::ivec2 last_mouse_pos;

	bool enable_rotating_around;
};

void camera_update(Camera& self, float delta_time) {
	if (self.model) {
		const Uint8 * key_pressed = SDL_GetKeyboardState(nullptr);
		const float velocity = 0.40f * delta_time;
		if (key_pressed[SDL_SCANCODE_U]) {
			self.yaw += velocity;
		}
		if (key_pressed[SDL_SCANCODE_M]) {
			self.yaw -= velocity;
		}
		if (key_pressed[SDL_SCANCODE_K]) {
			self.pitch += velocity;
		}
		if (key_pressed[SDL_SCANCODE_H]) {
			self.pitch -= velocity;
		}

		if (self.enable_rotating_around) {
			self.pitch += (7 * delta_time) / DEGREES_MAX * RADIANS_MAX;
		}

		constexpr float CAMERA_ANGLES_MAX = 89.0f / DEGREES_MAX * RADIANS_MAX;
		self.yaw = clamp(self.yaw, -CAMERA_ANGLES_MAX, CAMERA_ANGLES_MAX);

		auto model_transformation = self.model->current_state.angles.matrix(self.model->current_state.translation);

		model_transformation = glm::rotate(model_transformation, self.pitch, glm::vec3{0, -1, 0});
		model_transformation = glm::rotate(model_transformation, self.yaw, glm::vec3{-1, 0, 0});
		self.position = model_transformation * glm::vec4{0, 0, -self.distance_from_model, 1};
	} else {
		// move with keyboard
		const Uint8 * key_pressed = SDL_GetKeyboardState(nullptr);
		const float velocity = self.movement_speed * delta_time;
		if (key_pressed[SDL_SCANCODE_W]) {
			self.position += self.front * velocity;
		}
		if (key_pressed[SDL_SCANCODE_S]) {
			self.position -= self.front * velocity;
		}
		if (key_pressed[SDL_SCANCODE_D]) {
			self.position += self.right * velocity;
		}
		if (key_pressed[SDL_SCANCODE_A]) {
			self.position -= self.right * velocity;
		}

		// move with mouse
		glm::ivec2 mouse_now;
		const auto buttons = SDL_GetMouseState(&mouse_now.x, &mouse_now.y);
		if ((buttons & SDL_BUTTON(SDL_BUTTON_RIGHT))) {
			self.yaw   += (mouse_now.x - self.last_mouse_pos.x) * self.mouse_sensitivity / 1000;
			self.pitch -= (mouse_now.y - self.last_mouse_pos.y) * self.mouse_sensitivity / 1000;

			// make sure that when pitch is out of bounds, screen doesn't get flipped
			constexpr float CAMERA_PITCH_MAX = 89.0f / DEGREES_MAX * RADIANS_MAX;
			self.pitch = clamp(self.pitch, -CAMERA_PITCH_MAX, CAMERA_PITCH_MAX);
		}
		self.last_mouse_pos = mouse_now;

		// update front, right and up Vectors using the updated Euler angles
		self.front = glm::normalize(glm::vec3 {
			glm::cos(self.yaw) * glm::cos(self.pitch),
			glm::sin(self.pitch),
			glm::sin(self.yaw) * glm::cos(self.pitch),
		});

		// normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
		self.right = glm::normalize(glm::cross(self.front, self.world_up));
		self.up    = glm::normalize(glm::cross(self.right, self.front));
	}
}

glm::mat4 camera_calc_view(const Camera& self) {
	if (self.model) {
		return glm::lookAt(self.position, self.model->current_state.translation, self.model->current_state.angles.up);
	}

	return glm::lookAt(self.position, self.position + self.front, self.up);
}

struct Block {
	enum { RIGHT=0, LEFT } orientation;
	glm::vec4 faces_color[2];
};

struct TerrMesh {
	mu::Str name, tag;
	FieldID id;

	// x,z
	glm::vec2 scale = {1,1};

	// [z][x] where (z=0,x=0) is bot-left most
	mu::Vec<mu::Vec<float>> nodes_height;
	mu::Vec<mu::Vec<Block>> blocks;

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
		bool visible = true;
	} current_state, initial_state;
};

void terr_mesh_load_to_gpu(TerrMesh& self) {
	struct Stride {
		glm::vec3 vertex;
		glm::vec4 color;
	};
	mu::Vec<Stride> buffer(mu::memory::tmp());

	// main triangles
	for (size_t z = 0; z < self.blocks.size(); z++) {
		for (size_t x = 0; x < self.blocks[z].size(); x++) {
			if (self.blocks[z][x].orientation == Block::RIGHT) {
				// face 1
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[0],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});

				// face 2
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[1],
				});
			} else {
				// face 1
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[0],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z+1][x+1], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[0],
				});

				// face 2
				buffer.push_back(Stride {
					.vertex=glm::vec3{x+1, -self.nodes_height[z][x+1], z},
					.color=self.blocks[z][x].faces_color[1],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z+1][x], z+1},
					.color=self.blocks[z][x].faces_color[1],
				});
				buffer.push_back(Stride {
					.vertex=glm::vec3{x, -self.nodes_height[z][x], z},
					.color=self.blocks[z][x].faces_color[1],
				});
			}
		}
	}
	self.gpu.array_count = buffer.size();

	for (auto& stride : buffer) {
		stride.vertex.x *= self.scale.x;
		stride.vertex.z *= self.scale.y;
	}

	// load buffer to gpu
	glGenVertexArrays(1, &self.gpu.vao);
	glBindVertexArray(self.gpu.vao);
		glGenBuffers(1, &self.gpu.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.gpu.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(Stride), buffer.data(), GL_STATIC_DRAW);

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
	mu::Vec<glm::vec2> vertices;

	struct {
		GLuint vao, vbo;
		GLenum primitive_type;
		size_t array_count;
	} gpu;
};

void primitive2d_load_to_gpu(Primitive2D& self) {
	mu::Vec<glm::vec2> vertices(mu::memory::tmp());
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
		for (int i = 0; i < (int)self.vertices.size() - 3; i += 4) {
			vertices.push_back(self.vertices[i]);
			vertices.push_back(self.vertices[i+3]);
			vertices.push_back(self.vertices[i+2]);

			vertices.push_back(self.vertices[i]);
			vertices.push_back(self.vertices[i+2]);
			vertices.push_back(self.vertices[i+1]);
		}
		break;
	case Primitive2D::Kind::GRADATION_QUAD_STRIPS: // same as QUAD_STRIPS but with extra color
	case Primitive2D::Kind::QUAD_STRIPS:
		self.gpu.primitive_type = GL_TRIANGLES;
		for (int i = 0; i < (int)self.vertices.size() - 2; i += 2) {
			vertices.push_back(self.vertices[i]);
			vertices.push_back(self.vertices[i+1]);
			vertices.push_back(self.vertices[i+3]);

			vertices.push_back(self.vertices[i]);
			vertices.push_back(self.vertices[i+2]);
			vertices.push_back(self.vertices[i+3]);
		}
		break;
	case Primitive2D::Kind::POLYGON:
	{
		self.gpu.primitive_type = GL_TRIANGLES;
		auto indices = polygons2d_to_triangles(self.vertices, mu::memory::tmp());
		for (auto& index : indices) {
			vertices.push_back(self.vertices[index]);
		}
		break;
	}
	default: unreachable();
	}
	self.gpu.array_count = vertices.size();

	// load vertices to gpu
	glGenVertexArrays(1, &self.gpu.vao);
	glBindVertexArray(self.gpu.vao);
		glGenBuffers(1, &self.gpu.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.gpu.vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2), vertices.data(), GL_STATIC_DRAW);

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

struct Picture2D {
	mu::Str name;
	FieldID id;

	mu::Vec<Primitive2D> primitives;
	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		bool visible = true;
	} current_state, initial_state;
};

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
			case AreaKind::NOAREA: return fmt::format_to(ctx.out(), "AreaKind::NOAREA");
			case AreaKind::LAND:   return fmt::format_to(ctx.out(), "AreaKind::LAND");
			case AreaKind::WATER:  return fmt::format_to(ctx.out(), "AreaKind::WATER");
			default: unreachable();
			}
			return fmt::format_to(ctx.out(), "????????");
		}
	};
}

// runway or viewpoint
struct FieldRegion {
	// (X,Z) y=0
	glm::vec2 min, max;
	glm::mat4 transformation;
	FieldID id;
	mu::Str tag;
};

struct Field {
	mu::Str name;
	FieldID id;

	AreaKind default_area;
	glm::vec3 ground_color, sky_color;
	bool ground_specular; // ????

	mu::Vec<TerrMesh> terr_meshes;
	mu::Vec<Picture2D> pictures;
	mu::Vec<FieldRegion> regions;
	mu::Vec<Field> subfields;
	mu::Vec<Mesh> meshes;

	mu::Str file_abs_path;
	bool should_select_file, should_load_file, should_transform = true;

	glm::mat4 transformation;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		bool visible = true;
	} current_state, initial_state;
};

Field _field_from_fld_str(Parser& parser) {
	parser_expect(parser, "FIELD\n");

	Field field {};

	while (true) {
		if (parser_accept(parser, "FLDVERSION ")) {
			// TODO
			mu::log_warning("{}: found FLDVERSION, doesn't support it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "FLDNAME ")) {
			mu::log_warning("{}: found FLDNAME, doesn't support it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, "TEXMAN")) {
			// TODO
			mu::log_warning("{}: found TEXMAN, doesn't support it, skip for now", parser.curr_line+1);
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
		const auto default_area_str = parser_token_str(parser, mu::memory::tmp());
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
		mu::log_warning("{}: found BASEELV, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, '\n');
	}

	if (parser_accept(parser, "MAGVAR ")) {
		// TODO
		mu::log_warning("{}: found MAGVAR, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, '\n');
	}

	if (parser_accept(parser, "CANRESUME TRUE\n") || parser_accept(parser, "CANRESUME FALSE\n")) {
		// TODO
		mu::log_warning("{}: found CANRESUME, doesn't understand it, skip for now", parser.curr_line+1);
	}

	while (parser_accept(parser, "AIRROUTE\n")) {
		// TODO
		mu::log_warning("{}: found AIRROUTE, doesn't understand it, skip for now", parser.curr_line+1);
		parser_skip_after(parser, "ENDAIRROUTE\n");
	}

	while (parser_accept(parser, "PCK ")) {
		auto name = parser_token_str(parser);
		_str_unquote(name);
		parser_expect(parser, ' ');

		const auto total_lines_count = parser_token_u64(parser);
		parser_expect(parser, '\n');

		const size_t first_line_no = parser.curr_line+1;
		if (parser_peek(parser, "FIELD\n")) {
			auto subparser = parser_fork(parser, total_lines_count);
			auto subfield = _field_from_fld_str(subparser);
			subfield.name = name;

			field.subfields.push_back(subfield);
		} else if (parser_accept(parser, "TerrMesh\n")) {
			TerrMesh terr_mesh { .name=name };

			if (parser_accept(parser, "SPEC TRUE\n") || parser_accept(parser, "SPEC FALSE\n")) {
				// TODO
				mu::log_warning("{}: found SPEC, doesn't understand it, skip for now", parser.curr_line+1);
			}

			if (parser_accept(parser, "TEX MAIN")) {
				// TODO
				mu::log_warning("{}: found TEX MAIN, doesn't understand it, skip for now", parser.curr_line+1);
				parser_skip_after(parser, "\n");
			}

			parser_expect(parser, "NBL ");
			const auto num_blocks_x = parser_token_u64(parser);
			parser_expect(parser, ' ');
			const auto num_blocks_z = parser_token_u64(parser);
			parser_expect(parser, '\n');

			parser_expect(parser, "TMS ");
			terr_mesh.scale.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			terr_mesh.scale.y = parser_token_float(parser);
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
			terr_mesh.blocks.resize(num_blocks_z);
			for (auto& row : terr_mesh.blocks) {
				row.resize(num_blocks_x);
			}

			// create nodes
			terr_mesh.nodes_height.resize(num_blocks_z+1);
			for (auto& row : terr_mesh.nodes_height) {
				row.resize(num_blocks_x+1);
			}

			// parse blocks and nodes
			for (size_t z = 0; z < terr_mesh.nodes_height.size(); z++) {
				for (size_t x = 0; x < terr_mesh.nodes_height[z].size(); x++) {
					parser_expect(parser, "BLO ");
					terr_mesh.nodes_height[z][x] = parser_token_float(parser);

					// don't read rest of block if node is on edge/wedge
					if (z == terr_mesh.nodes_height.size()-1 || x == terr_mesh.nodes_height[z].size()-1) {
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

			field.terr_meshes.push_back(terr_mesh);
		} else if (parser_accept(parser, "Pict2\n")) {
			Picture2D picture { .name=name };

			picture.current_state = picture.initial_state;

			while (parser_accept(parser, "ENDPICT\n") == false) {
				Primitive2D permitive {};

				auto kind_str = parser_token_str(parser, mu::memory::tmp());
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
					mu::log_warning("{}: invalid pict2 kind={}, skip for now", parser.curr_line+1, kind_str);
					parser_skip_after(parser, "ENDO\n");
					continue;
				}

				if (parser_accept(parser, "DST ")) {
					// TODO
					mu::log_warning("{}: found DST, doesn't understand it, skip for now", parser.curr_line+1);
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
						mu::log_warning("{}: found SPEC, doesn't understand it, skip for now", parser.curr_line+1);
						continue;
					}

					if (parser_accept(parser, "TXL")) {
						// TODO
						mu::log_warning("{}: found TXL, doesn't understand it, skip for now", parser.curr_line+1);
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

					permitive.vertices.push_back(vertex);
				}

				if (permitive.vertices.size() == 0) {
					parser_panic(parser, "{}: no vertices", parser.curr_line+1);
				} else if (permitive.kind == Primitive2D::Kind::TRIANGLES && permitive.vertices.size() % 3 != 0) {
					parser_panic(parser, "{}: kind is triangle but num of vertices ({}) isn't divisible by 3", parser.curr_line+1, permitive.vertices.size());
				} else if (permitive.kind == Primitive2D::Kind::LINES && permitive.vertices.size() % 2 != 0) {
					mu::log_error("{}: kind is line but num of vertices ({}) isn't divisible by 2, ignoring last vertex", parser.curr_line+1, permitive.vertices.size());
					permitive.vertices.pop_back();
				} else if (permitive.kind == Primitive2D::Kind::LINE_SEGMENTS && permitive.vertices.size() == 1) {
					parser_panic(parser, "{}: kind is line but has one point", parser.curr_line+1);
				} else if (permitive.kind == Primitive2D::Kind::QUADRILATERAL && permitive.vertices.size() % 4 != 0) {
					parser_panic(parser, "{}: kind is quadrilateral but num of vertices ({}) isn't divisible by 4", parser.curr_line+1, permitive.vertices.size());
				} else if (permitive.kind == Primitive2D::Kind::QUAD_STRIPS && (permitive.vertices.size() >= 4 && permitive.vertices.size() % 2 == 0) == false) {
					parser_panic(parser, "{}: kind is quad_strip but num of vertices ({}) isn't in (4,6,8,10,...)", parser.curr_line+1, permitive.vertices.size());
				}

				picture.primitives.push_back(permitive);
			}

			field.pictures.push_back(picture);
		} else if (parser_peek(parser, "Surf\n")) {
			auto subparser = parser_fork(parser, total_lines_count);
			auto mesh = mesh_from_srf_str(subparser, name);
			field.meshes.push_back(mesh);
		} else {
			parser_panic(parser, "{}: invalid type '{}'", parser.curr_line+1, parser_token_str(parser, mu::memory::tmp()));
		}

		const size_t last_line_no = parser.curr_line+1;
		const size_t curr_lines_count = last_line_no - first_line_no;
		if (curr_lines_count != total_lines_count) {
			mu::log_error("{}: expected {} lines, found {}", last_line_no, total_lines_count, curr_lines_count);
		}

		parser_expect(parser, "\n\n");

		// aomori.fld contains more than 2 empty lines
		while (parser_accept(parser, '\n')) {}
	}

	while (parser_finished(parser) == false) {
		if (parser_accept(parser, "FLD\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(name);
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
			auto name = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(name);
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
				_str_unquote(terr_mesh->tag);
				parser_expect(parser, '\n');
			}

			parser_expect(parser, "END\n");
		} else if (parser_accept(parser, "PC2\n") || parser_accept(parser, "PLT\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(name);
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
				mu::log_warning("{}: found SUB DEADLOCKFREEAP, doesn't understand it, skip for now", parser.curr_line+1);
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
				_str_unquote(region.tag);
				parser_expect(parser, '\n');
			}

			parser_expect(parser, "END\n");

			field.regions.push_back(region);
		} else if (parser_accept(parser, "PST\n")) {
			// TODO
			mu::log_warning("{}: found PST, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "GOB\n")) {
			// TODO
			mu::log_warning("{}: found GOB, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "AOB\n")) {
			// TODO
			mu::log_warning("{}: found AOB, doesn't understand it, skip for now", parser.curr_line+1);
			parser_skip_after(parser, "END\n");
		} else if (parser_accept(parser, "SRF\n")) {
			parser_expect(parser, "FIL ");
			auto name = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(name);
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
			parser_panic(parser, "{}: found invalid type = '{}'", parser.curr_line+1, parser_token_str(parser, mu::memory::tmp()));
		}
	}

	return field;
}

Field field_from_fld_file(mu::StrView fld_file_abs_path) {
	auto parser = parser_from_file(fld_file_abs_path, mu::memory::tmp());

	auto field = _field_from_fld_str(parser);
	field.file_abs_path = mu::Str(fld_file_abs_path);
	if (field.name.size() == 0) {
		field.name += mu::file_get_base_name(fld_file_abs_path);
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

mu::Vec<Field*> field_list_recursively(Field& self, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	mu::Vec<Field*> buf(allocator);
	buf.push_back(&self);

	for (size_t i = 0; i < buf.size(); i++) {
		for (auto& f : buf[i]->subfields) {
			buf.push_back(&f);
		}
	}

	return buf;
}

struct ZLPoint {
	glm::vec3 center, color;
};

struct Sounds {
	Audio bang, blast, blast2, bombsaway, burner, damage;
	Audio extendldg, gearhorn, gun, hit, missile, notice;
	Audio retractldg, rocket, stallhorn, touchdwn, warning, engine;
	mu::Arr<Audio, 10> engines, props;

	mu::Vec<Audio*> as_array;

	~Sounds() {
		for (int i = 0; i < as_array.size(); i++) {
			audio_free(*as_array[i]);
		}
	}
};

inline static void
_sounds_load(Sounds& self, Audio& audio, mu::StrView path) {
	audio = audio_new(path);
	self.as_array.emplace_back(&audio);
}

void sounds_load(Sounds& self) {
	_sounds_load(self, self.bang,       ASSETS_DIR "/sound/bang.wav");
	_sounds_load(self, self.blast,      ASSETS_DIR "/sound/blast.wav");
	_sounds_load(self, self.blast2,     ASSETS_DIR "/sound/blast2.wav");
	_sounds_load(self, self.bombsaway,  ASSETS_DIR "/sound/bombsaway.wav");
	_sounds_load(self, self.burner,     ASSETS_DIR "/sound/burner.wav");
	_sounds_load(self, self.damage,     ASSETS_DIR "/sound/damage.wav");
	_sounds_load(self, self.extendldg,  ASSETS_DIR "/sound/extendldg.wav");
	_sounds_load(self, self.gearhorn,   ASSETS_DIR "/sound/gearhorn.wav");
	_sounds_load(self, self.gun,        ASSETS_DIR "/sound/gun.wav");
	_sounds_load(self, self.hit,        ASSETS_DIR "/sound/hit.wav");
	_sounds_load(self, self.missile,    ASSETS_DIR "/sound/missile.wav");
	_sounds_load(self, self.notice,     ASSETS_DIR "/sound/notice.wav");
	_sounds_load(self, self.retractldg, ASSETS_DIR "/sound/retractldg.wav");
	_sounds_load(self, self.rocket,     ASSETS_DIR "/sound/rocket.wav");
	_sounds_load(self, self.stallhorn,  ASSETS_DIR "/sound/stallhorn.wav");
	_sounds_load(self, self.touchdwn,   ASSETS_DIR "/sound/touchdwn.wav");
	_sounds_load(self, self.warning,    ASSETS_DIR "/sound/warning.wav");
	_sounds_load(self, self.engine,     ASSETS_DIR "/sound/engine.wav");
	for (int i = 0; i < self.engines.size(); i++) {
		_sounds_load(self, self.engines[i], mu::str_tmpf(ASSETS_DIR "/sound/engine{}.wav", i));
	}
	for (int i = 0; i < self.engines.size(); i++) {
		_sounds_load(self, self.props[i], mu::str_tmpf(ASSETS_DIR "/sound/prop{}.wav", i));
	}
}

struct ImGuiWindowLogger : public mu::ILogger {
	mu::memory::Arena _arena;
	mu::Vec<mu::Str> logs;

	bool auto_scrolling = true;
	bool wrapped = false;
	float last_scrolled_line = 0;

	virtual void log_debug(mu::StrView str) override {
		logs.emplace_back(mu::str_format(&_arena, "> {}\n", str));
		fmt::print("[debug] {}\n", str);
	}

	virtual void log_info(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[info] {}\n", str);
		fmt::vprint(stdout, formatted, {});
		logs.emplace_back(std::move(formatted));
	}

	virtual void log_warning(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[warning] {}\n", str);
		fmt::vprint(stdout, formatted, {});
		logs.emplace_back(std::move(formatted));
	}

	virtual void log_error(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[error] {}\n", str);
		fmt::vprint(stderr, formatted, {});
		logs.emplace_back(std::move(formatted));
	}
};

int main() {
	ImGuiWindowLogger imgui_window_logger {};
	mu::log_global_logger = (mu::ILogger*) &imgui_window_logger;

	test_parser();
	test_aabbs_intersection();
	test_polygons_to_triangles();

	struct AircraftFiles {
		mu::Str short_name; // a4.dat -> a4
		mu::Str dat, dnm, collision, cockpit;
		mu::Str coarse; // optional
	};
	mu::Vec<AircraftFiles> aircrafts {};
	{
		auto parser = parser_from_file(ASSETS_DIR "/aircraft/aircraft.lst");

		while (!parser_finished(parser)) {
			AircraftFiles aircraft {};

			aircraft.dat = mu::str_format(ASSETS_DIR "/{}", parser_token_str(parser, mu::memory::tmp()));
			parser_expect(parser, ' ');

			aircraft.dnm = mu::str_format(ASSETS_DIR "/{}", parser_token_str(parser, mu::memory::tmp()));
			parser_expect(parser, ' ');

			aircraft.collision = mu::str_format(ASSETS_DIR "/{}", parser_token_str(parser, mu::memory::tmp()));
			parser_expect(parser, ' ');

			aircraft.cockpit = mu::str_format(ASSETS_DIR "/{}", parser_token_str(parser, mu::memory::tmp()));

			if (parser_accept(parser, ' ')) {
				aircraft.coarse = mu::str_format(ASSETS_DIR "/{}", parser_token_str(parser, mu::memory::tmp()));
			}
			parser_expect(parser, '\n');

			while (parser_accept(parser, '\n')) { }

			auto i = aircraft.dat.find_last_of('/') + 1;
			auto j = aircraft.dat.size() - 4;
			aircraft.short_name = aircraft.dat.substr(i, j-i);

			aircrafts.push_back(aircraft);
		}
	}

	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		mu::panic(SDL_GetError());
	}
	defer(SDL_Quit());

	auto sdl_window = SDL_CreateWindow(
		WND_TITLE,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WND_INIT_WIDTH, WND_INIT_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | WND_FLAGS
	);
	if (!sdl_window) {
		mu::panic(SDL_GetError());
	}
	defer(SDL_DestroyWindow(sdl_window));
	SDL_SetWindowBordered(sdl_window, SDL_TRUE);

	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_PROFILE)) { mu::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_CONTEXT_MAJOR))  { mu::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_CONTEXT_MINOR))  { mu::panic(SDL_GetError()); }
	if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_DOUBLE_BUFFER))           { mu::panic(SDL_GetError()); }

	auto gl_context = SDL_GL_CreateContext(sdl_window);
	if (!gl_context) {
		mu::panic(SDL_GetError());
	}
	defer(SDL_GL_DeleteContext(gl_context));

	// glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		mu::panic("failed to load GLAD function pointers");
	}

	// setup audio
	AudioDevice audio_device {};
	audio_device_init(&audio_device);
	defer(audio_device_free(audio_device));

	Sounds sounds{};
	sounds_load(sounds);

	// setup imgui
	auto _imgui_ini_file_path = mu::str_format("{}/{}", mu::folder_config(mu::memory::tmp()), "open-ysf-imgui.ini");

	IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
		mu::panic("failed to create imgui context");
	}
	defer(ImGui::DestroyContext());
	ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL2_InitForOpenGL(sdl_window, gl_context)) {
		mu::panic("failed to init imgui implementation for SDL2");
	}
	defer(ImGui_ImplSDL2_Shutdown());
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
		mu::panic("failed to init imgui implementation for OpenGL3");
	}
	defer(ImGui_ImplOpenGL3_Shutdown());

	ImGui::GetIO().IniFilename = _imgui_ini_file_path.c_str();

	auto meshes_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec3 attr_position;
			layout (location = 1) in vec4 attr_color;
			// layout (location = 2) in vec3 attr_normal;

			uniform mat4 projection_view_model;

			out float vs_vertex_y;
			out vec4 vs_color;
			// out vec3 vs_normal;

			void main() {
				gl_Position = projection_view_model * vec4(attr_position, 1.0);
				vs_color = attr_color;
				// vs_normal = attr_normal;
				vs_vertex_y = attr_position.y;
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core
			in float vs_vertex_y;
			in vec4 vs_color;
			// in vec3 vs_normal;

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
	defer(gpu_program_free(meshes_gpu_program));

	// models
	mu::Vec<Model> models;
	{
		auto model = model_from_dnm_file(ASSETS_DIR "/aircraft/ys11.dnm");
		model_load_to_gpu(model);
		models.push_back(model);
	}
	defer({
		for (auto& model : models) {
			model_unload_from_gpu(model);
		}
	});

	// field
	auto field = field_from_fld_file(ASSETS_DIR "/scenery/small.fld");
	field_load_to_gpu(field);
	defer(field_unload_from_gpu(field));

	// start infos
	auto start_infos = start_info_from_stp_file(ASSETS_DIR "/scenery/small.stp");
	start_infos.insert(start_infos.begin(), StartInfo {
		.name="-NULL-"
	});

	for (int i = 0; i < models.size(); i++) {
		model_set_start(models[i], start_infos[mod(i+1, start_infos.size())]);
	}

	Camera camera {
		.model = &models[0],
		.position = start_infos[1].position
	};
	PerspectiveProjection perspective_projection {};

	bool wnd_size_changed = true;
	bool running = true;
	bool fullscreen = false;

	Uint32 time_millis = SDL_GetTicks();
	double delta_time; // seconds since previous frame

	bool should_limit_fps = true;
	int fps_limit = 60;
	int millis_till_render = 0;

	struct {
		bool smooth_lines = true;
		GLfloat line_width = 3.0f;
		GLfloat point_size = 3.0f;

		GLenum regular_primitives_type = GL_TRIANGLES;
		GLenum light_primitives_type   = GL_TRIANGLES;
		GLenum polygon_mode            = GL_FILL;
	} rendering {};

	const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = gpu_get_float(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
	const GLfloat POINT_SIZE_GRANULARITY        = gpu_get_float(GL_POINT_SIZE_GRANULARITY);

	float current_angle_max = DEGREES_MAX;

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 5.0f;
		bool on_top = true;
	} axis_rendering {};
	defer({
		glDeleteBuffers(1, &axis_rendering.vbo);
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &axis_rendering.vao);
	});
	{
		struct Stride {
			glm::vec3 vertex;
			glm::vec4 color;
		};
		const mu::Vec<Stride> buffer {
			Stride {{0, 0, 0}, {1, 0, 0, 1}}, // X
			Stride {{1, 0, 0}, {1, 0, 0, 1}},
			Stride {{0, 0, 0}, {0, 1, 0, 1}}, // Y
			Stride {{0, 1, 0}, {0, 1, 0, 1}},
			Stride {{0, 0, 0}, {0, 0, 1, 1}}, // Z
			Stride {{0, 0, 1}, {0, 0, 1, 1}},
		};
		axis_rendering.points_count = buffer.size();

		glGenVertexArrays(1, &axis_rendering.vao);
		glBindVertexArray(axis_rendering.vao);
			glGenBuffers(1, &axis_rendering.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, axis_rendering.vbo);
			glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(Stride), buffer.data(), GL_STATIC_DRAW);

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

	struct {
		bool enabled = true;
		glm::vec2 position {-0.9f, -0.8f};
		float scale = 0.48f;
	} world_axis;

	auto lines_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec3 attr_position;
			uniform mat4 projection_view_model;
			void main() {
				gl_Position = projection_view_model * vec4(attr_position, 1.0);
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
	defer(gpu_program_free(lines_gpu_program));

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 1.0f;
	} box_rendering;
	defer({
		glDeleteBuffers(1, &box_rendering.vbo);
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &box_rendering.vao);
	});
	{
		const mu::Vec<glm::vec3> buffer {
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
		};
		box_rendering.points_count = buffer.size();

		glGenVertexArrays(1, &box_rendering.vao);
		glBindVertexArray(box_rendering.vao);
			glGenBuffers(1, &box_rendering.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, box_rendering.vbo);
			glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(glm::vec3), buffer.data(), GL_STATIC_DRAW);

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

	auto picture2d_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			layout (location = 0) in vec2 attr_position;

			uniform mat4 projection_view_model;

			out float vs_vertex_id;

			void main() {
				gl_Position = projection_view_model * vec4(attr_position.x, 0.0, attr_position.y, 1.0);
				vs_vertex_id = gl_VertexID % 6;
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core

			in float vs_vertex_id;

			uniform vec3 primitive_color[2];
			uniform bool gradation_enabled;
			uniform sampler2D groundtile;

			out vec4 out_fragcolor;

			const int color_indices[6] = int[] (
				0, 1, 1,
				0, 0, 1
			);

			const vec2 tex_coords[3] = vec2[] (
				vec2(0, 0), vec2(1, 0), vec2(1, 1)
			);

			void main() {
				int color_index = 0;
				if (gradation_enabled) {
					color_index = color_indices[int(vs_vertex_id)];
				}
				out_fragcolor = texture(groundtile, tex_coords[int(vs_vertex_id) % 3]).r * vec4(primitive_color[color_index], 1.0);
			}
		)GLSL"
	);
	defer(gpu_program_free(picture2d_gpu_program));

	// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
	auto ground_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			uniform mat4 proj_inv_view_inv;

			out vec3 vs_near_point;
			out vec3 vs_far_point;

			// grid position are in clipped space
			const vec2 grid_plane[6] = vec2[] (
				vec2(1, 1), vec2(-1, 1), vec2(-1, -1),
				vec2(-1, -1), vec2(1, -1), vec2(1, 1)
			);

			vec3 unproject_point(float x, float y, float z) {
				vec4 unprojectedPoint = proj_inv_view_inv * vec4(x, y, z, 1.0);
				return unprojectedPoint.xyz / unprojectedPoint.w;
			}

			void main() {
				vec2 p        = grid_plane[gl_VertexID];

				vs_near_point = unproject_point(p.x, p.y, 0.0); // unprojecting on the near plane
				vs_far_point  = unproject_point(p.x, p.y, 1.0); // unprojecting on the far plane
				gl_Position   = vec4(p.x, p.y, 0.0, 1.0);       // using directly the clipped coordinates
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core
			in vec3 vs_near_point;
			in vec3 vs_far_point;

			out vec4 out_fragcolor;

			uniform vec3 color;
			uniform sampler2D groundtile;
			uniform mat4 projection;

			void main() {
				float t = -vs_near_point.y / (vs_far_point.y - vs_near_point.y);
				if (t <= 0) {
					discard;
				} else {
					vec3 frag_pos_3d = vs_near_point + t * (vs_far_point - vs_near_point);
					vec4 clip_space_pos = projection * vec4(frag_pos_3d, 1.0);

					out_fragcolor = vec4(texture(groundtile, clip_space_pos.xz / 600).x * color, 1.0);
				}
			}
		)GLSL"
	);
	defer(gpu_program_free(ground_gpu_program));

	// opengl can't call shader without VAO even if shader doesn't take input
	// dummy_vao lets you call shader without input (useful when coords is embedded in shader)
	GLuint dummy_vao;
	glGenVertexArrays(1, &dummy_vao);
	defer({
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &dummy_vao);
	});

	// groundtile
	SDL_Surface* groundtile = IMG_Load(ASSETS_DIR "/misc/groundtile.png");
	if (groundtile == nullptr || groundtile->pixels == nullptr) {
		mu::panic("failed to load groundtile.png");
	}
	defer(SDL_FreeSurface(groundtile));

	GLuint groundtile_texture;
	glGenTextures(1, &groundtile_texture);
	defer({
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &groundtile_texture);
	});
	glBindTexture(GL_TEXTURE_2D, groundtile_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, groundtile->w, groundtile->h, 0, GL_RED, GL_UNSIGNED_BYTE, groundtile->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	auto sprite_gpu_program = gpu_program_new(
		// vertex shader
		R"GLSL(
			#version 330 core
			uniform mat4 projection_view_model;

			out vec2 vs_tex_coord;

			const vec2 quad[6] = vec2[] (
				vec2(1, 1), vec2(-1, 1), vec2(-1, -1),
				vec2(-1, -1), vec2(1, -1), vec2(1, 1)
			);

			const vec2 tex_coords[6] = vec2[] (
				vec2(1, 1), vec2(0, 1), vec2(0, 0),
				vec2(0, 0), vec2(1, 0), vec2(1, 1)
			);

			void main() {
				gl_Position = projection_view_model * vec4(quad[gl_VertexID], 0, 1);
				vs_tex_coord = tex_coords[gl_VertexID];
			}
		)GLSL",

		// fragment shader
		R"GLSL(
			#version 330 core
			in vec2 vs_tex_coord;

			out vec4 out_fragcolor;

			uniform sampler2D quad_texture;
			uniform vec3 color;

			void main() {
				// out_fragcolor = vec4(1, 0, 0, 1);
				out_fragcolor = texture(quad_texture, vs_tex_coord).r * vec4(color, 1);
			}
		)GLSL"
	);
	defer(gpu_program_free(sprite_gpu_program));

	// zl_sprite
	SDL_Surface* zl_sprite = IMG_Load(ASSETS_DIR "/misc/rwlight.png");
	if (zl_sprite == nullptr || zl_sprite->pixels == nullptr) {
		mu::panic("failed to load rwlight.png");
	}
	defer(SDL_FreeSurface(zl_sprite));

	GLuint zl_sprite_texture;
	glGenTextures(1, &zl_sprite_texture);
	defer({
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &zl_sprite_texture);
	});
	glBindTexture(GL_TEXTURE_2D, zl_sprite_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, zl_sprite->w, zl_sprite->h, 0, GL_RED, GL_UNSIGNED_INT, zl_sprite->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// text
	/// all state of loaded glyph using FreeType
	// https://learnopengl.com/img/in-practice/glyph_offset.png
	struct Glyph {
		GLuint texture;
		glm::ivec2 size;
		// Offset from baseline to left/top of glyph
		glm::ivec2 bearing;
		// horizontal offset to advance to next glyph
		uint32_t advance;
	};
	struct {
		GPU_Program program;
		GLuint vao, vbo;
		mu::Arr<Glyph, 128> glyphs;
	} text_rendering {};
	defer({
		gpu_program_free(text_rendering.program);
		glDeleteBuffers(1, &text_rendering.vbo);
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &text_rendering.vao);
		for (auto& g : text_rendering.glyphs) {
			glBindTexture(GL_TEXTURE_2D, 0);
			glDeleteTextures(1, &g.texture);
		}
	});
	{
		text_rendering.program = gpu_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
				out vec2 vs_tex_coord;

				uniform mat4 projection;

				void main() {
					gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
					vs_tex_coord = vertex.zw;
				}
			)GLSL",
			// fragment shader
			R"GLSL(
				#version 330 core
				in vec2 vs_tex_coord;
				out vec4 color;

				uniform sampler2D text_texture;
				uniform vec3 text_color;

				void main() {
					vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text_texture, vs_tex_coord).r);
					color = vec4(text_color, 1.0) * sampled;
				}
			)GLSL"
		);

		// texture quads
		glGenVertexArrays(1, &text_rendering.vao);
		glGenBuffers(1, &text_rendering.vbo);
		glBindVertexArray(text_rendering.vao);
			glBindBuffer(GL_ARRAY_BUFFER, text_rendering.vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		// disable byte-alignment restriction
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		// freetype
		FT_Library ft;
		if (FT_Init_FreeType(&ft)) {
			mu::panic("could not init FreeType Library");
		}
		defer(FT_Done_FreeType(ft));

		FT_Face face;
		if (FT_New_Face(ft, ASSETS_DIR "/fonts/zig.ttf", 0, &face)) {
			mu::panic("failed to load font");
		}
		defer(FT_Done_Face(face));

		uint16_t face_height = 48;
		uint16_t face_width = 0; // auto
		if (FT_Set_Pixel_Sizes(face, face_width, face_height)) {
			mu::panic("failed to set pixel size of font face");
		}

		if (FT_Load_Char(face, 'X', FT_LOAD_RENDER)) {
			mu::panic("failed to load glyph");
		}

		// generate textures
		for (uint8_t c = 0; c < text_rendering.glyphs.size(); c++) {
			if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
				mu::panic("failed to load glyph");
			}

			GLuint text_texture;
			glGenTextures(1, &text_texture);
			glBindTexture(GL_TEXTURE_2D, text_texture);
			glTexImage2D(
				GL_TEXTURE_2D,
				0,
				GL_RED,
				face->glyph->bitmap.width,
				face->glyph->bitmap.rows,
				0,
				GL_RED,
				GL_UNSIGNED_BYTE,
				face->glyph->bitmap.buffer
			);

			// texture options
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			text_rendering.glyphs[c] = Glyph {
				.texture = text_texture,
				.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
				.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
				.advance = uint32_t(face->glyph->advance.x)
			};
		}

		gpu_check_errors();
	}

	struct TextRenderReq {
		mu::Str text;
		float x, y, scale;
		glm::vec3 color;
	};

	while (running) {
		mu::memory::reset_tmp();

		mu::Vec<mu::Str> overlay_text(mu::memory::tmp());

		mu::Vec<TextRenderReq> text_render_list(mu::memory::tmp());
		text_render_list.emplace_back(TextRenderReq {
			.text = mu::str_tmpf("Hello OpenYSF"),
			.x = 25.0f,
			.y = 25.0f,
			.scale = 1.0f,
			.color = {0.5, 0.8f, 0.2f}
		});

		// time
		{
			Uint32 delta_time_millis = SDL_GetTicks() - time_millis;
			time_millis += delta_time_millis;

			if (should_limit_fps) {
				int millis_diff = (1000 / fps_limit) - delta_time_millis;
				millis_till_render = clamp(millis_till_render - millis_diff, 0, 1000);
				if (millis_till_render > 0) {
					SDL_Delay(2);
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
		overlay_text.emplace_back(mu::str_tmpf("fps: {:.2f}", 1.0f/delta_time));

		camera_update(camera, delta_time);

		SDL_Event event;
		bool pressed_tab = false;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					running = false;
					break;
				case SDLK_TAB:
					pressed_tab = true;
					break;
				case 'f':
					fullscreen = !fullscreen;
					wnd_size_changed = true;
					if (fullscreen) {
						if (SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
							mu::panic(SDL_GetError());
						}
					} else {
						if (SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_OPENGL)) {
							mu::panic(SDL_GetError());
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

			if (!perspective_projection.custom_aspect) {
				perspective_projection.aspect = (float) w / h;
			}
		}

		if (field.should_select_file) {
			field.should_select_file = false;

			auto result = pfd::open_file("Select FLD", "", {"FLD Files", "*.fld", "All Files", "*"}).result();
			if (result.size() == 1) {
				field.file_abs_path = result[0];
				mu::log_debug("loading '{}'", field.file_abs_path);
				field.should_load_file = true;
			}
		}
		if (field.should_load_file) {
			field.should_load_file = false;

			Field new_field {};
			if (field.file_abs_path.empty() == false) {
				new_field = field_from_fld_file(field.file_abs_path);
				field_load_to_gpu(new_field);
			}

			field_unload_from_gpu(field);
			field = new_field;
		}

		for (int i = 0; i < models.size(); i++) {
			if (models[i].should_load_file) {
				auto model = model_from_dnm_file(models[i].file_abs_path);
				model_load_to_gpu(model);

				if (models[i].engine_sound) {
					audio_device_stop(audio_device, *models[i].engine_sound);
				}
				model_unload_from_gpu(models[i]);

				model.control = models[i].control;
				model.current_state = models[i].current_state;
				models[i] = model;

				mu::log_debug("loaded '{}'", models[i].file_abs_path);
				models[i].should_load_file = false;
			}

			if (models[i].should_be_removed) {
				int tracked_model_index = -1;
				for (int i = 0; i < models.size(); i++) {
					if (camera.model == &models[i]) {
						tracked_model_index = i;
						break;
					}
				}

				models.erase(models.begin()+i);

				if (tracked_model_index > 0 && tracked_model_index >= i) {
					camera.model = &models[tracked_model_index-1];
				} else if (tracked_model_index == 0 && i == 0) {
					camera.model = models.empty()? nullptr : &models[0];
				}

				i--;
			}
		}

		// control currently tracked model
		if (camera.model) {
			float delta_yaw = 0, delta_roll = 0, delta_pitch = 0;
			const Uint8* key_pressed = SDL_GetKeyboardState(nullptr);
			constexpr auto ROTATE_SPEED = 12.0f / DEGREES_MAX * RADIANS_MAX;
			if (key_pressed[SDL_SCANCODE_DOWN]) {
				delta_pitch -= ROTATE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_UP]) {
				delta_pitch += ROTATE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_LEFT]) {
				delta_roll -= ROTATE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_RIGHT]) {
				delta_roll += ROTATE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_C]) {
				delta_yaw -= ROTATE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_Z]) {
				delta_yaw += ROTATE_SPEED * delta_time;
			}
			camera.model->current_state.angles.rotate(delta_yaw, delta_pitch, delta_roll);

			if (pressed_tab) {
				camera.model->control.afterburner_reheat_enabled = ! camera.model->control.afterburner_reheat_enabled;
			}
			if (camera.model->control.afterburner_reheat_enabled && camera.model->control.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
				camera.model->control.throttle = AFTERBURNER_THROTTLE_THRESHOLD;
			}

			if (key_pressed[SDL_SCANCODE_Q]) {
				camera.model->control.throttle += THROTTLE_SPEED * delta_time;
			}
			if (key_pressed[SDL_SCANCODE_A]) {
				camera.model->control.throttle -= THROTTLE_SPEED * delta_time;
			}

			// only currently controlled model has audio
			int audio_index = camera.model->control.throttle * (sounds.props.size()-1);

			Audio* audio;
			if (camera.model->has_propellers) {
				audio = &sounds.props[audio_index];
			} else if (camera.model->control.afterburner_reheat_enabled && camera.model->has_afterburner) {
				audio = &sounds.burner;
			} else {
				audio = &sounds.engines[audio_index];
			}

			if (camera.model->engine_sound != audio) {
				if (camera.model->engine_sound) {
					audio_device_stop(audio_device, *camera.model->engine_sound);
				}
				camera.model->engine_sound = audio;
				audio_device_play_looped(audio_device, *camera.model->engine_sound);
			}
		}

		// update models
		for (int i = 0; i < models.size(); i++) {
			Model& model = models[i];

			if (!model.current_state.visible) {
				continue;
			}

			model.anti_coll_lights.time_left_secs -= delta_time;
			if (model.anti_coll_lights.time_left_secs < 0) {
				model.anti_coll_lights.time_left_secs = ANTI_COLL_LIGHT_PERIOD;
				model.anti_coll_lights.visible = ! model.anti_coll_lights.visible;
			}

			// apply model transformation
			const auto model_transformation = model.current_state.angles.matrix(model.current_state.translation);

			model.control.throttle = clamp(model.control.throttle, 0.0f, 1.0f);
			model.current_state.speed = model.control.throttle * MAX_SPEED + MIN_SPEED;
			if (model.control.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
				model.control.afterburner_reheat_enabled = false;
			}

			model.current_state.translation += ((float)delta_time * model.current_state.speed) * model.current_state.angles.front;

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
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (const auto& name : model.root_meshes_names) {
				auto& mesh = model.meshes.at(name);
				mesh.transformation = model_transformation;
				meshes_stack.push_back(&mesh);
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				if (mesh->current_state.visible == false) {
					continue;
				}

				if (mesh->animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER) {
					mesh->current_state.rotation.x += model.control.throttle * PROPOLLER_MAX_ANGLE_SPEED * delta_time;
				}
				if (mesh->animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z) {
					mesh->current_state.rotation.z += model.control.throttle * PROPOLLER_MAX_ANGLE_SPEED * delta_time;
				}

				if (mesh->animation_type == AnimationClass::AIRCRAFT_LANDING_GEAR && mesh->animation_states.size() > 1) {
					// ignore 3rd STA, it should always be 0 (TODO are they always 0??)
					const MeshState& state_up   = mesh->animation_states[0];
					const MeshState& state_down = mesh->animation_states[1];
					const auto& alpha = model.control.landing_gear_alpha;

					mesh->current_state.translation = mesh->initial_state.translation + state_down.translation * (1-alpha) +  state_up.translation * alpha;
					mesh->current_state.rotation = glm::eulerAngles(glm::slerp(glm::quat(mesh->initial_state.rotation), glm::quat(state_up.rotation), alpha));// ???

					float visibilty = (float) state_down.visible * (1-alpha) + (float) state_up.visible * alpha;
					mesh->current_state.visible = visibilty > 0.05;;
				}

				// apply mesh transformation
				mesh->transformation = glm::translate(mesh->transformation, mesh->current_state.translation);
				mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[2], glm::vec3{0, 0, 1});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[1], glm::vec3{1, 0, 0});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->current_state.rotation[0], glm::vec3{0, -1, 0});

				// push children
				for (const mu::Str& child_name : mesh->children) {
					auto& child_mesh = model.meshes.at(child_name);
					child_mesh.transformation = mesh->transformation;
					meshes_stack.push_back(&child_mesh);
				}
			}
		}

		mu::Vec<glm::mat4> axis_instances(mu::memory::tmp());

		// update fields
		const auto all_fields = field_list_recursively(field, mu::memory::tmp());
		if (field.should_transform) {
			field.should_transform = false;

			// transform fields
			field.transformation = glm::identity<glm::mat4>();
			for (Field* fld : all_fields) {
				if (fld->current_state.visible == false) {
					continue;
				}

				fld->transformation = glm::translate(fld->transformation, fld->current_state.translation);
				fld->transformation = glm::rotate(fld->transformation, fld->current_state.rotation[2], glm::vec3{0, 0, 1});
				fld->transformation = glm::rotate(fld->transformation, fld->current_state.rotation[1], glm::vec3{1, 0, 0});
				fld->transformation = glm::rotate(fld->transformation, fld->current_state.rotation[0], glm::vec3{0, 1, 0});

				for (auto& subfield : fld->subfields) {
					subfield.transformation = fld->transformation;
				}

				for (auto& mesh : fld->meshes) {
					if (mesh.render_cnt_axis) {
						axis_instances.push_back(glm::translate(glm::identity<glm::mat4>(), mesh.cnt));
					}

					// apply mesh transformation
					mesh.transformation = fld->transformation;
					mesh.transformation = glm::translate(mesh.transformation, mesh.current_state.translation);
					mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[2], glm::vec3{0, 0, 1});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[1], glm::vec3{1, 0, 0});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.current_state.rotation[0], glm::vec3{0, 1, 0});

					if (mesh.render_pos_axis) {
						axis_instances.push_back(mesh.transformation);
					}
				}
			}
		}

		// test intersection
		struct Box { glm::vec3 translation, scale, color; };
		mu::Vec<Box> box_instances(mu::memory::tmp());
		if (models.size() > 0) {
			for (int i = 0; i < models.size()-1; i++) {
				if (models[i].current_state.visible == false) {
					overlay_text.emplace_back(mu::str_tmpf("model[{}] invisible and won't intersect", i));
					continue;
				}

				glm::vec3 i_color {0, 0, 1};

				for (int j = i+1; j < models.size(); j++) {
					if (models[j].current_state.visible == false) {
						overlay_text.emplace_back(mu::str_tmpf("model[{}] invisible and won't intersect", j));
						continue;
					}

					glm::vec3 j_color {0, 0, 1};

					if (aabbs_intersect(models[i].current_aabb, models[j].current_aabb)) {
						overlay_text.emplace_back(mu::str_tmpf("model[{}] intersects model[{}]", i, j));
						j_color = i_color = {1, 0, 0};
					} else {
						overlay_text.emplace_back(mu::str_tmpf("model[{}] doesn't intersect model[{}]", i, j));
					}

					if (models[j].render_aabb) {
						auto aabb = models[j].current_aabb;
						box_instances.push_back(Box {
							.translation = aabb.min,
							.scale = aabb.max - aabb.min,
							.color = j_color,
						});
					}
				}

				if (models[i].render_aabb) {
					auto aabb = models[i].current_aabb;
					box_instances.push_back(Box {
						.translation = aabb.min,
						.scale = aabb.max - aabb.min,
						.color = i_color,
					});
				}
			}
		}

		// view+projec matrices
		const auto view_mat = camera_calc_view(camera);
		const auto projection_mat = glm::perspective(
			perspective_projection.fovy,
			perspective_projection.aspect,
			perspective_projection.near,
			perspective_projection.far
		);

		const auto view_inverse_mat = glm::inverse(view_mat);
		const auto projection_inverse_mat = glm::inverse(projection_mat);
		const auto projection_view_mat = projection_mat * view_mat;
		const auto proj_inv_view_inv_mat = view_inverse_mat * projection_inverse_mat;

		glEnable(GL_DEPTH_TEST);
		glClearDepth(1);
		glClearColor(field.sky_color.x, field.sky_color.y, field.sky_color.z, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (rendering.smooth_lines) {
			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(rendering.line_width);
            #endif
		} else {
			glDisable(GL_LINE_SMOOTH);
		}
		glPointSize(rendering.point_size);
		glPolygonMode(GL_FRONT_AND_BACK, rendering.polygon_mode);

		// render last ground
		glDisable(GL_DEPTH_TEST);
		glUseProgram(ground_gpu_program);
		glUniformMatrix4fv(glGetUniformLocation(ground_gpu_program, "proj_inv_view_inv"), 1, false, glm::value_ptr(proj_inv_view_inv_mat));
		glUniformMatrix4fv(glGetUniformLocation(ground_gpu_program, "projection"), 1, false, glm::value_ptr(projection_mat));

		glBindTexture(GL_TEXTURE_2D, groundtile_texture);
		glBindVertexArray(dummy_vao);
		glUniform3fv(glGetUniformLocation(ground_gpu_program, "color"), 1, glm::value_ptr(all_fields[all_fields.size()-1]->ground_color));
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// render fields pictures
		glUseProgram(picture2d_gpu_program);
		for (const Field* fld : all_fields) {
			for (auto& picture : fld->pictures) {
				if (picture.current_state.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, picture.current_state.translation);
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.current_state.rotation[0], glm::vec3{0, 1, 0});
				glUniformMatrix4fv(glGetUniformLocation(picture2d_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * model_transformation));

				for (auto& primitive : picture.primitives) {
					glUniform3fv(glGetUniformLocation(picture2d_gpu_program, "primitive_color[0]"), 1, glm::value_ptr(primitive.color));

					const bool gradation_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS;
					glUniform1i(glGetUniformLocation(picture2d_gpu_program, "gradation_enabled"), (GLint) gradation_enabled);
					if (gradation_enabled) {
						glUniform3fv(glGetUniformLocation(picture2d_gpu_program, "primitive_color[1]"), 1, glm::value_ptr(primitive.color2));
					}

					glBindVertexArray(primitive.gpu.vao);
					glDrawArrays(primitive.gpu.primitive_type, 0, primitive.gpu.array_count);
				}
			}
		}
		glEnable(GL_DEPTH_TEST);

		// render fields terrains
		glUseProgram(meshes_gpu_program);
		glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), (GLint) false);
		for (const Field* fld : all_fields) {
			if (fld->current_state.visible == false) {
				continue;
			}

			for (const auto& terr_mesh : fld->terr_meshes) {
				if (terr_mesh.current_state.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.current_state.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.current_state.rotation[0], glm::vec3{0, 1, 0});
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * model_transformation));

				glUniform1i(glGetUniformLocation(meshes_gpu_program, "gradient_enabled"), (GLint) terr_mesh.gradiant.enabled);
				if (terr_mesh.gradiant.enabled) {
					glUniform1f(glGetUniformLocation(meshes_gpu_program, "gradient_bottom_y"), terr_mesh.gradiant.bottom_y);
					glUniform1f(glGetUniformLocation(meshes_gpu_program, "gradient_top_y"), terr_mesh.gradiant.top_y);
					glUniform3fv(glGetUniformLocation(meshes_gpu_program, "gradient_bottom_color"), 1, glm::value_ptr(terr_mesh.gradiant.bottom_color));
					glUniform3fv(glGetUniformLocation(meshes_gpu_program, "gradient_top_color"), 1, glm::value_ptr(terr_mesh.gradiant.top_color));
				}

				glBindVertexArray(terr_mesh.gpu.vao);
				glDrawArrays(rendering.regular_primitives_type, 0, terr_mesh.gpu.array_count);
			}
		}
		glUniform1i(glGetUniformLocation(meshes_gpu_program, "gradient_enabled"), (GLint) false);

		// render fields meshes
		for (const Field* fld : all_fields) {
			if (fld->current_state.visible == false) {
				continue;
			}

			for (const auto& mesh : fld->meshes) {
				if (mesh.current_state.visible == false) {
					continue;
				}

				// upload transofmation model
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * mesh.transformation));
				glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), (GLint) mesh.is_light_source);

				glBindVertexArray(mesh.gpu.vao);
				glDrawArrays(mesh.is_light_source? rendering.light_primitives_type : rendering.regular_primitives_type, 0, mesh.gpu.array_count);
			}
		}

		mu::Vec<ZLPoint> zlpoints(mu::memory::tmp());

		// render models
		for (int i = 0; i < models.size(); i++) {
			Model& model = models[i];

			if (!model.current_state.visible) {
				continue;
			}

			const auto model_transformation = model.current_state.angles.matrix(model.current_state.translation);

			// start with root meshes
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (const auto& name : model.root_meshes_names) {
				meshes_stack.push_back(&model.meshes.at(name));
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				const bool enable_high_throttle = almost_equal(model.control.throttle, 1.0f);
				if (mesh->animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE && enable_high_throttle == false) {
					continue;
				}
				if (mesh->animation_type == AnimationClass::AIRCRAFT_LOW_THROTTLE && enable_high_throttle && model.has_high_throttle_mesh) {
					continue;
				}

				if (mesh->animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
					if (model.control.afterburner_reheat_enabled == false) {
						continue;
					}

					if (model.control.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
						continue;
					}
				}

				if (!mesh->current_state.visible) {
					continue;
				}

				if (mesh->render_cnt_axis) {
					axis_instances.push_back(glm::translate(glm::identity<glm::mat4>(), mesh->cnt));
				}

				if (mesh->render_pos_axis) {
					axis_instances.push_back(mesh->transformation);
				}

				// upload transofmation model
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * mesh->transformation));

				glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), (GLint) mesh->is_light_source);

				glBindVertexArray(mesh->gpu.vao);
				glDrawArrays(mesh->is_light_source? rendering.light_primitives_type : rendering.regular_primitives_type, 0, mesh->gpu.array_count);

				// ZL
				if (mesh->animation_type != AnimationClass::AIRCRAFT_ANTI_COLLISION_LIGHTS || model.anti_coll_lights.visible) {
					for (size_t zlid : mesh->zls) {
						Face& face = mesh->faces[zlid];
						zlpoints.push_back(ZLPoint {
							.center = model_transformation * glm::vec4(face.center, 1.0f),
							.color = face.color
						});
					}
				}

				// push children
				for (const mu::Str& child_name : mesh->children) {
					meshes_stack.push_back(&model.meshes.at(child_name));
				}
			}
		}

		if (zlpoints.empty() == false) {
			auto model_transformation = glm::mat4(glm::mat3(view_inverse_mat)) * glm::scale(glm::vec3{ZL_SCALE, ZL_SCALE, 0});

			glUseProgram(sprite_gpu_program);
			glBindTexture(GL_TEXTURE_2D, zl_sprite_texture);
			glBindVertexArray(dummy_vao);

			for (const auto& zlpoint : zlpoints) {
				model_transformation[3] = glm::vec4{zlpoint.center.x, zlpoint.center.y, zlpoint.center.z, 1.0f};
				glUniform3fv(glGetUniformLocation(sprite_gpu_program, "color"), 1, glm::value_ptr(zlpoint.color));
				glUniformMatrix4fv(glGetUniformLocation(sprite_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * model_transformation));
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}
		}

		glUseProgram(meshes_gpu_program);

		// render axis
		if (axis_instances.empty() == false) {

			if (axis_rendering.on_top) {
				glDisable(GL_DEPTH_TEST);
			} else {
				glEnable(GL_DEPTH_TEST);
			}

			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(axis_rendering.line_width);
            #endif
			glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), 0);
			glBindVertexArray(axis_rendering.vao);
			for (const auto& transformation : axis_instances) {
				glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_mat * transformation));
				glDrawArrays(GL_LINES, 0, axis_rendering.points_count);
			}

			glEnable(GL_DEPTH_TEST);
		}

		if (world_axis.enabled) {
			glDisable(GL_DEPTH_TEST);

			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(axis_rendering.line_width);
            #endif

			glUniform1i(glGetUniformLocation(meshes_gpu_program, "is_light_source"), 0);
			glBindVertexArray(axis_rendering.vao);

			auto new_view_mat = view_mat;
			new_view_mat[3] = glm::vec4{0, 0, ((1 - world_axis.scale) * -39) - 1, 1};
			auto trans = glm::translate(glm::identity<glm::mat4>(), glm::vec3{world_axis.position.x, world_axis.position.y, 0});

			glUniformMatrix4fv(glGetUniformLocation(meshes_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(trans * projection_mat * new_view_mat));
			glDrawArrays(GL_LINES, 0, axis_rendering.points_count);

			glEnable(GL_DEPTH_TEST);
		}

		// render boxes
		if (box_instances.empty() == false) {
			glUseProgram(lines_gpu_program);
			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(box_rendering.line_width);
            #endif
			glBindVertexArray(box_rendering.vao);

			for (const auto& box : box_instances) {
				auto transformation = glm::translate(glm::identity<glm::mat4>(), box.translation);
				transformation = glm::scale(transformation, box.scale);
				const auto projection_view_model = projection_view_mat * transformation;
				glUniformMatrix4fv(glGetUniformLocation(lines_gpu_program, "projection_view_model"), 1, false, glm::value_ptr(projection_view_model));

				glUniform3fv(glGetUniformLocation(lines_gpu_program, "color"), 1, glm::value_ptr(box.color));

				glDrawArrays(GL_LINE_LOOP, 0, box_rendering.points_count);
			}
		}

		for (auto& txt_rndr : text_render_list) {
			int wnd_width, wnd_height;
			SDL_GetWindowSize(sdl_window, &wnd_width, &wnd_height);
			glm::mat4 projection = glm::ortho(0.0f, float(wnd_width), 0.0f, float(wnd_height));

			// activate corresponding render state
			glUseProgram(text_rendering.program);
			glUniformMatrix4fv(glGetUniformLocation(text_rendering.program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
			glUniform3f(glGetUniformLocation(text_rendering.program, "text_color"), txt_rndr.color.x, txt_rndr.color.y, txt_rndr.color.z);
			glActiveTexture(GL_TEXTURE0);
			glBindVertexArray(text_rendering.vao);

			for (char c : txt_rndr.text) {
				if (c >= text_rendering.glyphs.size()) {
					c = '?';
				}
				const Glyph& glyph = text_rendering.glyphs[c];

				// update text_rendering.vbo content
				float xpos = txt_rndr.x + glyph.bearing.x * txt_rndr.scale;
				float ypos = txt_rndr.y - (glyph.size.y - glyph.bearing.y) * txt_rndr.scale;
				float w = glyph.size.x * txt_rndr.scale;
				float h = glyph.size.y * txt_rndr.scale;
				float vertices[6][4] = {
					{ xpos,     ypos + h,   0.0f, 0.0f },
					{ xpos,     ypos,       0.0f, 1.0f },
					{ xpos + w, ypos,       1.0f, 1.0f },

					{ xpos,     ypos + h,   0.0f, 0.0f },
					{ xpos + w, ypos,       1.0f, 1.0f },
					{ xpos + w, ypos + h,   1.0f, 0.0f }
				};
				glBindBuffer(GL_ARRAY_BUFFER, text_rendering.vbo);
					glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
				glBindBuffer(GL_ARRAY_BUFFER, 0);

				// render glyph texture over quad
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
				// bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
				txt_rndr.x += (glyph.advance >> 6) * txt_rndr.scale;
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

				MyImGui::EnumsCombo("Angle Max", &current_angle_max, {
					{DEGREES_MAX, "DEGREES_MAX"},
					{RADIANS_MAX, "RADIANS_MAX"},
					{YS_MAX,      "YS_MAX"},
				});

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Projection")) {
				if (ImGui::Button("Reset")) {
					perspective_projection = {};
					wnd_size_changed = true;
				}

				ImGui::InputFloat("near", &perspective_projection.near, 1, 10);
				ImGui::InputFloat("far", &perspective_projection.far, 1, 10);
				ImGui::DragFloat("fovy (1/zoom)", &perspective_projection.fovy, 1, 1, 45);

				if (ImGui::Checkbox("custom aspect", &perspective_projection.custom_aspect) && !perspective_projection.custom_aspect) {
					wnd_size_changed = true;
				}
				ImGui::BeginDisabled(!perspective_projection.custom_aspect);
					ImGui::InputFloat("aspect", &perspective_projection.aspect, 1, 10);
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Camera")) {
				if (ImGui::Button("Reset")) {
					camera = { .model=camera.model };
				}

				if (camera.model) {
					int tracked_model_index = 0;
					for (int i = 0; i < models.size(); i++) {
						if (camera.model == &models[i]) {
							tracked_model_index = i;
							break;
						}
					}
					if (ImGui::BeginCombo("Tracked Model", mu::str_tmpf("Model[{}]", tracked_model_index).c_str())) {
						for (size_t j = 0; j < models.size(); j++) {
							if (ImGui::Selectable(mu::str_tmpf("Model[{}]", j).c_str(), j == tracked_model_index)) {
								camera.model = &models[j];
							}
						}

						ImGui::EndCombo();
					}

					ImGui::DragFloat("distance", &camera.distance_from_model, 1, 0);

					ImGui::Checkbox("Rotate Around", &camera.enable_rotating_around);
				} else {
					static size_t start_info_index = 0;
					if (ImGui::BeginCombo("Start Pos", start_infos[start_info_index].name.c_str())) {
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								camera.position = start_infos[j].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::DragFloat("movement_speed", &camera.movement_speed, 5, 50, 1000);
					ImGui::DragFloat("mouse_sensitivity", &camera.mouse_sensitivity, 1, 0.5, 10);
					ImGui::DragFloat3("world_up", glm::value_ptr(camera.world_up), 1, -100, 100);
					ImGui::DragFloat3("front", glm::value_ptr(camera.front), 0.1, -1, 1);
					ImGui::DragFloat3("right", glm::value_ptr(camera.right), 1, -100, 100);
					ImGui::DragFloat3("up", glm::value_ptr(camera.up), 1, -100, 100);

				}

				ImGui::SliderAngle("yaw", &camera.yaw, -89, 89);
				ImGui::SliderAngle("pitch", &camera.pitch, -179, 179);

				ImGui::DragFloat3("position", glm::value_ptr(camera.position), 1, -100, 100);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Rendering")) {
				if (ImGui::Button("Reset")) {
					rendering = {};
				}

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
                #ifndef OS_MACOS
				ImGui::BeginDisabled(!rendering.smooth_lines);
					ImGui::DragFloat("Line Width", &rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::EndDisabled();
                #endif

				ImGui::DragFloat("Point Size", &rendering.point_size, POINT_SIZE_GRANULARITY, 0.5, 100);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Axis Rendering")) {
				ImGui::Checkbox("On Top", &axis_rendering.on_top);
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &axis_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif

				ImGui::BulletText("World Axis:");
				if (ImGui::Button("Reset")) {
					world_axis = {};
				}
				ImGui::Checkbox("Enabled", &world_axis.enabled);
				ImGui::DragFloat2("Position", glm::value_ptr(world_axis.position), 0.05, -1, 1);
				ImGui::DragFloat("Scale", &world_axis.scale, .05, 0, 1);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("AABB Rendering")) {
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &box_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Audio")) {
				for (int i = 0; i < sounds.as_array.size(); i++) {
					ImGui::PushID(i);

					const Audio& sound = *sounds.as_array[i];

					if (ImGui::Button("Play")) {
						audio_device_play(audio_device, sound);
					}

					ImGui::SameLine();
					if (ImGui::Button("Loop")) {
						audio_device_play_looped(audio_device, sound);
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(audio_device_is_playing(audio_device, sound) == false);
					if (ImGui::Button("Stop")) {
						audio_device_stop(audio_device, sound);
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					ImGui::Text(mu::Str(mu::file_get_base_name(sound.file_path), mu::memory::tmp()).c_str());

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Aircrafts {}:", models.size()).c_str());

			{
				const bool should_add_aircraft = ImGui::Button("Add");
				static size_t aircraft_to_add = 0;
				ImGui::SameLine();
				if (ImGui::BeginCombo("##new_aircraft", aircrafts[aircraft_to_add].short_name.c_str())) {
					for (size_t j = 0; j < aircrafts.size(); j++) {
						if (ImGui::Selectable(aircrafts[j].short_name.c_str(), j == aircraft_to_add)) {
							aircraft_to_add = j;
						}
					}

					ImGui::EndCombo();
				}

				if (should_add_aircraft) {
					int tracked_model_index = -1;
					for (int i = 0; i < models.size(); i++) {
						if (camera.model == &models[i]) {
							tracked_model_index = i;
							break;
						}
					}

					models.push_back(Model {
						.file_abs_path = aircrafts[aircraft_to_add].dnm,
						.should_load_file = true,
					});

					if (tracked_model_index != -1) {
						camera.model = &models[tracked_model_index];
					}
				}
			}

			for (int i = 0; i < models.size(); i++) {
				Model& model = models[i];

				AircraftFiles* aircraft = nullptr;
				for (auto& a : aircrafts) {
					if (a.dnm == model.file_abs_path) {
						aircraft = &a;
						break;
					}
				}
				my_assert(aircraft);

				if (ImGui::TreeNode(mu::str_tmpf("[{}] {}", i, aircraft->short_name).c_str())) {
					models[i].should_load_file = ImGui::Button("Reload");
					ImGui::SameLine();
					if (ImGui::BeginCombo("DNM", mu::Str(mu::file_get_base_name(models[i].file_abs_path), mu::memory::tmp()).c_str())) {
						for (size_t j = 0; j < aircrafts.size(); j++) {
							if (ImGui::Selectable(aircrafts[j].short_name.c_str(), aircrafts[j].dnm == models[i].file_abs_path)) {
								models[i].file_abs_path = aircrafts[j].dnm;
								models[i].should_load_file = true;
							}
						}

						ImGui::EndCombo();
					}

					models[i].should_be_removed = ImGui::Button("Remove");

					if (ImGui::Button("Reset State")) {
						model.current_state = {};
					}
					if (ImGui::Button("Reset All")) {
						model.current_state = {};
						for (auto& [_, mesh] : model.meshes) {
							mesh.current_state = mesh.initial_state;
						}
					}

					static size_t start_info_index = 0;
					if (ImGui::BeginCombo("Start Pos", start_infos[start_info_index].name.c_str())) {
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								model_set_start(models[i], start_infos[start_info_index]);
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &model.current_state.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(model.current_state.translation));

					glm::vec3 now_rotation {
						model.current_state.angles.roll,
						model.current_state.angles.pitch,
						model.current_state.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, current_angle_max)) {
						model.current_state.angles.rotate(
							now_rotation.z - model.current_state.angles.yaw,
							now_rotation.y - model.current_state.angles.pitch,
							now_rotation.x - model.current_state.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(model.current_state.angles.up, model.current_state.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(model.current_state.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(model.current_state.angles.front));
					ImGui::EndDisabled();

					ImGui::DragFloat("Speed", &model.current_state.speed, 0.05f, MIN_SPEED, MAX_SPEED);

					ImGui::Checkbox("Render AABB", &model.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(model.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(model.current_aabb.max));

					if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
						if (ImGui::Button("Reset")) {
							model.control = {};
						}

						ImGui::DragFloat("Landing Gear", &model.control.landing_gear_alpha, 0.01, 0, 1);
						ImGui::SliderFloat("Throttle", &model.control.throttle, 0.0f, 1.0f);

						ImGui::Checkbox("Afterburner Reheat", &model.control.afterburner_reheat_enabled);

						ImGui::TreePop();
					}

					size_t light_sources_count = 0;
					for (const auto& [_, mesh] : model.meshes) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
					}

					ImGui::BulletText(mu::str_tmpf("Meshes: (total: {}, root: {}, light: {})", model.meshes.size(),
						model.root_meshes_names.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&model, &render_mesh_ui, current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							if (ImGui::Button("Reset")) {
								mesh.current_state = mesh.initial_state;
							}

							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.current_state.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.current_state.translation));
							MyImGui::SliderAngle3("rotation", &mesh.current_state.rotation, current_angle_max);

							ImGui::Text(mu::str_tmpf("{}", mesh.animation_type).c_str());

							ImGui::BulletText(mu::str_tmpf("Children: ({})", mesh.children.size()).c_str());
							ImGui::Indent();
							for (const auto& child_name : mesh.children) {
								render_mesh_ui(model.meshes.at(child_name.c_str()));
							}
							ImGui::Unindent();

							if (ImGui::TreeNode(mu::str_tmpf("Faces: ({})", mesh.faces.size()).c_str())) {
								for (size_t i = 0; i < mesh.faces.size(); i++) {
									if (ImGui::TreeNode(mu::str_tmpf("{}", i).c_str())) {
										ImGui::TextWrapped("Vertices: %s", mu::str_tmpf("{}", mesh.faces[i].vertices_ids).c_str());

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
					for (const auto& name : model.root_meshes_names) {
						render_mesh_ui(model.meshes.at(name));
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}

			ImGui::Separator();

			std::function<void(Field&,bool)> render_field_imgui;
			render_field_imgui = [&render_field_imgui, &current_angle_max](Field& field, bool is_root) {
				if (ImGui::TreeNode(mu::str_tmpf("Field {}", field.name).c_str())) {
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
					MyImGui::SliderAngle3("Rotation", &field.current_state.rotation, current_angle_max);

					ImGui::BulletText("Sub Fields:");
					for (auto& subfield : field.subfields) {
						render_field_imgui(subfield, false);
					}

					ImGui::BulletText("TerrMesh: %d", (int)field.terr_meshes.size());
					for (auto& terr_mesh : field.terr_meshes) {
						if (ImGui::TreeNode(terr_mesh.name.c_str())) {
							if (ImGui::Button("Reset State")) {
								terr_mesh.current_state = terr_mesh.initial_state;
							}

							ImGui::Text("Tag: %s", terr_mesh.tag.c_str());

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

							ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.current_state.translation));
							MyImGui::SliderAngle3("Rotation", &terr_mesh.current_state.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Pict2: %d", (int)field.pictures.size());
					for (auto& picture : field.pictures) {
						if (ImGui::TreeNode(picture.name.c_str())) {
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

							ImGui::DragFloat3("Translation", glm::value_ptr(picture.current_state.translation));
							MyImGui::SliderAngle3("Rotation", &picture.current_state.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Meshes: %d", (int)field.meshes.size());
					for (auto& mesh : field.meshes) {
						ImGui::Text("%s", mesh.name.c_str());
					}

					ImGui::TreePop();
				}
			};
			render_field_imgui(field, true);
		}
		ImGui::End();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Logs")) {
			ImGui::Checkbox("Auto-Scroll", &imgui_window_logger.auto_scrolling);
			ImGui::SameLine();
			ImGui::Checkbox("Wrapped", &imgui_window_logger.wrapped);
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				imgui_window_logger = {};
			}

			if (ImGui::BeginChild("logs child", {}, false, imgui_window_logger.wrapped? 0:ImGuiWindowFlags_HorizontalScrollbar)) {
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0, 0});
				ImGuiListClipper clipper(imgui_window_logger.logs.size());
				while (clipper.Step()) {
					for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
						if (imgui_window_logger.wrapped) {
							ImGui::TextWrapped("%s", imgui_window_logger.logs[i].c_str());
						} else {
							auto log = imgui_window_logger.logs[i];
							ImGui::TextUnformatted(&log[0], &log[log.size()-1]);
						}
					}
				}
				ImGui::PopStyleVar();

				// scroll
				if (imgui_window_logger.auto_scrolling) {
					if (imgui_window_logger.last_scrolled_line != imgui_window_logger.logs.size()) {
						imgui_window_logger.last_scrolled_line = imgui_window_logger.logs.size();
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
			ImGui::SetNextWindowSize(ImVec2 {300, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.35f);
		}
		if (ImGui::Begin("Overlay Info", nullptr, ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove)) {
			for (const auto& line : overlay_text) {
				ImGui::TextWrapped(mu::str_tmpf("> {}", line).c_str());
			}
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
