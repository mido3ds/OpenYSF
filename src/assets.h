#pragma once

#include <filesystem> // std::filesystem
#include <mu/utils.h>

#include "parser.h"

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
struct AnimationState {
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
	mu::Vec<AnimationState> animation_states; // STA
	AnimationState initial_state; // POS, should be kept const after init
	GLBuf gl_buf;

	// physics
	glm::mat4 transformation;
	glm::vec3 translation;
	glm::vec3 rotation; // roll, pitch, yaw
	bool visible;

	bool render_pos_axis;
	bool render_cnt_axis;
};

void mesh_load_to_gpu(Mesh& self) {
	struct Stride {
		glm::vec3 vertex;
		glm::vec4 color;
	};
	mu::Vec<Stride> buffer(mu::memory::tmp());
	for (const auto& face : self.faces) {
		for (size_t i = 0; i < face.vertices_ids.size(); i++) {
			buffer.push_back(Stride {
				.vertex=self.vertices[face.vertices_ids[i]],
				.color=face.color,
			});
		}
	}
	self.gl_buf = gl_buf_new<glm::vec3, glm::vec4>(buffer);

	for (auto& child : self.children) {
		mesh_load_to_gpu(child);
	}
}

void mesh_unload_from_gpu(Mesh& self) {
	gl_buf_free(self.gl_buf);

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
			mesh->transformation = glm::translate(mesh->transformation, mesh->translation);
			mesh->transformation = glm::rotate(mesh->transformation, mesh->rotation[2], glm::vec3{0, 0, 1});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->rotation[1], glm::vec3{1, 0, 0});
			mesh->transformation = glm::rotate(mesh->transformation, mesh->rotation[0], glm::vec3{0, 1, 0});
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

			AnimationState sta {};
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

				surf.translation.x = parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.translation.y = -parser_token_float(parser);
				parser_expect(parser, ' ');
				surf.translation.z = parser_token_float(parser);
				parser_expect(parser, ' ');

				// aircraft/cessna172r.dnm is the only one with float rotations (all 0)
				surf.rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf.rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
				parser_expect(parser, ' ');
				surf.rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;

				// aircraft/cessna172r.dnm is the only file with no visibility
				if (parser_accept(parser, ' ')) {
					uint8_t visible = parser_token_u8(parser);
					if (visible == 1 || visible == 0) {
						surf.visible = (visible == 1);
					} else {
						mu::log_error("'{}':{} invalid visible token, found {} expected either 1 or 0", name, parser.curr_line+1, visible);
					}
				} else {
					surf.visible = true;
				}

				parser_expect(parser, '\n');
				surf.initial_state.translation = surf.translation;
				surf.initial_state.rotation = surf.rotation;
				surf.initial_state.visible = surf.visible;
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

	Model model {
		.meshes = { mesh_from_srf_str(main_srf_parser, name) },
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

		// get short_name from dat IDENTIFY
		auto dat_parser = parser_from_file(aircraft.dat, mu::memory::tmp());
		parser_skip_after(dat_parser, "IDENTIFY ");
		aircraft.short_name = parser_token_str(dat_parser);
		_str_unquote(aircraft.short_name);

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
	} gradient;

	glm::vec4 top_side_color, bottom_side_color, right_side_color, left_side_color;

	GLBuf gl_buf;

	glm::vec3 translation;
	glm::vec3 rotation; // roll, pitch, yaw
	bool visible = true;
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

	// scale
	for (auto& stride : buffer) {
		stride.vertex.x *= self.scale.x;
		stride.vertex.z *= self.scale.y;
	}

	self.gl_buf = gl_buf_new<glm::vec3, glm::vec4>(buffer);
}

void terr_mesh_unload_from_gpu(TerrMesh& self) {
	gl_buf_free(self.gl_buf);
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
	glm::vec3 gradient_color2; // only for kind=GRADATION_QUAD_STRIPS

	// (X,Z), y=0
	mu::Vec<glm::vec2> vertices;

	GLBuf gl_buf;
};

void primitive2d_load_to_gpu(Primitive2D& self) {
	self.gl_buf = gl_buf_new<glm::vec2>(self.vertices);
}

