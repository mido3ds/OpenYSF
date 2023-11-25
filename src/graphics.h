#pragma once

#include <glad/glad.h>

void gl_process_errors() {
	#ifndef NDEBUG
		GLenum err_code;
		int errors = 0;
		while ((err_code = glGetError()) != GL_NO_ERROR) {
			switch (err_code) {
			case GL_INVALID_ENUM:                  mu::log_error("GL::INVALID_ENUM"); break;
			case GL_INVALID_VALUE:                 mu::log_error("GL::INVALID_VALUE"); break;
			case GL_INVALID_OPERATION:             mu::log_error("GL::INVALID_OPERATION"); break;
			case GL_STACK_OVERFLOW:                mu::log_error("GL::STACK_OVERFLOW"); break;
			case GL_STACK_UNDERFLOW:               mu::log_error("GL::STACK_UNDERFLOW"); break;
			case GL_OUT_OF_MEMORY:                 mu::log_error("GL::OUT_OF_MEMORY"); break;
			case GL_INVALID_FRAMEBUFFER_OPERATION: mu::log_error("GL::INVALID_FRAMEBUFFER_OPERATION"); break;
			}
			errors++;
		}
		if (errors > 0) {
			mu::panic("found {} opengl error(s)", errors);
		}
	#endif
}

GLfloat gl_get_float(GLenum e) {
	GLfloat out;
	glGetFloatv(e, &out);
	return out;
}

struct GLProgram {
	GLuint id;
};

GLProgram gl_program_new(const char* vertex_shader_src, const char* fragment_shader_src) {
	// vertex shader
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_src, NULL);
    glCompileShader(vertex_shader);

    GLint vertex_shader_success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_shader_success);
    if (!vertex_shader_success) {
    	char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, info_log);
        mu::panic("failed to compile vertex shader, err: {}", info_log);
    }

    // fragment shader
    const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_src, NULL);
    glCompileShader(fragment_shader);

	GLint fragment_shader_success;
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_shader_success);
    if (!fragment_shader_success) {
    	char info_log[512];
        glGetShaderInfoLog(fragment_shader, 512, NULL, info_log);
        mu::panic("failed to compile fragment shader, err: {}", info_log);
    }

    // link shaders
    const GLuint gpu_program = glCreateProgram();
    glAttachShader(gpu_program, vertex_shader);
    glAttachShader(gpu_program, fragment_shader);
    glLinkProgram(gpu_program);

	GLint shader_program_success;
    glGetProgramiv(gpu_program, GL_LINK_STATUS, &shader_program_success);
    if (!shader_program_success) {
    	char info_log[512];
        glGetProgramInfoLog(gpu_program, 512, NULL, info_log);
        mu::panic("failed to link vertex and fragment shaders, err: {}", info_log);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

	return GLProgram { .id = gpu_program };
}

void gl_program_free(GLProgram& self) {
	glDeleteProgram(self.id);
	self.id = 0;
}

void gl_program_use(GLProgram& self) {
	glUseProgram(self.id);
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, bool b) {
	glUniform1i(glGetUniformLocation(self.id, uniform), b? 1 : 0);
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, int i) {
	glUniform1i(glGetUniformLocation(self.id, uniform), i);
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, float f) {
	glUniform1f(glGetUniformLocation(self.id, uniform), f);
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::vec3& f) {
	glUniform3fv(glGetUniformLocation(self.id, uniform), 1, glm::value_ptr(f));
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::mat4& f, bool transpose = false) {
	glUniformMatrix4fv(glGetUniformLocation(self.id, uniform), 1, transpose, glm::value_ptr(f));
}

namespace canvas {
	struct MeshStride {
		glm::vec3 vertex;
		glm::vec4 color;
	};

	struct SpriteStride {
		glm::vec2 vertex;
		glm::vec2 tex_coord;
	};

	struct LineStride {
		glm::vec4 vertex;
		glm::vec4 color;
	};
}

// opengl buffer, resides in GPU memory
// use `gl_buf_new` to load from CPU memory
// or allocate dynamic buf with given size
struct GLBuf {
	GLuint vao, vbo;
	size_t len;
};

GLBuf gl_buf_new(const mu::Vec<glm::vec2>& buffer) {
	GLBuf self {
		.len = buffer.size()
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(glm::vec2), buffer.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,                 /*index*/
			2,                 /*#components*/
			GL_FLOAT,          /*type*/
			GL_FALSE,          /*normalize*/
			sizeof(glm::vec2), /*stride bytes*/
			(void*)0           /*offset*/
		);
	glBindVertexArray(0);

	return self;
}

GLBuf gl_buf_new(const mu::Vec<glm::vec3>& buffer) {
	GLBuf self {
		.len = buffer.size()
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(glm::vec3), buffer.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,                 /*index*/
			3,                 /*#components*/
			GL_FLOAT,          /*type*/
			GL_FALSE,          /*normalize*/
			sizeof(glm::vec3), /*stride bytes*/
			(void*)0           /*offset*/
		);
	glBindVertexArray(0);

	return self;
}

GLBuf gl_buf_new(glm::vec4, size_t len) {
	GLBuf self {
		.len = len
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, len * sizeof(glm::vec4), NULL, GL_DYNAMIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,                 /*index*/
			4,                 /*#components*/
			GL_FLOAT,          /*type*/
			GL_FALSE,          /*normalize*/
			sizeof(glm::vec4), /*stride bytes*/
			(void*)0           /*offset*/
		);
	glBindVertexArray(0);

	return self;
}

GLBuf gl_buf_new(const mu::Vec<canvas::MeshStride>& buffer) {
	constexpr auto STRIDE_SIZE = sizeof(canvas::MeshStride);

	GLBuf self {
		.len = buffer.size()
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * STRIDE_SIZE, buffer.data(), GL_STATIC_DRAW);

		size_t offset = 0;

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,              /*index*/
			3,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::MeshStride::vertex);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(
			1,              /*index*/
			4,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::MeshStride::color);
	glBindVertexArray(0);

	return self;
}

GLBuf gl_buf_new(const mu::Vec<canvas::SpriteStride>& buffer) {
	constexpr auto STRIDE_SIZE = sizeof(canvas::SpriteStride);

	GLBuf self {
		.len = buffer.size()
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * STRIDE_SIZE, buffer.data(), GL_STATIC_DRAW);

		size_t offset = 0;

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,              /*index*/
			2,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::SpriteStride::vertex);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(
			1,              /*index*/
			2,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::SpriteStride::tex_coord);
	glBindVertexArray(0);

	return self;
}

GLBuf gl_buf_new(canvas::LineStride, size_t len) {
	constexpr auto STRIDE_SIZE = sizeof(canvas::LineStride);

	GLBuf self {
		.len = len
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, self.len * STRIDE_SIZE, NULL, GL_DYNAMIC_DRAW);

		size_t offset = 0;

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			0,              /*index*/
			4,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::LineStride::vertex);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(
			1,              /*index*/
			4,              /*#components*/
			GL_FLOAT,       /*type*/
			GL_FALSE,       /*normalize*/
			STRIDE_SIZE,    /*stride bytes*/
			(void*)offset   /*offset*/
		);
		offset += sizeof(canvas::LineStride::color);
	glBindVertexArray(0);

	return self;
}

void gl_buf_free(GLBuf& self) {
	glDeleteBuffers(1, &self.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.vao);
	self = {};
}
