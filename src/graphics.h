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

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::vec2& f) {
	glUniform2fv(glGetUniformLocation(self.id, uniform), 1, glm::value_ptr(f));
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::vec3& f) {
	glUniform3fv(glGetUniformLocation(self.id, uniform), 1, glm::value_ptr(f));
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::vec4& f) {
	glUniform4fv(glGetUniformLocation(self.id, uniform), 1, glm::value_ptr(f));
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::mat3& f, bool transpose = false) {
	glUniformMatrix3fv(glGetUniformLocation(self.id, uniform), 1, transpose, glm::value_ptr(f));
}

void gl_program_uniform_set(GLProgram& self, const char* uniform, const glm::mat4& f, bool transpose = false) {
	glUniformMatrix4fv(glGetUniformLocation(self.id, uniform), 1, transpose, glm::value_ptr(f));
}

struct GLVertexAttrib {
	GLenum type;
	size_t num_components;
	size_t size;
};

template<typename T> constexpr GLVertexAttrib _gl_vertex_attrib();

#define GL_REGISTER_TYPE(T, gl_type_enum, nc)			 		\
	template<> constexpr GLVertexAttrib _gl_vertex_attrib<T>() {\
		return GLVertexAttrib { 								\
			.type=gl_type_enum,									\
			.num_components=nc,									\
			.size=sizeof(T) 									\
		};														\
	}

GL_REGISTER_TYPE(float,     GL_FLOAT, 1)
GL_REGISTER_TYPE(glm::vec2, GL_FLOAT, 2)
GL_REGISTER_TYPE(glm::vec3, GL_FLOAT, 3)
GL_REGISTER_TYPE(glm::vec4, GL_FLOAT, 4)
GL_REGISTER_TYPE(int,        GL_INT, 1)
GL_REGISTER_TYPE(glm::ivec2, GL_INT, 2)
GL_REGISTER_TYPE(glm::ivec3, GL_INT, 3)
GL_REGISTER_TYPE(glm::ivec4, GL_INT, 4)
GL_REGISTER_TYPE(unsigned int, GL_UNSIGNED_INT, 1)
GL_REGISTER_TYPE(glm::uvec2,   GL_UNSIGNED_INT, 2)
GL_REGISTER_TYPE(glm::uvec3,   GL_UNSIGNED_INT, 3)
GL_REGISTER_TYPE(glm::uvec4,   GL_UNSIGNED_INT, 4)

// opengl buffer, resides in GPU memory
// use `gl_buf_new` to load from CPU memory
// or allocate dynamic buf with given size
struct GLBuf {
	GLuint vao, vbo;
	size_t len;
};

template<typename... AttribType, typename T>
GLBuf gl_buf_new(const mu::Vec<T>& buffer) {
	constexpr size_t attributes_size = sizeof...(AttribType);
	constexpr GLVertexAttrib attributes[attributes_size] = { _gl_vertex_attrib<AttribType>()... };
	constexpr size_t stride_size = sizeof(T);

	GLBuf self {
		.len = buffer.size()
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, buffer.size() * stride_size, buffer.data(), GL_STATIC_DRAW);

		size_t offset = 0;
		bool normalize = false;
		for (int i = 0; i < attributes_size; i++) {
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(
				i,
				attributes[i].num_components,
				attributes[i].type,
				normalize,
				stride_size,
				(void*)offset
			);
			offset += attributes[i].size;
		}
	glBindVertexArray(0);

	return self;
}

template<typename... AttribType>
GLBuf gl_buf_new_dyn(size_t len) {
	constexpr size_t attributes_size = sizeof...(AttribType);
	constexpr GLVertexAttrib attributes[attributes_size] = { _gl_vertex_attrib<AttribType>()... };

	size_t stride_size = 0;
	for (int i = 0; i < attributes_size; i++) {
		stride_size += attributes[i].size;
	}

	GLBuf self {
		.len = len
	};

	glGenVertexArrays(1, &self.vao);
	glBindVertexArray(self.vao);
		glGenBuffers(1, &self.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, self.vbo);
		glBufferData(GL_ARRAY_BUFFER, len * stride_size, NULL, GL_DYNAMIC_DRAW);

		size_t offset = 0;
		bool normalize = false;
		for (int i = 0; i < attributes_size; i++) {
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(
				i,
				attributes[i].num_components,
				attributes[i].type,
				normalize,
				stride_size,
				(void*)offset
			);
			offset += attributes[i].size;
		}
	glBindVertexArray(0);

	return self;
}

void gl_buf_free(GLBuf& self) {
	glDeleteBuffers(1, &self.vbo);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &self.vao);
	self = {};
}
