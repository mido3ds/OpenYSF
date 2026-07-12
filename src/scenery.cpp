#include "world.h"

namespace sys {

	void models_handle_collision(World& world) {
		DEF_SYSTEM

		if (!world.settings.handle_collision) {
			return;
		}

		// adhoc entity query
		struct Entity {
			AABB* aabb;
			const char* name;
			bool render_aabb, visible, is_aircraft, collided;
		};
		mu::Vec<Entity> e(mu::memory::tmp());
		for (auto& a : world.aircrafts) {
			e.push_back(Entity {
				.aabb = &a.current_aabb,
				.name = a.aircraft_template.short_name.c_str(),
				.render_aabb = a.render_aabb,
				.visible = a.visible,
				.is_aircraft = true,
				.collided = false,
			});
		}
		for (auto& g : world.ground_objs) {
			e.push_back(Entity {
				.aabb = &g.current_aabb,
				.name = g.ground_obj_template.short_name.c_str(),
				.render_aabb = g.render_aabb,
				.visible = g.visible,
				.is_aircraft = false,
				.collided = false,
			});
		}

		// test collision
		for (uint32_t i = 0; e.size() > 1 && i < e.size()-1 && e[i].is_aircraft; i++) {
			if (e[i].visible == false) {
				continue;
			}

			for (uint32_t j = i+1; j < e.size(); j++) {
				if (e[j].visible && aabbs_intersect(*e[i].aabb, *e[j].aabb)) {
					e[i].collided = true;
					e[j].collided = true;
					TEXT_OVERLAY("{}[air] collided with {}[{}]", e[i].name, e[j].name, e[j].is_aircraft ? "air":"gro");
				}
			}
		}

		// render boxes as lines (12 edges per aabb)
		constexpr glm::vec3 RED {1,0,0};
		constexpr glm::vec3 BLU {0,0,1};
		for (int i = 0; i < e.size(); i++) {
			if (e[i].visible && e[i].render_aabb) {
				canvas_add(world.canvas, canvas::Box {
					.translation = e[i].aabb->min,
					.scale = e[i].aabb->max - e[i].aabb->min,
					.color = e[i].collided ? RED : BLU,
				});
			}
		}
	}

	void scenery_init(World& world) {
		DEF_SYSTEM

		world.scenery_templates = scenery_templates_from_dir(ASSETS_DIR "/scenery");
		world.scenery = scenery_new(world.scenery_templates["SMALL_MAP"]);
	}

	void scenery_free(World& world) {
		DEF_SYSTEM

		field_unload_from_gpu(world.scenery.root_fld);
	}

	void scenery_update(World& world) {
		DEF_SYSTEM

		auto& self = world.scenery;
		const auto all_fields = field_list_recursively(self.root_fld, mu::memory::tmp());

		if (self.should_be_loaded) {
			scenery_unload(self);
			scenery_load(self);
			signal_fire(world.signals.scenery_loaded);
		}

		if (self.root_fld.should_be_transformed) {
			self.root_fld.should_be_transformed = false;

			// transform fields
			self.root_fld.transformation = glm::identity<glm::mat4>();
			for (Field* fld : all_fields) {
				if (fld->visible == false) {
					continue;
				}

				fld->transformation = glm::translate(fld->transformation, fld->translation);
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[2], glm::vec3{0, 0, 1});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[1], glm::vec3{1, 0, 0});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[0], glm::vec3{0, 1, 0});

				for (auto& subfield : fld->subfields) {
					subfield.transformation = fld->transformation;
				}

				meshes_foreach(fld->meshes, [&](Mesh& mesh) {
					if (mesh.render_cnt_axis) {
						canvas_add(world.canvas, canvas::Axis { glm::translate(glm::identity<glm::mat4>(), mesh.cnt) });
					}

					// apply mesh transformation
					mesh.transformation = fld->transformation;
					mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, 1, 0});

					if (mesh.render_pos_axis) {
						canvas_add(world.canvas, canvas::Axis { mesh.transformation });
					}

