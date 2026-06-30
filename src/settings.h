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
