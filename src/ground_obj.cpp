#include "world.h"

namespace sys {

	void ground_objs_init(World& world) {
		DEF_SYSTEM

		signal_listen(world.signals.scenery_loaded);

		world.ground_obj_templates = ground_obj_templates_from_dir(ASSETS_DIR "/ground");
	}

	void ground_objs_free(World& world) {
		DEF_SYSTEM

		for (auto& gro : world.ground_objs) {
			ground_obj_unload(gro);
		}
	}

	void _ground_objs_reload(World& world) {
		DEF_SYSTEM

		for (auto& gobj : world.ground_objs) {
			if (gobj.should_be_loaded) {
				ground_obj_unload(gobj);
				ground_obj_load(gobj);
				mu::log_debug("loaded '{}'", gobj.ground_obj_template.main);
			}
		}
	}

	void _ground_objs_autoremove(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			if (world.ground_objs[i].should_be_removed) {
				world.ground_objs.erase(world.ground_objs.begin()+i);
				i--;
			}
		}
	}

	void _ground_objs_apply_physics(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.visible) {
				continue;
			}

			// apply model transformation
			const auto model_transformation = local_euler_angles_matrix(gro.angles, gro.translation);

			gro.translation += ((float)world.loop_timer.delta_time * gro.speed) * gro.angles.front;

			// transform AABB (estimate new AABB after rotation)
			{
				// translate AABB
				gro.current_aabb.min = gro.current_aabb.max = gro.translation;

				// new rotated AABB (no translation)
				const auto model_rotation = glm::mat3(model_transformation);
				const auto rotated_min = model_rotation * gro.initial_aabb.min;
				const auto rotated_max = model_rotation * gro.initial_aabb.max;
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
							gro.current_aabb.min[i] += e;
							gro.current_aabb.max[i] += f;
						} else {
							gro.current_aabb.min[i] += f;
							gro.current_aabb.max[i] += e;
						}
					}
				}
			}

			for (auto& mesh : gro.model.meshes) {
				mesh.transformation = model_transformation;
			}

			meshes_foreach(gro.model.meshes, [&](Mesh& mesh) {
				if (mesh.visible == false) {
					return false;
				}

				// apply mesh transformation
				mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
				mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, -1, 0});

				for (auto& child : mesh.children) {
					child.transformation = mesh.transformation;
				}

				return true;
			});
		}
	}

	void ground_objs_update(World& world) {
		DEF_SYSTEM

		if (signal_handle(world.signals.scenery_loaded)) {
			for (auto& gob : world.ground_objs) {
				gob.should_be_removed = true;
			}

			mu::Vec<GroundObjSpawn*> gob_spawns(mu::memory::tmp());
			auto fields = field_list_recursively(world.scenery.root_fld);
			for (auto f : fields) {
				for (auto& gob : f->gobs) {
					gob_spawns.push_back(&gob);
				}
			}

			for (auto gob_s : gob_spawns) {
				auto tmpl_it = world.ground_obj_templates.find(gob_s->name);
				if (tmpl_it != world.ground_obj_templates.end()) {
					const auto& [_, gob_tmpl] = *tmpl_it;
					world.ground_objs.push_back(ground_obj_new(gob_tmpl, gob_s->pos, gob_s->rotation));
				} else {
					mu::log_error("tryed to load {} but didn't find it in ground_obj_templates, ignore it", gob_s->name);
				}
			}
		}

		_ground_objs_reload(world);
		_ground_objs_autoremove(world);

		_ground_objs_apply_physics(world);
	}

	void ground_objs_prepare_render(World& world) {
		DEF_SYSTEM

		for (int i = 0; i < world.ground_objs.size(); i++) {
			GroundObj& gro = world.ground_objs[i];

			if (!gro.visible) {
				continue;
			}


			meshes_foreach(gro.model.meshes, [&](const Mesh& mesh) {
				if (!mesh.visible) {
					return false;
				}

				if (mesh.render_cnt_axis) {
					canvas_add(world.canvas, canvas::Axis { glm::translate(glm::identity<glm::mat4>(), mesh.cnt) });
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

				return true;
			});
		}
	}

} // namespace sys
