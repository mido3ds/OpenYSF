cmake_minimum_required(VERSION 3.16)
project(JFS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# set(CMAKE_CXX_STANDARD 20) # can't set to c++20 as of mn
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CPM)
CPMAddPackage(
	NAME             mn
	GIT_REPOSITORY   git@github.com:MoustaphaSaad/mn.git
	GIT_TAG          v4.3.0
	GIT_SHALLOW      TRUE
)
CPMAddPackage(
	NAME             SDL2
	GIT_REPOSITORY   git@github.com:libsdl-org/SDL.git
	GIT_TAG          release-2.0.16
	GIT_SHALLOW      TRUE
)

find_package(OpenGL REQUIRED)

add_executable(jfs
	src/main.cpp
)

target_link_libraries(jfs
	PRIVATE
		MoustaphaSaad::mn
		SDL2::SDL2
		OpenGL::GL
)