					return true;
				});
			}

			// collect tower viewpoints from VIEW_POINT regions
			world.camera.tower_viewpoints.clear();
			for (const Field* fld : all_fields) {
				if (fld->visible == false) {
					continue;
				}
				for (const auto& region : fld->regions) {
					if (region.id == FieldID::TOWER) {
						auto pos = glm::vec3(fld->transformation * region.transformation * glm::vec4{0, 0, 0, 1});
						pos.y = -pos.y; // flip Y: file coords have +Y up, renderer has -Y up
						world.camera.tower_viewpoints.push_back(pos);
					}
				}
			}
		}
	}

	void scenery_prepare_render(World& world) {
		DEF_SYSTEM

		const auto all_fields = field_list_recursively(world.scenery.root_fld, mu::memory::tmp());

		for (const Field* fld : all_fields) {
			if (fld->visible == false) {
				continue;
			}

			// ground
			canvas_add(world.canvas, canvas::Ground {
				.color = fld->ground_color,
			});

			// pictures
			for (const auto& picture : fld->pictures) {
				if (picture.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, picture.translation);
				model_transformation = glm::rotate(model_transformation, picture.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.rotation[0], glm::vec3{0, 1, 0});

				auto gnd_pic = canvas::GndPic {
					.projection_view_model = world.mats.projection_view * model_transformation,
					.list_primitives = mu::Vec<canvas::GndPic::Primitive>(&world.canvas.arena),
				};

				for (const auto& primitive : picture.primitives) {
					GLenum gl_primitive_type;
					switch (primitive.kind) {
					case Primitive2D::Kind::POINTS:
						gl_primitive_type = GL_POINTS;
						break;
					case Primitive2D::Kind::LINES:
						gl_primitive_type = GL_LINES;
						break;
					case Primitive2D::Kind::LINE_SEGMENTS:
						gl_primitive_type = GL_LINE_STRIP;
						break;
					case Primitive2D::Kind::TRIANGLES:
					case Primitive2D::Kind::QUAD_STRIPS:
					case Primitive2D::Kind::QUADRILATERAL:
					case Primitive2D::Kind::POLYGON:
					case Primitive2D::Kind::GRADATION_QUAD_STRIPS:
						gl_primitive_type = GL_TRIANGLES;
						break;
					default: mu_unreachable();
					}

					canvas::GndPic::Primitive cp {
						.vao = primitive.gl_buf.vao,
						.buf_len = primitive.gl_buf.len,
						.gl_primitive_type = gl_primitive_type,

						.color = primitive.color,
						.gradient_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS,
						.gradient_color2 = primitive.gradient_color2,
					};

					if (primitive.tex_name.size() > 0 && fld->textures.contains(primitive.tex_name)) {
						cp.tex_enabled = true;
						cp.texture_id = fld->textures.at(primitive.tex_name);
					}

					gnd_pic.list_primitives.push_back(std::move(cp));
				}

				canvas_add(world.canvas, std::move(gnd_pic));
			}

			// terrains
			for (const auto& terr_mesh : fld->terr_meshes) {
				if (terr_mesh.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[0], glm::vec3{0, 1, 0});

				if (terr_mesh.gradient.enabled) {
					canvas_add(world.canvas, canvas::GradientMesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,
						.model_normal = glm::transpose(glm::inverse(glm::mat3(model_transformation))),

						.gradient_bottom_y = terr_mesh.gradient.bottom_y,
						.gradient_top_y = terr_mesh.gradient.top_y,
						.gradient_bottom_color = terr_mesh.gradient.bottom_color,
						.gradient_top_color = terr_mesh.gradient.top_color,
					});
				} else {
					auto mesh = canvas::Mesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,
						.model_normal = glm::transpose(glm::inverse(glm::mat3(model_transformation)))
					};

					if (!terr_mesh.tex_name.empty()) {
						auto it = fld->textures.find(terr_mesh.tex_name);
						if (it != fld->textures.end()) {
							mesh.texture_id = it->second;
							mesh.tex_enabled = true;
						}
					}

					canvas_add(world.canvas, std::move(mesh));
				}
			}

			// meshes
			meshes_foreach(fld->meshes, [&](const Mesh& mesh) {
				if (mesh.visible == false) {
					return false;
				}

				canvas_add(world.canvas, canvas::Mesh {
					.vao = mesh.gl_buf.vao,
					.buf_len = mesh.gl_buf.len,
					.projection_view_model =
						world.mats.projection_view
						* mesh.transformation
						* fld->transformation,
					.model_normal = glm::transpose(glm::inverse(
						glm::mat3(mesh.transformation * fld->transformation)))
				});

				return true;
			});
		}
	}

} // namespace sys
