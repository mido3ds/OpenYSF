#pragma once

#include <SDL.h>

#include <cstdint>

#include <glm/glm.hpp>

#include <mu/utils.h>

#include "settings.h"
#include "camera.h"
#include "sysmon.h"
#include "canvas.h"
#include "scenery.h"
#include "ground_obj.h"
#include "aircraft.h"
#include "audio.h"

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
	bool brake;
	bool landing_gear_toggle;
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

	bool mouse_plane_control_enabled;
	int mouse_dx, mouse_dy;

	glm::ivec2 mouse_pos;
};

struct Signal {
	uint16_t _num_listeners, _num_handles;
};

inline void signal_listen(Signal& self) {
	self._num_listeners++;
}

inline bool signal_handle(Signal& self) {
	mu_assert_msg(self._num_listeners > 0, "signal has no registered listeners");

	if (self._num_handles > 0) {
		self._num_handles--;
		return true;
	}
	return false;
}

inline void signal_fire(Signal& self) {
	mu_assert_msg(self._num_listeners > 0, "signal has no registered listeners");

	self._num_handles = self._num_listeners;
}

// same as Events but don't get reset each frame (to be able to handle at any frame)
struct Signals {
	Signal quit;
	Signal wnd_configs_changed;
	Signal scenery_loaded;
};

struct LoopTimer {
	uint64_t _last_time_millis;
	int64_t _millis_till_render;

	// seconds since previous frame
	double delta_time;

	bool ready;
};

inline uint64_t time_now_millis() {
	return SDL_GetTicks64();
}

inline void time_delay_millis(uint32_t millis) {
	SDL_Delay(millis);
}

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

// Forward declarations for sys functions defined in separate .cpp files
namespace sys {
	void projection_init(World& world);
	void projection_update(World& world);
	void camera_update(World& world);
	void cached_matrices_recalc(World& world);

	void aircrafts_init(World& world);
	void aircrafts_free(World& world);
	void _aircrafts_apply_user_controls(World& world);
	void _aircrafts_apply_physics(World& world);
	void aircrafts_update(World& world);
	void aircrafts_prepare_render(World& world);

	void ground_objs_init(World& world);
	void ground_objs_free(World& world);
	void _ground_objs_apply_physics(World& world);
	void ground_objs_update(World& world);
	void ground_objs_prepare_render(World& world);

	void scenery_init(World& world);
	void scenery_free(World& world);
	void scenery_update(World& world);
	void scenery_prepare_render(World& world);
	void models_handle_collision(World& world);

	void canvas_init(World& world);
	void canvas_free(World& world);
	void canvas_rendering_begin(World& world);
	void canvas_rendering_end(World& world);
	void canvas_render_ground(World& world);
	void canvas_render_gnd_pictures(World& world);
	void canvas_render_zlpoints(World& world);
	void canvas_render_meshes(World& world);
	void canvas_render_axes(World& world);
	void canvas_render_text(World& world);
	void canvas_render_hud_text(World& world);
	void canvas_render_lines(World& world);
}
