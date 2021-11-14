#include <stdio.h>
#include <stdint.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <GL/gl.h>

#include <mn/Log.h>
#include <mn/Defer.h>

#define WinWidth 400
#define WinHeight 400

int main() {
	SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		mn::log_error("Failed to init video, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_Quit());

	auto wnd = SDL_CreateWindow(
		"JFS",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WinWidth, WinHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
	);
	if (!wnd) {
		mn::log_error("Faield to create window, err: {}", SDL_GetError());
		return 1;
	}
	mn_defer(SDL_DestroyWindow(wnd));
	SDL_SetWindowBordered(wnd, SDL_TRUE);

	auto cxt = SDL_GL_CreateContext(wnd);
	mn_defer(SDL_GL_DeleteContext(cxt));

	bool running = true;
	bool fullscreen = false;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					running = 0;
					break;
				case 'f':
					fullscreen = !fullscreen;
					if (fullscreen) {
						SDL_SetWindowFullscreen(wnd, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
					} else {
						SDL_SetWindowFullscreen(wnd, SDL_WINDOW_OPENGL);
					}
					break;
				default:
					break;
				}
			} else if (event.type == SDL_QUIT) {
				running = 0;
			}
		}

		glViewport(0, 0, WinWidth, WinHeight);
		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT);

		SDL_GL_SwapWindow(wnd);
	}

	return 0;
}
