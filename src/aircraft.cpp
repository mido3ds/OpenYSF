#include "world.h"

namespace sys {

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
				if (world.aircrafts[i].audio_playback_id != 0) {
					audio_device_stop_by_id(world.audio_device, world.aircrafts[i].audio_playback_id);
					world.aircrafts[i].audio_playback_id = 0;
				}

				int tracked_model_index = -1;
				for (int j = 0; j < world.aircrafts.size(); j++) {
					if (world.camera.aircraft == &world.aircrafts[j]) {
						tracked_model_index = j;
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

	void _aircrafts_update_cl_function(World& world) {
		DEF_SYSTEM

		for (auto& aircraft : world.aircrafts) {
			if (aircraft.cl_consts.quad_funcs_dirty) {
				aircraft.cl_consts.quad_funcs_dirty = false;
				aircraft.cl_consts.quad_neg = quad_func_new(
					{aircraft.cl_consts.aoa_crit_neg, linear_func_eval(aircraft.cl_consts.linear, aircraft.cl_consts.aoa_crit_neg)},
					{-100, 2}
				);
				aircraft.cl_consts.quad_pos = quad_func_new(
					{aircraft.cl_consts.aoa_crit_pos, linear_func_eval(aircraft.cl_consts.linear, aircraft.cl_consts.aoa_crit_pos)},
					{100, -2}
				);
			}
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
			if (aircraft.audio_playback_id != 0) {
				audio_device_stop_by_id(world.audio_device, aircraft.audio_playback_id);
				aircraft.audio_playback_id = 0;
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

		if (world.events.mouse_plane_control_enabled) {
			self.elevator_perc += -world.events.mouse_dy * MOUSE_SENSITIVITY;
			self.elevator_perc -= glm::sign(self.elevator_perc) * CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			if (std::abs(self.elevator_perc) <= 0.1f) {
				self.elevator_perc = 0;
			}
			self.elevator_perc = glm::clamp(self.elevator_perc, -1.0f, 1.0f);

			self.right_aileron_perc += world.events.mouse_dx * MOUSE_SENSITIVITY;
			self.right_aileron_perc -= glm::sign(self.right_aileron_perc) * CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			if (std::abs(self.right_aileron_perc) <= 0.1f) {
				self.right_aileron_perc = 0;
			}
			self.right_aileron_perc = glm::clamp(self.right_aileron_perc, -1.0f, 1.0f);
		} else {
			if (world.events.stick_front) {
				self.elevator_perc += CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			} else if (world.events.stick_back) {
				self.elevator_perc -= CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			} else {
				self.elevator_perc -= glm::sign(self.elevator_perc) * CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
				if (std::abs(self.elevator_perc) <= 0.1f) {
					self.elevator_perc = 0;
				}
			}
			self.elevator_perc = glm::clamp(self.elevator_perc, -1.0f, 1.0f);

			if (world.events.stick_right) {
				self.right_aileron_perc += CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			} else if (world.events.stick_left) {
				self.right_aileron_perc -= CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			} else {
				self.right_aileron_perc -= glm::sign(self.right_aileron_perc) * CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
				if (std::abs(self.right_aileron_perc) <= 0.1f) {
					self.right_aileron_perc = 0;
				}
			}
			self.right_aileron_perc = glm::clamp(self.right_aileron_perc, -1.0f, 1.0f);
		}

		if (world.events.rudder_right) {
			self.rudder_perc += CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
		} else if (world.events.rudder_left) {
			self.rudder_perc -= CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
		} else {
			self.rudder_perc -= glm::sign(self.rudder_perc) * CTRL_SURFACE_SPEED * world.loop_timer.delta_time;
			if (std::abs(self.rudder_perc) <= 0.1f) {
				self.rudder_perc = 0;
			}
		}
		self.rudder_perc = glm::clamp(self.rudder_perc, -1.0f, 1.0f);

		TEXT_OVERLAY("elevator = {}%", int(self.elevator_perc * 100));
		TEXT_OVERLAY("aileron = {}%", int(self.right_aileron_perc * 100));
		TEXT_OVERLAY("rudder = {}%", int(self.rudder_perc * 100));
		TEXT_OVERLAY("fuel = {:.1f}t", self.mass.fuel);
		float eff_burn = self.engine.fuel_mili + (self.engine.burner_enabled ? self.engine.fuel_abrn : 0);
		TEXT_OVERLAY("burn = {:.2f}kg/s", (self.engine.cutoff ? 0.0f : eff_burn * self.engine.speed_percent));



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

		if (world.events.brake) {
			self.braking = !self.braking;
		}

		if (world.events.landing_gear_toggle) {
			self.landing_gear_alpha = self.landing_gear_alpha > 0.5f ? 0.0f : 1.0f;
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

			if (!aircraft.engine.cutoff) {
				if (aircraft.engine.speed_percent < aircraft.throttle) {
					aircraft.engine.speed_percent += world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
					aircraft.engine.speed_percent = clamp(aircraft.engine.speed_percent, 0.0f, aircraft.throttle);
				} else if (aircraft.engine.speed_percent > aircraft.throttle) {
					aircraft.engine.speed_percent -= world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
					aircraft.engine.speed_percent = clamp(aircraft.engine.speed_percent, aircraft.throttle, 1.0f);
				}
			}

			// reset cutoff if fuel was replenished (e.g. via debug UI)
			if (aircraft.mass.fuel > 0.0f && aircraft.engine.cutoff) {
				aircraft.engine.cutoff = false;
			}

			// fuel consumption: burn proportional to engine speed
			if (aircraft.mass.fuel > 0.0f) {
				float effective_rate = aircraft.engine.fuel_mili + (aircraft.engine.burner_enabled ? aircraft.engine.fuel_abrn : 0);
				float fuel_burn_tons = effective_rate * aircraft.engine.speed_percent
										* (float)world.loop_timer.delta_time / 1000.0f;
				aircraft.mass.fuel = std::max(aircraft.mass.fuel - fuel_burn_tons, 0.0f);
			}

			// engine cutoff when fuel is empty
			if (aircraft.mass.fuel <= 1e-6f) {
				aircraft.mass.fuel = 0.0f;
				aircraft.engine.cutoff = true;
			}

			// decay engine speed when cutoff
			if (aircraft.engine.cutoff && aircraft.engine.speed_percent > 0) {
				aircraft.engine.speed_percent -= (float)world.loop_timer.delta_time / ENGINE_PROPELLERS_RESISTENCE;
				aircraft.engine.speed_percent = std::max(aircraft.engine.speed_percent, 0.0f);
			}

			// engine sound — runs for every aircraft, not just tracked
			int audio_index = aircraft.engine.speed_percent * 9;

			AudioBuffer* audio;
			if (aircraft.has_propellers) {
				audio = &world.audio_buffers.at(mu::str_tmpf("prop{}", audio_index));
			} else if (aircraft.engine.burner_enabled && aircraft.has_afterburner) {
				audio = &world.audio_buffers.at("burner");
			} else {
				audio = &world.audio_buffers.at(mu::str_tmpf("engine{}", audio_index));
			}

			if (aircraft.engine_sound != audio) {
				if (aircraft.audio_playback_id != 0) {
					audio_device_stop_by_id(world.audio_device, aircraft.audio_playback_id);
				}
				aircraft.engine_sound = audio;
				aircraft.audio_playback_id = audio_device_play_looped(world.audio_device, *aircraft.engine_sound);
				audio_device_set_gain(world.audio_device, aircraft.audio_playback_id, 0.0f);
			}

			if (aircraft.engine.cutoff || aircraft.mass.fuel <= 0) {
				if (aircraft.audio_playback_id != 0) {
					audio_device_stop_by_id(world.audio_device, aircraft.audio_playback_id);
					aircraft.audio_playback_id = 0;
					aircraft.engine_sound = nullptr;
				}
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
				auto engine_power_hp = aircraft.engine.cutoff ? 0.0f
					: aircraft.engine.speed_percent * aircraft.engine.max_power + (1-aircraft.engine.speed_percent) * aircraft.engine.idle_power;
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

			// elevator deflection → lift contribution (tail downforce)
			aircraft.forces.airlift += aircraft.elevator_perc * ELEVATOR_LIFT_SCALE
				* (float)air_density * vel_sq;

			// ground proximity factor: 1 on ground (y=-1), 0 at 1m above (y=-2)
			float ground_factor = glm::clamp(aircraft.translation.y + 2.0f, 0.0f, 1.0f);

			if (ground_factor > 0.0f) {
				aircraft.forces.weight = 0;
			}

			// rotation — proportional tracking controller through SI-corrected torque pipeline
			{
				// SI-corrected inverse inertia (code stores in g·m² — ×1000 for kg·m²)
				glm::mat3 I_inv_kg(0.0f);
				bool has_inertia = !glm::all(glm::equal(aircraft.inertia_tensor_inv[0], glm::vec3(0.0f)));
				if (has_inertia) I_inv_kg = aircraft.inertia_tensor_inv * 1000.0f;

				float vel = glm::length(aircraft.velocity);
				float v_ratio = glm::clamp((vel * vel) / (aircraft.max_velocity * aircraft.max_velocity), 0.0f, 1.0f);
				float dt = (float)world.loop_timer.delta_time;

				// desired angular velocity from control surfaces (old kinematic formula, preserved feel)
				glm::vec3 desired_omega{0.0f};
				desired_omega.x = -aircraft.elevator_perc * ELEVATOR_EFFICIENCY * v_ratio;
				desired_omega.y =  aircraft.rudder_perc * RUDDER_EFFICIENCY * v_ratio;
				desired_omega.z =  aircraft.right_aileron_perc * ROLL_EFFICIENCY * v_ratio;
				desired_omega.y += aircraft.right_aileron_perc * ADVERSE_YAW_COEFF * v_ratio;

				// Proportional controller: torque = I · K · (ω_desired - ω)
				// → α = I⁻¹ · torque = K · error (inertia cancels, pure first-order response)
				constexpr float CTRL_BANDWIDTH = 6.0f;   // ~0.17s time constant, matches previous lag
				glm::vec3 omega_err = desired_omega - aircraft.angular_velocity;

				aircraft.torque = glm::vec3{0.0f};
				if (has_inertia) {
					if (I_inv_kg[0][0] > 0) aircraft.torque.x = omega_err.x * CTRL_BANDWIDTH / I_inv_kg[0][0];
					if (I_inv_kg[1][1] > 0) aircraft.torque.y = omega_err.y * CTRL_BANDWIDTH / I_inv_kg[1][1];
					if (I_inv_kg[2][2] > 0) aircraft.torque.z = omega_err.z * CTRL_BANDWIDTH / I_inv_kg[2][2];
				}

				// External torques (SI N·m with thrust-fudge compensation)
				if (!glm::all(glm::equal(aircraft.thrust_offset, glm::vec3(0.0f)))) {
					// ponytail: thrust_multiplier=500 fudges thrust ~5700×; scale by 1e-4 for real N·m
					aircraft.torque += glm::cross(aircraft.thrust_offset,
						glm::vec3{0.0f, 0.0f, aircraft.forces.thrust}) * 1e-4f;
				}

				// integrate angular velocity through corrected inertia
				if (has_inertia) {
					aircraft.angular_velocity += (I_inv_kg * aircraft.torque) * dt;
				}

				// GROUND OVERRIDE (direct ω control, before quaternion integration)
				if (aircraft_on_ground(aircraft)) {
					aircraft.angular_velocity.x = 0.0f;
					aircraft.angular_velocity.z = 0.0f;
					float wheel_angle = aircraft.rudder_perc * MAX_WHEEL_STEER_ANGLE;
					aircraft.angular_velocity.y = +(vel * std::tan(wheel_angle)) / aircraft.wheelbase;
				}

				// integrate orientation from local angular velocity (post-multiply)
				float omega_mag = glm::length(aircraft.angular_velocity);
				if (omega_mag > 0.0001f) {
					glm::vec3 omega_axis = aircraft.angular_velocity / omega_mag;
					float angle = omega_mag * dt;
					aircraft.orientation = glm::normalize(aircraft.orientation * glm::angleAxis(angle, omega_axis));
				}
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

				// GROUND EFFECTS: friction + brake as velocity damping
				if (ground_factor > 0.0f && vel_mag > 0.01f) {
					float mass = aircraft_mass_total(aircraft);
					// airlift is in Newtons; weight is zeroed on ground, so recompute full weight here
					float normal_force = std::max(mass * 9.86f - aircraft.forces.airlift, 0.0f);

					// rolling friction opposes horizontal velocity
					float hor_vel_sq = aircraft.velocity.x * aircraft.velocity.x + aircraft.velocity.z * aircraft.velocity.z;
					if (hor_vel_sq > 0.0001f) {
						float hor_vel = std::sqrt(hor_vel_sq);
						// ponytail: no dt — physics velocity integration doesn't multiply other forces by dt
						float friction_decel = aircraft.friction_coeff * normal_force / mass * ground_factor;
						float friction_dv = std::min(friction_decel, hor_vel);
						glm::vec2 hor_dir = glm::normalize(glm::vec2(aircraft.velocity.x, aircraft.velocity.z));
						aircraft.velocity.x -= hor_dir.x * friction_dv;
						aircraft.velocity.z -= hor_dir.y * friction_dv;
					}

					// brake opposes total velocity
					if (aircraft.braking) {
						// ponytail: no dt — matches thrust/everything else
						float brake_decel = world.settings.brake_coeff * normal_force / mass * ground_factor;
						float brake_dv = std::min(brake_decel, vel_mag);
						aircraft.velocity -= glm::normalize(aircraft.velocity) * brake_dv;
					}
				}
			}

			aircraft.translation += (float)world.loop_timer.delta_time * aircraft.velocity;

			// soft push toward ground when proximity active
			if (ground_factor > 0.0f) {
				float ground_y = -1.0f;
				aircraft.translation.y += (ground_y - aircraft.translation.y)
										* ground_factor * (float)world.loop_timer.delta_time * 5.0f;
			}
			aircraft.translation.y = std::min(aircraft.translation.y, -1.0f);

			// transform AABB (estimate new AABB after rotation)
			const auto model_transformation = local_euler_angles_matrix(aircraft_angles(aircraft), aircraft.translation);
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
		_aircrafts_update_cl_function(world);
		_aircrafts_apply_user_controls(world);
		_aircrafts_apply_physics(world);

		// distance-based audio gain
		constexpr float MAX_AUDIBLE_DIST = 5000.0f;
		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];
			if (aircraft.audio_playback_id == 0) continue;
			float dist = glm::distance(world.camera.position, aircraft.translation);
			float gain = 1.0f - glm::clamp(dist / MAX_AUDIBLE_DIST, 0.0f, 1.0f);
			audio_device_set_gain(world.audio_device, aircraft.audio_playback_id, gain);
		}
	}

	void aircrafts_prepare_render(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.aircrafts.size(); i++) {
			Aircraft& aircraft = world.aircrafts[i];

			if (!aircraft.visible) {
				continue;
			}

			if (aircraft.render_axes) {
				auto ang = aircraft_angles(aircraft);
				canvas_add(world.canvas, canvas::Vector {
					.label = "front",
					.p = aircraft.translation,
					.dir = ang.front,
					.len = 35.0f,
					.color = glm::vec4{1,0,0,0.3}
				});
				canvas_add(world.canvas, canvas::Vector {
					.label = "right",
					.p = aircraft.translation,
					.dir = glm::normalize(glm::cross(ang.front, ang.up)),
					.len = 20.0f,
					.color = glm::vec4{0,1,0,0.3}
				});
				canvas_add(world.canvas, canvas::Vector {
					.label = "up",
					.p = aircraft.translation,
					.dir = ang.up,
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

			if (world.camera.mode == CameraMode::Cockpit) {
				glm::quat rot = aircraft.orientation * glm::quat(glm::radians(world.settings.rendering.cockpit_rotation_offset));
				glm::vec3 pos = world.camera.position + world.camera.front * world.settings.rendering.cockpit_forward_offset;
				glm::mat4 model = glm::translate(glm::mat4{1.0f}, pos)
								* glm::mat4_cast(rot);
				glm::mat4 pvm = world.mats.projection_view * model;
				glm::mat3 model_normal = glm::transpose(glm::inverse(glm::mat3(model)));
				meshes_foreach(aircraft.cockpit_model.meshes, [&](Mesh& mesh) {
					canvas_add(world.canvas, canvas::Cockpit{
						.vao = mesh.gl_buf.vao,
						.buf_len = mesh.gl_buf.len,
						.projection_view_model = pvm,
						.model_normal = model_normal,
					});
					return true;
				});
			} else {
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
						.projection_view_model = world.mats.projection_view * mesh.transformation,
						.model_normal = glm::transpose(glm::inverse(glm::mat3(mesh.transformation)))
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

			if (world.camera.aircraft == &aircraft && world.settings.hud.enabled && world.camera.mode != CameraMode::Tower) {
				float airspeed_kt = glm::length(aircraft.velocity) * 1.94384f;
				float altitude_ft = (- aircraft.translation.y + 1.0f) * 3.28084f;

				canvas_add(world.canvas, canvas::hud::Text {
					.text = mu::str_tmpf("SPD {:0.2f} kt", airspeed_kt),
					.p = {0.02f, 0.95f},
					.scale = 0.5f,
					.color = {1,1,1,0.8f}
				});
				canvas_add(world.canvas, canvas::hud::Text {
					.text = mu::str_tmpf("ALT {:3.2f} ft", altitude_ft),
					.p = {0.02f, 0.90f},
					.scale = 0.5f,
					.color = {1,1,1,0.8f}
				});
				canvas_add(world.canvas, canvas::hud::Text {
					.text = mu::str_tmpf("THR {:0.0f}%", aircraft.throttle * 100.0f),
					.p = {0.02f, 0.85f},
					.scale = 0.5f,
					.color = {1,1,1,0.8f}
				});
				canvas_add(world.canvas, canvas::hud::Text {
					.text = mu::str_tmpf("GEAR {}", aircraft.landing_gear_alpha > 0.5f ? "UP" : "DOWN"),
					.p = {0.02f, 0.80f},
					.scale = 0.5f,
					.color = {1,1,1,0.8f}
				});
				if (aircraft.braking) {
					canvas_add(world.canvas, canvas::hud::Text {
						.text = mu::str_tmpf("BRK"),
						.p = {0.02f, 0.75f},
						.scale = 0.5f,
						.color = {1,0.2f,0.2f,0.8f}
					});
				}

				// AoA indicator — vertical strip gauge
				{
					auto& aoa_st = world.settings.hud.aoa;
					float aoa = aircraft_angle_of_attack(aircraft);
					float aoa_min = -5.0f, aoa_max = 25.0f;
					float bar_x = aoa_st.position.x;
					float bar_top = aoa_st.position.y + aoa_st.height * 0.5f;
					float bar_bot = aoa_st.position.y - aoa_st.height * 0.5f;
					float bar_h = aoa_st.height;
					auto aoa_y = [&](float deg) { return bar_bot + bar_h * (deg - aoa_min) / (aoa_max - aoa_min); };

					float aoa_labels[] = {-5.0f, 0.0f, 10.0f, 20.0f, 25.0f};
					for (float deg : aoa_labels) {
						float y = aoa_y(deg);
						bool major = (deg == 0.0f || deg == 10.0f || deg == 20.0f);
						float tick_w = major ? 0.012f : 0.007f;
						canvas_add(world.canvas, canvas::hud::Line{
							.p0 = {bar_x - tick_w, y},
							.p1 = {bar_x + tick_w, y},
							.color = aoa_st.tick_color,
						});
						if (major) {
							canvas_add(world.canvas, canvas::hud::Text{
								.text = mu::str_tmpf("{:.0f}", deg),
								.p = {bar_x + 0.014f, y - 0.01f},
								.scale = 0.3f,
								.color = aoa_st.label_color,
							});
						}
					}

					float clamped_aoa = glm::clamp(aoa, aoa_min, aoa_max);
					float iy = aoa_y(clamped_aoa);
					float tri_h = 0.012f;
					float tx = bar_x + aoa_st.indicator_offset.x;
					float ty = iy + aoa_st.indicator_offset.y;
					canvas_add(world.canvas, canvas::hud::FilledTriangle{
						.p0 = {tx, ty},
						.p1 = {tx + tri_h, ty + tri_h * 0.6f},
						.p2 = {tx + tri_h, ty - tri_h * 0.6f},
						.color = aoa_st.indicator_color,
					});
				}

				// Heading indicator (compass rose)
				auto ang = aircraft_angles(aircraft);
				float heading_rad = std::atan2(ang.front.x, ang.front.z);
				if (heading_rad < 0) heading_rad += RADIANS_MAX;

				auto& hdg = world.settings.hud.heading;

				canvas_add(world.canvas, canvas::hud::Circle{hdg.position, hdg.radius, hdg.color});

				for (int i = 0; i < 36; i++) {
					float tick_compass_rad = (i * 10.0f) / 360.0f * RADIANS_MAX;
					float screen_angle = -RADIANS_MAX/4 + tick_compass_rad - heading_rad;
					bool major = (i % 3 == 0);
					float inner = major ? hdg.radius * 0.78f : hdg.radius * 0.88f;
					glm::vec2 dir = {std::cos(screen_angle), std::sin(screen_angle)};
					canvas_add(world.canvas, canvas::hud::Line{
						.p0 = hdg.position + dir * inner,
						.p1 = hdg.position + dir * hdg.radius,
						.color = hdg.color,
					});
				}

				const char* card_names[] = {"N", "E", "S", "W"};
				for (int c = 0; c < 4; c++) {
					float card_compass_rad = (c * 90.0f) / 360.0f * RADIANS_MAX;
					float screen_angle = -RADIANS_MAX/4 + card_compass_rad - heading_rad;
					glm::vec2 lp = hdg.position + glm::vec2{std::cos(screen_angle), std::sin(screen_angle)} * (hdg.radius * 0.68f);
					canvas_add(world.canvas, canvas::hud::Text{
						.text = mu::str_tmpf("{}", card_names[c]),
						.p = lp - glm::vec2{0.012f, 0.018f},
						.scale = 0.4f,
						.color = {1,0,0,0.9f},
					});
				}

				float heading_deg = heading_rad / RADIANS_MAX * 360.0f;
				canvas_add(world.canvas, canvas::hud::Text{
					.text = mu::str_tmpf("{:03.0f}", heading_deg),
					.p = {hdg.position.x - 0.02f, hdg.position.y - hdg.radius - 0.03f},
					.scale = 0.4f,
					.color = hdg.color,
				});

				// VSI (Vertical Speed Indicator)
				float vsi_ftmin = -aircraft.velocity.y * 196.8504f;
				auto& vsi = world.settings.hud.vsi;

				canvas_add(world.canvas, canvas::hud::FilledArc{
					.center = vsi.position, .radius = vsi.radius,
					.start_angle = 3*RADIANS_MAX/8, .end_angle = 5*RADIANS_MAX/8,
					.color = vsi.arc_color,
				});

				for (int t = 0; t <= 12; t++) {
					float val = (t - 6) * 1.0f;
					float angle = RADIANS_MAX/2 - val / 6.0f * RADIANS_MAX/8;
					bool major = (val == 0 || val == 6 || val == -6);
					float inner = major ? vsi.radius * 0.78f : vsi.radius * 0.88f;
					glm::vec2 dir = {std::cos(angle), std::sin(angle)};
					canvas_add(world.canvas, canvas::hud::Line{
						.p0 = vsi.position + dir * inner,
						.p1 = vsi.position + dir * vsi.radius,
						.color = vsi.color,
					});
					if (val != 0) {
						canvas_add(world.canvas, canvas::hud::Text{
							.text = mu::str_tmpf("{:.0f}", std::abs(val)),
							.p = vsi.position + dir * (vsi.radius * 0.65f) - glm::vec2{0.01f, 0.012f},
							.scale = 0.3f,
							.color = vsi.color,
						});
					}
				}

				float vsi_angle = RADIANS_MAX/2 - glm::clamp(vsi_ftmin, -6000.0f, 6000.0f) / 6000.0f * RADIANS_MAX/8;
				glm::vec2 needle_dir = {std::cos(vsi_angle), std::sin(vsi_angle)};
				canvas_add(world.canvas, canvas::hud::Line{
					.p0 = vsi.position - needle_dir * 0.008f,
					.p1 = vsi.position + needle_dir * vsi.radius * 0.85f,
					.color = {1,0.8f,0.2f,0.9f},
				});
				canvas_add(world.canvas, canvas::hud::Circle{vsi.position, 0.008f, {1,0.8f,0.2f,0.9f}});

				// ADI (Artificial Horizon)
				{
					auto ang = aircraft_angles(aircraft);

					float pitch_rad = std::asin(glm::clamp(-ang.front.y, -1.0f, 1.0f));
					float pitch_deg = pitch_rad / RADIANS_MAX * 360.0f;

					glm::vec3 world_up = {0,-1,0};
					glm::vec3 right_dir = glm::normalize(glm::cross(ang.front, world_up));
					glm::vec3 vert_dir = glm::normalize(glm::cross(right_dir, ang.front));
					float roll_rad = std::atan2(glm::dot(ang.up, right_dir), glm::dot(ang.up, vert_dir));

					auto& adi = world.settings.hud.adi;

					canvas_add(world.canvas, canvas::hud::FilledArc{adi.position, adi.radius, roll_rad, roll_rad + RADIANS_MAX/2, adi.ground_color});
					canvas_add(world.canvas, canvas::hud::FilledArc{adi.position, adi.radius, roll_rad + RADIANS_MAX/2, roll_rad, adi.sky_color});

					float pitch_scale = adi.radius * 0.85f / 30.0f;
					float horizon_off = -pitch_deg * pitch_scale;
					glm::vec2 up_dir = {-std::sin(roll_rad), std::cos(roll_rad)};
					glm::vec2 h_dir = {std::cos(roll_rad), std::sin(roll_rad)};
					glm::vec2 h_center = adi.position + up_dir * glm::clamp(horizon_off, -adi.radius, adi.radius);
					canvas_add(world.canvas, canvas::hud::Line{
						.p0 = h_center - h_dir * (adi.radius * 0.95f),
						.p1 = h_center + h_dir * (adi.radius * 0.95f),
						.color = adi.color,
					});

					for (int rel = -25; rel <= 25; rel += 5) {
						if (rel == 0) continue;
						float off = -(pitch_deg - rel) * pitch_scale;
						if (std::abs(off) > adi.radius * 1.1f) continue;
						glm::vec2 lc = adi.position + up_dir * off;
						bool is_10 = (std::abs(rel) % 10 == 0);
						float hl = is_10 ? adi.radius * 0.35f : adi.radius * 0.20f;
						canvas_add(world.canvas, canvas::hud::Line{
							.p0 = lc - h_dir * hl,
							.p1 = lc + h_dir * hl,
							.color = adi.color,
						});
					}

					glm::vec2 ct = adi.position + up_dir * (adi.radius + 0.012f);
					float cs = 0.012f;
					canvas_add(world.canvas, canvas::hud::FilledTriangle{
						.p0 = ct,
						.p1 = ct - up_dir * cs + h_dir * cs * 0.7f,
						.p2 = ct - up_dir * cs - h_dir * cs * 0.7f,
						.color = {1,0.6f,0,0.9f},
					});
					canvas_add(world.canvas, canvas::hud::Circle{adi.position, adi.radius, adi.color});
				}
			}
		}
	}
}