void primitive2d_unload_from_gpu(Primitive2D& self) {
	gl_buf_free(self.gl_buf);
}

struct Picture2D {
	mu::Str name;
	FieldID id;

	mu::Vec<Primitive2D> primitives;

	glm::vec3 translation;
	glm::vec3 rotation; // roll, pitch, yaw
	bool visible = true;
};

void picture2d_load_to_gpu(Picture2D& self) {
	for (auto& primitive : self.primitives) {
		primitive2d_load_to_gpu(primitive);
	}
}

void picture2d_unload_from_gpu(Picture2D& self) {
	for (auto& primitive : self.primitives) {
		primitive2d_unload_from_gpu(primitive);
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

	glm::vec3 translation;
	glm::vec3 rotation; // roll, pitch, yaw
	bool visible = true;
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

			if (parser_accept(parser, "CBE ")) {
				terr_mesh.gradient.enabled = true;

				terr_mesh.gradient.top_y = parser_token_float(parser);
				parser_expect(parser, ' ');
				terr_mesh.gradient.bottom_y = -parser_token_float(parser);
				parser_expect(parser, ' ');

				terr_mesh.gradient.top_color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradient.top_color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradient.top_color.b = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');

				terr_mesh.gradient.bottom_color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradient.bottom_color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				terr_mesh.gradient.bottom_color.b = parser_token_u8(parser) / 255.0f;
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

			while (parser_accept(parser, "ENDPICT\n") == false) {
				Primitive2D primitive {};

				auto kind_str = parser_token_str(parser, mu::memory::tmp());
				parser_expect(parser, '\n');

				if (kind_str == "LSQ") {
					primitive.kind = Primitive2D::Kind::LINES;
				} else if (kind_str == "PLG") {
					primitive.kind = Primitive2D::Kind::POLYGON;
				} else if (kind_str == "PLL") {
					primitive.kind = Primitive2D::Kind::LINE_SEGMENTS;
				} else if (kind_str == "PST") {
					primitive.kind = Primitive2D::Kind::POINTS;
				} else if (kind_str == "QDR") {
					primitive.kind = Primitive2D::Kind::QUADRILATERAL;
				} else if (kind_str == "GQS") {
					primitive.kind = Primitive2D::Kind::GRADATION_QUAD_STRIPS;
				} else if (kind_str == "QST") {
					primitive.kind = Primitive2D::Kind::QUAD_STRIPS;
				} else if (kind_str == "TRI") {
					primitive.kind = Primitive2D::Kind::TRIANGLES;
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
				primitive.color.r = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				primitive.color.g = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, ' ');
				primitive.color.b = parser_token_u8(parser) / 255.0f;
				parser_expect(parser, '\n');

				if (primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS) {
					parser_expect(parser, "CL2 ");
					primitive.gradient_color2.r = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					primitive.gradient_color2.g = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, ' ');
					primitive.gradient_color2.b = parser_token_u8(parser) / 255.0f;
					parser_expect(parser, '\n');
				}

				mu::Vec<glm::vec2> tmp_verts;
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

					tmp_verts.push_back(vertex);
				}

				if (tmp_verts.size() == 0) {
					parser_panic(parser, "{}: no vertices", parser.curr_line+1);
				} else if (primitive.kind == Primitive2D::Kind::TRIANGLES && tmp_verts.size() % 3 != 0) {
					parser_panic(parser, "{}: kind is triangle but num of vertices ({}) isn't divisible by 3", parser.curr_line+1, tmp_verts.size());
				} else if (primitive.kind == Primitive2D::Kind::LINES && tmp_verts.size() % 2 != 0) {
					mu::log_error("{}: kind is line but num of vertices ({}) isn't divisible by 2, ignoring last vertex", parser.curr_line+1, tmp_verts.size());
					tmp_verts.pop_back();
				} else if (primitive.kind == Primitive2D::Kind::LINE_SEGMENTS && tmp_verts.size() == 1) {
					parser_panic(parser, "{}: kind is line but has one point", parser.curr_line+1);
				} else if (primitive.kind == Primitive2D::Kind::QUADRILATERAL && tmp_verts.size() % 4 != 0) {
					parser_panic(parser, "{}: kind is quadrilateral but num of vertices ({}) isn't divisible by 4", parser.curr_line+1, tmp_verts.size());
				} else if (primitive.kind == Primitive2D::Kind::QUAD_STRIPS && (tmp_verts.size() >= 4 && tmp_verts.size() % 2 == 0) == false) {
					parser_panic(parser, "{}: kind is quad_strip but num of vertices ({}) isn't in (4,6,8,10,...)", parser.curr_line+1, tmp_verts.size());
				}

				// build final vertices
				switch (primitive.kind) {
				case Primitive2D::Kind::QUADRILATERAL:
				{
					for (int i = 0; i < (int)tmp_verts.size() - 3; i += 4) {
						primitive.vertices.push_back(tmp_verts[i]);
						primitive.vertices.push_back(tmp_verts[i+3]);
						primitive.vertices.push_back(tmp_verts[i+2]);

						primitive.vertices.push_back(tmp_verts[i]);
						primitive.vertices.push_back(tmp_verts[i+2]);
						primitive.vertices.push_back(tmp_verts[i+1]);
					}
					break;
				}
				case Primitive2D::Kind::GRADATION_QUAD_STRIPS: // same as QUAD_STRIPS but with extra color
				case Primitive2D::Kind::QUAD_STRIPS:
				{
					for (int i = 0; i < (int)tmp_verts.size() - 2; i += 2) {
						primitive.vertices.push_back(tmp_verts[i]);
						primitive.vertices.push_back(tmp_verts[i+1]);
						primitive.vertices.push_back(tmp_verts[i+3]);

						primitive.vertices.push_back(tmp_verts[i]);
						primitive.vertices.push_back(tmp_verts[i+2]);
						primitive.vertices.push_back(tmp_verts[i+3]);
					}
					break;
				}
				case Primitive2D::Kind::POLYGON:
				{
					auto indices = polygons2d_to_triangles(tmp_verts, mu::memory::tmp());
					for (auto& index : indices) {
						primitive.vertices.push_back(tmp_verts[index]);
					}
					break;
				}
				default:
					primitive.vertices = std::move(tmp_verts);
					break;
				}

				picture.primitives.push_back(primitive);
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
			subfield->translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			subfield->translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			subfield->translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			subfield->rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			subfield->rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			subfield->rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');

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
			terr_mesh->translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			terr_mesh->translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			terr_mesh->translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			terr_mesh->rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			terr_mesh->rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			terr_mesh->rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');

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
			picture->translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			picture->translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			picture->translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			picture->rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			picture->rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			picture->rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');

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
			mesh->translation.x = parser_token_float(parser);
			parser_expect(parser, ' ');
			mesh->translation.y = parser_token_float(parser);
			parser_expect(parser, ' ');
			mesh->translation.z = parser_token_float(parser);
			parser_expect(parser, ' ');

			mesh->rotation.x = -parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			mesh->rotation.y = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, ' ');
			mesh->rotation.z = parser_token_float(parser) / YS_MAX * RADIANS_MAX;
			parser_expect(parser, '\n');
			mesh->visible = true;

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
	for (auto& mesh : self.meshes) {
		mesh_load_to_gpu(mesh);
	}

	// recurse
	for (auto& subfield : self.subfields) {
		field_load_to_gpu(subfield);
	}
}

void field_unload_from_gpu(Field& self) {
	for (auto& terr_mesh : self.terr_meshes) {
		terr_mesh_unload_from_gpu(terr_mesh);
	}
	for (auto& pict : self.pictures) {
		picture2d_unload_from_gpu(pict);
	}
	for (auto& mesh : self.meshes) {
		mesh_unload_from_gpu(mesh);
	}

	// recurse
	for (auto& subfield : self.subfields) {
		field_unload_from_gpu(subfield);
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

			// get short_name from dat IDENTIFY
			auto dat_parser = parser_from_file(tmpl.dat, mu::memory::tmp());
			parser_skip_after(dat_parser, "IDENTIFY ");
			tmpl.short_name = parser_token_str(dat_parser);

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
