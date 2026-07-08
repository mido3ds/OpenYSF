#pragma once

#include <glad/glad.h>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include "math.h"

struct Settings {
	bool fullscreen = false;
	bool should_limit_fps = true;
	int fps_limit = 60;
	bool custom_aspect_ratio = false;
	float current_angle_max = DEGREES_MAX;
	bool handle_collision = true;
	float brake_coeff = 1.0f;

	struct {
		bool smooth_lines = true;
		GLfloat line_width = 3.0f;
		GLfloat point_size = 3.0f;

		GLenum primitives_type = GL_TRIANGLES;
		GLenum polygon_mode    = GL_FILL;

		bool lighting = true;
		glm::vec3 ambient_color {0.784f, 0.784f, 0.784f}; // RGB(200,200,200) / 255
		glm::vec3 light_dir {0.577f, 0.577f, 0.577f}; // normalize(1,1,1)

		bool fog_enabled = false;
		float fog_density = 0.0001f;
		glm::vec3 fog_color {0.247f, 0.329f, 0.475f}; // (63, 84, 121)
	} rendering;

	struct {
		bool enabled = true;
		glm::vec2 position {-0.9f, -0.8f};
		float scale = 0.48f;
	} world_axis;
};

// Aircraft/physics constants
constexpr float PROPOLLER_MAX_ANGLE_SPEED = 10 * RADIANS_MAX;
constexpr float AFTERBURNER_THROTTLE_THRESHOLD = 0.80f;

constexpr float THROTTLE_SPEED = 1.0f;

constexpr float ENGINE_PROPELLERS_RESISTENCE = 15.0f;

// control surface efficiency coefficients (airspeed-scaled torques)
constexpr float CTRL_SURFACE_SPEED = 1.9f;
constexpr float ROLL_EFFICIENCY     = 0.5f;
constexpr float ELEVATOR_EFFICIENCY = 0.3f;
constexpr float RUDDER_EFFICIENCY   = 0.1f;
constexpr float ADVERSE_YAW_COEFF   = 0.2f;

// elevator deflection lift contribution (tail downforce)
constexpr float ELEVATOR_LIFT_SCALE = 5.0f;

// ground handling
constexpr float MAX_WHEEL_STEER_ANGLE = 0.5f;   // ~30° max nose-wheel deflection

// 2D sprite scaling
constexpr float ZL_SCALE = 0.151f;
