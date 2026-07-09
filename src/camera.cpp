#include "world.h"

namespace sys {

	void projection_init(World& world) {
		DEF_SYSTEM

		signal_listen(world.signals.wnd_configs_changed);
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

		// calc camera distance based on how large model is
		auto dy = self.aircraft->initial_aabb.max.y - self.aircraft->initial_aabb.min.y;
		auto dist_from_model = self.zoom_multiplier * dy;

		auto model_transformation = local_euler_angles_matrix(aircraft_angles(*self.aircraft), self.aircraft->translation);
		model_transformation = glm::rotate(model_transformation, self.pitch, glm::vec3{0, -1, 0});
		model_transformation = glm::rotate(model_transformation, self.yaw, glm::vec3{-1, 0, 0});
		self.position = model_transformation * glm::vec4{0, 0, -dist_from_model, 1};

		self.target_pos = self.aircraft->translation;
		self.up = aircraft_angles(*self.aircraft).up;
	}

	void _camera_update_excamera_mode(World& world) {
		DEF_SYSTEM

		auto& self = world.camera;
		auto& ac = *self.aircraft;
		auto& excamera = ac.excameras[ac.excamera_index];

		auto ang = aircraft_angles(ac);
		self.position = ac.translation + ac.orientation * excamera.pos;
		self.front = ang.front;
		self.target_pos = self.position + self.front;
		self.up = ang.up;
	}

	void _camera_update_cockpit_mode(World& world) {
		DEF_SYSTEM

		auto& ac = *world.camera.aircraft;
		auto ang = aircraft_angles(ac);

		world.camera.position = ac.translation + ac.orientation * ac.cockpit_pos;
		world.camera.front = ang.front;
		world.camera.target_pos = world.camera.position + world.camera.front;
		world.camera.up = ang.up;
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

		if (world.events.camera_cycle && world.camera.aircraft) {
			auto& ac = *world.camera.aircraft;
			if (ac.excamera_index >= 0) {
				ac.excamera_index++;
				if (ac.excamera_index >= (int)ac.excameras.size())
					ac.excamera_index = -1;
			} else if (!ac.excameras.empty()) {
				ac.excamera_index = 0;
			}
		}

		if (world.events.cockpit_toggle && world.camera.aircraft) {
			auto& ac = *world.camera.aircraft;
			ac.cockpit_mode = !ac.cockpit_mode;
			if (ac.cockpit_mode)
				ac.excamera_index = -1;
		}

		if (world.camera.aircraft) {
			auto& ac = *world.camera.aircraft;
			if (ac.cockpit_mode) {
				_camera_update_cockpit_mode(world);
			} else if (ac.excamera_index >= 0) {
				_camera_update_excamera_mode(world);
			} else {
				_camera_update_model_tracking_mode(world);
			}
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

}
