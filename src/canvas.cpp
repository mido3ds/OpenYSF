#undef near
#undef far

#include <SDL.h>
#include <SDL_image.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "world.h"

namespace sys {

	void canvas_init(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		signal_listen(world.signals.wnd_configs_changed);

		self.meshes.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec3 attr_position;
				layout (location = 1) in vec4 attr_color;
				layout (location = 2) in vec3 attr_normal;
				layout (location = 3) in vec2 attr_uv;

				uniform mat4 projection_view_model;
				uniform mat3 model_normal;

				out float vs_vertex_y;
				out vec4 vs_color;
				out vec3 vs_normal;
				out float vs_depth;
				out vec2 vs_uv;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position, 1.0);
					vs_color = attr_color;
					vs_vertex_y = attr_position.y;
					vs_normal = normalize(model_normal * attr_normal);
					vs_depth = gl_Position.w;
					vs_uv = attr_uv;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in float vs_vertex_y;
				in vec4 vs_color;
				in vec3 vs_normal;
				in float vs_depth;
				in vec2 vs_uv;

				out vec4 out_fragcolor;

				uniform bool gradient_enabled;
				uniform float gradient_bottom_y, gradient_top_y;
				uniform vec3 gradient_bottom_color, gradient_top_color;

				uniform vec3 light_dir;
				uniform vec3 ambient_color;
				uniform bool lighting_enabled;

				uniform bool fog_enabled;
				uniform float fog_density;
				uniform vec3 fog_color;

				uniform bool tex_enabled;
				uniform sampler2D terrain_tex;

