#pragma once

#include <cstdint>

#include <glad/glad.h>
#include <SDL.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>

#include <mu/utils.h>

#include "graphics.h"

namespace canvas {
	// all state of loaded glyph using FreeType
	// https://learnopengl.com/img/in-practice/glyph_offset.png
	struct Glyph {
		GLuint texture;
		glm::ivec2 size;
		// Offset from baseline to left/top of glyph
		glm::ivec2 bearing;
		// horizontal offset to advance to next glyph
		uint32_t advance;
	};

	// text for debugging, rendered in imgui overlay window
	struct TextOverlay {
		mu::Str text;
	};

	struct ZLPoint {
		glm::vec3 center, color;
	};

	// text that always faces camera
	struct Text {
		mu::Str text;
		glm::vec3 p; // world coords, left-bottom corner
		float scale;
		glm::vec4 color;
	};

	struct Axis {
		glm::mat4 transformation;
	};

	struct Line {
		// world coordinates
		glm::vec3 p0, p1;
		glm::vec4 color;
	};

	struct Box {
		glm::vec3 translation, scale, color;
	};

	struct Mesh {
		GLuint vao;
		size_t buf_len;
		glm::mat4 projection_view_model;
		glm::mat3 model_normal;

		GLuint texture_id = 0;
		bool tex_enabled = false;
	};

	struct Cockpit {
		GLuint vao;
		size_t buf_len;
		glm::mat4 projection_view_model;
		glm::mat3 model_normal;
	};

	struct GradientMesh {
		GLuint vao;
		size_t buf_len;
		glm::mat4 projection_view_model;
		glm::mat3 model_normal;

		float gradient_bottom_y, gradient_top_y;
		glm::vec3 gradient_bottom_color, gradient_top_color;
	};

	struct Ground {
		glm::vec3 color;
	};

	// 2d picture rendered on ground
	struct GndPic {
		glm::mat4 projection_view_model;

		// points, lines, line segments or triangles
		struct Primitive {
			GLuint vao;
			size_t buf_len;
			GLenum gl_primitive_type;

			glm::vec3 color;
			bool gradient_enabled;
			glm::vec3 gradient_color2;

			GLuint texture_id = 0;
			bool tex_enabled = false;
		};

		mu::Vec<Primitive> list_primitives;
	};

	// heads up display, 2d shapes that sticks to window
	// all positions are in [0,1] range
	namespace hud {
		struct Text {
			mu::Str text;
			glm::vec2 p; // left-bottom corner
			float scale;
			glm::vec4 color;
		};

		struct Circle {
			glm::vec2 center;
			float radius;
			glm::vec4 color;
		};

		struct Line {
			glm::vec2 p0, p1;
			glm::vec4 color;
		};

		struct LineStrip {
			glm::vec4 color;
			mu::Vec<glm::vec2> points;
		};

		struct FilledArc {
			glm::vec2 center;
			float radius;
			float start_angle, end_angle;
			glm::vec4 color;
		};

		struct FilledTriangle {
			glm::vec2 p0, p1, p2;
			glm::vec4 color;
		};
	}

	struct Vector {
		mu::Str label;
		glm::vec3 p, dir;
		float len;
		glm::vec4 color;
	};
}

struct Canvas {
	mu::memory::Arena arena;

	struct {
		GLProgram program;

		mu::Vec<canvas::Mesh> list_regular;
		mu::Vec<canvas::GradientMesh> list_gradient;
		mu::Vec<canvas::Cockpit> list_cockpit;
	} meshes;

	struct {
		GLProgram program;
		GLBuf gl_buf;
		SDL_Surface* tile_surface;
		GLuint tile_texture;

		// we currently only render last ground in loaded fields
		canvas::Ground last_gnd;
	} ground;

	struct {
		GLProgram program;

		mu::Vec<canvas::GndPic> list;
	} gnd_pics;

	struct {
		GLProgram program;
		GLBuf gl_buf;

		GLuint sprite_texture;
		SDL_Surface* sprite_surface;

		mu::Vec<canvas::ZLPoint> list;
	} zlpoints;

	struct {
		GLBuf gl_buf; // single axis vertices
		GLfloat line_width = 5.0f;
		bool on_top = true;

		mu::Vec<canvas::Axis> list;
	} axes;

	struct {
		GLProgram program;
		GLBuf gl_buf; // single character quad vertices

		mu::Arr<canvas::Glyph, 128> glyphs;

		mu::Vec<canvas::Text> list_world;
		mu::Vec<canvas::hud::Text> list_hud;
	} text;

