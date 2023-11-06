// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>

// don't export min/max/near/far definitions with windows.h otherwise other includes might break
#define NOMINMAX
#include <portable-file-dialogs.h>
#undef near
#undef far

#include <filesystem>

#include "imgui.h"
#include "graphics.h"
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

constexpr float ENGINE_PROPELLERS_RESISTENCE = 15.0f;

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

	mu::Str name; // name in SRF (not FIL)
	mu::Vec<glm::vec3> vertices;
	mu::Vec<bool> vertices_has_smooth_shading; // ???
	mu::Vec<Face> faces;
	mu::Vec<uint64_t> gfs; // ???
	mu::Vec<uint64_t> zls; // ids of faces to create a light sprite at the center of them
	mu::Vec<uint64_t> zzs; // ???
	mu::Vec<Mesh> children;
	mu::Vec<MeshState> animation_states; // STA

	// POS
	MeshState initial_state; // should be kepts const after init

	struct {
		GLuint vao, vbo;
		size_t array_count;
	} gpu;

	// physics
	glm::mat4 transformation;
	MeshState state;

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

	gl_process_errors();

	for (auto& child : self.children) {
		mesh_load_to_gpu(child);
	}
}

void mesh_unload_from_gpu(Mesh& self) {
	glDeleteBuffers(1, &self.gpu.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.gpu.vao);
	self.gpu = {};

	for (auto& child : self.children) {
		mesh_unload_from_gpu(child);
	}
}

struct StartInfo {
	mu::Str name;
	glm::vec3 position;
	glm::vec3 attitude;
	float speed;
	float throttle;
	bool landing_gear_is_out = true;
};

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

		while (parser_accept(parser, 'P')) {
			mu::log_warning("found P line, ignoring it");
			parser_skip_after(parser, '\n');
		}

		while (parser_accept(parser, "C ")) {
			if (parser_accept(parser, "POSITION ")) {
				start_info.position.x = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, ' ');
				start_info.position.y = -parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, ' ');
				start_info.position.z = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "ATTITUDE ")) {
				start_info.attitude.x = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, ' ');
				start_info.attitude.y = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, ' ');
				start_info.attitude.z = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "INITSPED ")) {
				start_info.speed = parser_token_float(parser) * parser_accept_unit(parser);
				parser_expect(parser, '\n');
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
	mu::Vec<Mesh> meshes;

	AABB initial_aabb;
	AABB current_aabb;
	bool render_aabb;

	bool has_propellers;
	bool has_afterburner;
	bool has_high_throttle_mesh;
};

void model_load_to_gpu(Model& self) {
	for (auto& mesh : self.meshes) {
		mesh_load_to_gpu(mesh);
	}
}

void model_unload_from_gpu(Model& self) {
	for (auto& mesh : self.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

Mesh mesh_from_srf_str(Parser& parser, mu::StrView name) {
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
					mu_assert(packed_color.as_struct.padding == 0);
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

void _model_adjust_after_loading(Model& self) {
	// for each mesh: vertex -= mesh.CNT, mesh.children.each.cnt += mesh.cnt
	mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
	for (auto& mesh : self.meshes) {
		mesh.transformation = glm::identity<glm::mat4>();
		meshes_stack.push_back(&mesh);
	}
	while (meshes_stack.empty() == false) {
		Mesh* mesh = *meshes_stack.rbegin();
		meshes_stack.pop_back();

		for (auto& v : mesh->vertices) {
			v -= mesh->cnt;

			// apply mesh transformation to get model space vertex
			mesh->transformation = glm::translate(mesh->transformation, mesh->state.translation);
			mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[2], glm::vec3{0, 0, 1});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[1], glm::vec3{1, 0, 0});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[0], glm::vec3{0, 1, 0});
			const auto model_v = mesh->transformation * glm::vec4(v, 1.0);

			// update AABB
			for (int i = 0; i < 3; i++) {
				if (model_v[i] < self.initial_aabb.min[i]) {
					self.initial_aabb.min[i] = model_v[i];
				}
				if (model_v[i] > self.initial_aabb.max[i]) {
					self.initial_aabb.max[i] = model_v[i];
				}
			}
		}

		for (auto& child : mesh->children) {
			child.cnt += mesh->cnt;
			meshes_stack.push_back(&child);
		}
	}

	self.current_aabb = self.initial_aabb;
}

Model model_from_dnm_file(mu::StrView dnm_file_abs_path) {
	auto parser = parser_from_file(dnm_file_abs_path, mu::memory::tmp());
	Model model {
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

	mu::Map<mu::Str, mu::Vec<mu::Str>> mesh_name_to_children_names(mu::memory::tmp());
	while (parser_accept(parser, "SRF ")) {
		auto name = parser_token_str(parser);
		if (!(name.starts_with("\"") && name.ends_with("\""))) {
			mu::panic("name must be in \"\" found={}", name);
		}
		_str_unquote(name);
		parser_expect(parser, '\n');

		parser_expect(parser, "FIL ");
		auto fil = parser_token_str(parser, mu::memory::tmp());
		parser_expect(parser, '\n');
		auto it = meshes.find(fil);
		if (it == meshes.end()) {
			mu::panic("'{}': line referenced undeclared surf={}", name, fil);
		}
		auto& [_k, surf] = *it;

		surf.name = name;

		parser_expect(parser, "CLA ");
		auto animation_type = parser_token_u8(parser);
		surf.animation_type = (AnimationClass) animation_type;
		if (surf.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER || surf.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z) {
			model.has_propellers = true;
		} else if (surf.animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
			model.has_afterburner = true;
		} else if (surf.animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE) {
			model.has_high_throttle_mesh = true;
		}
		parser_expect(parser, '\n');

		parser_expect(parser, "NST ");
		auto num_stas = parser_token_u64(parser);
		surf.animation_states.reserve(num_stas);
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

			surf.animation_states.push_back(sta);
		}

		bool read_pos = false, read_cnt = false, read_rel_dep = false, read_nch = false;
		mu::Vec<mu::Str> children_names(mu::memory::tmp());
		while (true) {
			if (parser_accept(parser, "POS ")) {
				read_pos = true;

				surf.initial_state.translation.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.initial_state.translation.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.initial_state.translation.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
				surf.initial_state.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf.initial_state.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf.initial_state.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;

				// aircraft/cessna172r.dnm is the only file with no visibility
				if (parser_accept(parser, ' ')) {
					uint8_t visible = parser_token_u8(parser);
					if (visible == 1 || visible == 0) {
						surf.initial_state.visible = (visible == 1);
					} else {
						mu::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
					}
				} else {
					surf.initial_state.visible = true;
				}

				parser_expect(parser, '\n');

				surf.state = surf.initial_state;
			} else if (parser_accept(parser, "CNT ")) {
				read_cnt = true;

				surf.cnt.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.cnt.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.cnt.z = parser_token_float(parser);
				parser_expect(parser, '\n');
			} else if (parser_accept(parser, "PAX")) {
				parser_skip_after(parser, '\n');
			} else if (parser_accept(parser, "REL DEP\n")) {
				read_rel_dep = true;
			} else if (parser_accept(parser, "NCH ")) {
				read_nch = true;

				const auto num_children = parser_token_u64(parser);
				parser_expect(parser, '\n');
				children_names.reserve(num_children);

				for (size_t i = 0; i < num_children; i++) {
					parser_expect(parser, "CLD ");
					auto child_name = parser_token_str(parser);
					if (!(child_name.starts_with("\"") && child_name.ends_with("\""))) {
						mu::panic("'{}': child_name must be in \"\" found={}", name, child_name);
					}
					_str_unquote(child_name);
					children_names.push_back(child_name);
					parser_expect(parser, '\n');
				}
			} else {
				break;
			}
		}
		mesh_name_to_children_names[name] = children_names;

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
		meshes[name] = std::move(surf);
		if (meshes.erase(fil) == 0) {
			parser_panic(parser, "must be able to remove {} from meshes", name, fil);
		}

		parser_expect(parser, "END\n");
	}
	// aircraft/cessna172r.dnm doesn't have final END
	if (parser_finished(parser) == false) {
		parser_expect(parser, "END\n");
	}

	// add children to their parents
	for (auto& [name, parent] : meshes) {
		for (auto& child_name : mesh_name_to_children_names.at(name)) {
			auto& child = meshes.at(child_name);

			if (child.name == parent.name) {
				mu::log_warning("SURF {} references itself", child_name);
			} else {
				parent.children.push_back(std::move(child));
			}

			meshes.erase(child_name);
		}
	}

	// top level nodes = nodes without parents
	for (auto& [_, mesh] : meshes) {
		model.meshes.push_back(std::move(mesh));
	}

	_model_adjust_after_loading(model);

	return model;
}

Model model_from_srf_file(mu::StrView srf_file_abs_path) {
	auto main_srf_parser = parser_from_file(srf_file_abs_path, mu::memory::tmp());

	auto i = srf_file_abs_path.find_last_of('/') + 1;
	auto j = srf_file_abs_path.size() - 4;
	auto name = srf_file_abs_path.substr(i, j-i);

	auto mesh = mesh_from_srf_str(main_srf_parser, name);
	mesh.initial_state.visible = true;
	mesh.state = mesh.initial_state;

	Model model {
		.meshes = {mesh},
		.initial_aabb = AABB {
			.min={+FLT_MAX, +FLT_MAX, +FLT_MAX},
			.max={-FLT_MAX, -FLT_MAX, -FLT_MAX},
		},
	};

	_model_adjust_after_loading(model);

	return model;
}

