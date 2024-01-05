// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>

// don't export min/max/near/far definitions with windows.h otherwise other includes might break
#define NOMINMAX
#include <portable-file-dialogs.h>
#undef near
#undef far

#include <ft2build.h>
#include FT_FREETYPE_H

#include <mu/utils.h>

#include "imgui.h"
#include "graphics.h"
#include "parser.h"
#include "math.h"
#include "audio.h"
#include "assets.h"

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

// flash anti collision lights
constexpr double ANTI_COLL_LIGHT_PERIOD = 1;

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

void scenery_load(Scenery& self) {
	self.root_fld = field_from_fld_file(self.scenery_template.fld);
	field_load_to_gpu(self.root_fld);

	self.start_infos = start_info_from_stp_file(self.scenery_template.stp);
	self.should_be_loaded = false;
}

void scenery_unload(Scenery& self) {
	field_unload_from_gpu(self.root_fld);
}

struct GroundObj {
	GroundObjTemplate ground_obj_template;
	Model model;
	DATMap dat;

	AABB initial_aabb;
	AABB current_aabb;
	bool render_aabb;

	glm::vec3 translation;
	LocalEulerAngles angles;
	bool visible = true;
	float speed;

	bool should_be_loaded;
	bool should_be_removed;
};

GroundObj ground_obj_new(GroundObjTemplate ground_obj_template, glm::vec3 pos, glm::vec3 attitude) {
	return GroundObj {
		.ground_obj_template = ground_obj_template,
		.translation = pos,
		.angles = local_euler_angles_from_attitude(attitude),
		.should_be_loaded = true,
	};
}

void ground_obj_load(GroundObj& self) {
	auto& main = self.ground_obj_template.main;
	if (main.ends_with(".srf")) {
		self.model = model_from_srf_file(self.ground_obj_template.main);
	} else {
		self.model = model_from_dnm_file(self.ground_obj_template.main);
	}

	for (auto& mesh : self.model.meshes) {
		mesh_load_to_gpu(mesh);
	}

	self.current_aabb = self.initial_aabb = aabb_from_meshes(self.model.meshes);

	self.dat = datmap_from_dat_file(self.ground_obj_template.dat);

	self.should_be_loaded = false;
}

