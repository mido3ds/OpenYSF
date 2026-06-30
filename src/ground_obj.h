#pragma once

#include <glm/glm.hpp>

#include "math.h"
#include "assets.h"

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

inline GroundObj ground_obj_new(GroundObjTemplate ground_obj_template, glm::vec3 pos, glm::vec3 attitude) {
	return GroundObj {
		.ground_obj_template = ground_obj_template,
		.translation = pos,
		.angles = local_euler_angles_from_attitude(attitude),
		.should_be_loaded = true,
	};
}

inline void ground_obj_load(GroundObj& self) {
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

inline void ground_obj_unload(GroundObj& self) {
	for (auto& mesh : self.model.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}
