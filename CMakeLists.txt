cmake_minimum_required(VERSION 3.16)
project(OpenYSF VERSION 0.1.0 LANGUAGES CXX C)

if(APPLE)
    enable_language(OBJC)
endif()

option(OPENYSF_PEDANTIC_BUILD "Enable pedantic warnings during build" OFF)

if (CMAKE_BUILD_TYPE STREQUAL "")
	set(CMAKE_BUILD_TYPE Debug)
endif()

if (CPM_SOURCE_CACHE STREQUAL "")
	message(WARNING "CPM_SOURCE_CACHE is empty, this will download locally all packages")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CPM)
CPMAddPackage(
	NAME             fmt
	GIT_REPOSITORY   https://github.com/fmtlib/fmt.git
	GIT_TAG          8.1.1
	GIT_SHALLOW      TRUE
)
CPMAddPackage(
	NAME             SDL2
	GIT_REPOSITORY   https://github.com/libsdl-org/SDL.git
	GIT_TAG          release-2.24.1
	GIT_SHALLOW      TRUE
	OPTIONS
		"SDL_WAYLAND OFF"
)
CPMAddPackage(
	NAME             SDL_image
	GIT_REPOSITORY   https://github.com/libsdl-org/SDL_image.git
	GIT_TAG          release-2.6.2
	GIT_SHALLOW      TRUE
	OPTIONS
		"SUPPORT_PNG ON"
		"SUPPORT_JPG OFF"
		"SUPPORT_WEBP OFF"
		"SDL2IMAGE_INSTALL OFF"
)
CPMAddPackage(
	NAME             glm
	GIT_REPOSITORY   https://github.com/g-truc/glm.git
	GIT_TAG          0.9.9.8
	GIT_SHALLOW      TRUE
)
CPMAddPackage(
	NAME             portable-file-dialogs
	GIT_REPOSITORY   https://github.com/samhocevar/portable-file-dialogs.git
	GIT_TAG          0.1.0
	GIT_SHALLOW      TRUE
)

find_package(OpenGL REQUIRED)

# glad
add_library(_glad STATIC extern/glad/src/glad.c extern/glad/include/glad/glad.h)
target_include_directories(_glad PUBLIC extern/glad/include)

# imgui
add_library(_imgui
	STATIC
		extern/imgui-1.85/backends/imgui_impl_opengl3.cpp
		extern/imgui-1.85/backends/imgui_impl_sdl.cpp
		extern/imgui-1.85/imgui.cpp
		extern/imgui-1.85/imgui_demo.cpp
		extern/imgui-1.85/imgui_draw.cpp
		extern/imgui-1.85/imgui_tables.cpp
		extern/imgui-1.85/imgui_widgets.cpp

		extern/imgui-1.85/backends/imgui_impl_opengl3.h
		extern/imgui-1.85/backends/imgui_impl_opengl3_loader.h
		extern/imgui-1.85/backends/imgui_impl_sdl.h
		extern/imgui-1.85/imconfig.h
		extern/imgui-1.85/imgui.h
		extern/imgui-1.85/imgui_internal.h
		extern/imgui-1.85/imstb_rectpack.h
		extern/imgui-1.85/imstb_textedit.h
		extern/imgui-1.85/imstb_truetype.h
)
target_include_directories(_imgui PUBLIC extern/imgui-1.85)
target_link_libraries(_imgui PRIVATE SDL2::SDL2)

# open-ysf
add_executable(open-ysf
	src/main.cpp
	src/parser.hpp
	src/math.hpp
	src/gpu.hpp
	src/imgui.hpp
	src/audio.hpp
	src/utils.hpp
	src/utils.cpp
)

target_link_libraries(open-ysf
	PRIVATE
		fmt::fmt
		SDL2::SDL2
		SDL2_image::SDL2_image
		OpenGL::GL
		glm
		_glad
		_imgui
		portable_file_dialogs
		$<$<PLATFORM_ID:Windows>:dbghelp>
		${CMAKE_DL_LIBS}
)

set_property(TARGET open-ysf
	PROPERTY
		CXX_STANDARD 20
		CMAKE_CXX_STANDARD_REQUIRED YES
		CMAKE_CXX_EXTENSIONS NO
)

target_compile_definitions(open-ysf
	PRIVATE
		ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
		$<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE>
		$<$<PLATFORM_ID:Windows>:OS_WINDOWS=1>
		$<$<PLATFORM_ID:Linux>:OS_LINUX=1>
		$<$<PLATFORM_ID:Darwin>:OS_MACOS=1>
		$<$<CXX_COMPILER_ID:Clang>:COMPILER_CLANG=1>
		$<$<CXX_COMPILER_ID:AppleClang>:COMPILER_CLANG=1>
		$<$<CXX_COMPILER_ID:AppleClang>:COMPILER_APPLE_CLANG=1>
		$<$<CXX_COMPILER_ID:GNU>:COMPILER_GNU=1>
		$<$<CXX_COMPILER_ID:MSVC>:COMPILER_MSVC=1>
		$<$<CONFIG:DEBUG>:DEBUG>
)

if (${OPENYSF_PEDANTIC_BUILD})
	target_compile_options(open-ysf
		PRIVATE
			$<$<CXX_COMPILER_ID:MSVC>:/W4 /NODEFAULTLIB:library>
			$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic -Wno-nested-anon-types>
	)
endif()
