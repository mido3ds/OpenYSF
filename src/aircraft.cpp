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

			if (aircraft_on_ground(aircraft)) {
				float friction = aircraft.friction_coeff * std::max(aircraft.forces.weight - aircraft.forces.airlift, 0.0f);
				aircraft.forces.thrust = std::max(aircraft.forces.thrust - friction, 0.0f);

				aircraft.forces.weight = 0;
			}

			// rotation — torque from control surfaces, scaled by airspeed
			{
				float vel = glm::length(aircraft.velocity);
				float max_vel = aircraft.max_velocity;
				float airspeed_factor = glm::clamp((vel * vel) / (max_vel * max_vel), 0.0f, 1.0f);

				float delta_yaw = 0, delta_roll = 0, delta_pitch = 0;

				delta_roll += aircraft.right_aileron_perc * ROLL_EFFICIENCY
					* airspeed_factor * (float)world.loop_timer.delta_time;

				delta_pitch += aircraft.elevator_perc * ELEVATOR_EFFICIENCY
					* airspeed_factor * (float)world.loop_timer.delta_time;

				delta_yaw += -aircraft.rudder_perc * RUDDER_EFFICIENCY
					* airspeed_factor * (float)world.loop_timer.delta_time;

				// aileron adverse yaw
				delta_yaw += -aircraft.right_aileron_perc * ADVERSE_YAW_COEFF
					* airspeed_factor * (float)world.loop_timer.delta_time;

				// integrate quaternion using local-frame rotation deltas
				auto ang = aircraft_angles(aircraft);
				auto right = glm::cross(ang.up, ang.front);
				glm::quat q_yaw = glm::angleAxis(delta_yaw, ang.up);
				glm::quat q_pitch = glm::angleAxis(delta_pitch, right);
				glm::quat q_roll = glm::angleAxis(delta_roll, ang.front);
				aircraft.orientation = glm::normalize(q_roll * q_pitch * q_yaw * aircraft.orientation);
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
	}

}
