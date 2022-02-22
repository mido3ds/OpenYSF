#pragma once

#include <glad/glad.h>

void myglCheckError() {
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

GLfloat myglGetFloat(GLenum e) {
	GLfloat out;
	glGetFloatv(e, &out);
	return out;
}