void ground_obj_unload(GroundObj& self) {
	for (auto& mesh : self.model.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

struct Aircraft {
	AircraftTemplate aircraft_template;
	Model model;
	DATMap dat;
	AudioBuffer* engine_sound;

	AABB initial_aabb;
	AABB current_aabb;
	bool render_aabb;

	glm::vec3 translation;
	LocalEulerAngles angles;
	bool visible = true;
	glm::vec3 acceleration, velocity;
	float max_velocity;

	float wing_area; // m^2
	float friction_coeff = 0.032f;
	float thrust_multiplier = 500; // too lazy to calculate real thrust
	float landing_gear_alpha = 0; // 0 -> DOWN, 1 -> UP
	float throttle = 0;

	struct {
		float aoa_crit_neg, aoa_crit_pos;
		QuadraticFuncConsts quad_neg;
		LinearFuncConsts linear;
		QuadraticFuncConsts quad_pos;
	} cl_consts;
	QuadraticFuncConsts cd_consts;

	struct {
		float speed_percent; // 0 -> 1
		bool burner_enabled = false;
		float max_power, idle_power; // HP
	} engine;

	// in newtons
	struct {
		float thrust, airlift, drag, weight;
	} forces;

	// in tons
	struct {
		float clean, load, fuel;
	} mass;

	struct {
		bool visible = true;
		double time_left_secs = ANTI_COLL_LIGHT_PERIOD;
	} anti_coll_lights;

	bool should_be_loaded;
	bool should_be_removed;

	bool render_axes;
	bool render_total_force = true;

	bool has_propellers;
	bool has_afterburner;
	bool has_high_throttle_mesh;
};

Aircraft aircraft_new(AircraftTemplate aircraft_template) {
	return Aircraft {
		.aircraft_template = aircraft_template,
		.should_be_loaded = true,
	};
}

void aircraft_load(Aircraft& self) {
	self.model = model_from_dnm_file(self.aircraft_template.dnm);

	for (auto& mesh : self.model.meshes) {
		mesh_load_to_gpu(mesh);
	}

	meshes_foreach(self.model.meshes, [&self](Mesh& mesh) {
		switch (mesh.animation_type) {
		case AnimationClass::AIRCRAFT_SPINNER_PROPELLER:
		case AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z:
			self.has_propellers = true;
			break;
		case AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT:
			self.has_afterburner = true;
			break;
		case AnimationClass::AIRCRAFT_HIGH_THROTTLE:
			self.has_high_throttle_mesh = true;
			break;
		}

		return true;
	});

	self.current_aabb = self.initial_aabb = aabb_from_meshes(self.model.meshes);

	self.dat = datmap_from_dat_file(self.aircraft_template.dat);

	// mass
	self.mass.clean = 15.0f;
	self.mass.fuel = 5.0f;
	self.mass.load = 4.5f;
	if (auto arr = datmap_get_floats(self.dat, "WEIGHCLN", mu::memory::tmp()); arr.size() == 1) {
		self.mass.clean = arr[0] / 1e6;
	}
	if (auto arr = datmap_get_floats(self.dat, "WEIGFUEL", mu::memory::tmp()); arr.size() == 1) {
		self.mass.fuel = arr[0] / 1e6;
	}
	if (auto arr = datmap_get_floats(self.dat, "WEIGLOAD", mu::memory::tmp()); arr.size() == 1) {
		self.mass.load = arr[0] / 1e6;
	}

	// engine power
	self.engine.max_power = 3060; self.engine.idle_power = 30;
	if (auto arr = datmap_get_ints(self.dat, "NREALPRP", mu::memory::tmp()); arr.size() == 1 && arr[0] > 0) {
		size_t n = arr[0];
		self.engine.max_power = 0; self.engine.idle_power = 0;

		for (int i = 0; i < n; i++) {
			if (auto arr = datmap_get_floats(self.dat, mu::str_tmpf("REALPROP {} MAXPOWER", i), mu::memory::tmp()); arr.size() == 1) {
				self.engine.max_power += arr[0];
			}
			if (auto arr = datmap_get_floats(self.dat, mu::str_tmpf("REALPROP {} IDLEPOWER", i), mu::memory::tmp()); arr.size() == 1) {
				self.engine.idle_power += arr[0];
			}
		}

		self.engine.max_power /= float(n);
		self.engine.idle_power /= float(n);
	}

	self.max_velocity = 133;
	if (auto arr = datmap_get_floats(self.dat, "MAXSPEED", mu::memory::tmp()); arr.size() == 1) {
		self.max_velocity = arr[0];
	}

	self.wing_area = 91;
	if (auto arr = datmap_get_floats(self.dat, "WINGAREA", mu::memory::tmp()); arr.size() == 1) {
		self.wing_area = arr[0];
	}

	// Cl
	// REALPROP 0 CL 0deg 0.2 15deg 1.2      # 4 argument.  AOA1 cl1 AOA2 cl2   (Approximated by a linear function)
	{
		float aoa1 = 0, cl1 = 0.2, aoa2 = 15, cl2 = 1.2;

		if (auto arr = datmap_get_ints(self.dat, "NREALPRP", mu::memory::tmp()); arr.size() == 1 && arr[0] > 0) {
			if (auto arr = datmap_get_floats(self.dat, "REALPROP 0 CL", mu::memory::tmp()); arr.size() == 4) {
				aoa1 = arr[0]; cl1 = arr[1]; aoa2 = arr[2]; cl2 = arr[3];
			}
		}
		self.cl_consts.linear = linear_func_new({aoa1, cl1}, {aoa2, cl2});

		self.cl_consts.aoa_crit_pos = 20;
		if (auto arr = datmap_get_floats(self.dat, "CRITAOAP", mu::memory::tmp()); arr.size() == 1) {
			self.cl_consts.aoa_crit_pos = arr[0];
		}
		self.cl_consts.aoa_crit_neg = -15;
		if (auto arr = datmap_get_floats(self.dat, "CRITAOAM", mu::memory::tmp()); arr.size() == 1) {
			self.cl_consts.aoa_crit_neg = arr[0];
		}

		self.cl_consts.quad_neg = quad_func_new(
			{self.cl_consts.aoa_crit_neg, linear_func_eval(self.cl_consts.linear, self.cl_consts.aoa_crit_neg)},
			{-100, 2}
		);
		self.cl_consts.quad_pos = quad_func_new(
			{self.cl_consts.aoa_crit_pos, linear_func_eval(self.cl_consts.linear, self.cl_consts.aoa_crit_pos)},
			{100, -2}
		);
	}

	// Cd
	// REALPROP 0 CD -5deg 0.006 20deg 0.4   # 4 argument.  AOAminCd minCd AOA1 cd1 (Approximated by a quadratic function)
	{
		float aoa_min = -5, cd_min = 0.006f, aoa1 = 20, cl1 = 0.4f;

		if (auto arr = datmap_get_ints(self.dat, "NREALPRP", mu::memory::tmp()); arr.size() == 1 && arr[0] > 0) {
			if (auto arr = datmap_get_floats(self.dat, "REALPROP 0 CD", mu::memory::tmp()); arr.size() == 4) {
				aoa_min = arr[0]; cd_min = arr[1]; aoa1 = arr[2]; cl1 = arr[3];
			}
		}

		self.cd_consts = quad_func_new({aoa_min, cd_min}, {aoa1, cl1});
	}

	self.should_be_loaded = false;
}

// degrees
float aircraft_angle_of_attack(const Aircraft& self) {
	bool other_side = glm::acos(-self.angles.up.y) > 1.5708f;
	auto a = 90 + (other_side ? +1 : -1) * glm::acos(-self.angles.front.y) / RADIANS_MAX * DEGREES_MAX;
	if (a > 180) {
		return a - 360;
	}
	return a;
}

float aircraft_calc_drag_coeff(const Aircraft& self, float angle_of_attack) {
	return quad_func_eval(self.cd_consts, angle_of_attack);
}

float aircraft_calc_lift_coeff(const Aircraft& self, float angle_of_attack) {
	if (angle_of_attack < self.cl_consts.aoa_crit_neg) {
		return quad_func_eval(self.cl_consts.quad_neg, angle_of_attack);
	}
	if (angle_of_attack > self.cl_consts.aoa_crit_pos) {
		return quad_func_eval(self.cl_consts.quad_pos, angle_of_attack);
	}
	return linear_func_eval(self.cl_consts.linear, angle_of_attack);
}

void aircraft_unload(Aircraft& self) {
	for (auto& mesh : self.model.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

void aircraft_set_start(Aircraft& self, const StartInfo& start_info) {
	self.translation = start_info.position;
	self.angles = local_euler_angles_from_attitude(start_info.attitude);
	self.landing_gear_alpha = start_info.landing_gear_is_out? 0.0f : 1.0f;
	self.throttle = start_info.throttle;
	self.engine.speed_percent = start_info.throttle;
}

bool aircraft_on_ground(const Aircraft& self) {
	return self.translation.y >= -1.0f;
}

float aircraft_mass_total(const Aircraft& self) {
	return (self.mass.clean + self.mass.fuel + self.mass.load) * 1e6;
}

glm::vec3 aircraft_thrust(const Aircraft& self)   { return self.angles.front * self.forces.thrust; }
glm::vec3 aircraft_drag(const Aircraft& self)     { return -self.angles.front * self.forces.drag;  }
glm::vec3 aircraft_airlift(const Aircraft& self)  { return self.angles.up * self.forces.airlift;   }
glm::vec3 aircraft_weight(const Aircraft& self)   { return glm::vec3{0,1,0} * self.forces.weight;  }

glm::vec3 aircraft_forces_total(const Aircraft& self) {
	return aircraft_weight(self)+ aircraft_airlift(self) + aircraft_drag(self) + aircraft_thrust(self);
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
	glm::vec3 target_pos;

	float yaw   = 15.0f / DEGREES_MAX * RADIANS_MAX;
	float pitch = 0.0f / DEGREES_MAX * RADIANS_MAX;

	glm::ivec2 last_mouse_pos;

	bool enable_rotating_around;
};

glm::mat4 camera_calc_view(const Camera& self) {
	return glm::lookAt(self.position, self.target_pos, self.up);
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

		GLenum primitives_type = GL_TRIANGLES;
		GLenum polygon_mode    = GL_FILL;
	} rendering;

	struct {
		bool enabled = true;
		glm::vec2 position {-0.9f, -0.8f};
		float scale = 0.48f;
	} world_axis;
};

namespace canvas {
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

	// text for debugging, rendered in imgui overlay window
	struct TextOverlay {
		mu::Str text;
	};

	struct ZLPoint {
		glm::vec3 center, color;
	};

	// text that always faces camera
	struct Text {
		mu::Str text;
		glm::vec3 p; // world coords, left-bottom corner
		float scale;
		glm::vec4 color;
	};

	struct Axis {
		glm::mat4 transformation;
	};

	struct Box {
		glm::vec3 translation, scale, color;
	};

	struct Line {
		// world coordinates
		glm::vec3 p0, p1;
		glm::vec4 color;
	};

	struct Mesh {
		GLuint vao;
		size_t buf_len;
		glm::mat4 projection_view_model;
	};

	struct GradientMesh {
		GLuint vao;
		size_t buf_len;
		glm::mat4 projection_view_model;

		float gradient_bottom_y, gradient_top_y;
		glm::vec3 gradient_bottom_color, gradient_top_color;
	};

	struct Ground {
		glm::vec3 color;
	};

	// 2d picture rendered on ground
	struct GndPic {
		glm::mat4 projection_view_model;

		// points, lines, line segments or triangles
		struct Primitive {
			GLuint vao;
			size_t buf_len;
			GLenum gl_primitive_type;

			glm::vec3 color;
			bool gradient_enabled;
			glm::vec3 gradient_color2;
		};

		mu::Vec<Primitive> list_primitives;
	};

	// heads up display, 2d shapes that sticks to window
	// all positions are in [0,1] range
	namespace hud {
		struct Text {
			mu::Str text;
			glm::vec2 p; // left-bottom corner
			float scale;
			glm::vec4 color;
		};
	}

	struct Vector {
		mu::Str label;
		glm::vec3 p, dir;
		float len;
		glm::vec4 color;
	};
}

struct Canvas {
	mu::memory::Arena arena;

	struct {
		GLProgram program;

		mu::Vec<canvas::Mesh> list_regular;
		mu::Vec<canvas::GradientMesh> list_gradient;
	} meshes;

	struct {
		GLProgram program;
		GLBuf gl_buf;
		SDL_Surface* tile_surface;
		GLuint tile_texture;

		// we currently only render last ground in loaded fields
		canvas::Ground last_gnd;
	} ground;

	struct {
		GLProgram program;

		mu::Vec<canvas::GndPic> list;
	} gnd_pics;

	struct {
		GLProgram program;
		GLBuf gl_buf;

		GLuint sprite_texture;
		SDL_Surface* sprite_surface;

		mu::Vec<canvas::ZLPoint> list;
	} zlpoints;

	struct {
		GLBuf gl_buf; // single axis vertices
		GLfloat line_width = 5.0f;
		bool on_top = true;

		mu::Vec<canvas::Axis> list;
	} axes;

	struct {
		GLProgram program;
		GLBuf gl_buf; // single box vertices
		GLfloat line_width = 1.0f;

		mu::Vec<canvas::Box> list;
	} boxes;

	struct {
		GLProgram program;
		GLBuf gl_buf; // single character quad vertices

		mu::Arr<canvas::Glyph, 128> glyphs;

		mu::Vec<canvas::Text> list_world;
		mu::Vec<canvas::hud::Text> list_hud;
	} text;

	struct {
		GLProgram program;
		GLBuf gl_buf;
		GLfloat line_width = 1.0f;

		mu::Vec<canvas::Line> list;
	} lines;
};

void canvas_add(Canvas& self, canvas::Text&& t) {
	self.text.list_world.push_back(std::move(t));
}

void canvas_add(Canvas& self, canvas::hud::Text&& t) {
	self.text.list_hud.push_back(std::move(t));
}

void canvas_add(Canvas& self, canvas::Axis&& a) {
	self.axes.list.push_back(std::move(a));
}

void canvas_add(Canvas& self, canvas::Box&& b) {
	self.boxes.list.push_back(std::move(b));
}

void canvas_add(Canvas& self, canvas::ZLPoint&& z) {
	self.zlpoints.list.push_back(std::move(z));
}

void canvas_add(Canvas& self, canvas::Line&& l) {
	self.lines.list.push_back(std::move(l));
}

void canvas_add(Canvas& self, canvas::Mesh&& m) {
	self.meshes.list_regular.push_back(std::move(m));
}

void canvas_add(Canvas& self, canvas::GradientMesh&& m) {
	self.meshes.list_gradient.push_back(std::move(m));
}

void canvas_add(Canvas& self, canvas::Ground&& g) {
	self.ground.last_gnd = g;
}

void canvas_add(Canvas& self, canvas::GndPic&& p) {
	self.gnd_pics.list.push_back(std::move(p));
}

void canvas_add(Canvas& self, const canvas::Vector& v) {
	canvas_add(self, canvas::Line {
		.p0 = v.p,
		.p1 = v.p + v.dir * v.len,
		.color = v.color
	});
	canvas_add(self, canvas::Text {
		.text = v.label,
		.p = v.p + v.dir * v.len,
		.scale = 0.02f,
		.color = v.color
	});
}

// precalculated matrices
struct CachedMatrices {
	glm::mat4 view;
	glm::mat4 view_inverse;

	glm::mat4 projection;
	glm::mat4 projection_inverse;

	glm::mat4 projection_view;
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

struct SysInfo {
	mu::Str name;
	bool enabled;
	uint64_t latency_micros, latency_micros_min, latency_micros_max, latency_micros_avg;
	uint64_t num_calls;
};

// systems performance monitor
struct SysMon {
	mu::Vec<SysInfo> systems;
};

#ifdef DEBUG
	// called once per system
	int _sysmon_register_system(SysMon& self, mu::StrView&& system_name) {
		self.systems.push_back(SysInfo {
			.name = mu::Str(system_name),
			.enabled = true,
			.latency_micros_min = UINT64_MAX,
			.latency_micros_max = 0,
		});
		return self.systems.size()-1;
	}

	void _sysinfo_update(SysInfo& self, std::chrono::steady_clock::time_point start_time) {
		self.latency_micros = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - start_time
		).count();

		self.latency_micros_avg = double(self.num_calls * self.latency_micros_avg + self.latency_micros) / (self.num_calls+1);
		self.num_calls++;

		self.latency_micros_max = std::max(self.latency_micros, self.latency_micros_max);
		self.latency_micros_min = std::min(self.latency_micros, self.latency_micros_min);
	}

	#ifndef __FUNCTION_NAME__
		#ifdef WIN32   // WINDOWS
			#define __FUNCTION_NAME__   __FUNCTION__
		#else          // OTHER
			#define __FUNCTION_NAME__   __func__
		#endif
	#endif

	#define DEF_SYSTEM																					\
		static const auto __sysmon_index = _sysmon_register_system(world.sysmon, __FUNCTION_NAME__);	\
		if (world.sysmon.systems[__sysmon_index].enabled == false) { return; }							\
		const auto __sysmon_start = std::chrono::high_resolution_clock::now();							\
		mu_defer(_sysinfo_update(world.sysmon.systems[__sysmon_index], __sysmon_start));
#else
	#define DEF_SYSTEM(_) void();
#endif

struct World {
	SDL_Window* sdl_window;
	SDL_GLContext sdl_gl_context;

	ImGuiWindowLogger imgui_window_logger;
	mu::Str imgui_ini_file_path;
	mu::Vec<mu::Str> text_overlay_list;

	LoopTimer loop_timer;

	// name -> templates
	mu::Map<mu::Str, AircraftTemplate> aircraft_templates;
	mu::Map<mu::Str, SceneryTemplate> scenery_templates;
	mu::Map<mu::Str, GroundObjTemplate> ground_obj_templates;

	AudioDevice audio_device;
	mu::Map<mu::Str, AudioBuffer> audio_buffers; // "engine2" -> AudioBufer{...}

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

	SysMon sysmon;
};

#define TEXT_OVERLAY(...) world.text_overlay_list.push_back(mu::str_tmpf(__VA_ARGS__))

namespace sys {
	void sdl_init(World& world) {
		DEF_SYSTEM

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

	void sdl_free(World& world) {
		DEF_SYSTEM

		SDL_GL_DeleteContext(world.sdl_gl_context);
		SDL_DestroyWindow(world.sdl_window);
		SDL_Quit();
	}

	void imgui_init(World& world) {
		DEF_SYSTEM

		IMGUI_CHECKVERSION();
		if (ImGui::CreateContext() == nullptr) {
			mu::panic("failed to create imgui context");
		}
		if (ImPlot::CreateContext() == nullptr) {
			mu::panic("failed to create implot context");
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

	void imgui_free(World& world) {
		DEF_SYSTEM

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}

	void imgui_rendering_begin(World& world) {
		DEF_SYSTEM

		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
	}

	void imgui_rendering_end(World& world) {
		DEF_SYSTEM

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void imgui_logs_window(World& world) {
		DEF_SYSTEM

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
	}

	void imgui_overlay_text(World& world) {
		DEF_SYSTEM

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

		if (ImGui::Begin("Overlay Info", nullptr, ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove)) {
			for (const auto& line : world.text_overlay_list) {
				ImGui::TextWrapped(mu::str_tmpf("> {}", line).c_str());
			}
			world.text_overlay_list = mu::Vec<mu::Str>(mu::memory::tmp());
		}
		ImGui::End();
	}

	void imgui_debug_window(World& world) {
		DEF_SYSTEM

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug")) {
			if (ImGui::TreeNode("Window")) {
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

				MyImGui::EnumsCombo("Regular Mesh Primitives", &world.settings.rendering.primitives_type, {
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

			if (ImGui::TreeNode("Axes Rendering")) {
				ImGui::Checkbox("On Top", &world.canvas.axes.on_top);
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &world.canvas.axes.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
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

			if (ImGui::TreeNode("Lines Rendering")) {
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &world.canvas.lines.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Physics")) {
				#ifndef OS_MACOS
				ImGui::Text("AABB Rendering");
				ImGui::DragFloat("Line Width", &world.canvas.boxes.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				#endif

				ImGui::Checkbox("Handle Collision", &world.settings.handle_collision);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Audio")) {
				for (const auto& [_, buf] : world.audio_buffers) {
					ImGui::PushID(buf.file_path.c_str());

					if (ImGui::Button("Play")) {
						audio_device_play(world.audio_device, buf);
					}

					ImGui::SameLine();
					if (ImGui::Button("Loop")) {
						audio_device_play_looped(world.audio_device, buf);
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(audio_device_is_playing(world.audio_device, buf) == false);
					if (ImGui::Button("Stop")) {
						audio_device_stop(world.audio_device, buf);
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					ImGui::Text(mu::Str(mu::file_get_base_name(buf.file_path), mu::memory::tmp()).c_str());

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Systems")) {
				int enabled_count = 0;
				uint64_t total_latency = 0;
				uint64_t max_latency = 0;
				for (auto& sysinfo : world.sysmon.systems) {
					if (sysinfo.enabled) {
						enabled_count++;
						total_latency += sysinfo.latency_micros;
						max_latency = std::max(max_latency, sysinfo.latency_micros);
					}
				}

				ImGui::Text(mu::str_tmpf("Total Systems: {}", world.sysmon.systems.size()).c_str());
				ImGui::Text(mu::str_tmpf("Enabled: {}", enabled_count).c_str());
				ImGui::Text(mu::str_tmpf("Total Latency: {}", total_latency).c_str());
				ImGui::Text(mu::str_tmpf("Max Latest Avg: {}", max_latency).c_str());

				for (auto& sysinfo : world.sysmon.systems) {
					if (ImGui::TreeNode(sysinfo.name.c_str())) {
						ImGui::Text(mu::str_tmpf("latency (micros): last {}, avg {}, min {}, max {}",
							sysinfo.latency_micros, sysinfo.latency_micros_avg, sysinfo.latency_micros_min, sysinfo.latency_micros_max).c_str());
						ImGui::Checkbox("enabled", &sysinfo.enabled);

						ImGui::TreePop();
					}
				}

				ImGui::TreePop();
			}

			ImGui::Separator();
			ImGui::Text("Scenery");

			if (ImGui::BeginCombo("##scenery.name", world.scenery.scenery_template.name.c_str())) {
				for (const auto& [name, scenery_template] : world.scenery_templates) {
					if (ImGui::Selectable(name.c_str(), scenery_template.name == world.scenery.scenery_template.name)) {
						world.scenery.scenery_template = scenery_template;
						world.scenery.should_be_loaded = true;
					}
				}

				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload")) {
				world.scenery.should_be_loaded = true;
			}

			std::function<void(Field&,bool)> render_field_imgui;
			render_field_imgui = [&render_field_imgui, current_angle_max=world.settings.current_angle_max](Field& field, bool is_root) {
				if (ImGui::TreeNode(mu::str_tmpf("Field {}", field.name).c_str())) {
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

					ImGui::Checkbox("Visible", &field.visible);

					ImGui::DragFloat3("Translation", glm::value_ptr(field.translation));
					MyImGui::SliderAngle3("Rotation", &field.rotation, current_angle_max);

					ImGui::BulletText("Sub Fields:");
					for (auto& subfield : field.subfields) {
						render_field_imgui(subfield, false);
					}

					ImGui::BulletText("TerrMesh: %d", (int)field.terr_meshes.size());
					for (auto& terr_mesh : field.terr_meshes) {
						if (ImGui::TreeNode(terr_mesh.name.c_str())) {
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

							ImGui::Checkbox("Visible", &terr_mesh.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.translation));
							MyImGui::SliderAngle3("Rotation", &terr_mesh.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Pict2: %d", (int)field.pictures.size());
					for (auto& picture : field.pictures) {
						if (ImGui::TreeNode(picture.name.c_str())) {
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

							ImGui::Checkbox("Visible", &picture.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(picture.translation));
							MyImGui::SliderAngle3("Rotation", &picture.rotation, current_angle_max);

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

					ImGui::TreePop();
				}
			};
			render_field_imgui(world.scenery.root_fld, true);

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Aircrafts {}:", world.aircrafts.size()).c_str());

			{
				static mu::Str aircraft_to_add = world.aircraft_templates.begin()->first;
				if (ImGui::BeginCombo("##new_aircraft", world.aircraft_templates[aircraft_to_add].short_name.c_str())) {
					for (const auto& [name, aircraft] : world.aircraft_templates) {
						if (ImGui::Selectable(name.c_str(), name == aircraft_to_add)) {
							aircraft_to_add = name;
						}
					}

					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add##aircraft")) {
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
					if (ImGui::BeginCombo("##aircraft_to_load", aircraft_template->short_name.c_str())) {
						for (const auto& [_name, aircraft_template] : world.aircraft_templates) {
							if (ImGui::Selectable(aircraft_template.short_name.c_str(), aircraft_template.short_name == world.aircrafts[i].aircraft_template.short_name)) {
								world.aircrafts[i].aircraft_template = aircraft_template;
								world.aircrafts[i].should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}
					ImGui::SameLine();

					if (ImGui::Button("Reload")) {
						world.aircrafts[i].should_be_loaded = true;
					}
					world.aircrafts[i].should_be_removed = ImGui::Button("Remove");

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

					ImGui::Checkbox("visible", &aircraft.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(aircraft.translation));

					glm::vec3 now_rotation {
						aircraft.angles.roll,
						aircraft.angles.pitch,
						aircraft.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							aircraft.angles,
							now_rotation.z - aircraft.angles.yaw,
							now_rotation.y - aircraft.angles.pitch,
							now_rotation.x - aircraft.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(aircraft.angles.up, aircraft.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(aircraft.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(aircraft.angles.front));
					ImGui::EndDisabled();

					ImGui::Checkbox("Render AABB", &aircraft.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(aircraft.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(aircraft.current_aabb.max));

					ImGui::Checkbox("Render Axes", &aircraft.render_axes);

					if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::Checkbox("Burner", &aircraft.engine.burner_enabled);

						ImGui::SliderFloat("Landing Gear", &aircraft.landing_gear_alpha, 0, 1);
						ImGui::SliderFloat("Throttle", &aircraft.throttle, 0.0f, 1.0f);
						ImGui::DragFloat("Thrust Coeff", &aircraft.thrust_multiplier);
						ImGui::SliderFloat("Friction Coeff", &aircraft.friction_coeff, 0.0f, 1.0f);

						if (ImGui::TreeNode("Aerodynamic Coefficients")) {
							if (ImPlot::BeginPlot("Aerodynamic Coefficients", {-1,0}, ImPlotFlags_Crosshairs)) {
								ImPlot::SetupAxes("AoA", "C", ImPlotAxisFlags_AutoFit);

								mu::Arr<glm::vec2, 1001> p;
								for (int i = 0; i < p.size(); i++) {
									p[i].x = -180 + (i / float(p.size())) * 360;
								}

								for (int i = 0; i < p.size(); i++) {
									p[i].y = aircraft_calc_drag_coeff(aircraft, p[i].x);
								}
								ImPlot::PlotLine("Cd", &p[0].x, &p[0].y, p.size(), 0, 0, sizeof(glm::vec2));

								for (int i = 0; i < p.size(); i++) {
									p[i].y = aircraft_calc_lift_coeff(aircraft, p[i].x);
								}
								ImPlot::PlotLine("Cl", &p[0].x, &p[0].y, p.size(), 0, 0, sizeof(glm::vec2));

								float aoa = aircraft_angle_of_attack(aircraft);
								ImPlot::PlotInfLines("AoA", &aoa, 1);

								ImPlot::EndPlot();
							}

							ImGui::DragFloat("Cd.x", &aircraft.cd_consts.x, 0.0001, 0, 0.08);
							ImGui::DragFloat("Cd.y", &aircraft.cd_consts.y, 0.1);
							ImGui::DragFloat("Cd.z", &aircraft.cd_consts.z, 0.1);

							ImGui::TreePop();
						}

						ImGui::BeginDisabled();
							ImGui::SliderFloat("Engine Speed %%", &aircraft.engine.speed_percent, 0.0f, 1.0f);
							ImGui::DragFloat("Engine MAX power", &aircraft.engine.max_power);
							ImGui::DragFloat("Engine IDLE power", &aircraft.engine.idle_power);

							auto accel = glm::length(aircraft.acceleration);
							auto vel = glm::length(aircraft.velocity);
							ImGui::DragFloat("Acceleration", &accel);
							ImGui::DragFloat("Velocity", &vel);

						ImGui::EndDisabled();

						ImGui::Text("Forces (mega-newtons)");
						ImGui::Checkbox("Render Total", &aircraft.render_total_force);
						ImGui::BeginDisabled();
							MyImGui::SliderMultiplier("Thrust", &aircraft.forces.thrust, 1);
							MyImGui::SliderMultiplier("Drag", &aircraft.forces.drag, 1);
							MyImGui::SliderMultiplier("Airlift", &aircraft.forces.airlift, 1);
							MyImGui::SliderMultiplier("Weight", &aircraft.forces.weight, 1);
						ImGui::EndDisabled();

						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Mass (tons)")) {
						ImGui::DragFloat("Clean", &aircraft.mass.clean, 0.05);
						ImGui::DragFloat("Fuel", &aircraft.mass.fuel, 0.05);
						ImGui::DragFloat("Load", &aircraft.mass.load, 0.05);

						ImGui::BeginDisabled();
						auto total = aircraft_mass_total(aircraft);
						ImGui::DragFloat("Total", &total);
						ImGui::EndDisabled();

						ImGui::TreePop();
					}

					size_t light_sources_count = 0;
					meshes_foreach(aircraft.model.meshes, [&](const Mesh& mesh) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
						return true;
					});

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", aircraft.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&aircraft, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.translation));
							MyImGui::SliderAngle3("rotation", &mesh.rotation, current_angle_max);

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
											for (auto& mesh : aircraft.model.meshes) {
												mesh_unload_from_gpu(mesh);
												mesh_load_to_gpu(mesh);
											}
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
				static mu::Str gro_obj_to_add = world.ground_obj_templates.begin()->first;
				if (ImGui::BeginCombo("##new_ground_obj", world.ground_obj_templates[gro_obj_to_add].short_name.c_str())) {
					for (const auto& [name, _gro] : world.ground_obj_templates) {
						if (ImGui::Selectable(name.c_str(), name == gro_obj_to_add)) {
							gro_obj_to_add = name;
						}
					}

					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add##gro_obj")) {
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
					if (ImGui::BeginCombo("Name", gro.ground_obj_template.short_name.c_str())) {
						for (const auto& [name, gro_obj_template] : world.ground_obj_templates) {
							if (ImGui::Selectable(name.c_str(), gro_obj_template.short_name == gro.ground_obj_template.short_name)) {
								gro.ground_obj_template = gro_obj_template;
								gro.should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}

					gro.should_be_loaded = ImGui::Button("Reload");
					gro.should_be_removed = ImGui::Button("Remove");

					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							gro.translation = {};
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								gro.translation = start_infos[start_info_index].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &gro.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(gro.translation));

					glm::vec3 now_rotation {
						gro.angles.roll,
						gro.angles.pitch,
						gro.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							gro.angles,
							now_rotation.z - gro.angles.yaw,
							now_rotation.y - gro.angles.pitch,
							now_rotation.x - gro.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(gro.angles.up, gro.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(gro.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(gro.angles.front));
					ImGui::EndDisabled();

					ImGui::DragFloat("Speed", &gro.speed, 0.05f, MIN_SPEED, MAX_SPEED);

					ImGui::Checkbox("Render AABB", &gro.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(gro.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(gro.current_aabb.max));

					size_t light_sources_count = 0;
					meshes_foreach(gro.model.meshes, [&](const Mesh& mesh) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
						return true;
					});

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", gro.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&gro, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.translation));
							MyImGui::SliderAngle3("rotation", &mesh.rotation, current_angle_max);

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
											for (auto& mesh : gro.model.meshes) {
												mesh_unload_from_gpu(mesh);
												mesh_load_to_gpu(mesh);
											}
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
	}

	void loop_timer_update(World& world) {
		DEF_SYSTEM

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
		DEF_SYSTEM

		audio_device_init(&world.audio_device);

		// load audio buffers
		auto file_paths = mu::dir_list_files_with(ASSETS_DIR "/sound", [](const auto& s) { return s.ends_with(".wav"); });
		for (const auto& file_path : file_paths) {
			auto base = mu::file_get_base_name(file_path);
			base.remove_suffix(mu::StrView(".wav").length());

			world.audio_buffers[mu::Str(base)] = audio_buffer_from_wav(file_path);
		}
	}

	void audio_free(World& world) {
		DEF_SYSTEM

		for (auto& [_, buf] : world.audio_buffers) {
			audio_buffer_free(buf);
		}

		audio_device_free(world.audio_device);
	}

	void projection_init(World& world) {
		DEF_SYSTEM

		signal_listen(world.signals.wnd_configs_changed);
	}

	void canvas_init(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		signal_listen(world.signals.wnd_configs_changed);

		self.meshes.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec3 attr_position;
				layout (location = 1) in vec4 attr_color;

				uniform mat4 projection_view_model;

				out float vs_vertex_y;
				out vec4 vs_color;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position, 1.0);
					vs_color = attr_color;
					vs_vertex_y = attr_position.y;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in float vs_vertex_y;
				in vec4 vs_color;

				out vec4 out_fragcolor;

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
			self.axes.gl_buf = gl_buf_new<glm::vec3, glm::vec4>(mu::Vec<Stride> {
				Stride {{0, 0, 0}, {1, 0, 0, 1}}, // X
				Stride {{1, 0, 0}, {1, 0, 0, 1}},
				Stride {{0, 0, 0}, {0, 1, 0, 1}}, // Y
				Stride {{0, 1, 0}, {0, 1, 0, 1}},
				Stride {{0, 0, 0}, {0, 0, 1, 1}}, // Z
				Stride {{0, 0, 1}, {0, 0, 1, 1}},
			});
		}

		self.boxes.program = gl_program_new(
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

		self.boxes.gl_buf = gl_buf_new<glm::vec3>(mu::Vec<glm::vec3> {
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

		self.gnd_pics.program = gl_program_new(
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
				uniform bool gradient_enabled;
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
					if (gradient_enabled) {
						color_index = color_indices[int(vs_vertex_id)];
					}
					out_fragcolor = texture(groundtile, tex_coords[int(vs_vertex_id) % 3]).r * vec4(primitive_color[color_index], 1.0);
				}
			)GLSL"
		);

		// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
		self.ground.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;

				uniform mat4 projection_inverse;
				uniform mat4 view_inverse;

				out vec3 vs_near_point;
				out vec3 vs_far_point;

				vec3 unproject_point(float x, float y, float z) {
					vec4 p = view_inverse * projection_inverse * vec4(x, y, z, 1.0);
					return p.xyz / p.w;
				}

				void main() {
					vs_near_point = unproject_point(attr_position.x, attr_position.y, 0.0); // unprojecting on the near plane
					vs_far_point  = unproject_point(attr_position.x, attr_position.y, 1.0); // unprojecting on the far plane
					gl_Position   = vec4(attr_position.x, attr_position.y, 0.0, 1.0);       // using directly the clipped coordinates
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

				void main() {
					float t = -vs_near_point.y / (vs_far_point.y - vs_near_point.y);
					if (t <= 0) {
						discard;
					} else {
						vec3 frag_pos_3d = vs_near_point + t * (vs_far_point - vs_near_point);
						out_fragcolor = vec4(texture(groundtile, frag_pos_3d.xz / 600).x * color, 1.0);
					}
				}
			)GLSL"
		);

		// grid position are in clipped space
		self.ground.gl_buf = gl_buf_new<glm::vec2>(mu::Vec<glm::vec2> {
			glm::vec2{1, 1}, glm::vec2{-1, 1}, glm::vec2{-1, -1},
			glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1}
		});

		// groundtile
		self.ground.tile_surface = IMG_Load(ASSETS_DIR "/misc/groundtile.png");
		if (self.ground.tile_surface == nullptr || self.ground.tile_surface->pixels == nullptr) {
			mu::panic("failed to load groundtile.png");
		}
		glGenTextures(1, &self.ground.tile_texture);
		glBindTexture(GL_TEXTURE_2D, self.ground.tile_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.ground.tile_surface->w, self.ground.tile_surface->h, 0, GL_RED, GL_UNSIGNED_BYTE, self.ground.tile_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		self.zlpoints.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;
				layout (location = 1) in vec2 attr_tex_coord;

				uniform mat4 projection_view_model;

				out vec2 vs_tex_coord;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position, 0, 1);
					vs_tex_coord = attr_tex_coord;
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
					out_fragcolor = texture(quad_texture, vs_tex_coord).r * vec4(color, 1);
				}
			)GLSL"
		);

		{
			struct Stride {
				glm::vec2 vertex;
				glm::vec2 tex_coord;
			};
			self.zlpoints.gl_buf = gl_buf_new<glm::vec2, glm::vec2>(mu::Vec<Stride> {
				Stride { .vertex = glm::vec2{+1, +1}, .tex_coord = glm::vec2{+1, +1} },
				Stride { .vertex = glm::vec2{-1, +1}, .tex_coord = glm::vec2{.0, +1} },
				Stride { .vertex = glm::vec2{-1, -1}, .tex_coord = glm::vec2{.0, .0} },

				Stride { .vertex = glm::vec2{-1, -1}, .tex_coord = glm::vec2{.0, .0} },
				Stride { .vertex = glm::vec2{+1, -1}, .tex_coord = glm::vec2{+1, .0} },
				Stride { .vertex = glm::vec2{+1, +1}, .tex_coord = glm::vec2{+1, +1} },
			});
		}

		// zl_sprite
		self.zlpoints.sprite_surface = IMG_Load(ASSETS_DIR "/misc/rwlight.png");
		if (self.zlpoints.sprite_surface == nullptr || self.zlpoints.sprite_surface->pixels == nullptr) {
			mu::panic("failed to load rwlight.png");
		}
		glGenTextures(1, &self.zlpoints.sprite_texture);
		glBindTexture(GL_TEXTURE_2D, self.zlpoints.sprite_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.zlpoints.sprite_surface->w, self.zlpoints.sprite_surface->h, 0, GL_RED, GL_UNSIGNED_INT, self.zlpoints.sprite_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// text
		self.text.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec3 attr_position;
				layout (location = 1) in vec2 attr_tex_coord;

				uniform mat4 projection_view;

				out vec2 vs_tex_coord;

				void main() {
					gl_Position = projection_view * vec4(attr_position, 1.0);
					vs_tex_coord = attr_tex_coord;
				}
			)GLSL",
			// fragment shader
			R"GLSL(
				#version 330 core
				in vec2 vs_tex_coord;
				out vec4 color;

				uniform sampler2D text_texture;
				uniform vec4 text_color;

				void main() {
					vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text_texture, vs_tex_coord).r);
					color = text_color * sampled;
				}
			)GLSL"
		);

		self.text.gl_buf = gl_buf_new_dyn<glm::vec3, glm::vec2>(6);

		// disable byte-alignment restriction
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		// freetype
		FT_Library ft;
		if (FT_Init_FreeType(&ft)) {
			mu::panic("could not init FreeType Library");
		}
		mu_defer(FT_Done_FreeType(ft));

		FT_Face face;
		if (FT_New_Face(ft, ASSETS_DIR "/fonts/zig.ttf", 0, &face)) {
			mu::panic("failed to load font");
		}
		mu_defer(FT_Done_Face(face));

		uint16_t face_height = 48;
		uint16_t face_width = 0; // auto
		if (FT_Set_Pixel_Sizes(face, face_width, face_height)) {
			mu::panic("failed to set pixel size of font face");
		}

		if (FT_Load_Char(face, 'X', FT_LOAD_RENDER)) {
			mu::panic("failed to load glyph");
		}

		// generate textures
		for (uint8_t c = 0; c < self.text.glyphs.size(); c++) {
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

			self.text.glyphs[c] = canvas::Glyph {
				.texture = text_texture,
				.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
				.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
				.advance = uint32_t(face->glyph->advance.x)
			};
		}

		// lines
		self.lines.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec4 attr_position;
				layout (location = 1) in vec4 attr_color;

				out vec4 vs_color;

				void main() {
					gl_Position = attr_position;
					vs_color = attr_color;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in vec4 vs_color;

				out vec4 out_fragcolor;

				void main() {
					out_fragcolor = vs_color;
				}
			)GLSL"
		);

		self.lines.gl_buf = gl_buf_new_dyn<glm::vec4, glm::vec4>(100);

		gl_process_errors();
	}

	void canvas_free(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		glUseProgram(0);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);

		// text
		gl_program_free(self.text.program);
		gl_buf_free(self.text.gl_buf);
		for (auto& g : self.text.glyphs) {
			glDeleteTextures(1, &g.texture);
		}

		// zlpoints
		SDL_FreeSurface(self.zlpoints.sprite_surface);
		glDeleteTextures(1, &self.zlpoints.sprite_texture);
		gl_program_free(self.zlpoints.program);
		gl_buf_free(self.zlpoints.gl_buf);

		// ground
		SDL_FreeSurface(self.ground.tile_surface);
		glDeleteTextures(1, &self.ground.tile_texture);
		gl_program_free(self.ground.program);
		gl_buf_free(self.ground.gl_buf);

		// boxes
		gl_program_free(self.boxes.program);
		gl_buf_free(self.boxes.gl_buf);

		// axes
		gl_buf_free(self.axes.gl_buf);

		// lines
		gl_program_free(self.lines.program);
		gl_buf_free(self.lines.gl_buf);

		gl_program_free(self.meshes.program);
		gl_program_free(self.gnd_pics.program);
	}

	void canvas_rendering_begin(World& world) {
		DEF_SYSTEM

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
		DEF_SYSTEM

		auto& self = world.canvas;

		SDL_GL_SwapWindow(world.sdl_window);
		gl_process_errors();

		self.arena = {};
		self.text.list_world       = mu::Vec<canvas::Text>(&self.arena);
		self.text.list_hud         = mu::Vec<canvas::hud::Text>(&self.arena);
		self.axes.list             = mu::Vec<canvas::Axis>(&self.arena);
		self.boxes.list            = mu::Vec<canvas::Box>(&self.arena);
		self.zlpoints.list         = mu::Vec<canvas::ZLPoint>(&self.arena);
		self.lines.list            = mu::Vec<canvas::Line>(&self.arena);
		self.meshes.list_regular   = mu::Vec<canvas::Mesh>(&self.arena);
		self.meshes.list_gradient  = mu::Vec<canvas::GradientMesh>(&self.arena);
		self.gnd_pics.list         = mu::Vec<canvas::GndPic>(&self.arena);
	}

	void _camera_update_model_tracking_mode(World& world) {
		DEF_SYSTEM

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

		auto model_transformation = local_euler_angles_matrix(self.aircraft->angles, self.aircraft->translation);

		model_transformation = glm::rotate(model_transformation, self.pitch, glm::vec3{0, -1, 0});
		model_transformation = glm::rotate(model_transformation, self.yaw, glm::vec3{-1, 0, 0});
		self.position = model_transformation * glm::vec4{0, 0, -self.distance_from_model, 1};

		self.target_pos = self.aircraft->translation;
		self.up = self.aircraft->angles.up;
	}

	void _camera_update_flying_mode(World& world) {
		DEF_SYSTEM

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

		self.target_pos = self.position + self.front;
	}

	void camera_update(World& world) {
		DEF_SYSTEM

		if (world.camera.aircraft) {
			_camera_update_model_tracking_mode(world);
		} else {
			_camera_update_flying_mode(world);
		}
	}

	void projection_update(World& world) {
		DEF_SYSTEM

		auto& self = world.projection;

		if (signal_handle(world.signals.wnd_configs_changed) && !world.settings.custom_aspect_ratio) {
			int w, h;
			SDL_GL_GetDrawableSize(world.sdl_window, &w, &h);
			self.aspect = (float) w / h;
		}
	}

	void cached_matrices_recalc(World& world) {
		DEF_SYSTEM

		auto& self = world.mats;
		auto& camera = world.camera;
		auto& proj = world.projection;

		self.view = camera_calc_view(camera);
		self.view_inverse = glm::inverse(self.view);

		self.projection = projection_calc_mat(proj);
		self.projection_inverse = glm::inverse(self.projection);

		self.projection_view = self.projection * self.view;
	}

	void events_collect(World& world) {
		DEF_SYSTEM

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
		DEF_SYSTEM

		if (!world.settings.handle_collision) {
			return;
		}

		// adhoc entity query
		struct Entity {
			AABB* aabb;
			const char* name;
			bool render_aabb, visible, is_aircraft, collided;
		};
		mu::Vec<Entity> e(mu::memory::tmp());
		for (auto& a : world.aircrafts) {
			e.push_back(Entity {
				.aabb = &a.current_aabb,
				.name = a.aircraft_template.short_name.c_str(),
				.render_aabb = a.render_aabb,
				.visible = a.visible,
				.is_aircraft = true,
				.collided = false,
			});
		}
		for (auto& g : world.ground_objs) {
			e.push_back(Entity {
				.aabb = &g.current_aabb,
				.name = g.ground_obj_template.short_name.c_str(),
				.render_aabb = g.render_aabb,
				.visible = g.visible,
				.is_aircraft = false,
				.collided = false,
			});
		}

		// test collision
		for (uint32_t i = 0; e.size() > 1 && i < e.size()-1 && e[i].is_aircraft; i++) {
			if (e[i].visible == false) {
				continue;
			}

			for (uint32_t j = i+1; j < e.size(); j++) {
				if (e[j].visible && aabbs_intersect(*e[i].aabb, *e[j].aabb)) {
					e[i].collided = true;
					e[j].collided = true;
					TEXT_OVERLAY("{}[air] collided with {}[{}]", e[i].name, e[j].name, e[j].is_aircraft ? "air":"gro");
				}
			}
		}

		// render boxes
		constexpr glm::vec3 RED {1,0,0};
		constexpr glm::vec3 BLU {0,0,1};
		for (int i = 0; i < e.size(); i++) {
			if (e[i].visible && e[i].render_aabb) {
				canvas_add(world.canvas, canvas::Box {
					.translation = e[i].aabb->min,
					.scale = e[i].aabb->max - e[i].aabb->min,
					.color = e[i].collided ? RED : BLU,
				});
			}
		}
	}

	void ground_objs_init(World& world) {
		DEF_SYSTEM

		signal_listen(world.signals.scenery_loaded);

		world.ground_obj_templates = ground_obj_templates_from_dir(ASSETS_DIR "/ground");
	}

	void ground_objs_free(World& world) {
		DEF_SYSTEM

		for (auto& gro : world.ground_objs) {
			ground_obj_unload(gro);
		}
	}

	void _ground_objs_reload(World& world) {
		DEF_SYSTEM

		for (auto& gobj : world.ground_objs) {
			if (gobj.should_be_loaded) {
				ground_obj_unload(gobj);
				ground_obj_load(gobj);
				mu::log_debug("loaded '{}'", gobj.ground_obj_template.main);
			}
		}
	}

	void _ground_objs_autoremove(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			if (world.ground_objs[i].should_be_removed) {
				world.ground_objs.erase(world.ground_objs.begin()+i);
				i--;
			}
		}
	}

	void _ground_objs_apply_physics(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.visible) {
				continue;
			}

			// apply model transformation
			const auto model_transformation = local_euler_angles_matrix(gro.angles, gro.translation);

			gro.translation += ((float)world.loop_timer.delta_time * gro.speed) * gro.angles.front;

			// transform AABB (estimate new AABB after rotation)
			{
				// translate AABB
				gro.current_aabb.min = gro.current_aabb.max = gro.translation;

				// new rotated AABB (no translation)
				const auto model_rotation = glm::mat3(model_transformation);
				const auto rotated_min = model_rotation * gro.initial_aabb.min;
				const auto rotated_max = model_rotation * gro.initial_aabb.max;
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
							gro.current_aabb.min[i] += e;
							gro.current_aabb.max[i] += f;
						} else {
							gro.current_aabb.min[i] += f;
							gro.current_aabb.max[i] += e;
						}
					}
				}
			}

			for (auto& mesh : gro.model.meshes) {
				mesh.transformation = model_transformation;
			}

			meshes_foreach(gro.model.meshes, [&](Mesh& mesh) {
				if (mesh.visible == false) {
					return false;
				}

				// apply mesh transformation
				mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, -1, 0});

				for (auto& child : mesh.children) {
					child.transformation = mesh.transformation;
				}

				return true;
			});
		}
	}

	void ground_objs_update(World& world) {
		DEF_SYSTEM

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
				} else {
					mu::log_error("tryed to load {} but didn't find it in ground_obj_templates, ignore it", gob_s->name);
				}
			}
		}

		_ground_objs_reload(world);
		_ground_objs_autoremove(world);

		_ground_objs_apply_physics(world);
	}

	void ground_objs_prepare_render(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.visible) {
				continue;
			}


			meshes_foreach(gro.model.meshes, [&](const Mesh& mesh) {
				if (!mesh.visible) {
					return false;
				}

				if (mesh.render_cnt_axis) {
					canvas_add(world.canvas, canvas::Axis { glm::translate(glm::identity<glm::mat4>(), mesh.cnt) });
				}

				if (mesh.render_pos_axis) {
					canvas_add(world.canvas, canvas::Axis { mesh.transformation });
				}

				canvas_add(world.canvas, canvas::Mesh {
					.vao = mesh.gl_buf.vao,
					.buf_len = mesh.gl_buf.len,
					.projection_view_model = world.mats.projection_view * mesh.transformation
				});

				return true;
			});
		}
	}

	void aircrafts_init(World& world) {
		DEF_SYSTEM

		signal_listen(world.signals.scenery_loaded);

		world.aircraft_templates = aircraft_templates_from_dir(ASSETS_DIR "/aircraft");

		auto ys11 = aircraft_new(world.aircraft_templates["YS-11"]);
		world.aircrafts.push_back(ys11);

		world.camera.aircraft = &world.aircrafts[0];
	}

	void aircrafts_free(World& world) {
		DEF_SYSTEM

		for (auto& aircraft : world.aircrafts) {
			if (aircraft.engine_sound) {
				audio_device_stop(world.audio_device, *aircraft.engine_sound);
			}
			aircraft_unload(aircraft);
		}
	}

	// allow user control over camera tracked aircraft
	void _aircrafts_apply_user_controls(World& world) {
		DEF_SYSTEM

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
		local_euler_angles_rotate(self.angles, delta_yaw, delta_pitch, delta_roll);

		if (world.events.afterburner_toggle) {
			self.engine.burner_enabled = ! self.engine.burner_enabled;
		}
		if (self.engine.burner_enabled && self.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
			self.throttle = AFTERBURNER_THROTTLE_THRESHOLD;
		}

		if (world.events.throttle_increase) {
			self.throttle += THROTTLE_SPEED * world.loop_timer.delta_time;
		}
		if (world.events.throttle_decrease) {
			self.throttle -= THROTTLE_SPEED * world.loop_timer.delta_time;
		}

		// only currently controlled model has audio
		int audio_index = self.engine.speed_percent * 9;

		AudioBuffer* audio;
		if (self.has_propellers) {
			audio = &world.audio_buffers.at(mu::str_tmpf("prop{}", audio_index));
		} else if (self.engine.burner_enabled && self.has_afterburner) {
			audio = &world.audio_buffers.at("burner");
		} else {
			audio = &world.audio_buffers.at(mu::str_tmpf("engine{}", audio_index));
		}

		if (self.engine_sound != audio) {
			if (self.engine_sound) {
				audio_device_stop(world.audio_device, *self.engine_sound);
			}
			self.engine_sound = audio;
			audio_device_play_looped(world.audio_device, *self.engine_sound);
		}
	}

	void _aircrafts_reload(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.aircrafts.size(); i++) {
			if (world.aircrafts[i].should_be_loaded) {
				aircraft_unload(world.aircrafts[i]);
				aircraft_load(world.aircrafts[i]);
				mu::log_debug("loaded '{}'", world.aircrafts[i].aircraft_template.short_name);
			}
		}
	}

	void _aircrafts_remove(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.aircrafts.size(); i++) {
			if (world.aircrafts[i].should_be_removed) {
				int tracked_model_index = -1;
				for (int i = 0; i < world.aircrafts.size(); i++) {
					if (world.camera.aircraft == &world.aircrafts[i]) {
						tracked_model_index = i;
						break;
					}
				}

				aircraft_unload(world.aircrafts[i]);
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
		DEF_SYSTEM

		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];

			if (!aircraft.visible) {
				continue;
			}

			// anti coll lights
			aircraft.anti_coll_lights.time_left_secs -= world.loop_timer.delta_time;
			if (aircraft.anti_coll_lights.time_left_secs < 0) {
				aircraft.anti_coll_lights.time_left_secs = ANTI_COLL_LIGHT_PERIOD;
				aircraft.anti_coll_lights.visible = ! aircraft.anti_coll_lights.visible;
			}

			// engine
			aircraft.throttle = clamp(aircraft.throttle, 0.0f, 1.0f);
			if (aircraft.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
				aircraft.engine.burner_enabled = false;
			}

			if (aircraft.engine.speed_percent < aircraft.throttle) {
				aircraft.engine.speed_percent += world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
				aircraft.engine.speed_percent = clamp(aircraft.engine.speed_percent, 0.0f, aircraft.throttle);
			} else if (aircraft.engine.speed_percent > aircraft.throttle) {
				aircraft.engine.speed_percent -= world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
				aircraft.engine.speed_percent = clamp(aircraft.engine.speed_percent, aircraft.throttle, 1.0f);
			}

			// air density, https://en.wikipedia.org/wiki/Density_of_air#Dry_air, https://www.mide.com/air-pressure-at-altitude-calculator
			double air_density = 0; {
				constexpr double BOT_PRESSURE = 101325.00; // Pa
				constexpr double TOP_PRESSURE = 12044.57;  // Pa
				constexpr double TOP_ALT = 15000.0; // m
				constexpr double AIR_TEMP = 15.0 + 273.15; // kelvin

				double altitude_meters = std::abs(aircraft.translation.y);
				double alt_percent = altitude_meters/TOP_ALT;
				double air_pressure = alt_percent * TOP_PRESSURE + (1-alt_percent) * BOT_PRESSURE;
				air_density =  air_pressure / (287.0 * AIR_TEMP);
			}

			// front v
			float vel_sq = pow(glm::length(aircraft.velocity), 2);

			// forces
			{
				auto engine_power_hp = aircraft.engine.speed_percent * aircraft.engine.max_power + (1-aircraft.engine.speed_percent) * aircraft.engine.idle_power;
				auto engine_power_j_s = engine_power_hp * 745.69;
				aircraft.forces.thrust = engine_power_j_s * aircraft.thrust_multiplier;
			}

			aircraft.forces.weight = aircraft_mass_total(aircraft) * 9.86f;

			float aoa = aircraft_angle_of_attack(aircraft);

			// https://www.grc.nasa.gov/www/k-12/VirtualAero/BottleRocket/airplane/drageq.html
			aircraft.forces.drag =
				aircraft_calc_drag_coeff(aircraft, aoa) *
				air_density *
				vel_sq *
				(0.05 * aircraft.wing_area);

			// https://www.grc.nasa.gov/www/k-12/VirtualAero/BottleRocket/airplane/lifteq.html
			aircraft.forces.airlift =
				aircraft_calc_lift_coeff(aircraft, aoa) *
				air_density *
				vel_sq *
				aircraft.wing_area;

			if (aircraft_on_ground(aircraft)) {
				float friction = aircraft.friction_coeff * std::max(aircraft.forces.weight - aircraft.forces.airlift, 0.0f);
				aircraft.forces.thrust = std::max(aircraft.forces.thrust - friction, 0.0f);

				aircraft.forces.weight = 0;
			}

			// translation
			{
				aircraft.acceleration = aircraft_forces_total(aircraft) / aircraft_mass_total(aircraft);

				glm::vec3 accel_dir = glm::normalize(aircraft.acceleration);
				float accel_mag = glm::length(aircraft.acceleration);
				aircraft.velocity += accel_mag * accel_dir;

				glm::vec3 vel_dir = glm::normalize(aircraft.velocity);
				float vel_mag = glm::length(aircraft.velocity);
				aircraft.velocity = std::min(vel_mag, aircraft.max_velocity) * vel_dir;
			}

			aircraft.translation += (float)world.loop_timer.delta_time * aircraft.velocity;

			// put back on ground
			aircraft.translation.y = std::min(aircraft.translation.y, -1.0f);

			// transform AABB (estimate new AABB after rotation)
			const auto model_transformation = local_euler_angles_matrix(aircraft.angles, aircraft.translation);
			{
				// translate AABB
				aircraft.current_aabb.min = aircraft.translation;
				aircraft.current_aabb.max = aircraft.translation;

				// new rotated AABB (no translation)
				const auto model_rotation = glm::mat3(model_transformation);
				const auto rotated_min = model_rotation * aircraft.initial_aabb.min;
				const auto rotated_max = model_rotation * aircraft.initial_aabb.max;
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
							aircraft.current_aabb.min[i] += e;
							aircraft.current_aabb.max[i] += f;
						} else {
							aircraft.current_aabb.min[i] += f;
							aircraft.current_aabb.max[i] += e;
						}
					}
				}
			}

			for (auto& mesh : aircraft.model.meshes) {
				mesh.transformation = model_transformation;
			}

			meshes_foreach(aircraft.model.meshes, [&](Mesh& mesh) {
				if (mesh.animation_type == AnimationClass::AIRCRAFT_LANDING_GEAR && mesh.animation_states.size() > 1) {
					// ignore 3rd STA, it should always be 0 (TODO are they always 0??)
					const AnimationState& state_up   = mesh.animation_states[0];
					const AnimationState& state_down = mesh.animation_states[1];
					const auto& alpha = aircraft.landing_gear_alpha;

					mesh.translation = mesh.initial_state.translation + state_down.translation * (1-alpha) +  state_up.translation * alpha;
					mesh.rotation = glm::eulerAngles(glm::slerp(glm::quat(mesh.initial_state.rotation), glm::quat(state_up.rotation), alpha));// ???

					float visibilty = (float) state_down.visible * (1-alpha) + (float) state_up.visible * alpha;
					mesh.visible = visibilty > 0.05;
				}

				if (mesh.visible == false) {
					return false;
				}

				if (mesh.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER) {
					mesh.rotation.x += aircraft.engine.speed_percent * PROPOLLER_MAX_ANGLE_SPEED * world.loop_timer.delta_time;
				}
				if (mesh.animation_type == AnimationClass::AIRCRAFT_SPINNER_PROPELLER_Z) {
					mesh.rotation.z += aircraft.engine.speed_percent * PROPOLLER_MAX_ANGLE_SPEED * world.loop_timer.delta_time;
				}

				// apply mesh transformation
				mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, -1, 0});

				// push children
				for (auto& child : mesh.children) {
					child.transformation = mesh.transformation;
				}

				return true;
			});
		}
	}

	void aircrafts_update(World& world) {
		DEF_SYSTEM

		if (signal_handle(world.signals.scenery_loaded)) {
			for (int i = 0; i < world.aircrafts.size(); i++) {
				aircraft_set_start(world.aircrafts[i], world.scenery.start_infos[i]);
			}
		}

		_aircrafts_reload(world);
		_aircrafts_remove(world);
		_aircrafts_apply_user_controls(world);
		_aircrafts_apply_physics(world);
	}

	void aircrafts_prepare_render(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];

			if (!aircraft.visible) {
				continue;
			}

			if (aircraft.render_axes) {
				canvas_add(world.canvas, canvas::Vector {
					.label = "front",
					.p = aircraft.translation,
					.dir = aircraft.angles.front,
					.len = 35.0f,
					.color = glm::vec4{1,0,0,0.3}
				});
				canvas_add(world.canvas, canvas::Vector {
					.label = "right",
					.p = aircraft.translation,
					.dir = glm::normalize(glm::cross(aircraft.angles.front, aircraft.angles.up)),
					.len = 20.0f,
					.color = glm::vec4{0,1,0,0.3}
				});
				canvas_add(world.canvas, canvas::Vector {
					.label = "up",
					.p = aircraft.translation,
					.dir = aircraft.angles.up,
					.len = 10.0f,
					.color = glm::vec4{0,0,1,0.3}
				});
			}

			if (aircraft.render_total_force) {
				auto total = aircraft_forces_total(aircraft);
				auto total_mag = glm::length(total);
				canvas_add(world.canvas, canvas::Vector {
					.label = mu::str_tmpf("total={}", total_mag),
					.p = aircraft.translation,
					.dir = glm::normalize(total),
					.len = std::min(total_mag, 15.0f),
					.color = glm::vec4{1,1,0,0.3}
				});
			}

			meshes_foreach(aircraft.model.meshes, [&](const Mesh& mesh) {
				if (!mesh.visible) {
					return false;
				}

				const bool enable_high_throttle = almost_equal(aircraft.throttle, 1.0f);
				if (mesh.animation_type == AnimationClass::AIRCRAFT_HIGH_THROTTLE && enable_high_throttle == false) {
					return false;
				}
				if (mesh.animation_type == AnimationClass::AIRCRAFT_LOW_THROTTLE && enable_high_throttle && aircraft.has_high_throttle_mesh) {
					return false;
				}

				if (mesh.animation_type == AnimationClass::AIRCRAFT_AFTERBURNER_REHEAT) {
					if (aircraft.engine.burner_enabled == false) {
						return false;
					}

					if (aircraft.throttle < AFTERBURNER_THROTTLE_THRESHOLD) {
						return false;
					}
				}

				if (mesh.render_cnt_axis) {
					canvas_add(world.canvas, canvas::Axis { mesh.transformation * glm::translate(mesh.cnt) });
				}

				if (mesh.render_pos_axis) {
					canvas_add(world.canvas, canvas::Axis { mesh.transformation });
				}

				canvas_add(world.canvas, canvas::Mesh {
					.vao = mesh.gl_buf.vao,
					.buf_len = mesh.gl_buf.len,
					.projection_view_model = world.mats.projection_view * mesh.transformation
				});

				// ZL
				if (mesh.animation_type != AnimationClass::AIRCRAFT_ANTI_COLLISION_LIGHTS || aircraft.anti_coll_lights.visible) {
					for (size_t zlid : mesh.zls) {
						const Face& face = mesh.faces[zlid];
						canvas_add(world.canvas, canvas::ZLPoint {
							.center = mesh.transformation * glm::vec4{face.center.x, face.center.y, face.center.z, 1.0f},
							.color = face.color
						});
					}
				}

				return true;
			});
		}
	}

	void scenery_init(World& world) {
		DEF_SYSTEM

		world.scenery_templates = scenery_templates_from_dir(ASSETS_DIR "/scenery");
		world.scenery = scenery_new(world.scenery_templates["SMALL_MAP"]);
	}

	void scenery_free(World& world) {
		DEF_SYSTEM

		field_unload_from_gpu(world.scenery.root_fld);
	}

	void scenery_update(World& world) {
		DEF_SYSTEM

		auto& self = world.scenery;
		const auto all_fields = field_list_recursively(self.root_fld, mu::memory::tmp());

		if (self.should_be_loaded) {
			scenery_unload(self);
			scenery_load(self);
			signal_fire(world.signals.scenery_loaded);
		}

		if (self.root_fld.should_be_transformed) {
			self.root_fld.should_be_transformed = false;

			// transform fields
			self.root_fld.transformation = glm::identity<glm::mat4>();
			for (Field* fld : all_fields) {
				if (fld->visible == false) {
					continue;
				}

				fld->transformation = glm::translate(fld->transformation, fld->translation);
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[2], glm::vec3{0, 0, 1});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[1], glm::vec3{1, 0, 0});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[0], glm::vec3{0, 1, 0});

				for (auto& subfield : fld->subfields) {
					subfield.transformation = fld->transformation;
				}

				meshes_foreach(fld->meshes, [&](Mesh& mesh) {
					if (mesh.render_cnt_axis) {
						canvas_add(world.canvas, canvas::Axis { glm::translate(glm::identity<glm::mat4>(), mesh.cnt) });
					}

					// apply mesh transformation
					mesh.transformation = fld->transformation;
					mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, 1, 0});

					if (mesh.render_pos_axis) {
						canvas_add(world.canvas, canvas::Axis { mesh.transformation });
					}

					return true;
				});
			}
		}
	}

	void scenery_prepare_render(World& world) {
		DEF_SYSTEM

		const auto all_fields = field_list_recursively(world.scenery.root_fld, mu::memory::tmp());

		for (const Field* fld : all_fields) {
			if (fld->visible == false) {
				continue;
			}

			// ground
			canvas_add(world.canvas, canvas::Ground {
				.color = fld->ground_color,
			});

			// pictures
			for (const auto& picture : fld->pictures) {
				if (picture.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, picture.translation);
				model_transformation = glm::rotate(model_transformation, picture.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.rotation[0], glm::vec3{0, 1, 0});

				auto gnd_pic = canvas::GndPic {
					.projection_view_model = world.mats.projection_view * model_transformation,
					.list_primitives = mu::Vec<canvas::GndPic::Primitive>(&world.canvas.arena),
				};

				for (const auto& primitive : picture.primitives) {
					GLenum gl_primitive_type;
					switch (primitive.kind) {
					case Primitive2D::Kind::POINTS:
						gl_primitive_type = GL_POINTS;
						break;
					case Primitive2D::Kind::LINES:
						gl_primitive_type = GL_LINES;
						break;
					case Primitive2D::Kind::LINE_SEGMENTS:
						gl_primitive_type = GL_LINE_STRIP;
						break;
					case Primitive2D::Kind::TRIANGLES:
					case Primitive2D::Kind::QUAD_STRIPS:
					case Primitive2D::Kind::QUADRILATERAL:
					case Primitive2D::Kind::POLYGON:
					case Primitive2D::Kind::GRADATION_QUAD_STRIPS:
						gl_primitive_type = GL_TRIANGLES;
						break;
					default: mu_unreachable();
					}

					gnd_pic.list_primitives.push_back(canvas::GndPic::Primitive {
						.vao = primitive.gl_buf.vao,
						.buf_len = primitive.gl_buf.len,
						.gl_primitive_type = gl_primitive_type,

						.color = primitive.color,
						.gradient_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS,
						.gradient_color2 = primitive.gradient_color2,
					});
				}

				canvas_add(world.canvas, std::move(gnd_pic));
			}

			// terrains
			for (const auto& terr_mesh : fld->terr_meshes) {
				if (terr_mesh.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[0], glm::vec3{0, 1, 0});

				if (terr_mesh.gradient.enabled) {
					canvas_add(world.canvas, canvas::GradientMesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,

						.gradient_bottom_y = terr_mesh.gradient.bottom_y,
						.gradient_top_y = terr_mesh.gradient.top_y,
						.gradient_bottom_color = terr_mesh.gradient.bottom_color,
						.gradient_top_color = terr_mesh.gradient.top_color,
					});
				} else {
					canvas_add(world.canvas, canvas::Mesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,
					});
				}
			}

			// meshes
			meshes_foreach(fld->meshes, [&](const Mesh& mesh) {
				if (mesh.visible == false) {
					return false;
				}

				canvas_add(world.canvas, canvas::Mesh {
					.vao = mesh.gl_buf.vao,
					.buf_len = mesh.gl_buf.len,
					.projection_view_model =
						world.mats.projection_view
						* mesh.transformation
						* fld->transformation
				});

				return true;
			});
		}
	}

	void canvas_render_zlpoints(World& world) {
		DEF_SYSTEM

		if (world.canvas.zlpoints.list.empty()) {
			return;
		}

		auto model_transformation = glm::mat4(glm::mat3(world.mats.view_inverse)) * glm::scale(glm::vec3{ZL_SCALE, ZL_SCALE, 0});

		gl_program_use(world.canvas.zlpoints.program);
		glBindTexture(GL_TEXTURE_2D, world.canvas.zlpoints.sprite_texture);
		glBindVertexArray(world.canvas.zlpoints.gl_buf.vao);

		for (const auto& zlpoint : world.canvas.zlpoints.list) {
			model_transformation[3] = glm::vec4{zlpoint.center.x, zlpoint.center.y, zlpoint.center.z, 1.0f};
			gl_program_uniform_set(world.canvas.zlpoints.program, "color", zlpoint.color);
			gl_program_uniform_set(world.canvas.zlpoints.program, "projection_view_model", world.mats.projection_view * model_transformation);
			glDrawArrays(GL_TRIANGLES, 0, world.canvas.zlpoints.gl_buf.len);
		}
	}

	void canvas_render_meshes(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.meshes.program);

		// regular
		for (const auto& mesh : world.canvas.meshes.list_regular) {
			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", mesh.projection_view_model);
			glBindVertexArray(mesh.vao);
			glDrawArrays(world.settings.rendering.primitives_type, 0, mesh.buf_len);
		}

		// gradient
		if (world.canvas.meshes.list_gradient.size() > 0) {
			gl_program_uniform_set(world.canvas.meshes.program, "gradient_enabled", true);
			for (const auto& mesh : world.canvas.meshes.list_gradient) {
				gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", mesh.projection_view_model);

				gl_program_uniform_set(world.canvas.meshes.program, "gradient_bottom_y", mesh.gradient_bottom_y);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_top_y", mesh.gradient_top_y);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_bottom_color", mesh.gradient_bottom_color);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_top_color", mesh.gradient_top_color);

				glBindVertexArray(mesh.vao);
				glDrawArrays(world.settings.rendering.primitives_type, 0, mesh.buf_len);
			}
			gl_program_uniform_set(world.canvas.meshes.program, "gradient_enabled", false);
		}
	}

	void canvas_render_axes(World& world) {
		DEF_SYSTEM

		if (world.canvas.axes.list.empty() == false) {
			gl_program_use(world.canvas.meshes.program);
			glEnable(GL_LINE_SMOOTH);
			#ifndef OS_MACOS
			glLineWidth(world.canvas.axes.line_width);
            #endif
			glBindVertexArray(world.canvas.axes.gl_buf.vao);

			if (world.canvas.axes.on_top) {
				glDisable(GL_DEPTH_TEST);
			}

			for (const auto& axis : world.canvas.axes.list) {
				gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", world.mats.projection_view * axis.transformation);
				glDrawArrays(GL_LINES, 0, world.canvas.axes.gl_buf.len);
			}

			glEnable(GL_DEPTH_TEST);
		}

		if (world.settings.world_axis.enabled) {
			gl_program_use(world.canvas.meshes.program);
			glEnable(GL_LINE_SMOOTH);
			#ifndef OS_MACOS
			glLineWidth(world.canvas.axes.line_width);
            #endif
			glBindVertexArray(world.canvas.axes.gl_buf.vao);

			float camera_z = 1 - world.settings.world_axis.scale; // invert scale because it's camera moving away
			camera_z *= -40; // arbitrary multiplier
			camera_z -= 1; // keep a fixed distance or axis will vanish
			auto new_view_mat = world.mats.view;
			new_view_mat[3] = glm::vec4{0, 0, camera_z, 1}; // scale is a camera zoom out in z

			auto translate = glm::translate(glm::identity<glm::mat4>(), glm::vec3{world.settings.world_axis.position.x, world.settings.world_axis.position.y, 0});

			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", translate * world.mats.projection * new_view_mat);
			glDrawArrays(GL_LINES, 0, world.canvas.axes.gl_buf.len);
		}
	}

	void canvas_render_boxes(World& world) {
		DEF_SYSTEM

		if (world.canvas.boxes.list.empty()) {
			return;
		}

		gl_program_use(world.canvas.boxes.program);
		glEnable(GL_LINE_SMOOTH);
		#ifndef OS_MACOS
		glLineWidth(world.canvas.boxes.line_width);
		#endif
		glBindVertexArray(world.canvas.boxes.gl_buf.vao);

		for (const auto& box : world.canvas.boxes.list) {
			auto transformation = glm::translate(glm::identity<glm::mat4>(), box.translation);
			transformation = glm::scale(transformation, box.scale);
			const auto projection_view_model = world.mats.projection_view * transformation;
			gl_program_uniform_set(world.canvas.boxes.program, "projection_view_model", projection_view_model);

			gl_program_uniform_set(world.canvas.boxes.program, "color", box.color);

			glDrawArrays(GL_LINE_LOOP, 0, world.canvas.boxes.gl_buf.len);
		}
	}

	void canvas_render_text(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.text.program);
		glBindVertexArray(world.canvas.text.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, world.canvas.text.gl_buf.vbo);

		for (auto& txt_rndr : world.canvas.text.list_world) {
			gl_program_uniform_set(world.canvas.text.program, "text_color", txt_rndr.color);

			auto model_transformation = glm::mat4(glm::mat3(world.mats.view_inverse));
			model_transformation[3] = glm::vec4{txt_rndr.p.x, txt_rndr.p.y, txt_rndr.p.z, 1.0f};
			gl_program_uniform_set(world.canvas.text.program, "projection_view", world.mats.projection_view * model_transformation);
			txt_rndr.p = {};

			for (char c : txt_rndr.text) {
				if (c >= world.canvas.text.glyphs.size()) {
					c = '?';
				}
				const canvas::Glyph& glyph = world.canvas.text.glyphs[c];

				// update vertices
				struct Stride {
					glm::vec3 pos;
					glm::vec2 tex_coord;
				};
				float x = txt_rndr.p.x + glyph.bearing.x * txt_rndr.scale;
				float y = txt_rndr.p.y - (glyph.size.y - glyph.bearing.y) * txt_rndr.scale;
				float w = glyph.size.x * txt_rndr.scale;
				float h = glyph.size.y * txt_rndr.scale;
				mu::Vec<Stride> buffer {
					Stride {{x,   y+h, txt_rndr.p.z}, {0.0f, 0.0f}}, // 0
					Stride {{x,   y  , txt_rndr.p.z}, {0.0f, 1.0f}}, // 1
					Stride {{x+w, y  , txt_rndr.p.z}, {1.0f, 1.0f}}, // 2

					Stride {{x,   y+h, txt_rndr.p.z}, {0.0f, 0.0f}}, // 0
					Stride {{x+w, y  , txt_rndr.p.z}, {1.0f, 1.0f}}, // 2
					Stride {{x+w, y+h, txt_rndr.p.z}, {1.0f, 0.0f}}, // 3
				};
				mu_assert(buffer.size() == world.canvas.text.gl_buf.len);
				glBufferSubData(GL_ARRAY_BUFFER, 0, buffer.size() * sizeof(Stride), buffer.data());

				// render glyph texture over quad
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
				// bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
				txt_rndr.p.x += (glyph.advance >> 6) * txt_rndr.scale;
			}
		}
	}

	void canvas_render_hud_text(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.text.program);

		int wnd_width, wnd_height;
		SDL_GL_GetDrawableSize(world.sdl_window, &wnd_width, &wnd_height);
		gl_program_uniform_set(world.canvas.text.program, "projection_view", glm::ortho(0.0f, float(wnd_width), 0.0f, float(wnd_height)));

		glBindVertexArray(world.canvas.text.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, world.canvas.text.gl_buf.vbo);

		for (auto& txt_rndr : world.canvas.text.list_hud) {
			gl_program_uniform_set(world.canvas.text.program, "text_color", txt_rndr.color);

			txt_rndr.p.x *= wnd_width;
			txt_rndr.p.y *= wnd_height;

			for (char c : txt_rndr.text) {
				if (c >= world.canvas.text.glyphs.size()) {
					c = '?';
				}
				const canvas::Glyph& glyph = world.canvas.text.glyphs[c];

				// update vertices
				struct Stride {
					glm::vec3 pos;
					glm::vec2 tex_coord;
				};
				float x = txt_rndr.p.x + glyph.bearing.x * txt_rndr.scale;
				float y = txt_rndr.p.y - (glyph.size.y - glyph.bearing.y) * txt_rndr.scale;
				float w = glyph.size.x * txt_rndr.scale;
				float h = glyph.size.y * txt_rndr.scale;
				mu::Vec<Stride> buffer {
					Stride {{x,   y+h, 0}, {0.0f, 0.0f}}, // 0
					Stride {{x,   y  , 0}, {0.0f, 1.0f}}, // 1
					Stride {{x+w, y  , 0}, {1.0f, 1.0f}}, // 2

					Stride {{x,   y+h, 0}, {0.0f, 0.0f}}, // 0
					Stride {{x+w, y  , 0}, {1.0f, 1.0f}}, // 2
					Stride {{x+w, y+h, 0}, {1.0f, 0.0f}}, // 3
				};
				mu_assert(buffer.size() == world.canvas.text.gl_buf.len);
				glBufferSubData(GL_ARRAY_BUFFER, 0, buffer.size() * sizeof(Stride), buffer.data());

				// render glyph texture over quad
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
				// bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
				txt_rndr.p.x += (glyph.advance >> 6) * txt_rndr.scale;
			}
		}
	}

	void canvas_render_lines(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas.lines;

		if (self.list.empty()) {
			return;
		}

		struct Stride {
			glm::vec4 vertex;
			glm::vec4 color;
		};

		mu::Vec<Stride> strides(mu::memory::tmp());
		strides.reserve(self.list.size()*2);

		for (const auto& line : self.list) {
			strides.push_back(Stride {
				.vertex = world.mats.projection_view * glm::vec4(line.p0, 1.0f),
				.color = line.color
			});
			strides.push_back(Stride {
				.vertex = world.mats.projection_view * glm::vec4(line.p1, 1.0f),
				.color = line.color
			});
		}

		gl_program_use(self.program);
		glBindVertexArray(self.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, self.gl_buf.vbo);

		glEnable(GL_LINE_SMOOTH);
		#ifndef OS_MACOS
		glLineWidth(self.line_width);
		#endif

		for (int i = 0; i < strides.size(); i += self.gl_buf.len) {
			const size_t count = std::min(self.gl_buf.len, strides.size()-i);
			glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Stride), strides.data()+i);
			glDrawArrays(GL_LINES, 0, count);
		}
	}

	void canvas_render_ground(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		gl_program_use(self.ground.program);
		gl_program_uniform_set(self.ground.program, "projection_inverse", world.mats.projection_inverse);
		gl_program_uniform_set(self.ground.program, "view_inverse", world.mats.view_inverse);
		gl_program_uniform_set(self.ground.program, "color", self.ground.last_gnd.color);

		glDisable(GL_DEPTH_TEST);

		glBindTexture(GL_TEXTURE_2D, self.ground.tile_texture);
		glBindVertexArray(self.ground.gl_buf.vao);
		glDrawArrays(GL_TRIANGLES, 0, self.ground.gl_buf.len);

		glEnable(GL_DEPTH_TEST);
	}

	void canvas_render_gnd_pictures(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		if (self.gnd_pics.list.empty()) {
			return;
		}

		glDisable(GL_DEPTH_TEST);
		gl_program_use(world.canvas.gnd_pics.program);

		for (const auto& gnd_pic : self.gnd_pics.list) {
			gl_program_uniform_set(self.gnd_pics.program, "projection_view_model", gnd_pic.projection_view_model);

			for (const auto& primitives : gnd_pic.list_primitives) {
				gl_program_uniform_set(self.gnd_pics.program, "primitive_color[0]", primitives.color);

				gl_program_uniform_set(self.gnd_pics.program, "gradient_enabled", primitives.gradient_enabled);
				if (primitives.gradient_enabled) {
					gl_program_uniform_set(self.gnd_pics.program, "primitive_color[1]", primitives.gradient_color2);
				}

				glBindVertexArray(primitives.vao);
				glDrawArrays(primitives.gl_primitive_type, 0, primitives.buf_len);
			}
		}

		glEnable(GL_DEPTH_TEST);
	}
}