				void main() {
					if (vs_color.a == 0) {
						discard;
					}

					vec4 base_color;
					if (gradient_enabled) {
						float alpha = (vs_vertex_y - gradient_bottom_y) / (gradient_top_y - gradient_bottom_y);
						base_color = vec4(mix(gradient_bottom_color, gradient_top_color, alpha), 1.0f);
					} else {
						base_color = vs_color;
					}

					if (tex_enabled) {
						base_color *= texture(terrain_tex, vs_uv);
					}

				// dot(light_dir) > 0 avoids normalize(0) undefined when user drags all axes to zero via UI
				if (lighting_enabled && dot(light_dir, light_dir) > 0.0) {
					float diff = max(dot(vs_normal, normalize(light_dir)), 0.0);
					out_fragcolor = base_color * vec4(ambient_color + diff, 1.0);
				} else {
					out_fragcolor = base_color;
				}

				if (fog_enabled) {
					float d = vs_depth * fog_density;
					float fog_factor = exp(-d * d);
					out_fragcolor = mix(vec4(fog_color, 1.0), out_fragcolor, fog_factor);
				}
				}
			)GLSL"
		);

		{
			struct Stride {
				glm::vec3 vertex;
				glm::vec4 color;
			};
			self.axes.gl_buf = gl_buf_new<glm::vec3, glm::vec4>(mu::Vec<Stride> {
				Stride {{0, 0, 0}, {1, 0, 0, 1}}, // X
				Stride {{1, 0, 0}, {1, 0, 0, 1}},
				Stride {{0, 0, 0}, {0, 1, 0, 1}}, // Y
				Stride {{0, 1, 0}, {0, 1, 0, 1}},
				Stride {{0, 0, 0}, {0, 0, 1, 1}}, // Z
				Stride {{0, 0, 1}, {0, 0, 1, 1}},
			});
		}

		self.gnd_pics.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;
				layout (location = 1) in vec2 attr_uv;

				uniform mat4 projection_view_model;

				out float vs_vertex_id;
				out float vs_depth;
				out vec2 vs_uv;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position.x, 0.0, attr_position.y, 1.0);
					vs_vertex_id = gl_VertexID % 6;
					vs_depth = gl_Position.w;
					vs_uv = attr_uv;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core

				in float vs_vertex_id;
				in float vs_depth;
				in vec2 vs_uv;

				uniform vec3 primitive_color[2];
				uniform bool gradient_enabled;
				uniform sampler2D groundtile;

				uniform bool tex_enabled;
				uniform sampler2D terrain_tex;

				uniform bool fog_enabled;
				uniform float fog_density;
				uniform vec3 fog_color;

				out vec4 out_fragcolor;

				const int color_indices[6] = int[] (
					0, 1, 1,
					0, 0, 1
				);

				const vec2 default_tex_coords[3] = vec2[] (
					vec2(0, 0), vec2(1, 0), vec2(1, 1)
				);

				void main() {
					int color_index = 0;
					if (gradient_enabled) {
						color_index = color_indices[int(vs_vertex_id)];
					}
					vec2 tc = tex_enabled ? vs_uv : default_tex_coords[int(vs_vertex_id) % 3];
					vec4 base = texture(groundtile, tc).r * vec4(primitive_color[color_index], 1.0);
					if (tex_enabled) {
						base *= texture(terrain_tex, vs_uv);
					}
					out_fragcolor = base;
					if (fog_enabled) {
						float d = vs_depth * fog_density;
						float fog_factor = exp(-d * d);
						out_fragcolor = mix(vec4(fog_color, 1.0), out_fragcolor, fog_factor);
					}
				}
			)GLSL"
		);

		// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/
		self.ground.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;

				uniform mat4 projection_inverse;
				uniform mat4 view_inverse;

				out vec3 vs_near_point;
				out vec3 vs_far_point;

				vec3 unproject_point(float x, float y, float z) {
					vec4 p = view_inverse * projection_inverse * vec4(x, y, z, 1.0);
					return p.xyz / p.w;
				}

				void main() {
					vs_near_point = unproject_point(attr_position.x, attr_position.y, 0.0); // unprojecting on the near plane
					vs_far_point  = unproject_point(attr_position.x, attr_position.y, 1.0); // unprojecting on the far plane
					gl_Position   = vec4(attr_position.x, attr_position.y, 0.0, 1.0);       // using directly the clipped coordinates
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in vec3 vs_near_point;
				in vec3 vs_far_point;

				out vec4 out_fragcolor;

				uniform vec3 color;
				uniform sampler2D groundtile;

				uniform bool fog_enabled;
				uniform float fog_density;
				uniform vec3 fog_color;
				uniform vec3 camera_pos;

				void main() {
					float t = -vs_near_point.y / (vs_far_point.y - vs_near_point.y);
					if (t <= 0) {
						discard;
					} else {
						vec3 frag_pos_3d = vs_near_point + t * (vs_far_point - vs_near_point);
						out_fragcolor = vec4(texture(groundtile, frag_pos_3d.xz / 600).x * color, 1.0);
						if (fog_enabled) {
							float d = distance(frag_pos_3d, camera_pos) * fog_density;
							float fog_factor = exp(-d * d);
							out_fragcolor = mix(vec4(fog_color, 1.0), out_fragcolor, fog_factor);
						}
					}
				}
			)GLSL"
		);

		// grid position are in clipped space
		self.ground.gl_buf = gl_buf_new<glm::vec2>(mu::Vec<glm::vec2> {
			glm::vec2{1, 1}, glm::vec2{-1, 1}, glm::vec2{-1, -1},
			glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1}
		});

		// groundtile
		self.ground.tile_surface = IMG_Load(ASSETS_DIR "/misc/groundtile.png");
		if (self.ground.tile_surface == nullptr || self.ground.tile_surface->pixels == nullptr) {
			mu::panic("failed to load groundtile.png");
		}
		glGenTextures(1, &self.ground.tile_texture);
		glBindTexture(GL_TEXTURE_2D, self.ground.tile_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.ground.tile_surface->w, self.ground.tile_surface->h, 0, GL_RED, GL_UNSIGNED_BYTE, self.ground.tile_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		self.zlpoints.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;
				layout (location = 1) in vec2 attr_tex_coord;

				uniform mat4 projection_view_model;

				out vec2 vs_tex_coord;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position, 0, 1);
					vs_tex_coord = attr_tex_coord;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in vec2 vs_tex_coord;

				out vec4 out_fragcolor;

				uniform sampler2D quad_texture;
				uniform vec3 color;

				void main() {
					out_fragcolor = texture(quad_texture, vs_tex_coord).r * vec4(color, 1);
				}
			)GLSL"
		);

		{
			struct Stride {
				glm::vec2 vertex;
				glm::vec2 tex_coord;
			};
			self.zlpoints.gl_buf = gl_buf_new<glm::vec2, glm::vec2>(mu::Vec<Stride> {
				Stride { .vertex = glm::vec2{+1, +1}, .tex_coord = glm::vec2{+1, +1} },
				Stride { .vertex = glm::vec2{-1, +1}, .tex_coord = glm::vec2{.0, +1} },
				Stride { .vertex = glm::vec2{-1, -1}, .tex_coord = glm::vec2{.0, .0} },

				Stride { .vertex = glm::vec2{-1, -1}, .tex_coord = glm::vec2{.0, .0} },
				Stride { .vertex = glm::vec2{+1, -1}, .tex_coord = glm::vec2{+1, .0} },
				Stride { .vertex = glm::vec2{+1, +1}, .tex_coord = glm::vec2{+1, +1} },
			});
		}

		// zl_sprite
		self.zlpoints.sprite_surface = IMG_Load(ASSETS_DIR "/misc/rwlight.png");
		if (self.zlpoints.sprite_surface == nullptr || self.zlpoints.sprite_surface->pixels == nullptr) {
			mu::panic("failed to load rwlight.png");
		}
		glGenTextures(1, &self.zlpoints.sprite_texture);
		glBindTexture(GL_TEXTURE_2D, self.zlpoints.sprite_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, self.zlpoints.sprite_surface->w, self.zlpoints.sprite_surface->h, 0, GL_RED, GL_UNSIGNED_INT, self.zlpoints.sprite_surface->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		// text
		self.text.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec3 attr_position;
				layout (location = 1) in vec2 attr_tex_coord;

				uniform mat4 projection_view;

				out vec2 vs_tex_coord;

				void main() {
					gl_Position = projection_view * vec4(attr_position, 1.0);
					vs_tex_coord = attr_tex_coord;
				}
			)GLSL",
			// fragment shader
			R"GLSL(
				#version 330 core
				in vec2 vs_tex_coord;
				out vec4 color;

				uniform sampler2D text_texture;
				uniform vec4 text_color;

				void main() {
					vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text_texture, vs_tex_coord).r);
					color = text_color * sampled;
				}
			)GLSL"
		);

		self.text.gl_buf = gl_buf_new_dyn<glm::vec3, glm::vec2>(6);

		// disable byte-alignment restriction
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		// freetype
		FT_Library ft;
		if (FT_Init_FreeType(&ft)) {
			mu::panic("could not init FreeType Library");
		}
		mu_defer(FT_Done_FreeType(ft));

		FT_Face face;
		if (FT_New_Face(ft, ASSETS_DIR "/fonts/zig.ttf", 0, &face)) {
			mu::panic("failed to load font");
		}
		mu_defer(FT_Done_Face(face));

		uint16_t face_height = 48;
		uint16_t face_width = 0; // auto
		if (FT_Set_Pixel_Sizes(face, face_width, face_height)) {
			mu::panic("failed to set pixel size of font face");
		}

		if (FT_Load_Char(face, 'X', FT_LOAD_RENDER)) {
			mu::panic("failed to load glyph");
		}

		// generate textures
		for (uint8_t c = 0; c < self.text.glyphs.size(); c++) {
			if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
				mu::panic("failed to load glyph");
			}

			GLuint text_texture;
			glGenTextures(1, &text_texture);
			glBindTexture(GL_TEXTURE_2D, text_texture);
			glTexImage2D(
				GL_TEXTURE_2D,
				0,
				GL_RED,
				face->glyph->bitmap.width,
				face->glyph->bitmap.rows,
				0,
				GL_RED,
				GL_UNSIGNED_BYTE,
				face->glyph->bitmap.buffer
			);

			// texture options
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			self.text.glyphs[c] = canvas::Glyph {
				.texture = text_texture,
				.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
				.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
				.advance = uint32_t(face->glyph->advance.x)
			};
		}

		// lines
		self.lines.program = gl_program_new(
			// vertex shader
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec4 attr_position;
				layout (location = 1) in vec4 attr_color;

				out vec4 vs_color;

				void main() {
					gl_Position = attr_position;
					vs_color = attr_color;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in vec4 vs_color;

				out vec4 out_fragcolor;

				void main() {
					out_fragcolor = vs_color;
				}
			)GLSL"
		);

		self.lines.gl_buf = gl_buf_new_dyn<glm::vec4, glm::vec4>(100);

		// hud geoms
		self.hud_geoms.program = gl_program_new(
			R"GLSL(
				#version 330 core
				layout (location = 0) in vec2 attr_position;
				layout (location = 1) in vec4 attr_color;

				uniform mat4 projection_view;

				out vec4 vs_color;

				void main() {
					gl_Position = projection_view * vec4(attr_position, 0.0, 1.0);
					vs_color = attr_color;
				}
			)GLSL",
			R"GLSL(
				#version 330 core
				in vec4 vs_color;
				out vec4 out_fragcolor;

				void main() {
					out_fragcolor = vs_color;
				}
			)GLSL"
		);
		self.hud_geoms.gl_buf = gl_buf_new_dyn<glm::vec2, glm::vec4>(512);

		gl_process_errors();
	}

	void canvas_free(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		glUseProgram(0);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);

		// text
		gl_program_free(self.text.program);
		gl_buf_free(self.text.gl_buf);
		for (auto& g : self.text.glyphs) {
			glDeleteTextures(1, &g.texture);
		}

		// zlpoints
		SDL_FreeSurface(self.zlpoints.sprite_surface);
		glDeleteTextures(1, &self.zlpoints.sprite_texture);
		gl_program_free(self.zlpoints.program);
		gl_buf_free(self.zlpoints.gl_buf);

		// ground
		SDL_FreeSurface(self.ground.tile_surface);
		glDeleteTextures(1, &self.ground.tile_texture);
		gl_program_free(self.ground.program);
		gl_buf_free(self.ground.gl_buf);

		// axes
		gl_buf_free(self.axes.gl_buf);

		// lines
		gl_program_free(self.lines.program);
		gl_buf_free(self.lines.gl_buf);

		// hud geoms
		gl_program_free(self.hud_geoms.program);
		gl_buf_free(self.hud_geoms.gl_buf);

		gl_program_free(self.meshes.program);
		gl_program_free(self.gnd_pics.program);
	}

	void canvas_rendering_begin(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		if (signal_handle(world.signals.wnd_configs_changed)) {
			int w, h;
			SDL_GL_GetDrawableSize(world.sdl_window, &w, &h);
			glViewport(0, 0, w, h);
		}

		glEnable(GL_DEPTH_TEST);
		glClearDepth(1);
		glm::vec3 clear_color = world.scenery.root_fld.sky_color;
		if (world.settings.rendering.fog_enabled) {
			clear_color = world.settings.rendering.fog_color;
		}
		glClearColor(clear_color.x, clear_color.y, clear_color.z, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (world.settings.rendering.smooth_lines) {
			glEnable(GL_LINE_SMOOTH);
			#ifndef OS_MACOS
			glLineWidth(world.settings.rendering.line_width);
			#endif
		} else {
			glDisable(GL_LINE_SMOOTH);
		}
		glPointSize(world.settings.rendering.point_size);
		glPolygonMode(GL_FRONT_AND_BACK, world.settings.rendering.polygon_mode);
	}

	void canvas_rendering_end(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		SDL_GL_SwapWindow(world.sdl_window);
		gl_process_errors();

		self.arena = {};
		self.text.list_world       = mu::Vec<canvas::Text>(&self.arena);
		self.text.list_hud         = mu::Vec<canvas::hud::Text>(&self.arena);
		self.axes.list             = mu::Vec<canvas::Axis>(&self.arena);
		self.zlpoints.list         = mu::Vec<canvas::ZLPoint>(&self.arena);
		self.lines.list            = mu::Vec<canvas::Line>(&self.arena);
		self.meshes.list_regular   = mu::Vec<canvas::Mesh>(&self.arena);
		self.meshes.list_gradient  = mu::Vec<canvas::GradientMesh>(&self.arena);
		self.meshes.list_cockpit   = mu::Vec<canvas::Cockpit>(&self.arena);
		self.gnd_pics.list         = mu::Vec<canvas::GndPic>(&self.arena);
		self.hud_geoms.list_circles          = mu::Vec<canvas::hud::Circle>(&self.arena);
		self.hud_geoms.list_lines            = mu::Vec<canvas::hud::Line>(&self.arena);
		self.hud_geoms.list_line_strips      = mu::Vec<canvas::hud::LineStrip>(&self.arena);
		self.hud_geoms.list_filled_arcs      = mu::Vec<canvas::hud::FilledArc>(&self.arena);
		self.hud_geoms.list_filled_triangles = mu::Vec<canvas::hud::FilledTriangle>(&self.arena);
	}

	void canvas_render_zlpoints(World& world) {
		DEF_SYSTEM

		if (world.canvas.zlpoints.list.empty()) {
			return;
		}

		auto model_transformation = glm::mat4(glm::mat3(world.mats.view_inverse)) * glm::scale(glm::vec3{ZL_SCALE, ZL_SCALE, 0});

		gl_program_use(world.canvas.zlpoints.program);
		glBindTexture(GL_TEXTURE_2D, world.canvas.zlpoints.sprite_texture);
		glBindVertexArray(world.canvas.zlpoints.gl_buf.vao);

		for (const auto& zlpoint : world.canvas.zlpoints.list) {
			model_transformation[3] = glm::vec4{zlpoint.center.x, zlpoint.center.y, zlpoint.center.z, 1.0f};
			gl_program_uniform_set(world.canvas.zlpoints.program, "color", zlpoint.color);
			gl_program_uniform_set(world.canvas.zlpoints.program, "projection_view_model", world.mats.projection_view * model_transformation);
			glDrawArrays(GL_TRIANGLES, 0, world.canvas.zlpoints.gl_buf.len);
		}
	}

	void canvas_render_meshes(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.meshes.program);

		// normalize CPU-side once per frame instead of per-fragment
		auto light_dir = world.settings.rendering.light_dir;
		float len = glm::length(light_dir);
		gl_program_uniform_set(world.canvas.meshes.program, "light_dir",
			len > 0.0001f ? light_dir / len : light_dir);
		gl_program_uniform_set(world.canvas.meshes.program, "ambient_color",
			world.settings.rendering.ambient_color);
		gl_program_uniform_set(world.canvas.meshes.program, "lighting_enabled",
			world.settings.rendering.lighting);

		// fog
		gl_program_uniform_set(world.canvas.meshes.program, "fog_enabled",
			world.settings.rendering.fog_enabled);
		gl_program_uniform_set(world.canvas.meshes.program, "fog_density",
			world.settings.rendering.fog_density);
		gl_program_uniform_set(world.canvas.meshes.program, "fog_color",
			world.settings.rendering.fog_color);

		// texture sampler on unit 0
		gl_program_uniform_set(world.canvas.meshes.program, "terrain_tex", 0);
		gl_program_uniform_set(world.canvas.meshes.program, "tex_enabled", false);

		// regular
		for (const auto& mesh : world.canvas.meshes.list_regular) {
			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", mesh.projection_view_model);
			gl_program_uniform_set(world.canvas.meshes.program, "model_normal", mesh.model_normal);

			gl_program_uniform_set(world.canvas.meshes.program, "tex_enabled", mesh.tex_enabled);
			if (mesh.tex_enabled) {
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
			}

			glBindVertexArray(mesh.vao);
			glDrawArrays(world.settings.rendering.primitives_type, 0, mesh.buf_len);
		}

		// gradient
		if (world.canvas.meshes.list_gradient.size() > 0) {
			gl_program_uniform_set(world.canvas.meshes.program, "gradient_enabled", true);
			for (const auto& mesh : world.canvas.meshes.list_gradient) {
				gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", mesh.projection_view_model);
				gl_program_uniform_set(world.canvas.meshes.program, "model_normal", mesh.model_normal);

				gl_program_uniform_set(world.canvas.meshes.program, "gradient_bottom_y", mesh.gradient_bottom_y);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_top_y", mesh.gradient_top_y);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_bottom_color", mesh.gradient_bottom_color);
				gl_program_uniform_set(world.canvas.meshes.program, "gradient_top_color", mesh.gradient_top_color);

				glBindVertexArray(mesh.vao);
				glDrawArrays(world.settings.rendering.primitives_type, 0, mesh.buf_len);
			}
			gl_program_uniform_set(world.canvas.meshes.program, "gradient_enabled", false);
		}

		glDisable(GL_CULL_FACE);
		for (const auto& cockpit : world.canvas.meshes.list_cockpit) {
			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", cockpit.projection_view_model);
			gl_program_uniform_set(world.canvas.meshes.program, "model_normal", cockpit.model_normal);
			glBindVertexArray(cockpit.vao);
			glDrawArrays(world.settings.rendering.primitives_type, 0, cockpit.buf_len);
		}
		glEnable(GL_CULL_FACE);
	}

	void canvas_render_axes(World& world) {
		DEF_SYSTEM

		if (world.canvas.axes.list.empty() == false) {
			gl_program_use(world.canvas.meshes.program);
			gl_program_uniform_set(world.canvas.meshes.program, "lighting_enabled", false);
			gl_program_uniform_set(world.canvas.meshes.program, "fog_enabled", false);
			glEnable(GL_LINE_SMOOTH);
			#ifndef OS_MACOS
			glLineWidth(world.canvas.axes.line_width);
			#endif
			glBindVertexArray(world.canvas.axes.gl_buf.vao);

			if (world.canvas.axes.on_top) {
				glDisable(GL_DEPTH_TEST);
			}

			for (const auto& axis : world.canvas.axes.list) {
				gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", world.mats.projection_view * axis.transformation);
				glDrawArrays(GL_LINES, 0, world.canvas.axes.gl_buf.len);
			}

			glEnable(GL_DEPTH_TEST);
		}

		if (world.settings.world_axis.enabled) {
			gl_program_use(world.canvas.meshes.program);
			gl_program_uniform_set(world.canvas.meshes.program, "lighting_enabled", false);
			gl_program_uniform_set(world.canvas.meshes.program, "fog_enabled", false);
			glEnable(GL_LINE_SMOOTH);
			#ifndef OS_MACOS
			glLineWidth(world.canvas.axes.line_width);
			#endif
			glBindVertexArray(world.canvas.axes.gl_buf.vao);

			float camera_z = 1 - world.settings.world_axis.scale; // invert scale because it's camera moving away
			camera_z *= -40; // arbitrary multiplier
			camera_z -= 1; // keep a fixed distance or axis will vanish
			auto new_view_mat = world.mats.view;
			new_view_mat[3] = glm::vec4{0, 0, camera_z, 1}; // scale is a camera zoom out in z

			auto translate = glm::translate(glm::identity<glm::mat4>(), glm::vec3{world.settings.world_axis.position.x, world.settings.world_axis.position.y, 0});

			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", translate * world.mats.projection * new_view_mat);
			glDrawArrays(GL_LINES, 0, world.canvas.axes.gl_buf.len);
		}
	}

	void canvas_render_text(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.text.program);
		glBindVertexArray(world.canvas.text.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, world.canvas.text.gl_buf.vbo);

		for (auto& txt_rndr : world.canvas.text.list_world) {
			gl_program_uniform_set(world.canvas.text.program, "text_color", txt_rndr.color);

			auto model_transformation = glm::mat4(glm::mat3(world.mats.view_inverse));
			model_transformation[3] = glm::vec4{txt_rndr.p.x, txt_rndr.p.y, txt_rndr.p.z, 1.0f};
			gl_program_uniform_set(world.canvas.text.program, "projection_view", world.mats.projection_view * model_transformation);
			txt_rndr.p = {};

			for (char c : txt_rndr.text) {
				if (c >= world.canvas.text.glyphs.size()) {
					c = '?';
				}
				const canvas::Glyph& glyph = world.canvas.text.glyphs[c];

				// update vertices
				struct Stride {
					glm::vec3 pos;
					glm::vec2 tex_coord;
				};
				float x = txt_rndr.p.x + glyph.bearing.x * txt_rndr.scale;
				float y = txt_rndr.p.y - (glyph.size.y - glyph.bearing.y) * txt_rndr.scale;
				float w = glyph.size.x * txt_rndr.scale;
				float h = glyph.size.y * txt_rndr.scale;
				mu::Vec<Stride> buffer {
					Stride {{x,   y+h, txt_rndr.p.z}, {0.0f, 0.0f}}, // 0
					Stride {{x,   y  , txt_rndr.p.z}, {0.0f, 1.0f}}, // 1
					Stride {{x+w, y  , txt_rndr.p.z}, {1.0f, 1.0f}}, // 2

					Stride {{x,   y+h, txt_rndr.p.z}, {0.0f, 0.0f}}, // 0
					Stride {{x+w, y  , txt_rndr.p.z}, {1.0f, 1.0f}}, // 2
					Stride {{x+w, y+h, txt_rndr.p.z}, {1.0f, 0.0f}}, // 3
				};
				mu_assert(buffer.size() == world.canvas.text.gl_buf.len);
				glBufferSubData(GL_ARRAY_BUFFER, 0, buffer.size() * sizeof(Stride), buffer.data());

				// render glyph texture over quad
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
				// bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
				txt_rndr.p.x += (glyph.advance >> 6) * txt_rndr.scale;
			}
		}
	}

	void canvas_render_hud_text(World& world) {
		DEF_SYSTEM

		gl_program_use(world.canvas.text.program);

		int wnd_width, wnd_height;
		SDL_GL_GetDrawableSize(world.sdl_window, &wnd_width, &wnd_height);
		gl_program_uniform_set(world.canvas.text.program, "projection_view", glm::ortho(0.0f, float(wnd_width), 0.0f, float(wnd_height)));

		glBindVertexArray(world.canvas.text.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, world.canvas.text.gl_buf.vbo);

		for (auto& txt_rndr : world.canvas.text.list_hud) {
			gl_program_uniform_set(world.canvas.text.program, "text_color", txt_rndr.color);

			txt_rndr.p.x *= wnd_width;
			txt_rndr.p.y *= wnd_height;

			for (char c : txt_rndr.text) {
				if (c >= world.canvas.text.glyphs.size()) {
					c = '?';
				}
				const canvas::Glyph& glyph = world.canvas.text.glyphs[c];

				// update vertices
				struct Stride {
					glm::vec3 pos;
					glm::vec2 tex_coord;
				};
				float x = txt_rndr.p.x + glyph.bearing.x * txt_rndr.scale;
				float y = txt_rndr.p.y - (glyph.size.y - glyph.bearing.y) * txt_rndr.scale;
				float w = glyph.size.x * txt_rndr.scale;
				float h = glyph.size.y * txt_rndr.scale;
				mu::Vec<Stride> buffer {
					Stride {{x,   y+h, 0}, {0.0f, 0.0f}}, // 0
					Stride {{x,   y  , 0}, {0.0f, 1.0f}}, // 1
					Stride {{x+w, y  , 0}, {1.0f, 1.0f}}, // 2

					Stride {{x,   y+h, 0}, {0.0f, 0.0f}}, // 0
					Stride {{x+w, y  , 0}, {1.0f, 1.0f}}, // 2
					Stride {{x+w, y+h, 0}, {1.0f, 0.0f}}, // 3
				};
				mu_assert(buffer.size() == world.canvas.text.gl_buf.len);
				glBufferSubData(GL_ARRAY_BUFFER, 0, buffer.size() * sizeof(Stride), buffer.data());

				// render glyph texture over quad
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				// now advance cursors for next glyph (note that advance is number of 1/64 pixels)
				// bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
				txt_rndr.p.x += (glyph.advance >> 6) * txt_rndr.scale;
			}
		}
	}

	void canvas_render_hud_geoms(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas.hud_geoms;

		if (self.list_circles.empty() && self.list_lines.empty() && self.list_line_strips.empty()
			&& self.list_filled_arcs.empty() && self.list_filled_triangles.empty()) {
			return;
		}

		struct Stride {
			glm::vec2 pos;
			glm::vec4 color;
		};

		int wnd_width, wnd_height;
		SDL_GL_GetDrawableSize(world.sdl_window, &wnd_width, &wnd_height);

		gl_program_use(self.program);
		gl_program_uniform_set(self.program, "projection_view",
			glm::ortho(0.0f, float(wnd_width), 0.0f, float(wnd_height)));

		glDisable(GL_CULL_FACE);
		glBindVertexArray(self.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, self.gl_buf.vbo);

		constexpr float PI = 3.14159265f;
		constexpr int SEGMENTS = 40;

		for (auto& c : self.list_circles) {
			mu::Vec<Stride> verts(mu::memory::tmp());
			float cx = c.center.x * wnd_width;
			float cy = c.center.y * wnd_height;
			float r = c.radius * wnd_width;
			for (int i = 0; i <= SEGMENTS; i++) {
				float a = (float(i) / SEGMENTS) * 2.0f * PI;
				verts.push_back(Stride{
					.pos = glm::vec2{cx + r * cosf(a), cy + r * sinf(a)},
					.color = c.color
				});
			}
			size_t cnt = verts.size();
			size_t buf_cap = self.gl_buf.len;
			for (size_t i = 0; i < cnt; i += buf_cap) {
				size_t n = std::min(buf_cap, cnt - i);
				glBufferSubData(GL_ARRAY_BUFFER, 0, n * sizeof(Stride), verts.data() + i);
				glDrawArrays(GL_LINE_STRIP, 0, n);
			}
		}

		for (auto& l : self.list_lines) {
			Stride verts[2] = {
				{.pos = {l.p0.x * wnd_width, l.p0.y * wnd_height}, .color = l.color},
				{.pos = {l.p1.x * wnd_width, l.p1.y * wnd_height}, .color = l.color},
			};
			glBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(Stride), verts);
			glDrawArrays(GL_LINES, 0, 2);
		}

		for (auto& l : self.list_line_strips) {
			mu::Vec<Stride> verts(mu::memory::tmp());
			for (auto& p : l.points) {
				verts.push_back(Stride{
					.pos = {p.x * wnd_width, p.y * wnd_height},
					.color = l.color
				});
			}
			if (verts.empty()) continue;
			size_t cnt = verts.size();
			size_t buf_cap = self.gl_buf.len;
			for (size_t i = 0; i < cnt; i += buf_cap) {
				size_t n = std::min(buf_cap, cnt - i);
				glBufferSubData(GL_ARRAY_BUFFER, 0, n * sizeof(Stride), verts.data() + i);
				glDrawArrays(GL_LINE_STRIP, 0, n);
			}
		}

		for (auto& a : self.list_filled_arcs) {
			mu::Vec<Stride> verts(mu::memory::tmp());
			float cx = a.center.x * wnd_width;
			float cy = a.center.y * wnd_height;
			float r = a.radius * wnd_width;
			verts.push_back(Stride{.pos = {cx, cy}, .color = a.color});
			int seg = std::max(2, int(SEGMENTS * (a.end_angle - a.start_angle) / (2.0f * PI)));
			for (int i = 0; i <= seg; i++) {
				float t = float(i) / seg;
				float ang = a.start_angle + t * (a.end_angle - a.start_angle);
				verts.push_back(Stride{
					.pos = {cx + r * cosf(ang), cy + r * sinf(ang)},
					.color = a.color
				});
			}
			size_t cnt = verts.size();
			size_t buf_cap = self.gl_buf.len;
			for (size_t i = 0; i < cnt; i += buf_cap) {
				size_t chunk = std::min(buf_cap, cnt - i);
				glBufferSubData(GL_ARRAY_BUFFER, 0, chunk * sizeof(Stride), verts.data() + i);
				glDrawArrays(GL_TRIANGLE_FAN, 0, chunk);
			}
		}

		for (auto& t : self.list_filled_triangles) {
			Stride verts[3] = {
				{.pos = {t.p0.x * wnd_width, t.p0.y * wnd_height}, .color = t.color},
				{.pos = {t.p1.x * wnd_width, t.p1.y * wnd_height}, .color = t.color},
				{.pos = {t.p2.x * wnd_width, t.p2.y * wnd_height}, .color = t.color},
			};
			glBufferSubData(GL_ARRAY_BUFFER, 0, 3 * sizeof(Stride), verts);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
		glEnable(GL_CULL_FACE);
	}

	void canvas_render_lines(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas.lines;

		if (self.list.empty()) {
			return;
		}

		struct Stride {
			glm::vec4 vertex;
			glm::vec4 color;
		};

		mu::Vec<Stride> strides(mu::memory::tmp());
		strides.reserve(self.list.size()*2);

		for (const auto& line : self.list) {
			strides.push_back(Stride {
				.vertex = world.mats.projection_view * glm::vec4(line.p0, 1.0f),
				.color = line.color
			});
			strides.push_back(Stride {
				.vertex = world.mats.projection_view * glm::vec4(line.p1, 1.0f),
				.color = line.color
			});
		}

		gl_program_use(self.program);
		glBindVertexArray(self.gl_buf.vao);
		glBindBuffer(GL_ARRAY_BUFFER, self.gl_buf.vbo);

		glEnable(GL_LINE_SMOOTH);
		#ifndef OS_MACOS
		glLineWidth(self.line_width);
		#endif

		for (int i = 0; i < strides.size(); i += self.gl_buf.len) {
			const size_t count = std::min(self.gl_buf.len, strides.size()-i);
			glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Stride), strides.data()+i);
			glDrawArrays(GL_LINES, 0, count);
		}
	}

	void canvas_render_ground(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		gl_program_use(self.ground.program);
		gl_program_uniform_set(self.ground.program, "projection_inverse", world.mats.projection_inverse);
		gl_program_uniform_set(self.ground.program, "view_inverse", world.mats.view_inverse);
		gl_program_uniform_set(self.ground.program, "color", self.ground.last_gnd.color);

		// fog
		gl_program_uniform_set(self.ground.program, "fog_enabled",
			world.settings.rendering.fog_enabled);
		gl_program_uniform_set(self.ground.program, "fog_density",
			world.settings.rendering.fog_density);
		gl_program_uniform_set(self.ground.program, "fog_color",
			world.settings.rendering.fog_color);
		gl_program_uniform_set(self.ground.program, "camera_pos",
			world.camera.position);

		glDisable(GL_DEPTH_TEST);

		glBindTexture(GL_TEXTURE_2D, self.ground.tile_texture);
		glBindVertexArray(self.ground.gl_buf.vao);
		glDrawArrays(GL_TRIANGLES, 0, self.ground.gl_buf.len);

		glEnable(GL_DEPTH_TEST);
	}

	void canvas_render_gnd_pictures(World& world) {
		DEF_SYSTEM

		auto& self = world.canvas;

		if (self.gnd_pics.list.empty()) {
			return;
		}

		glDisable(GL_DEPTH_TEST);
		gl_program_use(world.canvas.gnd_pics.program);

		// fog
		gl_program_uniform_set(self.gnd_pics.program, "fog_enabled",
			world.settings.rendering.fog_enabled);
		gl_program_uniform_set(self.gnd_pics.program, "fog_density",
			world.settings.rendering.fog_density);
		gl_program_uniform_set(self.gnd_pics.program, "fog_color",
			world.settings.rendering.fog_color);

		for (const auto& gnd_pic : self.gnd_pics.list) {
			gl_program_uniform_set(self.gnd_pics.program, "projection_view_model", gnd_pic.projection_view_model);

			for (const auto& primitives : gnd_pic.list_primitives) {
				gl_program_uniform_set(self.gnd_pics.program, "primitive_color[0]", primitives.color);

				gl_program_uniform_set(self.gnd_pics.program, "gradient_enabled", primitives.gradient_enabled);
				if (primitives.gradient_enabled) {
					gl_program_uniform_set(self.gnd_pics.program, "primitive_color[1]", primitives.gradient_color2);
				}

				gl_program_uniform_set(self.gnd_pics.program, "tex_enabled", primitives.tex_enabled);
				if (primitives.tex_enabled) {
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, primitives.texture_id);
					gl_program_uniform_set(self.gnd_pics.program, "terrain_tex", 0);
				}

				glBindVertexArray(primitives.vao);
				glDrawArrays(primitives.gl_primitive_type, 0, primitives.buf_len);
			}
		}

		glEnable(GL_DEPTH_TEST);
	}

} // namespace sys
