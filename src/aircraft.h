#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <mu/utils.h>

#include "math.h"
#include "audio.h"
#include "assets.h"

constexpr double ANTI_COLL_LIGHT_PERIOD = 1;

struct Aircraft {
	AircraftTemplate aircraft_template;
	Model model;
	DATMap dat;
	AudioBuffer* engine_sound;
	uint64_t audio_playback_id = 0;

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
	float prop_efficiency = 0.8f;
	float landing_gear_alpha = 0; // 0 -> DOWN, 1 -> UP
	float throttle = 0;
	float pitch_input_max;
	float yaw_input_max;
	float roll_input_max;

	float elevator_perc; // [-1,1], +ve -> elevator upside, both wings
	float rudder_perc; // [-1,1], +ve -> rudder is right-side
	float right_aileron_perc; // [-1,1], +ve -> right aileron is right-side, left one is the opposite direction

	struct {
		LinearFuncConsts linear;

		bool quad_funcs_dirty;
		float aoa_crit_neg, aoa_crit_pos;
		QuadraticFuncConsts quad_neg, quad_pos;
	} cl_consts;
	QuadraticFuncConsts cd_consts;

	struct {
		float speed_percent; // 0 -> 1
		bool burner_enabled = false;
		bool cutoff = false;
		float max_power, idle_power; // HP
		float fuel_mili = 0.45f;     // kg/s at military power
		float fuel_abrn = 0.0f;      // kg/s additional with afterburner
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

inline Aircraft aircraft_new(AircraftTemplate aircraft_template) {
	return Aircraft {
		.aircraft_template = aircraft_template,
		.should_be_loaded = true,
	};
}

inline void aircraft_load(Aircraft& self) {
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
	// WEIGHCLN 19.0t                #WEIGHT CLEAN
	self.mass.clean = 15.0f;
	if (datmap_get_floats(self.dat, "WEIGHCLN", {&self.mass.clean})) { self.mass.clean /= 1e6; }
	// WEIGFUEL  5.0t                #WEIGHT OF FUEL
	self.mass.fuel = 5.0f;
	if (datmap_get_floats(self.dat, "WEIGFUEL", {&self.mass.fuel}))  { self.mass.fuel /= 1e6; }
	// WEIGLOAD  4.5t                #WEIGHT OF PAYLOAD
	self.mass.load = 4.5f;
	if (datmap_get_floats(self.dat, "WEIGLOAD", {&self.mass.load}))  { self.mass.load /= 1e6; }

	// engine power, assume engines are equal
	// REALPROP 0 MAXPOWER       3060HP      # 1 argument.  Maximum horse power or J/s
	self.engine.max_power = 3060;
	datmap_get_floats(self.dat, "REALPROP 0 MAXPOWER", {&self.engine.max_power});

	// REALPROP 0 IDLEPOWER      30HP        # 1 argument.  Idling horse power or J/s
	self.engine.idle_power = 30;
	datmap_get_floats(self.dat, "REALPROP 0 IDLEPOWER", {&self.engine.idle_power});

	// FUELMILI  0.45kg               # FUEL CONSUMPTION at military power
	self.engine.fuel_mili = 0.45f;
	if (datmap_get_floats(self.dat, "FUELMILI", {&self.engine.fuel_mili})) { self.engine.fuel_mili /= 1000; }
	// FUELABRN  6.5kg                # FUEL CONSUMPTION additional with afterburner
	self.engine.fuel_abrn = 0.0f;
	if (datmap_get_floats(self.dat, "FUELABRN", {&self.engine.fuel_abrn})) { self.engine.fuel_abrn /= 1000; }

	// MAXSPEED 480km/h              #MAXIMUM SPEED
	self.max_velocity = 133;
	datmap_get_floats(self.dat, "MAXSPEED", {&self.max_velocity});

	// WINGAREA 91m^2                #WING AREA
	self.wing_area = 91;
	datmap_get_floats(self.dat, "WINGAREA", {&self.wing_area});

	// Cl
	// REALPROP 0 CL 0deg 0.2 15deg 1.2      # 4 argument.  AOA1 cl1 AOA2 cl2   (Approximated by a linear function)
	{
		float aoa1 = 0, cl1 = 0.2, aoa2 = 15, cl2 = 1.2;
		datmap_get_floats(self.dat, "REALPROP 0 CL", {&aoa1, &cl1, &aoa2, &cl2});
		self.cl_consts.linear = linear_func_new({aoa1, cl1}, {aoa2, cl2});
		self.cl_consts.quad_funcs_dirty = true;
	}

	// CRITAOAP  20deg               #CRITICAL AOA POSITIVE
	self.cl_consts.aoa_crit_pos = 20;
	datmap_get_floats(self.dat, "CRITAOAP", {&self.cl_consts.aoa_crit_pos});

	// CRITAOAM -15deg               #CRITICAL AOA NEGATIVE
	self.cl_consts.aoa_crit_neg = -15;
	datmap_get_floats(self.dat, "CRITAOAM", {&self.cl_consts.aoa_crit_neg});

	// Cd
	// REALPROP 0 CD -5deg 0.006 20deg 0.4   # 4 argument.  AOAminCd minCd AOA1 cd1 (Approximated by a quadratic function)
	{
		float aoa_min = -5, cd_min = 0.006f, aoa1 = 20, cl1 = 0.4f;
		datmap_get_floats(self.dat, "REALPROP 0 CD", {&aoa_min, &cd_min, &aoa1, &cl1});
		self.cd_consts = quad_func_new({aoa_min, cd_min}, {aoa1, cl1});
	}

	// MXIPTAOA 20.0deg              #MAX INPUT AOA
	self.pitch_input_max = 20;
	datmap_get_floats(self.dat, "MXIPTAOA", {&self.pitch_input_max});

	// MXIPTSSA 5.0deg               #MAX INPUT SSA
	self.yaw_input_max = 5;
	datmap_get_floats(self.dat, "MXIPTSSA", {&self.yaw_input_max});

	// MXIPTROL 60.0deg              #MAX INPUT ROLL
	self.roll_input_max = 60;
	datmap_get_floats(self.dat, "MXIPTROL", {&self.roll_input_max});

	self.should_be_loaded = false;
}

// degrees
inline float aircraft_angle_of_attack(const Aircraft& self) {
	bool other_side = glm::acos(-self.angles.up.y) > 1.5708f;
	auto a = 90 + (other_side ? +1 : -1) * glm::acos(-self.angles.front.y) / RADIANS_MAX * DEGREES_MAX;
	if (a > 180) {
		return a - 360;
	}
	return a;
}

inline float aircraft_calc_drag_coeff(const Aircraft& self, float angle_of_attack) {
	return quad_func_eval(self.cd_consts, angle_of_attack);
}

inline float aircraft_calc_lift_coeff(const Aircraft& self, float angle_of_attack) {
	if (angle_of_attack < self.cl_consts.aoa_crit_neg) {
		return quad_func_eval(self.cl_consts.quad_neg, angle_of_attack);
	}
	if (angle_of_attack > self.cl_consts.aoa_crit_pos) {
		return quad_func_eval(self.cl_consts.quad_pos, angle_of_attack);
	}
	return linear_func_eval(self.cl_consts.linear, angle_of_attack);
}

inline void aircraft_unload(Aircraft& self) {
	for (auto& mesh : self.model.meshes) {
		mesh_unload_from_gpu(mesh);
	}
}

inline void aircraft_set_start(Aircraft& self, const StartInfo& start_info) {
	self.translation = start_info.position;
	self.angles = local_euler_angles_from_attitude(start_info.attitude);
	self.landing_gear_alpha = start_info.landing_gear_is_out? 0.0f : 1.0f;
	self.throttle = start_info.throttle;
	self.engine.speed_percent = start_info.throttle;
}

inline bool aircraft_on_ground(const Aircraft& self) {
	return self.translation.y >= -1.0f;
}

inline float aircraft_mass_total(const Aircraft& self) {
	return (self.mass.clean + self.mass.fuel + self.mass.load) * 1000.0f;
}

inline glm::vec3 aircraft_thrust(const Aircraft& self)   { return self.angles.front * self.forces.thrust; }
inline glm::vec3 aircraft_drag(const Aircraft& self)     { return -self.angles.front * self.forces.drag;  }
inline glm::vec3 aircraft_airlift(const Aircraft& self)  { return self.angles.up * self.forces.airlift;   }
inline glm::vec3 aircraft_weight(const Aircraft& self)   { return glm::vec3{0,1,0} * self.forces.weight;  }

inline glm::vec3 aircraft_forces_total(const Aircraft& self) {
	return aircraft_weight(self)+ aircraft_airlift(self) + aircraft_drag(self) + aircraft_thrust(self);
}