// trim from start (in place)
void _str_ltrim(mu::Str& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
void _str_rtrim(mu::Str& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
void _str_trim(mu::Str& s) {
    _str_rtrim(s);
    _str_ltrim(s);
}

struct DATMap {
	mu::Map<mu::Str, mu::Str> map;
};

DATMap datmap_from_dat_file(mu::StrView dat_file_path) {
	DATMap dat {};

	auto parser = parser_from_file(dat_file_path, mu::memory::tmp());

	while (!parser_finished(parser)) {
		if (parser_accept(parser, '\n')) {
		} else if (parser_accept(parser, "REM ")) {
			parser_skip_after(parser, '\n');
		} else {
			auto key = parser_token_str(parser);

			if (key == "AUTOCALC") { break; }

			// | <--key--> | |  <--  value  -->  |
			// REALPROP 0 CD -5deg 0.006 20deg 0.4
			// key = "REALPROP 0 CD"
			// val = "-5deg 0.006 20deg 0.4"
			if (key == "REALPROP") {
				parser_expect(parser, ' ');
				int index = parser_token_u8(parser);
				parser_expect(parser, ' ');
				auto rest_of_key = parser_token_str(parser, mu::memory::tmp());
				key = mu::str_format("REALPROP {} {}", index, rest_of_key);
			}

			// | <--   key   --> | |  <--            value            -->  |
			// EXCAMERA "CO-PILOT" 0.4m  1.22m  9.00m 0deg 0deg 0deg INSIDE
			// key = "EXCAMERA \"CO-PILOT\""
			// val = "0.4m  1.22m  9.00m 0deg 0deg 0deg INSIDE"
			if (key == "EXCAMERA") {
				parser_expect(parser, ' ');
				auto camera_name = parser_token_str(parser, mu::memory::tmp());
				key = mu::str_format("EXCAMERA {}", camera_name);
			}

			auto value = parser_token_str_with(parser, [](char c){ return c != '#' && c != '\n'; });
			_str_trim(value);

			parser_skip_after(parser, '\n');

			dat.map[std::move(key)] = std::move(value);
		}
	}

	return dat;
}

mu::Str datmap_get_str(const DATMap& self, const mu::Str& key,
	mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	auto it = self.map.find(key);
	if (it == self.map.end()) {
		return "";
	}
	return mu::Str(it->second, allocator);
}

mu::Vec<float> datmap_get_floats(const DATMap& self, const mu::Str& key,
	mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	auto it = self.map.find(key);
	if (it == self.map.end()) {
		return {};
	}
	auto parser = parser_from_str(it->second, mu::memory::tmp());
	mu::Vec<float> out(allocator);
	while (!parser_finished(parser)) {
		out.push_back(parser_token_float(parser) * parser_accept_unit(parser));
		while (parser_accept(parser, ' ')) { }
	}
	return out;
}

mu::Vec<int64_t> datmap_get_ints(const DATMap& self, const mu::Str& key,
	mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	auto it = self.map.find(key);
	if (it == self.map.end()) {
		return {};
	}
	auto parser = parser_from_str(it->second, mu::memory::tmp());
	mu::Vec<int64_t> out(allocator);
	while (!parser_finished(parser)) {
		out.push_back(parser_token_i64(parser) * parser_accept_unit(parser));
		while (parser_accept(parser, ' ')) { }
	}
	return out;
}

struct ExternalCameraLocation {
	mu::Str name;
	glm::vec3 pos, angles;
	bool inside;
};

mu::Vec<ExternalCameraLocation> datmap_get_excameras(const DATMap& self,
	mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	constexpr mu::StrView SUFFIX = "EXCAMERA ";
	mu::Vec<ExternalCameraLocation> out(allocator);

	for (const auto& [k, v] : self.map) {
		if (k.starts_with(SUFFIX)) {
			ExternalCameraLocation excamera {  };

			excamera.name = mu::Str(k.substr(SUFFIX.size(), k.size()-SUFFIX.size()), allocator);
			_str_unquote(excamera.name);

			auto parser = parser_from_str(v, mu::memory::tmp());

			for (int i = 0; i < 3; i++) {
				excamera.pos[i] = parser_token_float(parser) * parser_accept_unit(parser);
				while (parser_accept(parser, ' ')) { }
			}

			for (int i = 0; i < 3; i++) {
				excamera.angles[i] = parser_token_float(parser) * parser_accept_unit(parser);
				while (parser_accept(parser, ' ')) { }
			}

			excamera.inside = parser_token_str(parser, mu::memory::tmp()) == "INSIDE";

			out.push_back(excamera);
		}
	}

	return out;
}

// paths of files of one single aircraft
struct AircraftTemplate {
	mu::Str short_name; // a4.dat -> a4
	mu::Str dat, dnm, collision, cockpit;
	mu::Str coarse; // optional
};

void _aircraft_templates_from_lst_file(mu::StrView lst_file_path, mu::Map<mu::Str, AircraftTemplate>& aircraft_templates) {
	auto parser = parser_from_file(lst_file_path);

	while (!parser_finished(parser)) {
		AircraftTemplate aircraft {};

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

		aircraft_templates[aircraft.short_name] = aircraft;
	}
}

template<typename Function>
mu::Vec<mu::Str> _dir_list_files_with(mu::StrView dir_abs_path, Function predicate, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	mu::Vec<mu::Str> out;

	for (const auto & entry : std::filesystem::directory_iterator(dir_abs_path)) {
		if (entry.is_regular_file()) {
			auto filename = entry.path().filename().string();
			auto path = entry.path().string();

			if (predicate(filename)) {
				out.push_back(mu::Str(path, allocator));
			}
		}
	}

	return out;
}

mu::Map<mu::Str, AircraftTemplate> aircraft_templates_from_dir(mu::StrView dir_abs_path) {
	auto sce_lst_files = _dir_list_files_with(dir_abs_path, [](const auto& filename) {
		return filename.starts_with("air") && filename.ends_with(".lst");
	}, mu::memory::tmp());

	mu::Map<mu::Str, AircraftTemplate> aircraft_templates;
	for (const auto& file : sce_lst_files) {
		_aircraft_templates_from_lst_file(file, aircraft_templates);
	}
	return aircraft_templates;
}

struct Aircraft {
	AircraftTemplate aircraft_template;
	Model model;
	DATMap dat;
	Audio* engine_sound;

	struct {
		glm::vec3 translation;
		LocalEulerAngles angles;
		bool visible = true;
		float speed;

		float landing_gear_alpha = 0; // 0 -> DOWN, 1 -> UP
		float throttle = 0;
		float engine_speed; // 0 -> 1
		bool afterburner_reheat_enabled = false;
	} state;

	struct {
		bool visible = true;
		double time_left_secs = ANTI_COLL_LIGHT_PERIOD;
	} anti_coll_lights;

	bool should_be_loaded;
	bool should_be_removed;
};

Aircraft aircraft_new(AircraftTemplate aircraft_template) {
	return Aircraft {
		.aircraft_template = aircraft_template,
		.should_be_loaded = true,
	};
}

void aircraft_set_start(Aircraft& self, const StartInfo& start_info) {
	self.state.translation = start_info.position;
	self.state.angles = local_euler_angles_from_attitude(start_info.attitude);
	self.state.landing_gear_alpha = start_info.landing_gear_is_out? 0.0f : 1.0f;
	self.state.throttle = start_info.throttle;
	self.state.engine_speed = start_info.throttle;
	self.state.speed = start_info.speed;
}

struct PerspectiveProjection {
	float near         = 0.1f;
	float far          = 100000;
	float fovy         = 45.0f / DEGREES_MAX * RADIANS_MAX;
	float aspect       = (float) WND_INIT_WIDTH / WND_INIT_HEIGHT;
};

glm::mat4 projection_calc_mat(PerspectiveProjection& self) {
	return glm::perspective(self.fovy, self.aspect, self.near, self.far);
}

struct Camera {
	Aircraft* aircraft;
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

glm::mat4 camera_calc_view(const Camera& self) {
	if (self.aircraft) {
		return glm::lookAt(self.position, self.aircraft->state.translation, self.aircraft->state.angles.up);
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
	} state, initial_state;
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

	gl_process_errors();
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

	gl_process_errors();
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
	} state, initial_state;
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

struct GroundObjSpawn {
	mu::Str name;
	glm::vec3 pos, rotation;
	FieldID id;
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
	mu::Vec<GroundObjSpawn> gobs;

	bool should_be_transformed = true;
	glm::mat4 transformation;

	struct {
		glm::vec3 translation;
		glm::vec3 rotation; // roll, pitch, yaw
		bool visible = true;
	} state, initial_state;
};

void _str_to_lower(mu::Str& s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
}

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

			terr_mesh.state = terr_mesh.initial_state;

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

			picture.state = picture.initial_state;

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
			subfield->state = subfield->initial_state;

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
			terr_mesh->state = terr_mesh->initial_state;

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
			picture->state = picture->initial_state;

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
			GroundObjSpawn gob {};

			parser_expect(parser, "POS ");
			gob.pos.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			gob.pos.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			gob.pos.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			gob.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			gob.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			gob.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');

			parser_expect(parser, "ID ");
			gob.id = (FieldID) parser_token_u8(parser);
			parser_expect(parser, '\n');

			if (parser_accept(parser, "TAG")) {
				// TODO don't understand yet TAG, skip it
				parser_skip_after(parser, '\n');

			}

			parser_expect(parser, "NAM ");
			gob.name = parser_token_str(parser);
			_str_to_lower(gob.name);

			// TODO don't understand yet IFF and FLG, skip them
			parser_skip_after(parser, "END\n");

			field.gobs.push_back(std::move(gob));
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
			mesh->state = mesh->initial_state;

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

// paths of files of one single scenery
struct SceneryTemplate {
	mu::Str name;
	mu::Str fld, stp;
	mu::Str yfs; // optional, may be empty
	bool is_airrace;
};

void _scenery_templates_from_lst_file(mu::StrView file_abs_path, mu::Map<mu::Str, SceneryTemplate>& map) {
	auto parser = parser_from_file(file_abs_path, mu::memory::tmp());

	while (!parser_finished(parser)) {
		if (parser_accept(parser, ' ')) {
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, '\n')) {
		} else {
			SceneryTemplate tmpl{};

			tmpl.name = parser_token_str(parser);
			while (parser_accept(parser, ' ')) { }

			tmpl.fld = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(tmpl.fld);
			tmpl.fld = mu::str_format(ASSETS_DIR "/{}", tmpl.fld);
			while (parser_accept(parser, ' ')) { }

			tmpl.stp = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(tmpl.stp);
			tmpl.stp = mu::str_format(ASSETS_DIR "/{}", tmpl.stp);
			while (parser_accept(parser, ' ')) { }

			if (!parser_accept(parser, '\n')) {
				tmpl.yfs = parser_token_str(parser, mu::memory::tmp());
				_str_unquote(tmpl.yfs);
				if (tmpl.yfs.size() > 0) {
					tmpl.yfs = mu::str_format(ASSETS_DIR "/{}", tmpl.yfs);
				}
				while (parser_accept(parser, ' ')) { }

				tmpl.is_airrace = parser_accept(parser, "AIRRACE");
				while (parser_accept(parser, ' ')) { }

				parser_expect(parser, '\n');
			}

			map[tmpl.name] = std::move(tmpl);
		}
	}
}

mu::Map<mu::Str, SceneryTemplate> scenery_templates_from_dir(mu::StrView dir_abs_path) {
	auto sce_lst_files = _dir_list_files_with(dir_abs_path, [](const auto& filename) {
		return filename.starts_with("sce") && filename.ends_with(".lst");
	}, mu::memory::tmp());

	mu::Map<mu::Str, SceneryTemplate> scenery_templates;
	for (const auto& file : sce_lst_files) {
		_scenery_templates_from_lst_file(file, scenery_templates);
	}
	return scenery_templates;
}

struct Scenery {
	SceneryTemplate scenery_template;
	Field root_fld;
	mu::Vec<StartInfo> start_infos;

	bool should_be_loaded;
};

Scenery scenery_new(SceneryTemplate& scenery_template) {
	return Scenery {
		.scenery_template = scenery_template,
		.should_be_loaded = true
	};
}

// paths of files of one single scenery
struct GroundObjTemplate {
	mu::Str short_name; // ground/castle.dat -> castle
	mu::Str dat;
	mu::Str main; // either .srf or .dnm for model
	mu::Str coll_srf, cockpit_srf, coarse_srf; // optional
};

void _ground_obj_templates_from_lst_file(mu::StrView file_abs_path, mu::Map<mu::Str, GroundObjTemplate>& map) {
	auto parser = parser_from_file(file_abs_path, mu::memory::tmp());

	while (!parser_finished(parser)) {
		if (parser_accept(parser, ' ')) {
			parser_skip_after(parser, '\n');
		} else if (parser_accept(parser, '\n')) {
		} else {
			GroundObjTemplate tmpl{};

			tmpl.dat = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(tmpl.dat);
			tmpl.dat = mu::str_format(ASSETS_DIR "/{}", tmpl.dat);
			while (parser_accept(parser, ' ')) { }

			tmpl.main = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(tmpl.main);
			tmpl.main = mu::str_format(ASSETS_DIR "/{}", tmpl.main);
			while (parser_accept(parser, ' ')) { }

			tmpl.coll_srf = parser_token_str(parser, mu::memory::tmp());
			_str_unquote(tmpl.coll_srf);
			tmpl.coll_srf = mu::str_format(ASSETS_DIR "/{}", tmpl.coll_srf);
			while (parser_accept(parser, ' ')) { }

			if (!parser_accept(parser, '\n')) {
				tmpl.cockpit_srf = parser_token_str(parser, mu::memory::tmp());
				_str_unquote(tmpl.cockpit_srf);
				if (tmpl.cockpit_srf.size() > 0) {
					tmpl.cockpit_srf = mu::str_format(ASSETS_DIR "/{}", tmpl.cockpit_srf);
				}
				while (parser_accept(parser, ' ')) { }

				if (!parser_accept(parser, '\n')) {
					tmpl.coarse_srf = parser_token_str(parser, mu::memory::tmp());
					_str_unquote(tmpl.coarse_srf);
					if (tmpl.coarse_srf.size() > 0) {
						tmpl.coarse_srf = mu::str_format(ASSETS_DIR "/{}", tmpl.coarse_srf);
					}
					while (parser_accept(parser, ' ')) { }

					parser_expect(parser, '\n');
				}
			}

			auto i = tmpl.dat.find_last_of('/') + 1;
			auto j = tmpl.dat.size() - 4;
			tmpl.short_name = tmpl.dat.substr(i, j-i);

			map[tmpl.short_name] = std::move(tmpl);
		}
	}
}

mu::Map<mu::Str, GroundObjTemplate> ground_obj_templates_from_dir(mu::StrView dir_abs_path) {
	auto sce_lst_files = _dir_list_files_with(dir_abs_path, [](const auto& filename) {
		return filename.starts_with("gro") && filename.ends_with(".lst");
	}, mu::memory::tmp());

	mu::Map<mu::Str, GroundObjTemplate> scenery_templates;
	for (const auto& file : sce_lst_files) {
		_ground_obj_templates_from_lst_file(file, scenery_templates);
	}
	return scenery_templates;
}

struct GroundObj {
	GroundObjTemplate ground_obj_template;
	Model model;
	DATMap dat;

	struct {
		glm::vec3 translation;
		LocalEulerAngles angles;
		bool visible = true;
		float speed;
	} state;

	bool should_be_loaded;
	bool should_be_removed;
};

GroundObj ground_obj_new(GroundObjTemplate ground_obj_template, glm::vec3 pos, glm::vec3 attitude) {
	return GroundObj {
		.ground_obj_template = ground_obj_template,
		.state = {
			.translation = pos,
			.angles = local_euler_angles_from_attitude(attitude),
		},
		.should_be_loaded = true,
	};
}

struct Sounds {
	Audio bang, blast, blast2, bombsaway, burner, damage;
	Audio extendldg, gearhorn, gun, hit, missile, notice;
	Audio retractldg, rocket, stallhorn, touchdwn, warning, engine;
	mu::Arr<Audio, 10> engines, props;

	mu::Vec<Audio*> as_array;
};

inline static void
_sounds_load(Sounds& self, Audio& audio, mu::StrView path) {
	audio = audio_new(path);
	self.as_array.push_back(&audio);
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

void sounds_free(Sounds& self) {
	for (int i = 0; i < self.as_array.size(); i++) {
		audio_free(*self.as_array[i]);
	}
}

struct ImGuiWindowLogger : public mu::ILogger {
	mu::memory::Arena _arena;
	mu::Vec<mu::Str> logs;

	bool auto_scrolling = true;
	bool wrapped = false;
	float last_scrolled_line = 0;

	virtual void log_debug(mu::StrView str) override {
		logs.push_back(mu::str_format(&_arena, "> {}\n", str));
		fmt::print("[debug] {}\n", str);
	}

	virtual void log_info(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[info] {}\n", str);
		fmt::vprint(stdout, formatted, {});
		logs.push_back(std::move(formatted));
	}

	virtual void log_warning(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[warning] {}\n", str);
		fmt::vprint(stdout, formatted, {});
		logs.push_back(std::move(formatted));
	}

	virtual void log_error(mu::StrView str) override {
		auto formatted = mu::str_format(&_arena, "[error] {}\n", str);
		fmt::vprint(stderr, formatted, {});
		logs.push_back(std::move(formatted));
	}
};

struct Events {
	// aircraft control
	bool afterburner_toggle;
	bool stick_right;
	bool stick_left;
	bool stick_front;
	bool stick_back;
	bool rudder_right;
	bool rudder_left;
	bool throttle_increase;
	bool throttle_decrease;

	// camera control
	bool camera_tracking_up;
	bool camera_tracking_down;
	bool camera_tracking_right;
	bool camera_tracking_left;
	bool camera_flying_up;
	bool camera_flying_down;
	bool camera_flying_right;
	bool camera_flying_left;
	bool camera_flying_rotate_enabled;

	glm::ivec2 mouse_pos;
};

struct Signal {
	uint16_t _num_listeners, _num_handles;
};

void signal_listen(Signal& self) {
	self._num_listeners++;
}

bool signal_handle(Signal& self) {
	mu_assert_msg(self._num_listeners > 0, "signal has no registered listeners");

	if (self._num_handles > 0) {
		self._num_handles--;
		return true;
	}
	return false;
}

void signal_fire(Signal& self) {
	mu_assert_msg(self._num_listeners > 0, "signal has no registered listeners");

	self._num_handles = self._num_listeners;
}

// same as Events but don't get reset each frame (to be able to handle at any frame)
struct Signals {
	Signal quit;
	Signal wnd_configs_changed;
	Signal scenery_loaded;
};

struct Settings {
	bool fullscreen = false;
	bool should_limit_fps = true;
	int fps_limit = 60;
	bool custom_aspect_ratio = false;
	float current_angle_max = DEGREES_MAX;
	bool handle_collision = true;

	struct {
		bool smooth_lines = true;
		GLfloat line_width = 3.0f;
		GLfloat point_size = 3.0f;

		GLenum regular_primitives_type = GL_TRIANGLES;
		GLenum light_primitives_type   = GL_TRIANGLES;
		GLenum polygon_mode            = GL_FILL;
	} rendering;

	struct {
		bool enabled = true;
		glm::vec2 position {-0.9f, -0.8f};
		float scale = 0.48f;
	} world_axis;
};

struct ZLPoint {
	glm::vec3 center, color;
};

struct Box { glm::vec3 translation, scale, color; };

struct TextRenderReq {
	mu::Str text;
	float x, y, scale;
	glm::vec3 color;
};

// all state of loaded glyph using FreeType
// https://learnopengl.com/img/in-practice/glyph_offset.png
struct Glyph {
	GLuint texture;
	glm::ivec2 size;
	// Offset from baseline to left/top of glyph
	glm::ivec2 bearing;
	// horizontal offset to advance to next glyph
	uint32_t advance;
};

struct Canvas {
	mu::memory::Arena _arena;

	// render lists
	mu::Vec<mu::Str> overlay_text_list; // debugging
	mu::Vec<TextRenderReq> text_list;
	mu::Vec<glm::mat4> axis_list;
	mu::Vec<Box> boxes_list;
	mu::Vec<ZLPoint> zlpoints_list;

	GLProgram meshes_program, lines_program, picture2d_program, ground_program, sprite_program;

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 5.0f;
		bool on_top = true;
	} axis_rendering;

	struct {
		GLuint vao, vbo;
		size_t points_count;
		GLfloat line_width = 1.0f;
	} box_rendering;

	// opengl can't call shader without VAO even if shader doesn't take input
	// dummy_vao lets you call shader without input (useful when coords is embedded in shader)
	GLuint dummy_vao;

	SDL_Surface* groundtile_surface;
	GLuint groundtile_texture;

	SDL_Surface* zl_sprite_surface;
	GLuint zl_sprite_texture;

	struct {
		GLProgram program;
		GLuint vao, vbo;
		mu::Arr<Glyph, 128> glyphs;
	} text_rendering;
};

// precalculated matrices
struct CachedMatrices {
	glm::mat4 view;
	glm::mat4 view_inverse;

	glm::mat4 projection;
	glm::mat4 projection_inverse;

	glm::mat4 projection_view;
	glm::mat4 proj_inv_view_inv;
};

struct LoopTimer {
	uint64_t _last_time_millis;
	int64_t _millis_till_render;

	// seconds since previous frame
	double delta_time;

	bool ready;
};

uint64_t time_now_millis() {
	return SDL_GetTicks64();
}

void time_delay_millis(uint32_t millis) {
	SDL_Delay(millis);
}

struct World {
	SDL_Window* sdl_window;
	SDL_GLContext sdl_gl_context;

	ImGuiWindowLogger imgui_window_logger;
	mu::Str imgui_ini_file_path;

	LoopTimer loop_timer;

	// name -> templates
	mu::Map<mu::Str, AircraftTemplate> aircraft_templates;
	mu::Map<mu::Str, SceneryTemplate> scenery_templates;
	mu::Map<mu::Str, GroundObjTemplate> ground_obj_templates;

	AudioDevice audio_device;
	Sounds sounds;

	mu::Vec<Aircraft> aircrafts;
	mu::Vec<GroundObj> ground_objs;
	Scenery scenery;

	Camera camera;
	PerspectiveProjection projection;
	CachedMatrices mats;

	Signals signals;
	Events events;
	Settings settings;

	Canvas canvas;
};

namespace sys {
	void sdl_init(World& world) {
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
			mu::panic(SDL_GetError());
		}

		world.sdl_window = SDL_CreateWindow(
			WND_TITLE,
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			WND_INIT_WIDTH, WND_INIT_HEIGHT,
			SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | WND_FLAGS
		);
		if (!world.sdl_window) {
			mu::panic(SDL_GetError());
		}

		SDL_SetWindowBordered(world.sdl_window, SDL_TRUE);

		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_PROFILE)) { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_CONTEXT_MAJOR))  { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_CONTEXT_MINOR))  { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_DOUBLE_BUFFER))           { mu::panic(SDL_GetError()); }

		world.sdl_gl_context = SDL_GL_CreateContext(world.sdl_window);
		if (!world.sdl_gl_context) {
			mu::panic(SDL_GetError());
		}

		// glad: load all OpenGL function pointers
		if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
			mu::panic("failed to load GLAD function pointers");
		}
	}

	void sdl_shutdown(World& world) {
		SDL_GL_DeleteContext(world.sdl_gl_context);
		SDL_DestroyWindow(world.sdl_window);
		SDL_Quit();
	}

	void imgui_init(World& world) {
		IMGUI_CHECKVERSION();
		if (ImGui::CreateContext() == nullptr) {
			mu::panic("failed to create imgui context");
		}
		ImGui::StyleColorsDark();

		if (!ImGui_ImplSDL2_InitForOpenGL(world.sdl_window, world.sdl_gl_context)) {
			mu::panic("failed to init imgui implementation for SDL2");
		}

		if (!ImGui_ImplOpenGL3_Init("#version 330")) {
			mu::panic("failed to init imgui implementation for OpenGL3");
		}

		world.imgui_ini_file_path = mu::str_format("{}/{}", mu::folder_config(mu::memory::tmp()), "open-ysf-imgui.ini");
		ImGui::GetIO().IniFilename = world.imgui_ini_file_path.c_str();
	}

	void imgui_shutdown(World& world) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
	}

	void imgui_render(World& world) {
		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::TreeNodeEx("Window")) {
				ImGui::Checkbox("Limit FPS", &world.settings.should_limit_fps);
				ImGui::BeginDisabled(!world.settings.should_limit_fps); {
					ImGui::InputInt("FPS", &world.settings.fps_limit, 1, 5);
				}
				ImGui::EndDisabled();

				int size[2];
				SDL_GetWindowSize(world.sdl_window, &size[0], &size[1]);
				const bool width_changed = ImGui::InputInt("Width", &size[0]);
				const bool height_changed = ImGui::InputInt("Height", &size[1]);
				if (width_changed || height_changed) {
					signal_fire(world.signals.wnd_configs_changed);
					SDL_SetWindowSize(world.sdl_window, size[0], size[1]);
				}

				MyImGui::EnumsCombo("Angle Max", &world.settings.current_angle_max, {
					{DEGREES_MAX, "DEGREES_MAX"},
					{RADIANS_MAX, "RADIANS_MAX"},
					{YS_MAX,      "YS_MAX"},
				});

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Projection")) {
				if (ImGui::Button("Reset")) {
					world.projection = {};
					signal_fire(world.signals.wnd_configs_changed);
				}

				ImGui::InputFloat("near", &world.projection.near, 1, 10);
				ImGui::InputFloat("far", &world.projection.far, 1, 10);
				ImGui::DragFloat("fovy (1/zoom)", &world.projection.fovy, 1, 1, 45);

				if (ImGui::Checkbox("custom aspect", &world.settings.custom_aspect_ratio) && !world.settings.custom_aspect_ratio) {
					signal_fire(world.signals.wnd_configs_changed);
				}
				ImGui::BeginDisabled(!world.settings.custom_aspect_ratio);
					ImGui::InputFloat("aspect", &world.projection.aspect, 1, 10);
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Camera")) {
				if (ImGui::Button("Reset")) {
					world.camera = { .aircraft=world.camera.aircraft };
				}

				int tracked_model_index = -1;
				for (int i = 0; i < world.aircrafts.size(); i++) {
					if (world.camera.aircraft == &world.aircrafts[i]) {
						tracked_model_index = i;
						break;
					}
				}
				if (ImGui::BeginCombo("Tracked Model", world.camera.aircraft ? mu::str_tmpf("Model[{}]", tracked_model_index).c_str() : "-NULL-")) {
					if (ImGui::Selectable("-NULL-", world.camera.aircraft == nullptr)) {
						world.camera.aircraft = nullptr;
					}
					for (size_t j = 0; j < world.aircrafts.size(); j++) {
						if (ImGui::Selectable(mu::str_tmpf("Model[{}]", j).c_str(), j == tracked_model_index)) {
							world.camera.aircraft = &world.aircrafts[j];
						}
					}

					ImGui::EndCombo();
				}

				if (world.camera.aircraft) {
					ImGui::DragFloat("distance", &world.camera.distance_from_model, 1, 0);

					ImGui::Checkbox("Rotate Around", &world.camera.enable_rotating_around);
				} else {
					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							world.camera.position = {};
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								world.camera.position = start_infos[j].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::DragFloat("movement_speed", &world.camera.movement_speed, 5, 50, 1000);
					ImGui::DragFloat("mouse_sensitivity", &world.camera.mouse_sensitivity, 1, 0.5, 10);
					ImGui::DragFloat3("world_up", glm::value_ptr(world.camera.world_up), 1, -100, 100);
					ImGui::DragFloat3("front", glm::value_ptr(world.camera.front), 0.1, -1, 1);
					ImGui::DragFloat3("right", glm::value_ptr(world.camera.right), 1, -100, 100);
					ImGui::DragFloat3("up", glm::value_ptr(world.camera.up), 1, -100, 100);

				}

				ImGui::SliderAngle("yaw", &world.camera.yaw, -89, 89);
				ImGui::SliderAngle("pitch", &world.camera.pitch, -179, 179);

				ImGui::DragFloat3("position", glm::value_ptr(world.camera.position), 1, -100, 100);

				ImGui::TreePop();
			}

			const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = gl_get_float(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
			if (ImGui::TreeNode("Rendering")) {
				if (ImGui::Button("Reset")) {
					world.settings.rendering = {};
				}

				MyImGui::EnumsCombo("Polygon Mode", &world.settings.rendering.polygon_mode, {
					{GL_POINT, "GL_POINT"},
					{GL_LINE,  "GL_LINE"},
					{GL_FILL,  "GL_FILL"},
				});

				MyImGui::EnumsCombo("Regular Mesh Primitives", &world.settings.rendering.regular_primitives_type, {
					{GL_POINTS,          "GL_POINTS"},
					{GL_LINES,           "GL_LINES"},
					{GL_LINE_LOOP,       "GL_LINE_LOOP"},
					{GL_LINE_STRIP,      "GL_LINE_STRIP"},
					{GL_TRIANGLES,       "GL_TRIANGLES"},
					{GL_TRIANGLE_STRIP,  "GL_TRIANGLE_STRIP"},
					{GL_TRIANGLE_FAN,    "GL_TRIANGLE_FAN"},
				});

				MyImGui::EnumsCombo("Light Mesh Primitives", &world.settings.rendering.light_primitives_type, {
					{GL_POINTS,          "GL_POINTS"},
					{GL_LINES,           "GL_LINES"},
					{GL_LINE_LOOP,       "GL_LINE_LOOP"},
					{GL_LINE_STRIP,      "GL_LINE_STRIP"},
					{GL_TRIANGLES,       "GL_TRIANGLES"},
					{GL_TRIANGLE_STRIP,  "GL_TRIANGLE_STRIP"},
					{GL_TRIANGLE_FAN,    "GL_TRIANGLE_FAN"},
				});

				ImGui::Checkbox("Smooth Lines", &world.settings.rendering.smooth_lines);
                #ifndef OS_MACOS
				ImGui::BeginDisabled(!world.settings.rendering.smooth_lines);
					ImGui::DragFloat("Line Width", &world.settings.rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::EndDisabled();
                #endif

				const GLfloat POINT_SIZE_GRANULARITY = gl_get_float(GL_POINT_SIZE_GRANULARITY);
				ImGui::DragFloat("Point Size", &world.settings.rendering.point_size, POINT_SIZE_GRANULARITY, 0.5, 100);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Axis Rendering")) {
				ImGui::Checkbox("On Top", &world.canvas.axis_rendering.on_top);
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &world.canvas.axis_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif

				ImGui::BulletText("World Axis:");
				if (ImGui::Button("Reset")) {
					world.settings.world_axis = {};
				}
				ImGui::Checkbox("Enabled", &world.settings.world_axis.enabled);
				ImGui::DragFloat2("Position", glm::value_ptr(world.settings.world_axis.position), 0.05, -1, 1);
				ImGui::DragFloat("Scale", &world.settings.world_axis.scale, .05, 0, 1);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Physics")) {
				#ifndef OS_MACOS
				ImGui::Text("AABB Rendering");
				ImGui::DragFloat("Line Width", &world.canvas.box_rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				#endif

				ImGui::Checkbox("Handle Collision", &world.settings.handle_collision);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Audio")) {
				for (int i = 0; i < world.sounds.as_array.size(); i++) {
					ImGui::PushID(i);

					const Audio& sound = *world.sounds.as_array[i];

					if (ImGui::Button("Play")) {
						audio_device_play(world.audio_device, sound);
					}

					ImGui::SameLine();
					if (ImGui::Button("Loop")) {
						audio_device_play_looped(world.audio_device, sound);
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(audio_device_is_playing(world.audio_device, sound) == false);
					if (ImGui::Button("Stop")) {
						audio_device_stop(world.audio_device, sound);
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					ImGui::Text(mu::Str(mu::file_get_base_name(sound.file_path), mu::memory::tmp()).c_str());

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNode(mu::str_tmpf("Scenery ({})", world.scenery.scenery_template.name).c_str())) {
				world.scenery.should_be_loaded = ImGui::Button("Reload");
				ImGui::SameLine();
				if (ImGui::BeginCombo("##scenery.name", world.scenery.scenery_template.name.c_str())) {
					for (const auto& [name, scenery_template] : world.scenery_templates) {
						if (ImGui::Selectable(name.c_str(), scenery_template.name == world.scenery.scenery_template.name)) {
							world.scenery.scenery_template = scenery_template;
							world.scenery.should_be_loaded = true;
						}
					}

					ImGui::EndCombo();
				}

				std::function<void(Field&,bool)> render_field_imgui;
				render_field_imgui = [&render_field_imgui, current_angle_max=world.settings.current_angle_max](Field& field, bool is_root) {
					if (!is_root) {
						if (!ImGui::TreeNode(mu::str_tmpf("Field {}", field.name).c_str())) {
							return;
						}
					}

					if (ImGui::Button("Reset State")) {
						field.state = field.initial_state;
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

					ImGui::Checkbox("Visible", &field.state.visible);

					ImGui::DragFloat3("Translation", glm::value_ptr(field.state.translation));
					MyImGui::SliderAngle3("Rotation", &field.state.rotation, current_angle_max);

					ImGui::BulletText("Sub Fields:");
					for (auto& subfield : field.subfields) {
						render_field_imgui(subfield, false);
					}

					ImGui::BulletText("TerrMesh: %d", (int)field.terr_meshes.size());
					for (auto& terr_mesh : field.terr_meshes) {
						if (ImGui::TreeNode(terr_mesh.name.c_str())) {
							if (ImGui::Button("Reset State")) {
								terr_mesh.state = terr_mesh.initial_state;
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

							ImGui::Checkbox("Visible", &terr_mesh.state.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.state.translation));
							MyImGui::SliderAngle3("Rotation", &terr_mesh.state.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Pict2: %d", (int)field.pictures.size());
					for (auto& picture : field.pictures) {
						if (ImGui::TreeNode(picture.name.c_str())) {
							if (ImGui::Button("Reset State")) {
								picture.state = picture.initial_state;
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

							ImGui::Checkbox("Visible", &picture.state.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(picture.state.translation));
							MyImGui::SliderAngle3("Rotation", &picture.state.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Meshes: %d", (int)field.meshes.size());
					for (auto& mesh : field.meshes) {
						ImGui::Text("%s", mesh.name.c_str());
					}

					ImGui::BulletText("Ground Objects: %d", (int)field.gobs.size());
					for (const auto& gob : field.gobs) {
						ImGui::Text("%s", gob.name.c_str());
					}

					if (!is_root) {
						ImGui::TreePop();
					}
				};
				render_field_imgui(world.scenery.root_fld, true);

				ImGui::TreePop();
			}

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Aircrafts {}:", world.aircrafts.size()).c_str());

			{
				const bool should_add_aircraft = ImGui::Button("Add##aircraft");
				static mu::Str aircraft_to_add = world.aircraft_templates.begin()->first;
				ImGui::SameLine();
				if (ImGui::BeginCombo("##new_aircraft", world.aircraft_templates[aircraft_to_add].short_name.c_str())) {
					for (const auto& [name, aircraft] : world.aircraft_templates) {
						if (ImGui::Selectable(name.c_str(), name == aircraft_to_add)) {
							aircraft_to_add = name;
						}
					}

					ImGui::EndCombo();
				}

				if (should_add_aircraft) {
					int tracked_model_index = -1;
					for (int i = 0; i < world.aircrafts.size(); i++) {
						if (world.camera.aircraft == &world.aircrafts[i]) {
							tracked_model_index = i;
							break;
						}
					}

					world.aircrafts.push_back(aircraft_new(world.aircraft_templates[aircraft_to_add]));

					if (tracked_model_index != -1) {
						world.camera.aircraft = &world.aircrafts[tracked_model_index];
					}
				}
			}

			for (int i = 0; i < world.aircrafts.size(); i++) {
				Aircraft& aircraft = world.aircrafts[i];

				AircraftTemplate* aircraft_template = nullptr;
				for (auto& [_k, a] : world.aircraft_templates) {
					if (a.short_name == aircraft.aircraft_template.short_name) {
						aircraft_template = &a;
						break;
					}
				}
				mu_assert(aircraft_template);

				if (ImGui::TreeNode(mu::str_tmpf("[{}] {}", i, aircraft_template->short_name).c_str())) {
					world.aircrafts[i].should_be_loaded = ImGui::Button("Reload");
					ImGui::SameLine();
					if (ImGui::BeginCombo("DNM", mu::Str(mu::file_get_base_name(world.aircrafts[i].aircraft_template.dnm), mu::memory::tmp()).c_str())) {
						for (const auto& [name, aircraft_template] : world.aircraft_templates) {
							if (ImGui::Selectable(name.c_str(), aircraft_template.short_name == world.aircrafts[i].aircraft_template.short_name)) {
								world.aircrafts[i].aircraft_template = aircraft_template;
								world.aircrafts[i].should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}

					world.aircrafts[i].should_be_removed = ImGui::Button("Remove");

					if (ImGui::Button("Reset State")) {
						aircraft.state = {};
					}
					if (ImGui::Button("Reset All")) {
						aircraft.state = {};
						for (auto& mesh : aircraft.model.meshes) {
							mesh.state = mesh.initial_state;
						}
					}

					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							aircraft_set_start(world.aircrafts[i], StartInfo { .name="-NULL-" });
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								aircraft_set_start(world.aircrafts[i], start_infos[start_info_index]);
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &aircraft.state.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(aircraft.state.translation));

					glm::vec3 now_rotation {
						aircraft.state.angles.roll,
						aircraft.state.angles.pitch,
						aircraft.state.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							aircraft.state.angles,
							now_rotation.z - aircraft.state.angles.yaw,
							now_rotation.y - aircraft.state.angles.pitch,
							now_rotation.x - aircraft.state.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(aircraft.state.angles.up, aircraft.state.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(aircraft.state.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(aircraft.state.angles.front));
					ImGui::EndDisabled();

					ImGui::DragFloat("Speed", &aircraft.state.speed, 0.05f, MIN_SPEED, MAX_SPEED);

					ImGui::Checkbox("Render AABB", &aircraft.model.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(aircraft.model.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(aircraft.model.current_aabb.max));

					if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::DragFloat("Landing Gear", &aircraft.state.landing_gear_alpha, 0.01, 0, 1);
						ImGui::SliderFloat("Throttle", &aircraft.state.throttle, 0.0f, 1.0f);

						ImGui::BeginDisabled();
						ImGui::SliderFloat("Engine Speed %%", &aircraft.state.engine_speed, 0.0f, 1.0f);
						ImGui::EndDisabled();

						ImGui::Checkbox("Afterburner Reheat", &aircraft.state.afterburner_reheat_enabled);

						ImGui::TreePop();
					}

					size_t light_sources_count = 0;
					for (const auto& mesh : aircraft.model.meshes) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
					}

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", aircraft.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&aircraft, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							if (ImGui::Button("Reset")) {
								mesh.state = mesh.initial_state;
							}

							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.state.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.state.translation));
							MyImGui::SliderAngle3("rotation", &mesh.state.rotation, current_angle_max);

							ImGui::Text(mu::str_tmpf("{}", mesh.animation_type).c_str());

							ImGui::BulletText(mu::str_tmpf("Children: ({})", mesh.children.size()).c_str());
							ImGui::Indent();
							for (auto& child : mesh.children) {
								render_mesh_ui(child);
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
											model_unload_from_gpu(aircraft.model);
											model_load_to_gpu(aircraft.model);
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
					for (auto& child : aircraft.model.meshes) {
						render_mesh_ui(child);
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Ground Objs {}:", world.ground_objs.size()).c_str());

			{
				const bool should_add_gro_obj = ImGui::Button("Add##gro_obj");
				static mu::Str gro_obj_to_add = world.ground_obj_templates.begin()->first;
				ImGui::SameLine();
				if (ImGui::BeginCombo("##new_ground_obj", world.ground_obj_templates[gro_obj_to_add].short_name.c_str())) {
					for (const auto& [name, _gro] : world.ground_obj_templates) {
						if (ImGui::Selectable(name.c_str(), name == gro_obj_to_add)) {
							gro_obj_to_add = name;
						}
					}

					ImGui::EndCombo();
				}

				if (should_add_gro_obj) {
					world.ground_objs.push_back(ground_obj_new(world.ground_obj_templates[gro_obj_to_add], {}, {}));
				}
			}

			for (int i = 0; i < world.ground_objs.size(); i++) {
				auto& gro = world.ground_objs[i];

				GroundObjTemplate* gro_obj_template = nullptr;
				for (auto& [_k, a] : world.ground_obj_templates) {
					if (a.short_name == gro.ground_obj_template.short_name) {
						gro_obj_template = &a;
						break;
					}
				}
				mu_assert(gro_obj_template);

				if (ImGui::TreeNode(mu::str_tmpf("[{}] {}", i, gro_obj_template->short_name).c_str())) {
					gro.should_be_loaded = ImGui::Button("Reload");
					ImGui::SameLine();
					if (ImGui::BeginCombo("Name", gro.ground_obj_template.short_name.c_str())) {
						for (const auto& [name, gro_obj_template] : world.ground_obj_templates) {
							if (ImGui::Selectable(name.c_str(), gro_obj_template.short_name == gro.ground_obj_template.short_name)) {
								gro.ground_obj_template = gro_obj_template;
								gro.should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}

					gro.should_be_removed = ImGui::Button("Remove");

					if (ImGui::Button("Reset State")) {
						gro.state = {};
					}
					if (ImGui::Button("Reset All")) {
						gro.state = {};
						for (auto& mesh : gro.model.meshes) {
							mesh.state = mesh.initial_state;
						}
					}

					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							gro.state.translation = {};
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								gro.state.translation = start_infos[start_info_index].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &gro.state.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(gro.state.translation));

					glm::vec3 now_rotation {
						gro.state.angles.roll,
						gro.state.angles.pitch,
						gro.state.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							gro.state.angles,
							now_rotation.z - gro.state.angles.yaw,
							now_rotation.y - gro.state.angles.pitch,
							now_rotation.x - gro.state.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(gro.state.angles.up, gro.state.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(gro.state.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(gro.state.angles.front));
					ImGui::EndDisabled();

					ImGui::DragFloat("Speed", &gro.state.speed, 0.05f, MIN_SPEED, MAX_SPEED);

					ImGui::Checkbox("Render AABB", &gro.model.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(gro.model.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(gro.model.current_aabb.max));

					size_t light_sources_count = 0;
					for (const auto& mesh : gro.model.meshes) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
					}

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", gro.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&gro, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							if (ImGui::Button("Reset")) {
								mesh.state = mesh.initial_state;
							}

							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.state.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.state.translation));
							MyImGui::SliderAngle3("rotation", &mesh.state.rotation, current_angle_max);

							ImGui::Text(mu::str_tmpf("{}", mesh.animation_type).c_str());

							ImGui::BulletText(mu::str_tmpf("Children: ({})", mesh.children.size()).c_str());
							ImGui::Indent();
							for (auto& child : mesh.children) {
								render_mesh_ui(child);
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
											model_unload_from_gpu(gro.model);
											model_load_to_gpu(gro.model);
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
					for (auto& child : gro.model.meshes) {
						render_mesh_ui(child);
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}
		}
		ImGui::End();

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Logs")) {
			ImGui::Checkbox("Auto-Scroll", &world.imgui_window_logger.auto_scrolling);
			ImGui::SameLine();
			ImGui::Checkbox("Wrapped", &world.imgui_window_logger.wrapped);
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				world.imgui_window_logger = {};
			}

			if (ImGui::BeginChild("logs child", {}, false, world.imgui_window_logger.wrapped? 0:ImGuiWindowFlags_HorizontalScrollbar)) {
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0, 0});
				ImGuiListClipper clipper(world.imgui_window_logger.logs.size());
				while (clipper.Step()) {
					for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
						if (world.imgui_window_logger.wrapped) {
							ImGui::TextWrapped("%s", world.imgui_window_logger.logs[i].c_str());
						} else {
							auto log = world.imgui_window_logger.logs[i];
							ImGui::TextUnformatted(&log[0], &log[log.size()-1]);
						}
					}
				}
				ImGui::PopStyleVar();

				// scroll
				if (world.imgui_window_logger.auto_scrolling) {
					if (world.imgui_window_logger.last_scrolled_line != world.imgui_window_logger.logs.size()) {
						world.imgui_window_logger.last_scrolled_line = world.imgui_window_logger.logs.size();
						ImGui::SetScrollHereY();
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();

		// render overlay
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
			for (const auto& line : world.canvas.overlay_text_list) {
				ImGui::TextWrapped(mu::str_tmpf("> {}", line).c_str());
			}
		}
		ImGui::End();

		// ImGui::ShowDemoWindow();

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void loop_timer_update(World& world) {
		auto& self = world.loop_timer;
		auto& settings = world.settings;

		auto now_millis = time_now_millis();
		const uint64_t delta_time_millis = now_millis - self._last_time_millis;
		self._last_time_millis = now_millis;

		if (settings.should_limit_fps) {
			int millis_diff = (1000 / settings.fps_limit) - delta_time_millis;
			self._millis_till_render = clamp<int64_t>(self._millis_till_render - millis_diff, 0, 1000);
			if (self._millis_till_render > 0) {
				self.ready = false;
				return;
			} else {
				self._millis_till_render = 1000 / settings.fps_limit;
				self.delta_time = 1.0f/ settings.fps_limit;
			}
		} else {
			self.delta_time = (double) delta_time_millis / 1000;
		}

		if (self.delta_time < 0.0001f) {
			self.delta_time = 0.0001f;
		}

		self.ready = true;
	}

	void audio_init(World& world) {
		audio_device_init(&world.audio_device);
		sounds_load(world.sounds);
	}

	void audio_free(World& world) {
		sounds_free(world.sounds);
		audio_device_free(world.audio_device);
	}

	void projection_init(World& world) {
		signal_listen(world.signals.wnd_configs_changed);
	}

	void canvas_init(World& world) {
		auto& self = world.canvas;

		signal_listen(world.signals.wnd_configs_changed);

		self.meshes_program = gl_program_new(
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
			self.axis_rendering.points_count = buffer.size();

			glGenVertexArrays(1, &self.axis_rendering.vao);
			glBindVertexArray(self.axis_rendering.vao);
				glGenBuffers(1, &self.axis_rendering.vbo);
				glBindBuffer(GL_ARRAY_BUFFER, self.axis_rendering.vbo);
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

			gl_process_errors();
		}

		self.lines_program = gl_program_new(
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
			self.box_rendering.points_count = buffer.size();

			glGenVertexArrays(1, &self.box_rendering.vao);
			glBindVertexArray(self.box_rendering.vao);
				glGenBuffers(1, &self.box_rendering.vbo);
				glBindBuffer(GL_ARRAY_BUFFER, self.box_rendering.vbo);
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

			gl_process_errors();
		}

		self.picture2d_program = gl_program_new(
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

		// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
		self.ground_program = gl_program_new(
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

		glGenVertexArrays(1, &self.dummy_vao);

		// groundtile
		self.groundtile_surface = IMG_Load(ASSETS_DIR "/misc/groundtile.png");
		if (self.groundtile_surface == nullptr || self.groundtile_surface->pixels == nullptr) {
			mu::panic("failed to load groundtile.png");
		}
		glGenTextures(1, &self.groundtile_texture);
		glBindTexture(GL_TEXTURE_2D, self.groundtile_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.groundtile_surface->w, self.groundtile_surface->h, 0, GL_RED, GL_UNSIGNED_BYTE, self.groundtile_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		self.sprite_program = gl_program_new(
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

		// zl_sprite
		self.zl_sprite_surface = IMG_Load(ASSETS_DIR "/misc/rwlight.png");
		if (self.zl_sprite_surface == nullptr || self.zl_sprite_surface->pixels == nullptr) {
			mu::panic("failed to load rwlight.png");
		}
		glGenTextures(1, &self.zl_sprite_texture);
		glBindTexture(GL_TEXTURE_2D, self.zl_sprite_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.zl_sprite_surface->w, self.zl_sprite_surface->h, 0, GL_RED, GL_UNSIGNED_INT, self.zl_sprite_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// text
		self.text_rendering.program = gl_program_new(
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
		glGenVertexArrays(1, &self.text_rendering.vao);
		glGenBuffers(1, &self.text_rendering.vbo);
		glBindVertexArray(self.text_rendering.vao);
			glBindBuffer(GL_ARRAY_BUFFER, self.text_rendering.vbo);
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
		for (uint8_t c = 0; c < self.text_rendering.glyphs.size(); c++) {
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

			self.text_rendering.glyphs[c] = Glyph {
				.texture = text_texture,
				.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
				.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
				.advance = uint32_t(face->glyph->advance.x)
			};
		}

		gl_process_errors();
	}

	void canvas_free(World& world) {
		auto& self = world.canvas;

		glUseProgram(0);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);

		// text rendering
		gl_program_free(self.text_rendering.program);
		glDeleteBuffers(1, &self.text_rendering.vbo);
		glDeleteVertexArrays(1, &self.text_rendering.vao);
		for (auto& g : self.text_rendering.glyphs) {
			glDeleteTextures(1, &g.texture);
		}

		// zl sprite
		SDL_FreeSurface(self.zl_sprite_surface);
		glDeleteTextures(1, &self.zl_sprite_texture);

		// groundtile
		SDL_FreeSurface(self.groundtile_surface);
		glDeleteTextures(1, &self.groundtile_texture);

		glDeleteVertexArrays(1, &self.dummy_vao);

		// box rendering
		glDeleteBuffers(1, &self.box_rendering.vbo);
		glDeleteVertexArrays(1, &self.box_rendering.vao);

		// axis rendering
		glDeleteBuffers(1, &self.axis_rendering.vbo);
		glDeleteVertexArrays(1, &self.axis_rendering.vao);

		gl_program_free(self.meshes_program);
		gl_program_free(self.lines_program);
		gl_program_free(self.picture2d_program);
		gl_program_free(self.ground_program);
		gl_program_free(self.sprite_program);
	}

	void canvas_rendering_begin(World& world) {
		auto& self = world.canvas;

		if (signal_handle(world.signals.wnd_configs_changed)) {
			int w, h;
			SDL_GL_GetDrawableSize(world.sdl_window, &w, &h);
			glViewport(0, 0, w, h);
		}

		glEnable(GL_DEPTH_TEST);
		glClearDepth(1);
		glClearColor(world.scenery.root_fld.sky_color.x, world.scenery.root_fld.sky_color.y, world.scenery.root_fld.sky_color.z, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (world.settings.rendering.smooth_lines) {
			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(world.settings.rendering.line_width);
            #endif
		} else {
			glDisable(GL_LINE_SMOOTH);
		}
		glPointSize(world.settings.rendering.point_size);
		glPolygonMode(GL_FRONT_AND_BACK, world.settings.rendering.polygon_mode);
	}

	void canvas_rendering_end(World& world) {
		auto& self = world.canvas;

		SDL_GL_SwapWindow(world.sdl_window);
		gl_process_errors();

		self._arena = {};
		self.overlay_text_list = mu::Vec<mu::Str>(&self._arena);
		self.text_list         = mu::Vec<TextRenderReq>(&self._arena);
		self.axis_list         = mu::Vec<glm::mat4>(&self._arena);
		self.boxes_list        = mu::Vec<Box>(&self._arena);
		self.zlpoints_list     = mu::Vec<ZLPoint>(&self._arena);
	}

	void _camera_update_model_tracking_mode(World& world) {
		auto& self = world.camera;
		auto& events = world.events;

		const float velocity = 0.40f * world.loop_timer.delta_time;
		if (events.camera_tracking_up) {
			self.yaw += velocity;
		}
		if (events.camera_tracking_down) {
			self.yaw -= velocity;
		}
		if (events.camera_tracking_right) {
			self.pitch += velocity;
		}
		if (events.camera_tracking_left) {
			self.pitch -= velocity;
		}

		if (self.enable_rotating_around) {
			self.pitch += (7 * world.loop_timer.delta_time) / DEGREES_MAX * RADIANS_MAX;
		}

		constexpr float CAMERA_ANGLES_MAX = 89.0f / DEGREES_MAX * RADIANS_MAX;
		self.yaw = clamp(self.yaw, -CAMERA_ANGLES_MAX, CAMERA_ANGLES_MAX);

		auto model_transformation = local_euler_angles_matrix(self.aircraft->state.angles, self.aircraft->state.translation);

		model_transformation = glm::rotate(model_transformation, self.pitch, glm::vec3{0, -1, 0});
		model_transformation = glm::rotate(model_transformation, self.yaw, glm::vec3{-1, 0, 0});
		self.position = model_transformation * glm::vec4{0, 0, -self.distance_from_model, 1};
	}

	void _camera_update_flying_mode(World& world) {
		auto& self = world.camera;
		auto& events = world.events;

		// move with keyboard
		const float velocity = self.movement_speed * world.loop_timer.delta_time;
		if (events.camera_flying_up) {
			self.position += self.front * velocity;
		}
		if (events.camera_flying_down) {
			self.position -= self.front * velocity;
		}
		if (events.camera_flying_right) {
			self.position += self.right * velocity;
		}
		if (events.camera_flying_left) {
			self.position -= self.right * velocity;
		}

		// roate with mouse
		if (events.camera_flying_rotate_enabled) {
			self.yaw   += (events.mouse_pos.x - self.last_mouse_pos.x) * self.mouse_sensitivity / 1000;
			self.pitch -= (events.mouse_pos.y - self.last_mouse_pos.y) * self.mouse_sensitivity / 1000;

			// make sure that when pitch is out of bounds, screen doesn't get flipped
			constexpr float CAMERA_PITCH_MAX = 89.0f / DEGREES_MAX * RADIANS_MAX;
			self.pitch = clamp(self.pitch, -CAMERA_PITCH_MAX, CAMERA_PITCH_MAX);
		}
		self.last_mouse_pos = events.mouse_pos;

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

	void camera_update(World& world) {
		if (world.camera.aircraft) {
			_camera_update_model_tracking_mode(world);
		} else {
			_camera_update_flying_mode(world);
		}
	}

	void projection_update(World& world) {
		auto& self = world.projection;

		if (signal_handle(world.signals.wnd_configs_changed) && !world.settings.custom_aspect_ratio) {
			int w, h;
			SDL_GL_GetDrawableSize(world.sdl_window, &w, &h);
			self.aspect = (float) w / h;
		}
	}

	void cached_matrices_recalc(World& world) {
		auto& self = world.mats;
		auto& camera = world.camera;
		auto& proj = world.projection;

		self.view = camera_calc_view(camera);
		self.view_inverse = glm::inverse(self.view);

		self.projection = projection_calc_mat(proj);
		self.projection_inverse = glm::inverse(self.projection);

		self.projection_view = self.projection * self.view;
		self.proj_inv_view_inv = self.view_inverse * self.projection_inverse;
	}

	void events_collect(World& world) {
		auto& self = world.events;

		self = {};

		const Uint8* sdl_keyb_pressed = SDL_GetKeyboardState(nullptr);
		self.stick_right           = sdl_keyb_pressed[SDL_SCANCODE_RIGHT];
		self.stick_left            = sdl_keyb_pressed[SDL_SCANCODE_LEFT];
		self.stick_front           = sdl_keyb_pressed[SDL_SCANCODE_UP];
		self.stick_back            = sdl_keyb_pressed[SDL_SCANCODE_DOWN];
		self.rudder_right          = sdl_keyb_pressed[SDL_SCANCODE_C];
		self.rudder_left           = sdl_keyb_pressed[SDL_SCANCODE_Z];
		self.throttle_increase     = sdl_keyb_pressed[SDL_SCANCODE_Q];
		self.throttle_decrease     = sdl_keyb_pressed[SDL_SCANCODE_A];

		self.camera_tracking_up    = sdl_keyb_pressed[SDL_SCANCODE_U];
		self.camera_tracking_down  = sdl_keyb_pressed[SDL_SCANCODE_M];
		self.camera_tracking_right = sdl_keyb_pressed[SDL_SCANCODE_K];
		self.camera_tracking_left  = sdl_keyb_pressed[SDL_SCANCODE_H];

		self.camera_flying_up      = sdl_keyb_pressed[SDL_SCANCODE_W];
		self.camera_flying_down    = sdl_keyb_pressed[SDL_SCANCODE_S];
		self.camera_flying_right   = sdl_keyb_pressed[SDL_SCANCODE_D];
		self.camera_flying_left    = sdl_keyb_pressed[SDL_SCANCODE_A];

		Uint32 sdl_mouse_state = SDL_GetMouseState(&self.mouse_pos.x, &self.mouse_pos.y);
		self.camera_flying_rotate_enabled = sdl_mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					signal_fire(world.signals.quit);
					break;
				case SDLK_TAB:
					self.afterburner_toggle = true;
					break;
				case 'f':
					world.settings.fullscreen = !world.settings.fullscreen;
					signal_fire(world.signals.wnd_configs_changed);
					if (world.settings.fullscreen) {
						if (SDL_SetWindowFullscreen(world.sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
							mu::panic(SDL_GetError());
						}
					} else {
						if (SDL_SetWindowFullscreen(world.sdl_window, SDL_WINDOW_OPENGL)) {
							mu::panic(SDL_GetError());
						}
					}
					break;
				default:
					break;
				}
			} else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				signal_fire(world.signals.wnd_configs_changed);
			} else if (event.type == SDL_QUIT) {
				signal_fire(world.signals.quit);
			}
		}
	}

	void models_handle_collision(World& world) {
		if (!world.settings.handle_collision) {
			return;
		}

		mu::Vec<Model*> models(mu::memory::tmp());
		mu::Vec<bool> visible(mu::memory::tmp());
		for (auto& a : world.aircrafts) {
			models.push_back(&a.model);
			visible.push_back(a.state.visible);
		}
		for (auto& g : world.ground_objs) {
			models.push_back(&g.model);
			visible.push_back(g.state.visible);
		}

		// between models
		for (int i = 0; i < models.size()-1; i++) {
			if (visible[i] == false) {
				world.canvas.overlay_text_list.push_back(mu::str_tmpf("model[{}] invisible and won't intersect", i));
				continue;
			}

			glm::vec3 i_color {0, 0, 1};

			for (int j = i+1; j < models.size(); j++) {
				if (visible[j] == false) {
					world.canvas.overlay_text_list.push_back(mu::str_tmpf("model[{}] invisible and won't intersect", j));
					continue;
				}

				glm::vec3 j_color {0, 0, 1};

				if (aabbs_intersect(models[i]->current_aabb, models[j]->current_aabb)) {
					world.canvas.overlay_text_list.push_back(mu::str_tmpf("model[{}] intersects model[{}]", i, j));
					j_color = i_color = {1, 0, 0};
				} else {
					world.canvas.overlay_text_list.push_back(mu::str_tmpf("model[{}] doesn't intersect model[{}]", i, j));
				}

				if (models[j]->render_aabb) {
					auto aabb = models[j]->current_aabb;
					world.canvas.boxes_list.push_back(Box {
						.translation = aabb.min,
						.scale = aabb.max - aabb.min,
						.color = j_color,
					});
				}
			}

			if (models[i]->render_aabb) {
				auto aabb = models[i]->current_aabb;
				world.canvas.boxes_list.push_back(Box {
					.translation = aabb.min,
					.scale = aabb.max - aabb.min,
					.color = i_color,
				});
			}
		}

		// if one single model, just add its aabb for debugging
		if (models.size() == 1 && models[0]->render_aabb) {
			auto aabb = models[0]->current_aabb;
			world.canvas.boxes_list.push_back(Box {
				.translation = aabb.min,
				.scale = aabb.max - aabb.min,
				.color = glm::vec3{0, 0, 1.0f},
			});
		}
	}

	void ground_objs_init(World& world) {
		signal_listen(world.signals.scenery_loaded);

		world.ground_obj_templates = ground_obj_templates_from_dir(ASSETS_DIR "/ground");
	}

	void ground_objs_free(World& world) {
		for (auto& gro : world.ground_objs) {
			model_unload_from_gpu(gro.model);
		}
	}

	void _ground_objs_load_from_files(World& world) {
		for (int i = 0; i < world.ground_objs.size(); i++) {
			if (world.ground_objs[i].should_be_loaded) {
				model_unload_from_gpu(world.ground_objs[i].model);

				auto& main = world.ground_objs[i].ground_obj_template.main;
				if (main.ends_with(".srf")) {
					world.ground_objs[i].model = model_from_srf_file(world.ground_objs[i].ground_obj_template.main);
				} else {
					world.ground_objs[i].model = model_from_dnm_file(world.ground_objs[i].ground_obj_template.main);
				}
				model_load_to_gpu(world.ground_objs[i].model);

				world.ground_objs[i].dat = datmap_from_dat_file(world.ground_objs[i].ground_obj_template.dat);

				mu::log_debug("loaded '{}'", world.ground_objs[i].ground_obj_template.main);
				world.ground_objs[i].should_be_loaded = false;
			}
		}
	}

	void _ground_objs_autoremove(World& world) {
		for (int i = 0; i < world.ground_objs.size(); i++) {
			if (world.ground_objs[i].should_be_removed) {
				world.ground_objs.erase(world.ground_objs.begin()+i);
				i--;
			}
		}
	}

	void _ground_objs_apply_physics(World& world) {
		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.state.visible) {
				continue;
			}

			// apply model transformation
			const auto model_transformation = local_euler_angles_matrix(gro.state.angles, gro.state.translation);

			gro.state.translation += ((float)world.loop_timer.delta_time * gro.state.speed) * gro.state.angles.front;

			// transform AABB (estimate new AABB after rotation)
			{
				// translate AABB
				gro.model.current_aabb.min = gro.model.current_aabb.max = gro.state.translation;

				// new rotated AABB (no translation)
				const auto model_rotation = glm::mat3(model_transformation);
				const auto rotated_min = model_rotation * gro.model.initial_aabb.min;
				const auto rotated_max = model_rotation * gro.model.initial_aabb.max;
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
							gro.model.current_aabb.min[i] += e;
							gro.model.current_aabb.max[i] += f;
						} else {
							gro.model.current_aabb.min[i] += f;
							gro.model.current_aabb.max[i] += e;
						}
					}
				}
			}

			// start with root meshes
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (auto& mesh : gro.model.meshes) {
				mesh.transformation = model_transformation;
				meshes_stack.push_back(&mesh);
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				if (mesh->state.visible == false) {
					continue;
				}

				// apply mesh transformation
				mesh->transformation = glm::translate(mesh->transformation, mesh->state.translation);
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[2], glm::vec3{0, 0, 1});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[1], glm::vec3{1, 0, 0});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[0], glm::vec3{0, -1, 0});

				// push children
				for (auto& child : mesh->children) {
					child.transformation = mesh->transformation;
					meshes_stack.push_back(&child);
				}
			}
		}
	}

	void ground_objs_update(World& world) {
		if (signal_handle(world.signals.scenery_loaded)) {
			for (auto& gob : world.ground_objs) {
				gob.should_be_removed = true;
			}

			mu::Vec<GroundObjSpawn*> gob_spawns(mu::memory::tmp());
			auto fields = field_list_recursively(world.scenery.root_fld);
			for (auto f : fields) {
				for (auto& gob : f->gobs) {
					gob_spawns.push_back(&gob);
				}
			}

			for (auto gob_s : gob_spawns) {
				auto tmpl_it = world.ground_obj_templates.find(gob_s->name);
				if (tmpl_it != world.ground_obj_templates.end()) {
					const auto& [_, gob_tmpl] = *tmpl_it;
					world.ground_objs.push_back(ground_obj_new(gob_tmpl, gob_s->pos, gob_s->rotation));
				}
			}
		}

		_ground_objs_load_from_files(world);
		_ground_objs_autoremove(world);

		_ground_objs_apply_physics(world);
	}

	void ground_objs_render(World& world) {
		gl_program_use(world.canvas.meshes_program);

		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.state.visible) {
				continue;
			}

			const auto model_transformation = local_euler_angles_matrix(gro.state.angles, gro.state.translation);

			// start with root meshes
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (auto& mesh : gro.model.meshes) {
				meshes_stack.push_back(&mesh);
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				if (!mesh->state.visible) {
					continue;
				}

				if (mesh->render_cnt_axis) {
					world.canvas.axis_list.push_back(glm::translate(glm::identity<glm::mat4>(), mesh->cnt));
				}

				if (mesh->render_pos_axis) {
					world.canvas.axis_list.push_back(mesh->transformation);
				}

				gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", world.mats.projection_view * mesh->transformation);
				gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", mesh->is_light_source);

				glBindVertexArray(mesh->gpu.vao);
				glDrawArrays(mesh->is_light_source? world.settings.rendering.light_primitives_type : world.settings.rendering.regular_primitives_type, 0, mesh->gpu.array_count);

				// push children
				for (auto& child : mesh->children) {
					meshes_stack.push_back(&child);
				}
			}
		}
	}

	void aircrafts_init(World& world) {
		signal_listen(world.signals.scenery_loaded);

		world.aircraft_templates = aircraft_templates_from_dir(ASSETS_DIR "/aircraft");

		auto ys11 = aircraft_new(world.aircraft_templates["ys11"]);
		world.aircrafts.push_back(ys11);

		world.camera.aircraft = &world.aircrafts[0];
	}

	void aircrafts_free(World& world) {
		for (auto& aircraft : world.aircrafts) {
			if (aircraft.engine_sound) {
				audio_device_stop(world.audio_device, *aircraft.engine_sound);
			}
			model_unload_from_gpu(aircraft.model);
		}
	}

	// allow user control over camera tracked aircraft
	void _tracked_aircraft_control(World& world) {
		if (world.camera.aircraft == nullptr) {
			return;
		}
		auto& self = *world.camera.aircraft;

		float delta_yaw = 0, delta_roll = 0, delta_pitch = 0;
		constexpr auto ROTATE_SPEED = 12.0f / DEGREES_MAX * RADIANS_MAX;
		if (world.events.stick_back) {
			delta_pitch -= ROTATE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.stick_front) {
			delta_pitch += ROTATE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.stick_left) {
			delta_roll -= ROTATE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.stick_right) {
			delta_roll += ROTATE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.rudder_right) {
			delta_yaw -= ROTATE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.rudder_left) {
			delta_yaw += ROTATE_SPEED * world.loop_timer.delta_time;
		}
		local_euler_angles_rotate(self.state.angles, delta_yaw, delta_pitch, delta_roll);

		if (world.events.afterburner_toggle) {
			self.state.afterburner_reheat_enabled = ! self.state.afterburner_reheat_enabled;
		}
		if (self.state.afterburner_reheat_enabled && self.state.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
			self.state.throttle = AFTERBURNER_THROTTLE_THRESHOLD;
		}

		if (world.events.throttle_increase) {
			self.state.throttle += THROTTLE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.throttle_decrease) {
			self.state.throttle -= THROTTLE_SPEED * world.loop_timer.delta_time;
		}

		// only currently controlled model has audio
		int audio_index = self.state.engine_speed * (world.sounds.props.size()-1);

		Audio* audio;
		if (self.model.has_propellers) {
			audio = &world.sounds.props[audio_index];
		} else if (self.state.afterburner_reheat_enabled && self.model.has_afterburner) {
			audio = &world.sounds.burner;
		} else {
			audio = &world.sounds.engines[audio_index];
		}

		if (self.engine_sound != audio) {
			if (self.engine_sound) {
				audio_device_stop(world.audio_device, *self.engine_sound);
			}
			self.engine_sound = audio;
			audio_device_play_looped(world.audio_device, *self.engine_sound);
		}
	}

	void _aircrafts_load_from_files(World& world) {
		for (int i = 0; i < world.aircrafts.size(); i++) {
			if (world.aircrafts[i].should_be_loaded) {
				model_unload_from_gpu(world.aircrafts[i].model);
				world.aircrafts[i].model = model_from_dnm_file(world.aircrafts[i].aircraft_template.dnm);
				model_load_to_gpu(world.aircrafts[i].model);

				world.aircrafts[i].dat = datmap_from_dat_file(world.aircrafts[i].aircraft_template.dat);

				mu::log_debug("loaded '{}'", world.aircrafts[i].aircraft_template.dnm);
				world.aircrafts[i].should_be_loaded = false;
			}
		}
	}

	void _aircrafts_autoremove(World& world) {
		for (int i = 0; i < world.aircrafts.size(); i++) {
			if (world.aircrafts[i].should_be_removed) {
				int tracked_model_index = -1;
				for (int i = 0; i < world.aircrafts.size(); i++) {
					if (world.camera.aircraft == &world.aircrafts[i]) {
						tracked_model_index = i;
						break;
					}
				}

				world.aircrafts.erase(world.aircrafts.begin()+i);

				if (tracked_model_index > 0 && tracked_model_index >= i) {
					world.camera.aircraft = &world.aircrafts[tracked_model_index-1];
				} else if (tracked_model_index == 0 && i == 0) {
					world.camera.aircraft = world.aircrafts.empty()? nullptr : &world.aircrafts[0];
				}

				i--;
			}
		}
	}

	void _aircrafts_apply_physics(World& world) {
		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];

			if (!aircraft.state.visible) {
				continue;
			}

			aircraft.anti_coll_lights.time_left_secs -= world.loop_timer.delta_time;
			if (aircraft.anti_coll_lights.time_left_secs < 0) {
				aircraft.anti_coll_lights.time_left_secs = ANTI_COLL_LIGHT_PERIOD;
				aircraft.anti_coll_lights.visible = ! aircraft.anti_coll_lights.visible;
			}

			// apply model transformation
			const auto model_transformation = local_euler_angles_matrix(aircraft.state.angles, aircraft.state.translation);

			aircraft.state.throttle = clamp(aircraft.state.throttle, 0.0f, 1.0f);
			if (aircraft.state.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
				aircraft.state.afterburner_reheat_enabled = false;
			}

			if (aircraft.state.engine_speed < aircraft.state.throttle) {
				aircraft.state.engine_speed += world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
				aircraft.state.engine_speed = clamp(aircraft.state.engine_speed, 0.0f, aircraft.state.throttle);
			} else if (aircraft.state.engine_speed > aircraft.state.throttle) {
				aircraft.state.engine_speed -= world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
				aircraft.state.engine_speed = clamp(aircraft.state.engine_speed, aircraft.state.throttle, 1.0f);
			}
			aircraft.state.speed = aircraft.state.engine_speed * MAX_SPEED + MIN_SPEED;

			aircraft.state.translation += ((float)world.loop_timer.delta_time * aircraft.state.speed) * aircraft.state.angles.front;

			// transform AABB (estimate new AABB after rotation)
			{
				// translate AABB
				aircraft.model.current_aabb.min = aircraft.model.current_aabb.max = aircraft.state.translation;

				// new rotated AABB (no translation)
				const auto model_rotation = glm::mat3(model_transformation);
				const auto rotated_min = model_rotation * aircraft.model.initial_aabb.min;
				const auto rotated_max = model_rotation * aircraft.model.initial_aabb.max;
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
							aircraft.model.current_aabb.min[i] += e;
							aircraft.model.current_aabb.max[i] += f;
						} else {
							aircraft.model.current_aabb.min[i] += f;
							aircraft.model.current_aabb.max[i] += e;
						}
					}
				}
			}

			// start with root meshes
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (auto& mesh : aircraft.model.meshes) {
				mesh.transformation = model_transformation;
				meshes_stack.push_back(&mesh);
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				if (mesh->animation_type == AnimationClass::AIRCRAFT_LANDING_GEAR && mesh->animation_states.size() > 1) {
					// ignore 3rd STA, it should always be 0 (TODO are they always 0??)
					const MeshState& state_up   = mesh->animation_states[0];
					const MeshState& state_down = mesh->animation_states[1];
					const auto& alpha = aircraft.state.landing_gear_alpha;

					mesh->state.translation = mesh->initial_state.translation + state_down.translation * (1-alpha) +  state_up.translation * alpha;
					mesh->state.rotation = glm::eulerAngles(glm::slerp(glm::quat(mesh->initial_state.rotation), glm::quat(state_up.rotation), alpha));// ???

					float visibilty = (float) state_down.visible * (1-alpha) + (float) state_up.visible * alpha;
					mesh->state.visible = visibilty > 0.05;
				}

				if (mesh->state.visible == false) {
					continue;
				}

				if (mesh->animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER) {
					mesh->state.rotation.x += aircraft.state.engine_speed * PROPOLLER_MAX_ANGLE_SPEED * world.loop_timer.delta_time;
				}
				if (mesh->animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z) {
					mesh->state.rotation.z += aircraft.state.engine_speed * PROPOLLER_MAX_ANGLE_SPEED * world.loop_timer.delta_time;
				}

				// apply mesh transformation
				mesh->transformation = glm::translate(mesh->transformation, mesh->state.translation);
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[2], glm::vec3{0, 0, 1});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[1], glm::vec3{1, 0, 0});
				mesh->transformation = glm::rotate(mesh->transformation, mesh->state.rotation[0], glm::vec3{0, -1, 0});

				// push children
				for (auto& child : mesh->children) {
					child.transformation = mesh->transformation;
					meshes_stack.push_back(&child);
				}
			}
		}
	}

	void aircrafts_update(World& world) {
		if (signal_handle(world.signals.scenery_loaded)) {
			for (int i = 0; i < world.aircrafts.size(); i++) {
				aircraft_set_start(world.aircrafts[i], world.scenery.start_infos[i]);
			}
		}

		_aircrafts_load_from_files(world);
		_aircrafts_autoremove(world);

		_tracked_aircraft_control(world);

		_aircrafts_apply_physics(world);

		// aircrafts and ground
		for (auto& aircraft : world.aircrafts) {
			float model_y = aircraft.model.current_aabb.max.y;
			if (model_y > 0) {
				aircraft.state.translation.y -= model_y;
			}
		}
	}

	void aircrafts_render(World& world) {
		gl_program_use(world.canvas.meshes_program);

		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];

			if (!aircraft.state.visible) {
				continue;
			}

			const auto model_transformation = local_euler_angles_matrix(aircraft.state.angles, aircraft.state.translation);

			// start with root meshes
			mu::Vec<Mesh*> meshes_stack(mu::memory::tmp());
			for (auto& mesh : aircraft.model.meshes) {
				meshes_stack.push_back(&mesh);
			}

			while (meshes_stack.empty() == false) {
				Mesh* mesh = *meshes_stack.rbegin();
				meshes_stack.pop_back();

				const bool enable_high_throttle = almost_equal(aircraft.state.throttle, 1.0f);
				if (mesh->animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE && enable_high_throttle == false) {
					continue;
				}
				if (mesh->animation_type == AnimationClass::AIRCRAFT_LOW_THROTTLE && enable_high_throttle && aircraft.model.has_high_throttle_mesh) {
					continue;
				}

				if (mesh->animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
					if (aircraft.state.afterburner_reheat_enabled == false) {
						continue;
					}

					if (aircraft.state.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
						continue;
					}
				}

				if (!mesh->state.visible) {
					continue;
				}

				if (mesh->render_cnt_axis) {
					world.canvas.axis_list.push_back(glm::translate(glm::identity<glm::mat4>(), mesh->cnt));
				}

				if (mesh->render_pos_axis) {
					world.canvas.axis_list.push_back(mesh->transformation);
				}

				gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", world.mats.projection_view * mesh->transformation);
				gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", mesh->is_light_source);

				glBindVertexArray(mesh->gpu.vao);
				glDrawArrays(mesh->is_light_source? world.settings.rendering.light_primitives_type : world.settings.rendering.regular_primitives_type, 0, mesh->gpu.array_count);

				// ZL
				if (mesh->animation_type != AnimationClass::AIRCRAFT_ANTI_COLLISION_LIGHTS || aircraft.anti_coll_lights.visible) {
					for (size_t zlid : mesh->zls) {
						Face& face = mesh->faces[zlid];
						world.canvas.zlpoints_list.push_back(ZLPoint {
							.center = mesh->transformation * glm::vec4{face.center.x, face.center.y, face.center.z, 1.0f},
							.color = face.color
						});
					}
				}

				// push children
				for (auto& child : mesh->children) {
					meshes_stack.push_back(&child);
				}
			}
		}
	}

	void scenery_init(World& world) {
		world.scenery_templates = scenery_templates_from_dir(ASSETS_DIR "/scenery");
		world.scenery = scenery_new(world.scenery_templates["SMALL_MAP"]);
	}

	void scenery_free(World& world) {
		field_unload_from_gpu(world.scenery.root_fld);
	}

	void scenery_update(World& world) {
		auto& self = world.scenery;

		if (self.should_be_loaded) {
			self.should_be_loaded = false;

			field_unload_from_gpu(self.root_fld);
			self.root_fld = field_from_fld_file(self.scenery_template.fld);
			field_load_to_gpu(self.root_fld);

			self.start_infos = start_info_from_stp_file(self.scenery_template.stp);

			signal_fire(world.signals.scenery_loaded);
		}

		// transform subfields
		const auto all_fields = field_list_recursively(self.root_fld, mu::memory::tmp());
		if (self.root_fld.should_be_transformed) {
			self.root_fld.should_be_transformed = false;

			// transform fields
			self.root_fld.transformation = glm::identity<glm::mat4>();
			for (Field* fld : all_fields) {
				if (fld->state.visible == false) {
					continue;
				}

				fld->transformation = glm::translate(fld->transformation, fld->state.translation);
				fld->transformation = glm::rotate(fld->transformation, fld->state.rotation[2], glm::vec3{0, 0, 1});
				fld->transformation = glm::rotate(fld->transformation, fld->state.rotation[1], glm::vec3{1, 0, 0});
				fld->transformation = glm::rotate(fld->transformation, fld->state.rotation[0], glm::vec3{0, 1, 0});

				for (auto& subfield : fld->subfields) {
					subfield.transformation = fld->transformation;
				}

				for (auto& mesh : fld->meshes) {
					if (mesh.render_cnt_axis) {
						world.canvas.axis_list.push_back(glm::translate(glm::identity<glm::mat4>(), mesh.cnt));
					}

					// apply mesh transformation
					mesh.transformation = fld->transformation;
					mesh.transformation = glm::translate(mesh.transformation, mesh.state.translation);
					mesh.transformation = glm::rotate(mesh.transformation, mesh.state.rotation[2], glm::vec3{0, 0, 1});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.state.rotation[1], glm::vec3{1, 0, 0});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.state.rotation[0], glm::vec3{0, 1, 0});

					if (mesh.render_pos_axis) {
						world.canvas.axis_list.push_back(mesh.transformation);
					}
				}
			}
		}
	}

	void scenery_render(World& world) {
		const auto all_fields = field_list_recursively(world.scenery.root_fld, mu::memory::tmp());

		// render last ground
		gl_program_use(world.canvas.ground_program);
		gl_program_uniform_set(world.canvas.ground_program, "proj_inv_view_inv", world.mats.proj_inv_view_inv);
		gl_program_uniform_set(world.canvas.ground_program, "projection", world.mats.projection);
		gl_program_uniform_set(world.canvas.ground_program, "color", all_fields[all_fields.size()-1]->ground_color);

		glDisable(GL_DEPTH_TEST);
		glBindTexture(GL_TEXTURE_2D, world.canvas.groundtile_texture);
		glBindVertexArray(world.canvas.dummy_vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// render fields pictures
		gl_program_use(world.canvas.picture2d_program);
		for (const Field* fld : all_fields) {
			for (auto& picture : fld->pictures) {
				if (picture.state.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, picture.state.translation);
				model_transformation = glm::rotate(model_transformation, picture.state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.state.rotation[0], glm::vec3{0, 1, 0});
				gl_program_uniform_set(world.canvas.picture2d_program, "projection_view_model", world.mats.projection_view * model_transformation);

				for (auto& primitive : picture.primitives) {
					gl_program_uniform_set(world.canvas.picture2d_program, "primitive_color[0]", primitive.color);

					const bool gradation_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS;
					gl_program_uniform_set(world.canvas.picture2d_program, "gradation_enabled", gradation_enabled);
					if (gradation_enabled) {
						gl_program_uniform_set(world.canvas.picture2d_program, "primitive_color[1]", primitive.color2);
					}

					glBindVertexArray(primitive.gpu.vao);
					glDrawArrays(primitive.gpu.primitive_type, 0, primitive.gpu.array_count);
				}
			}
		}
		glEnable(GL_DEPTH_TEST);

		// render fields terrains
		gl_program_use(world.canvas.meshes_program);
		gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", false);
		for (const Field* fld : all_fields) {
			if (fld->state.visible == false) {
				continue;
			}

			for (const auto& terr_mesh : fld->terr_meshes) {
				if (terr_mesh.state.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.state.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.state.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.state.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.state.rotation[0], glm::vec3{0, 1, 0});
				gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", world.mats.projection_view * model_transformation);

				gl_program_uniform_set(world.canvas.meshes_program, "gradient_enabled", terr_mesh.gradiant.enabled);
				if (terr_mesh.gradiant.enabled) {
					gl_program_uniform_set(world.canvas.meshes_program, "gradient_bottom_y", terr_mesh.gradiant.bottom_y);
					gl_program_uniform_set(world.canvas.meshes_program, "gradient_top_y", terr_mesh.gradiant.top_y);
					gl_program_uniform_set(world.canvas.meshes_program, "gradient_bottom_color", terr_mesh.gradiant.bottom_color);
					gl_program_uniform_set(world.canvas.meshes_program, "gradient_top_color", terr_mesh.gradiant.top_color);
				}

				glBindVertexArray(terr_mesh.gpu.vao);
				glDrawArrays(world.settings.rendering.regular_primitives_type, 0, terr_mesh.gpu.array_count);
			}
		}
		gl_program_uniform_set(world.canvas.meshes_program, "gradient_enabled", false);

		// render fields meshes
		for (const Field* fld : all_fields) {
			if (fld->state.visible == false) {
				continue;
			}

			for (const auto& mesh : fld->meshes) {
				if (mesh.state.visible == false) {
					continue;
				}

				// upload transofmation model
				gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", world.mats.projection_view * mesh.transformation);
				gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", mesh.is_light_source);

				glBindVertexArray(mesh.gpu.vao);
				glDrawArrays(mesh.is_light_source? world.settings.rendering.light_primitives_type : world.settings.rendering.regular_primitives_type, 0, mesh.gpu.array_count);
			}
		}
	}

	void zlpoints_render(World& world) {
		if (world.canvas.zlpoints_list.empty()) {
			return;
		}

		auto model_transformation = glm::mat4(glm::mat3(world.mats.view_inverse)) * glm::scale(glm::vec3{ZL_SCALE, ZL_SCALE, 0});

		gl_program_use(world.canvas.sprite_program);
		glBindTexture(GL_TEXTURE_2D, world.canvas.zl_sprite_texture);
		glBindVertexArray(world.canvas.dummy_vao);

		for (const auto& zlpoint : world.canvas.zlpoints_list) {
			model_transformation[3] = glm::vec4{zlpoint.center.x, zlpoint.center.y, zlpoint.center.z, 1.0f};
			gl_program_uniform_set(world.canvas.sprite_program, "color", zlpoint.color);
			gl_program_uniform_set(world.canvas.sprite_program, "projection_view_model", world.mats.projection_view * model_transformation);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
	}

	void axis_render(World& world) {
		if (world.canvas.axis_list.empty() == false) {
			gl_program_use(world.canvas.meshes_program);
			if (world.canvas.axis_rendering.on_top) {
				glDisable(GL_DEPTH_TEST);
			}

			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(world.canvas.axis_rendering.line_width);
            #endif
			gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", false);
			glBindVertexArray(world.canvas.axis_rendering.vao);
			for (const auto& transformation : world.canvas.axis_list) {
				gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", world.mats.projection_view * transformation);
				glDrawArrays(GL_LINES, 0, world.canvas.axis_rendering.points_count);
			}

			glEnable(GL_DEPTH_TEST);
		}

		if (world.settings.world_axis.enabled) {
			gl_program_use(world.canvas.meshes_program);

			glEnable(GL_LINE_SMOOTH);
            #ifndef OS_MACOS
			glLineWidth(world.canvas.axis_rendering.line_width);
            #endif

			gl_program_uniform_set(world.canvas.meshes_program, "is_light_source", false);
			glBindVertexArray(world.canvas.axis_rendering.vao);

			float camera_z = 1 - world.settings.world_axis.scale; // invert scale because it's camera moving away
			camera_z *= -40; // arbitrary multiplier
			camera_z -= 1; // keep a fixed distance or axis will vanish
			auto new_view_mat = world.mats.view;
			new_view_mat[3] = glm::vec4{0, 0, camera_z, 1}; // scale is a camera zoom out in z

			auto translate = glm::translate(glm::identity<glm::mat4>(), glm::vec3{world.settings.world_axis.position.x, world.settings.world_axis.position.y, 0});

			gl_program_uniform_set(world.canvas.meshes_program, "projection_view_model", translate * world.mats.projection * new_view_mat);
			glDrawArrays(GL_LINES, 0, world.canvas.axis_rendering.points_count);
		}
	}

	void boxes_render(World& world) {
		if (world.canvas.boxes_list.empty()) {
			return;
		}

		gl_program_use(world.canvas.lines_program);
		glEnable(GL_LINE_SMOOTH);
		#ifndef OS_MACOS
		glLineWidth(world.canvas.box_rendering.line_width);
		#endif
		glBindVertexArray(world.canvas.box_rendering.vao);

		for (const auto& box : world.canvas.boxes_list) {
			auto transformation = glm::translate(glm::identity<glm::mat4>(), box.translation);
			transformation = glm::scale(transformation, box.scale);
			const auto projection_view_model = world.mats.projection_view * transformation;
			gl_program_uniform_set(world.canvas.lines_program, "projection_view_model", projection_view_model);

			gl_program_uniform_set(world.canvas.lines_program, "color", box.color);

			glDrawArrays(GL_LINE_LOOP, 0, world.canvas.box_rendering.points_count);
		}
	}

	void text_render(World& world) {
		gl_program_use(world.canvas.text_rendering.program);

		int wnd_width, wnd_height;
		SDL_GL_GetDrawableSize(world.sdl_window, &wnd_width, &wnd_height);
		glm::mat4 projection = glm::ortho(0.0f, float(wnd_width), 0.0f, float(wnd_height));
		gl_program_uniform_set(world.canvas.text_rendering.program, "projection", projection);

		glBindVertexArray(world.canvas.text_rendering.vao);

		for (auto& txt_rndr : world.canvas.text_list) {
			gl_program_uniform_set(world.canvas.text_rendering.program, "text_color", txt_rndr.color);

			for (char c : txt_rndr.text) {
				if (c >= world.canvas.text_rendering.glyphs.size()) {
					c = '?';
				}
				const Glyph& glyph = world.canvas.text_rendering.glyphs[c];

				// update vertices
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
				glBindBuffer(GL_ARRAY_BUFFER, world.canvas.text_rendering.vbo);
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
	}
}

int main() {
	World world {};
	mu::log_global_logger = (mu::ILogger*) &world.imgui_window_logger;

	test_parser();
	test_aabbs_intersection();
	test_polygons_to_triangles();

	sys::sdl_init(world);
	defer(sys::sdl_shutdown(world));

	sys::projection_init(world);

	sys::imgui_init(world);
	defer(sys::imgui_shutdown(world));

	sys::canvas_init(world);
	defer(sys::canvas_free(world));

	sys::audio_init(world);
	defer(sys::audio_free(world));

	sys::scenery_init(world);
	defer(sys::scenery_free(world));

	sys::aircrafts_init(world);
	defer(sys::aircrafts_free(world));

	sys::ground_objs_init(world);
	defer(sys::ground_objs_free(world));

	signal_listen(world.signals.quit);
	while (!signal_handle(world.signals.quit)) {
		sys::loop_timer_update(world);
		if (!world.loop_timer.ready) {
			time_delay_millis(2);
			continue;
		}

		sys::events_collect(world);

		sys::projection_update(world);
		sys::camera_update(world);
		sys::cached_matrices_recalc(world);

		sys::scenery_update(world);
		sys::aircrafts_update(world);
		sys::ground_objs_update(world);

		sys::models_handle_collision(world);

		// sample text
		world.canvas.text_list.push_back(TextRenderReq {
			.text = mu::str_tmpf("Hello OpenYSF"),
			.x = 25.0f,
			.y = 25.0f,
			.scale = 1.0f,
			.color = {0.5, 0.8f, 0.2f}
		});
		world.canvas.overlay_text_list.push_back(mu::str_tmpf("fps: {:.2f}", 1.0f/world.loop_timer.delta_time));

		sys::canvas_rendering_begin(world); {
			sys::scenery_render(world);

			sys::aircrafts_render(world);
			sys::zlpoints_render(world);

			sys::ground_objs_render(world);

			sys::axis_render(world);
			sys::boxes_render(world);
			sys::text_render(world);

			sys::imgui_render(world);
		}
		sys::canvas_rendering_end(world);

		mu::memory::reset_tmp();
	}

	return 0;
}