int main() {
	World world {};
	mu::log_global_logger = (mu::ILogger*) &world.imgui_window_logger;

	test_parser();
	test_aabbs_intersection();
	test_polygons_to_triangles();
	test_line_segments_to_lines();

	sys::sdl_init(world);
	mu_defer(sys::sdl_free(world));

	sys::projection_init(world);

	sys::imgui_init(world);
	mu_defer(sys::imgui_free(world));

	sys::canvas_init(world);
	mu_defer(sys::canvas_free(world));

	sys::audio_init(world);
	mu_defer(sys::audio_free(world));

	sys::scenery_init(world);
	mu_defer(sys::scenery_free(world));

	sys::aircrafts_init(world);
	mu_defer(sys::aircrafts_free(world));

	sys::ground_objs_init(world);
	mu_defer(sys::ground_objs_free(world));

	signal_listen(world.signals.quit);
	while (!signal_handle(world.signals.quit)) {
		sys::loop_timer_update(world);
		if (!world.loop_timer.ready) {
			time_delay_millis(2);
			continue;
		}
		TEXT_OVERLAY("fps: {:.2f}", 1.0f/world.loop_timer.delta_time);

		sys::events_collect(world);

		sys::projection_update(world);
		sys::camera_update(world);
		sys::cached_matrices_recalc(world);

		sys::scenery_update(world);
		sys::scenery_prepare_render(world);

		sys::aircrafts_update(world);
		sys::aircrafts_prepare_render(world);

		sys::ground_objs_update(world);
		sys::ground_objs_prepare_render(world);

		sys::models_handle_collision(world);

		sys::canvas_rendering_begin(world); {
			sys::canvas_render_ground(world);
			sys::canvas_render_gnd_pictures(world);
			sys::canvas_render_zlpoints(world);
			sys::canvas_render_meshes(world);
			sys::canvas_render_axes(world);
			sys::canvas_render_boxes(world);
			sys::canvas_render_lines(world);
			sys::canvas_render_text(world);
			sys::canvas_render_hud_text(world);

			sys::imgui_rendering_begin(world); {
				sys::imgui_debug_window(world);
				sys::imgui_logs_window(world);
				sys::imgui_overlay_text(world);
			}
			sys::imgui_rendering_end(world);
		}
		sys::canvas_rendering_end(world);

		mu::memory::reset_tmp();
	}

	return 0;
}