	struct {
		GLProgram program;
		GLBuf gl_buf;
		GLfloat line_width = 1.0f;

		mu::Vec<canvas::Line> list;
	} lines;

	struct {
		GLProgram program;
		GLBuf gl_buf;

		mu::Vec<canvas::hud::Circle> list_circles;
		mu::Vec<canvas::hud::Line> list_lines;
		mu::Vec<canvas::hud::LineStrip> list_line_strips;
		mu::Vec<canvas::hud::FilledArc> list_filled_arcs;
		mu::Vec<canvas::hud::FilledTriangle> list_filled_triangles;
	} hud_geoms;
};

inline void canvas_add(Canvas& self, canvas::Text&& t) {
	self.text.list_world.push_back(std::move(t));
}

inline void canvas_add(Canvas& self, canvas::hud::Text&& t) {
	self.text.list_hud.push_back(std::move(t));
}

inline void canvas_add(Canvas& self, canvas::hud::Circle&& c) {
	self.hud_geoms.list_circles.push_back(std::move(c));
}

inline void canvas_add(Canvas& self, canvas::hud::Line&& l) {
	self.hud_geoms.list_lines.push_back(std::move(l));
}

inline void canvas_add(Canvas& self, canvas::hud::LineStrip&& l) {
	self.hud_geoms.list_line_strips.push_back(std::move(l));
}

inline void canvas_add(Canvas& self, canvas::hud::FilledArc&& a) {
	self.hud_geoms.list_filled_arcs.push_back(std::move(a));
}

inline void canvas_add(Canvas& self, canvas::hud::FilledTriangle&& t) {
	self.hud_geoms.list_filled_triangles.push_back(std::move(t));
}

inline void canvas_add(Canvas& self, canvas::Axis&& a) {
	self.axes.list.push_back(std::move(a));
}

inline void canvas_add(Canvas& self, canvas::ZLPoint&& z) {
	self.zlpoints.list.push_back(std::move(z));
}

inline void canvas_add(Canvas& self, canvas::Line&& l) {
	self.lines.list.push_back(std::move(l));
}

inline void canvas_add(Canvas& self, canvas::Box&& b) {
	const auto min = b.translation;
	const auto max = b.translation + b.scale;
	const glm::vec4 color{b.color, 1.0f};

	const glm::vec3 c000 = min;
	const glm::vec3 c001{min.x, min.y, max.z};
	const glm::vec3 c010{min.x, max.y, min.z};
	const glm::vec3 c011{min.x, max.y, max.z};
	const glm::vec3 c100{max.x, min.y, min.z};
	const glm::vec3 c101{max.x, min.y, max.z};
	const glm::vec3 c110{max.x, max.y, min.z};
	const glm::vec3 c111 = max;

	canvas_add(self, canvas::Line{c000, c001, color});
	canvas_add(self, canvas::Line{c010, c011, color});
	canvas_add(self, canvas::Line{c100, c101, color});
	canvas_add(self, canvas::Line{c110, c111, color});

	canvas_add(self, canvas::Line{c000, c010, color});
	canvas_add(self, canvas::Line{c001, c011, color});
	canvas_add(self, canvas::Line{c100, c110, color});
	canvas_add(self, canvas::Line{c101, c111, color});

	canvas_add(self, canvas::Line{c000, c100, color});
	canvas_add(self, canvas::Line{c001, c101, color});
	canvas_add(self, canvas::Line{c010, c110, color});
	canvas_add(self, canvas::Line{c011, c111, color});
}

inline void canvas_add(Canvas& self, canvas::Mesh&& m) {
	self.meshes.list_regular.push_back(std::move(m));
}

inline void canvas_add(Canvas& self, canvas::Cockpit&& c) {
	self.meshes.list_cockpit.push_back(std::move(c));
}

inline void canvas_add(Canvas& self, canvas::GradientMesh&& m) {
	self.meshes.list_gradient.push_back(std::move(m));
}

inline void canvas_add(Canvas& self, canvas::Ground&& g) {
	self.ground.last_gnd = g;
}

inline void canvas_add(Canvas& self, canvas::GndPic&& p) {
	self.gnd_pics.list.push_back(std::move(p));
}

inline void canvas_add(Canvas& self, const canvas::Vector& v) {
	canvas_add(self, canvas::Line {
		.p0 = v.p,
		.p1 = v.p + v.dir * v.len,
		.color = v.color
	});
	canvas_add(self, canvas::Text {
		.text = v.label,
		.p = v.p + v.dir * v.len,
		.scale = 0.02f,
		.color = v.color
	});
}
