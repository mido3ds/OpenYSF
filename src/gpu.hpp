#pragma once

#include <glad/glad.h>

void gpu_check_errors() {
	#ifndef NDEBUG
		GLenum err_code;
		int errors = 0;
		while ((err_code = glGetError()) != GL_NO_ERROR) {
			std::string error;
			switch (err_code) {
			case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
			case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
			case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
			case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
			case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
			case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
			case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
			}
			mn::log_error("GL::{} at {}:{}\n", error);
			errors++;
		}
		if (errors > 0) {
			mn::panic("found {} opengl errors");
		}
	#endif
}

GLfloat gpu_get_float(GLenum e) {
	GLfloat out;
	glGetFloatv(e, &out);
	return out;
}

struct GPU_Program { GLuint handle; };

GPU_Program gpu_program_new(const char* vertex_shader_src, const char* fragment_shader_src) {
	// vertex shader
    const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_src, NULL);
    glCompileShader(vertex_shader);

    GLint vertex_shader_success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_shader_success);
    if (!vertex_shader_success) {
    	char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, info_log);
        mn::panic("failed to compile vertex shader, err: {}", info_log);
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
        mn::panic("failed to compile fragment shader, err: {}", info_log);
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
        mn::panic("failed to link vertex and fragment shaders, err: {}", info_log);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

	return GPU_Program { .handle=gpu_program, };
}

void gpu_program_free(GPU_Program& self) {
	glDeleteProgram(self.handle);
	self.handle = 0;
}

void destruct(GPU_Program& self) {
	gpu_program_free(self);
}
