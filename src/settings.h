#pragma once

#include <glad/glad.h>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

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

		float cockpit_forward_offset = -0.3f;
		glm::vec3 cockpit_rotation_offset{0, 0, 180}; // pitch, yaw, roll (degrees)

	} rendering;

	struct {
		bool enabled = true;
		bool geoms_demo = false;

		struct {
			glm::vec2 position {0.50f, 0.90f};
			float radius = 0.05f;
			glm::vec4 color {1,1,1,0.8f};
		} heading;

		struct {
			glm::vec2 position {0.185f, 0.50f};
			float radius = 0.15f;
			glm::vec4 color {1,1,1,0.8f};
			glm::vec4 arc_color {0.2f,0.2f,0.25f,0.6f};
		} vsi;

		struct {
			glm::vec2 position {0.50f, 0.50f};
			float radius = 0.156f;
			glm::vec4 color {1,1,1,0.9f};
			glm::vec4 sky_color {0.1f, 0.4f, 0.7f, 0.85f};
			glm::vec4 ground_color {0.35f, 0.25f, 0.15f, 0.0f};
		} adi;

		struct {
			glm::vec2 position {0.690f, 0.566f};
			float height = 0.20f;
			glm::vec4 tick_color {1, 1, 1, 0.6f};
			glm::vec4 label_color {1, 1, 1, 0.7f};
			glm::vec4 indicator_color {1, 0.6f, 0, 0.9f};
			glm::vec2 indicator_offset {0.025f, 0.0f};
		} aoa;
	} hud;

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

// mouse plane control sensitivity (pixels → control surface percentage)
constexpr float MOUSE_SENSITIVITY = 0.05f;
