#pragma once

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::perspective, glm::lookAt

#include "math.h"

// forward declaration to avoid circular dependency with aircraft.h
struct Aircraft;

enum class CameraMode { Orbit, Chase, Tower, EXCAMERA, Cockpit };

struct PerspectiveProjection {
	float near         = 0.01f;
	float far          = 100000;
	float fovy         = 45.0f / DEGREES_MAX * RADIANS_MAX;
	float aspect       = (float) 1028 / 680;
};

inline glm::mat4 projection_calc_mat(PerspectiveProjection& self) {
	return glm::perspective(self.fovy, self.aspect, self.near, self.far);
}

struct Camera {
	Aircraft* aircraft;
	CameraMode mode = CameraMode::Orbit;
	int camera_index = -1;         // EXCAMERA index or tower viewpoint index
	float zoom_multiplier = 5;
	mu::Vec<glm::vec3> tower_viewpoints;

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

inline glm::mat4 camera_calc_view(const Camera& self) {
	return glm::lookAt(self.position, self.target_pos, self.up);
}

// precalculated matrices
struct CachedMatrices {
	glm::mat4 view;
	glm::mat4 view_inverse;

	glm::mat4 projection;
	glm::mat4 projection_inverse;

	glm::mat4 projection_view;
};
