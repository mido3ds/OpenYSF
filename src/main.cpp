// without the following define, SDL will come with its main()
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>

// don't export min/max/near/far definitions with windows.h otherwise other includes might break
#define NOMINMAX
#include <portable-file-dialogs.h>
#undef near
#undef far

#include <ft2build.h>
#include FT_FREETYPE_H

#include <mu/utils.h>

#include "imgui.h"
#include "graphics.h"
#include "parser.h"
#include "math.h"
#include "audio.h"

#include "world.h"

constexpr auto WND_TITLE        = "OpenYSF";
constexpr int  WND_INIT_WIDTH   = 1028;
constexpr int  WND_INIT_HEIGHT  = 680;
constexpr Uint32 WND_FLAGS      = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
constexpr float IMGUI_WNDS_BG_ALPHA = 0.8f;
constexpr glm::vec3 CORNFLOWER_BLU_COLOR {0.392f, 0.584f, 0.929f};

constexpr auto GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_CORE;
constexpr int  GL_CONTEXT_MAJOR = 3;
constexpr int  GL_CONTEXT_MINOR = 3;
constexpr auto GL_DOUBLE_BUFFER = SDL_TRUE;

constexpr float MIN_SPEED = 0.0f;
constexpr float MAX_SPEED = 50.0f;

namespace sys {
	void sdl_init(World& world) {
		DEF_SYSTEM

		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
			mu::panic(SDL_GetError());
		}

		world.sdl_window = SDL_CreateWindow(
			WND_TITLE,
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			WND_INIT_WIDTH, WND_INIT_HEIGHT,
			SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | WND_FLAGS
		);
		if (!world.sdl_window) {
			mu::panic(SDL_GetError());
		}

		SDL_SetWindowBordered(world.sdl_window, SDL_TRUE);

		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_PROFILE)) { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_CONTEXT_MAJOR))  { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_CONTEXT_MINOR))  { mu::panic(SDL_GetError()); }
		if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_DOUBLE_BUFFER))           { mu::panic(SDL_GetError()); }

		world.sdl_gl_context = SDL_GL_CreateContext(world.sdl_window);
		if (!world.sdl_gl_context) {
			mu::panic(SDL_GetError());
		}

		// glad: load all OpenGL function pointers
		if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
			mu::panic("failed to load GLAD function pointers");
		}
	}

	void sdl_free(World& world) {
		DEF_SYSTEM

		SDL_GL_DeleteContext(world.sdl_gl_context);
		SDL_DestroyWindow(world.sdl_window);
#ifndef OS_MACOS
		// macOS: SDL_Quit → SDL_AudioQuit() → AudioUnitUninitialize blocks many ms.
		// Process exit tears down CoreAudio instantly without the wait; the OS
		// reclaims all resources. Guarding SDL_CloseAudioDevice too (see audio.h).
		SDL_Quit();
#endif
	}

	void imgui_init(World& world) {
		DEF_SYSTEM

		IMGUI_CHECKVERSION();
		if (ImGui::CreateContext() == nullptr) {
			mu::panic("failed to create imgui context");
		}
		if (ImPlot::CreateContext() == nullptr) {
			mu::panic("failed to create implot context");
		}

		ImGui::StyleColorsDark();

		if (!ImGui_ImplSDL2_InitForOpenGL(world.sdl_window, world.sdl_gl_context)) {
			mu::panic("failed to init imgui implementation for SDL2");
		}

		if (!ImGui_ImplOpenGL3_Init("#version 330")) {
			mu::panic("failed to init imgui implementation for OpenGL3");
		}

		world.imgui_ini_file_path = mu::str_format("{}/{}", mu::folder_config(mu::memory::tmp()), "open-ysf-imgui.ini");
		ImGui::GetIO().IniFilename = world.imgui_ini_file_path.c_str();
	}

	void imgui_free(World& world) {
		DEF_SYSTEM

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}

	void imgui_rendering_begin(World& world) {
		DEF_SYSTEM

		ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
	}

	void imgui_rendering_end(World& world) {
		DEF_SYSTEM

		ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void imgui_logs_window(World& world) {
		DEF_SYSTEM

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Logs")) {
			ImGui::Checkbox("Auto-Scroll", &world.imgui_window_logger.auto_scrolling);
			ImGui::SameLine();
			ImGui::Checkbox("Wrapped", &world.imgui_window_logger.wrapped);
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				world.imgui_window_logger = {};
			}

			if (ImGui::BeginChild("logs child", {}, false, world.imgui_window_logger.wrapped? 0:ImGuiWindowFlags_HorizontalScrollbar)) {
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {0, 0});
				ImGuiListClipper clipper(world.imgui_window_logger.logs.size());
				while (clipper.Step()) {
					for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
						if (world.imgui_window_logger.wrapped) {
							ImGui::TextWrapped("%s", world.imgui_window_logger.logs[i].c_str());
						} else {
							auto log = world.imgui_window_logger.logs[i];
							ImGui::TextUnformatted(&log[0], &log[log.size()-1]);
						}
					}
				}
				ImGui::PopStyleVar();

				// scroll
				if (world.imgui_window_logger.auto_scrolling) {
					if (world.imgui_window_logger.last_scrolled_line != world.imgui_window_logger.logs.size()) {
						world.imgui_window_logger.last_scrolled_line = world.imgui_window_logger.logs.size();
						ImGui::SetScrollHereY();
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}

	void imgui_overlay_text(World& world) {
		DEF_SYSTEM

		const float PAD = 10.0f;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
		const ImVec2 window_pos {
			work_pos.x + viewport->WorkSize.x - PAD,
			work_pos.y + PAD,
		};
		const ImVec2 window_pos_pivot { 1.0f, 0.0f };
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowSize(ImVec2 {300, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.35f);

		if (ImGui::Begin("Overlay Info", nullptr, ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove)) {
			for (const auto& line : world.text_overlay_list) {
				ImGui::TextWrapped(mu::str_tmpf("> {}", line).c_str());
			}
			world.text_overlay_list = mu::Vec<mu::Str>(mu::memory::tmp());
		}
		ImGui::End();
	}

	void imgui_debug_window(World& world) {
		DEF_SYSTEM

		ImGui::SetNextWindowBgAlpha(IMGUI_WNDS_BG_ALPHA);
		if (ImGui::Begin("Debug")) {
			if (ImGui::TreeNode("Window")) {
				ImGui::Checkbox("Limit FPS", &world.settings.should_limit_fps);
				ImGui::BeginDisabled(!world.settings.should_limit_fps); {
					ImGui::InputInt("FPS", &world.settings.fps_limit, 1, 5);
				}
				ImGui::EndDisabled();

				int size[2];
				SDL_GetWindowSize(world.sdl_window, &size[0], &size[1]);
				const bool width_changed = ImGui::InputInt("Width", &size[0]);
				const bool height_changed = ImGui::InputInt("Height", &size[1]);
				if (width_changed || height_changed) {
					signal_fire(world.signals.wnd_configs_changed);
					SDL_SetWindowSize(world.sdl_window, size[0], size[1]);
				}

				MyImGui::EnumsCombo("Angle Max", &world.settings.current_angle_max, {
					{DEGREES_MAX, "DEGREES_MAX"},
					{RADIANS_MAX, "RADIANS_MAX"},
					{YS_MAX,      "YS_MAX"},
				});

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Projection")) {
				if (ImGui::Button("Reset")) {
					world.projection = {};
					signal_fire(world.signals.wnd_configs_changed);
				}

				ImGui::InputFloat("near", &world.projection.near, 1, 10);
				ImGui::InputFloat("far", &world.projection.far, 1, 10);
				ImGui::DragFloat("fovy (1/zoom)", &world.projection.fovy, 1, 1, 45);

				if (ImGui::Checkbox("custom aspect", &world.settings.custom_aspect_ratio) && !world.settings.custom_aspect_ratio) {
					signal_fire(world.signals.wnd_configs_changed);
				}
				ImGui::BeginDisabled(!world.settings.custom_aspect_ratio);
					ImGui::InputFloat("aspect", &world.projection.aspect, 1, 10);
				ImGui::EndDisabled();

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Camera")) {
				if (ImGui::Button("Reset")) {
					world.camera = { .aircraft=world.camera.aircraft };
				}

				int tracked_model_index = -1;
				for (int i = 0; i < world.aircrafts.size(); i++) {
					if (world.camera.aircraft == &world.aircrafts[i]) {
						tracked_model_index = i;
						break;
					}
				}
				if (ImGui::BeginCombo("Tracked Model", world.camera.aircraft ? mu::str_tmpf("Model[{}]", tracked_model_index).c_str() : "-NULL-")) {
					if (ImGui::Selectable("-NULL-", world.camera.aircraft == nullptr)) {
						world.camera.aircraft = nullptr;
					}
					for (size_t j = 0; j < world.aircrafts.size(); j++) {
						if (ImGui::Selectable(mu::str_tmpf("Model[{}]", j).c_str(), j == tracked_model_index)) {
							world.camera.aircraft = &world.aircrafts[j];
						}
					}

					ImGui::EndCombo();
				}

				if (world.camera.aircraft) {
					ImGui::DragFloat("zoom", &world.camera.zoom_multiplier, 1, 5, 100);

					ImGui::Checkbox("Rotate Around", &world.camera.enable_rotating_around);
				} else {
					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							world.camera.position = {};
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								world.camera.position = start_infos[j].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::DragFloat("movement_speed", &world.camera.movement_speed, 5, 50, 1000);
					ImGui::DragFloat("mouse_sensitivity", &world.camera.mouse_sensitivity, 1, 0.5, 10);
					ImGui::DragFloat3("world_up", glm::value_ptr(world.camera.world_up), 1, -100, 100);
					ImGui::DragFloat3("front", glm::value_ptr(world.camera.front), 0.1, -1, 1);
					ImGui::DragFloat3("right", glm::value_ptr(world.camera.right), 1, -100, 100);
					ImGui::DragFloat3("up", glm::value_ptr(world.camera.up), 1, -100, 100);

				}

				ImGui::SliderAngle("yaw", &world.camera.yaw, -89, 89);
				ImGui::SliderAngle("pitch", &world.camera.pitch, -179, 179);

				ImGui::DragFloat3("position", glm::value_ptr(world.camera.position), 1, -100, 100);

				ImGui::TreePop();
			}

			const GLfloat SMOOTH_LINE_WIDTH_GRANULARITY = gl_get_float(GL_SMOOTH_LINE_WIDTH_GRANULARITY);
			if (ImGui::TreeNode("Rendering")) {
				if (ImGui::Button("Reset")) {
					world.settings.rendering = {};
				}

				MyImGui::EnumsCombo("Polygon Mode", &world.settings.rendering.polygon_mode, {
					{GL_POINT, "GL_POINT"},
					{GL_LINE,  "GL_LINE"},
					{GL_FILL,  "GL_FILL"},
				});

				MyImGui::EnumsCombo("Regular Mesh Primitives", &world.settings.rendering.primitives_type, {
					{GL_POINTS,          "GL_POINTS"},
					{GL_LINES,           "GL_LINES"},
					{GL_LINE_LOOP,       "GL_LINE_LOOP"},
					{GL_LINE_STRIP,      "GL_LINE_STRIP"},
					{GL_TRIANGLES,       "GL_TRIANGLES"},
					{GL_TRIANGLE_STRIP,  "GL_TRIANGLE_STRIP"},
					{GL_TRIANGLE_FAN,    "GL_TRIANGLE_FAN"},
				});

				ImGui::Checkbox("Smooth Lines", &world.settings.rendering.smooth_lines);
                #ifndef OS_MACOS
				ImGui::BeginDisabled(!world.settings.rendering.smooth_lines);
					ImGui::DragFloat("Line Width", &world.settings.rendering.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
				ImGui::EndDisabled();
                #endif

				const GLfloat POINT_SIZE_GRANULARITY = gl_get_float(GL_POINT_SIZE_GRANULARITY);
				ImGui::DragFloat("Point Size", &world.settings.rendering.point_size, POINT_SIZE_GRANULARITY, 0.5, 100);

				ImGui::Separator();
				ImGui::Checkbox("Lighting", &world.settings.rendering.lighting);
				if (world.settings.rendering.lighting) {
					ImGui::ColorEdit3("Ambient", (float*)&world.settings.rendering.ambient_color);
					ImGui::DragFloat3("Light Dir", (float*)&world.settings.rendering.light_dir, 0.01f, -1.0f, 1.0f);
				}

			ImGui::Separator();
			ImGui::Checkbox("Fog", &world.settings.rendering.fog_enabled);
			if (world.settings.rendering.fog_enabled) {
				ImGui::DragFloat("Density", &world.settings.rendering.fog_density, 0.0001f, 0.0f, 0.01f, "%.4f");
				ImGui::ColorEdit3("Color", (float*)&world.settings.rendering.fog_color);
			}

			ImGui::TreePop();
			}

			if (ImGui::TreeNode("HUD")) {
				if (ImGui::Button("Reset")) {
					world.settings.hud = {};
				}

				ImGui::Checkbox("Enable HUD", &world.settings.hud.enabled);
				ImGui::Checkbox("Geoms Demo", &world.settings.hud.geoms_demo);

				ImGui::Separator();
				ImGui::Text("Heading Indicator");
				ImGui::DragFloat2("Pos##hdg", glm::value_ptr(world.settings.hud.heading.position), 0.005f, 0.0f, 1.0f);
				ImGui::DragFloat("Radius##hdg", &world.settings.hud.heading.radius, 0.002f, 0.01f, 0.5f);
				ImGui::ColorEdit4("Color##hdg", glm::value_ptr(world.settings.hud.heading.color));

				ImGui::Separator();
				ImGui::Text("VSI");
				ImGui::DragFloat2("Pos##vsi", glm::value_ptr(world.settings.hud.vsi.position), 0.005f, 0.0f, 1.0f);
				ImGui::DragFloat("Radius##vsi", &world.settings.hud.vsi.radius, 0.002f, 0.01f, 0.5f);
				ImGui::ColorEdit4("Color##vsi", glm::value_ptr(world.settings.hud.vsi.color));
				ImGui::ColorEdit4("Arc Bg##vsi", glm::value_ptr(world.settings.hud.vsi.arc_color));

				ImGui::Separator();
				ImGui::Text("ADI");
				ImGui::DragFloat2("Pos##adi", glm::value_ptr(world.settings.hud.adi.position), 0.005f, 0.0f, 1.0f);
				ImGui::DragFloat("Radius##adi", &world.settings.hud.adi.radius, 0.002f, 0.01f, 0.5f);
				ImGui::ColorEdit4("Outline##adi", glm::value_ptr(world.settings.hud.adi.color));
				ImGui::ColorEdit4("Sky##adi", glm::value_ptr(world.settings.hud.adi.sky_color));
				ImGui::ColorEdit4("Ground##adi", glm::value_ptr(world.settings.hud.adi.ground_color));

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Axes Rendering")) {
				ImGui::Checkbox("On Top", &world.canvas.axes.on_top);
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &world.canvas.axes.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif

				ImGui::BulletText("World Axis:");
				if (ImGui::Button("Reset")) {
					world.settings.world_axis = {};
				}
				ImGui::Checkbox("Enabled", &world.settings.world_axis.enabled);
				ImGui::DragFloat2("Position", glm::value_ptr(world.settings.world_axis.position), 0.05, -1, 1);
				ImGui::DragFloat("Scale", &world.settings.world_axis.scale, .05, 0, 1);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Lines Rendering")) {
                #ifndef OS_MACOS
				ImGui::DragFloat("Line Width", &world.canvas.lines.line_width, SMOOTH_LINE_WIDTH_GRANULARITY, 0.5, 100);
                #endif

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Physics")) {
				ImGui::Checkbox("Handle Collision", &world.settings.handle_collision);
				ImGui::SliderFloat("Brake Coeff", &world.settings.brake_coeff, 0.0f, 1.0f);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Audio")) {
				for (const auto& [_, buf] : world.audio_buffers) {
					ImGui::PushID(buf.file_path.c_str());

					if (ImGui::Button("Play")) {
						audio_device_play(world.audio_device, buf);
					}

					ImGui::SameLine();
					if (ImGui::Button("Loop")) {
						audio_device_play_looped(world.audio_device, buf);
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(audio_device_is_playing(world.audio_device, buf) == false);
					if (ImGui::Button("Stop")) {
						audio_device_stop(world.audio_device, buf);
					}
					ImGui::EndDisabled();

					ImGui::SameLine();
					ImGui::Text(mu::Str(mu::file_get_base_name(buf.file_path), mu::memory::tmp()).c_str());

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Systems")) {
				int enabled_count = 0;
				uint64_t total_latency = 0;
				uint64_t max_latency = 0;
				for (auto& sysinfo : world.sysmon.systems) {
					if (sysinfo.enabled) {
						enabled_count++;
						total_latency += sysinfo.latency_micros;
						max_latency = std::max(max_latency, sysinfo.latency_micros);
					}
				}

				ImGui::Text(mu::str_tmpf("Total Systems: {}", world.sysmon.systems.size()).c_str());
				ImGui::Text(mu::str_tmpf("Enabled: {}", enabled_count).c_str());
				ImGui::Text(mu::str_tmpf("Total Latency: {}", total_latency).c_str());
				ImGui::Text(mu::str_tmpf("Max Latest Avg: {}", max_latency).c_str());

				for (auto& sysinfo : world.sysmon.systems) {
					if (ImGui::TreeNode(sysinfo.name.c_str())) {
						ImGui::Text(mu::str_tmpf("latency (micros): last {}, avg {}, min {}, max {}",
							sysinfo.latency_micros, sysinfo.latency_micros_avg, sysinfo.latency_micros_min, sysinfo.latency_micros_max).c_str());
						ImGui::Checkbox("enabled", &sysinfo.enabled);

						ImGui::TreePop();
					}
				}

				ImGui::TreePop();
			}

			ImGui::Separator();
			ImGui::Text("Scenery");

			if (ImGui::BeginCombo("##scenery.name", world.scenery.scenery_template.name.c_str())) {
				for (const auto& [name, scenery_template] : world.scenery_templates) {
					if (ImGui::Selectable(name.c_str(), scenery_template.name == world.scenery.scenery_template.name)) {
						world.scenery.scenery_template = scenery_template;
						world.scenery.should_be_loaded = true;
					}
				}

				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload")) {
				world.scenery.should_be_loaded = true;
			}

			std::function<void(Field&,bool)> render_field_imgui;
			render_field_imgui = [&render_field_imgui, current_angle_max=world.settings.current_angle_max](Field& field, bool is_root) {
				if (ImGui::TreeNode(mu::str_tmpf("Field {}", field.name).c_str())) {
					MyImGui::EnumsCombo("ID", &field.id, {
						{FieldID::NONE, "NONE"},
						{FieldID::RUNWAY, "RUNWAY"},
						{FieldID::TAXIWAY, "TAXIWAY"},
						{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
						{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
						{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
						{FieldID::TOWER, "TOWER"},
						{FieldID::VIEW_POINT, "VIEW_POINT"},
					});

					MyImGui::EnumsCombo("Default Area", &field.default_area, {
						{AreaKind::LAND, "LAND"},
						{AreaKind::WATER, "WATER"},
						{AreaKind::NOAREA, "NOAREA"},
					});
					ImGui::ColorEdit3("Sky Color", glm::value_ptr(field.sky_color));
					ImGui::ColorEdit3("GND Color", glm::value_ptr(field.ground_color));
					ImGui::Checkbox("GND Specular", &field.ground_specular);

					ImGui::Checkbox("Visible", &field.visible);

					ImGui::DragFloat3("Translation", glm::value_ptr(field.translation));
					MyImGui::SliderAngle3("Rotation", &field.rotation, current_angle_max);

					ImGui::BulletText("Sub Fields:");
					for (auto& subfield : field.subfields) {
						render_field_imgui(subfield, false);
					}

					ImGui::BulletText("TerrMesh: %d", (int)field.terr_meshes.size());
					for (auto& terr_mesh : field.terr_meshes) {
						if (ImGui::TreeNode(terr_mesh.name.c_str())) {
							ImGui::Text("Tag: %s", terr_mesh.tag.c_str());

							MyImGui::EnumsCombo("ID", &terr_mesh.id, {
								{FieldID::NONE, "NONE"},
								{FieldID::RUNWAY, "RUNWAY"},
								{FieldID::TAXIWAY, "TAXIWAY"},
								{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
								{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
								{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
								{FieldID::TOWER, "TOWER"},
								{FieldID::VIEW_POINT, "VIEW_POINT"},
							});

							ImGui::Checkbox("Visible", &terr_mesh.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(terr_mesh.translation));
							MyImGui::SliderAngle3("Rotation", &terr_mesh.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Pict2: %d", (int)field.pictures.size());
					for (auto& picture : field.pictures) {
						if (ImGui::TreeNode(picture.name.c_str())) {
							MyImGui::EnumsCombo("ID", &picture.id, {
								{FieldID::NONE, "NONE"},
								{FieldID::RUNWAY, "RUNWAY"},
								{FieldID::TAXIWAY, "TAXIWAY"},
								{FieldID::AIRPORT_AREA, "AIRPORT_AREA"},
								{FieldID::ENEMY_TANK_GENERATOR, "ENEMY_TANK_GENERATOR"},
								{FieldID::FRIENDLY_TANK_GENERATOR, "FRIENDLY_TANK_GENERATOR"},
								{FieldID::TOWER, "TOWER"},
								{FieldID::VIEW_POINT, "VIEW_POINT"},
							});

							ImGui::Checkbox("Visible", &picture.visible);

							ImGui::DragFloat3("Translation", glm::value_ptr(picture.translation));
							MyImGui::SliderAngle3("Rotation", &picture.rotation, current_angle_max);

							ImGui::TreePop();
						}
					}

					ImGui::BulletText("Meshes: %d", (int)field.meshes.size());
					for (auto& mesh : field.meshes) {
						ImGui::Text("%s", mesh.name.c_str());
					}

					ImGui::BulletText("Ground Objects: %d", (int)field.gobs.size());
					for (const auto& gob : field.gobs) {
						ImGui::Text("%s", gob.name.c_str());
					}

					ImGui::TreePop();
				}
			};
			render_field_imgui(world.scenery.root_fld, true);

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Aircrafts {}:", world.aircrafts.size()).c_str());

			{
				static mu::Str aircraft_to_add = world.aircraft_templates.begin()->first;
				if (ImGui::BeginCombo("##new_aircraft", world.aircraft_templates[aircraft_to_add].short_name.c_str())) {
					for (const auto& [name, aircraft] : world.aircraft_templates) {
						if (ImGui::Selectable(name.c_str(), name == aircraft_to_add)) {
							aircraft_to_add = name;
						}
					}

					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add##aircraft")) {
					int tracked_model_index = -1;
					for (int i = 0; i < world.aircrafts.size(); i++) {
						if (world.camera.aircraft == &world.aircrafts[i]) {
							tracked_model_index = i;
							break;
						}
					}

					world.aircrafts.push_back(aircraft_new(world.aircraft_templates[aircraft_to_add]));

					if (tracked_model_index != -1) {
						world.camera.aircraft = &world.aircrafts[tracked_model_index];
					}
				}
			}

			for (int i = 0; i < world.aircrafts.size(); i++) {
				Aircraft& aircraft = world.aircrafts[i];

				AircraftTemplate* aircraft_template = nullptr;
				for (auto& [_k, a] : world.aircraft_templates) {
					if (a.short_name == aircraft.aircraft_template.short_name) {
						aircraft_template = &a;
						break;
					}
				}
				mu_assert(aircraft_template);

				if (ImGui::TreeNode(mu::str_tmpf("[{}] {}", i, aircraft_template->short_name).c_str())) {
					if (ImGui::BeginCombo("##aircraft_to_load", aircraft_template->short_name.c_str())) {
						for (const auto& [_name, aircraft_template] : world.aircraft_templates) {
							if (ImGui::Selectable(aircraft_template.short_name.c_str(), aircraft_template.short_name == world.aircrafts[i].aircraft_template.short_name)) {
								world.aircrafts[i].aircraft_template = aircraft_template;
								world.aircrafts[i].should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}
					ImGui::SameLine();

					if (ImGui::Button("Reload")) {
						world.aircrafts[i].should_be_loaded = true;
					}
					world.aircrafts[i].should_be_removed = ImGui::Button("Remove");

					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							aircraft_set_start(world.aircrafts[i], StartInfo { .name="-NULL-" });
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								aircraft_set_start(world.aircrafts[i], start_infos[start_info_index]);
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &aircraft.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(aircraft.translation));

					{
						auto current_angles = aircraft_angles(aircraft);
						glm::vec3 now_rotation {
							current_angles.roll,
							current_angles.pitch,
							current_angles.yaw,
						};
						if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
							auto right = glm::cross(current_angles.up, current_angles.front);
							float dy = now_rotation.z - current_angles.yaw;
							float dp = now_rotation.y - current_angles.pitch;
							float dr = now_rotation.x - current_angles.roll;
							glm::quat q_yaw = glm::angleAxis(dy, current_angles.up);
							glm::quat q_pitch = glm::angleAxis(dp, right);
							glm::quat q_roll = glm::angleAxis(dr, current_angles.front);
							aircraft.orientation = glm::normalize(q_roll * q_pitch * q_yaw * aircraft.orientation);
						}
					}

					{
						auto ang = aircraft_angles(aircraft);
						ImGui::BeginDisabled();
						auto x = glm::cross(ang.up, ang.front);
						ImGui::DragFloat3("right", glm::value_ptr(x));
						ImGui::DragFloat3("up", glm::value_ptr(ang.up));
						ImGui::DragFloat3("front", glm::value_ptr(ang.front));
						ImGui::EndDisabled();
					}

					ImGui::Checkbox("Render AABB", &aircraft.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(aircraft.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(aircraft.current_aabb.max));

					ImGui::Checkbox("Render Axes", &aircraft.render_axes);

					if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::Checkbox("Burner", &aircraft.engine.burner_enabled);
						ImGui::Checkbox("Brakes", &aircraft.braking);

						ImGui::SliderFloat("Landing Gear", &aircraft.landing_gear_alpha, 0, 1);
						ImGui::SliderFloat("Throttle", &aircraft.throttle, 0.0f, 1.0f);
						ImGui::DragFloat("Thrust Coeff", &aircraft.thrust_multiplier);
						ImGui::SliderFloat("Friction Coeff", &aircraft.friction_coeff, 0.0f, 1.0f);

						if (ImGui::TreeNode("Aerodynamic Coefficients")) {
							if (ImPlot::BeginPlot("Aerodynamic Coefficients", {-1,0}, ImPlotFlags_Crosshairs)) {
								ImPlot::SetupAxes("AoA", "C", ImPlotAxisFlags_AutoFit);

								mu::Arr<glm::vec2, 1001> p;
								for (int i = 0; i < p.size(); i++) {
									p[i].x = -180 + (i / float(p.size())) * 360;
								}

								for (int i = 0; i < p.size(); i++) {
									p[i].y = aircraft_calc_drag_coeff(aircraft, p[i].x);
								}
								ImPlot::PlotLine("Cd", &p[0].x, &p[0].y, p.size(), 0, 0, sizeof(glm::vec2));

								for (int i = 0; i < p.size(); i++) {
									p[i].y = aircraft_calc_lift_coeff(aircraft, p[i].x);
								}
								ImPlot::PlotLine("Cl", &p[0].x, &p[0].y, p.size(), 0, 0, sizeof(glm::vec2));

								float aoa = aircraft_angle_of_attack(aircraft);
								ImPlot::PlotInfLines("AoA", &aoa, 1);

								ImPlot::EndPlot();
							}

							ImGui::DragFloat("Cd.x", &aircraft.cd_consts.x, 0.0001, 0, 0.08);
							ImGui::DragFloat("Cd.y", &aircraft.cd_consts.y, 0.1);
							ImGui::DragFloat("Cd.z", &aircraft.cd_consts.z, 0.1);
							ImGui::Spacing();
							aircraft.cl_consts.quad_funcs_dirty = false;
							aircraft.cl_consts.quad_funcs_dirty |= ImGui::DragFloat("Cl.AoA_crit-", &aircraft.cl_consts.aoa_crit_neg, 5, -100, 0);
							aircraft.cl_consts.quad_funcs_dirty |= ImGui::DragFloat("Cl.AoA_crit+", &aircraft.cl_consts.aoa_crit_pos, 5, 0, 100);

							ImGui::TreePop();
						}

						ImGui::BeginDisabled();
							ImGui::SliderFloat("Engine Speed %%", &aircraft.engine.speed_percent, 0.0f, 1.0f);
							ImGui::DragFloat("Engine MAX power", &aircraft.engine.max_power);
							ImGui::DragFloat("Engine IDLE power", &aircraft.engine.idle_power);

							auto accel = glm::length(aircraft.acceleration);
							auto vel = glm::length(aircraft.velocity);
							ImGui::DragFloat("Acceleration", &accel);
							ImGui::DragFloat("Velocity", &vel);

						ImGui::EndDisabled();

						ImGui::Text("Forces (mega-newtons)");
						ImGui::Checkbox("Render Total", &aircraft.render_total_force);
						ImGui::BeginDisabled();
							MyImGui::SliderMultiplier("Thrust", &aircraft.forces.thrust, 1);
							MyImGui::SliderMultiplier("Drag", &aircraft.forces.drag, 1);
							MyImGui::SliderMultiplier("Airlift", &aircraft.forces.airlift, 1);
							MyImGui::SliderMultiplier("Weight", &aircraft.forces.weight, 1);
						ImGui::EndDisabled();

						ImGui::Text("Fuel Consumption (kg/s)");
						ImGui::DragFloat("Mil", &aircraft.engine.fuel_mili, 0.05f, 0, 50);
						ImGui::DragFloat("AB", &aircraft.engine.fuel_abrn, 0.05f, 0, 50);

						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Mass (tons)")) {
						ImGui::DragFloat("Clean", &aircraft.mass.clean, 0.05);
						ImGui::DragFloat("Fuel", &aircraft.mass.fuel, 0.05);
						ImGui::DragFloat("Load", &aircraft.mass.load, 0.05);

						ImGui::BeginDisabled();
						float eff_rate = aircraft.engine.fuel_mili + (aircraft.engine.burner_enabled ? aircraft.engine.fuel_abrn : 0);
						float burn_rate_kgs = aircraft.engine.cutoff ? 0.0f : eff_rate * aircraft.engine.speed_percent;
						ImGui::DragFloat("Burn Rate (kg/s)", &burn_rate_kgs);
						ImGui::EndDisabled();

						ImGui::BeginDisabled();
						auto total = aircraft_mass_total(aircraft);
						ImGui::DragFloat("Total", &total);
						ImGui::EndDisabled();

						ImGui::TreePop();
					}

					size_t light_sources_count = 0;
					meshes_foreach(aircraft.model.meshes, [&](const Mesh& mesh) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
						return true;
					});

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", aircraft.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&aircraft, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.translation));
							MyImGui::SliderAngle3("rotation", &mesh.rotation, current_angle_max);

							ImGui::Text(mu::str_tmpf("{}", mesh.animation_type).c_str());

							ImGui::BulletText(mu::str_tmpf("Children: ({})", mesh.children.size()).c_str());
							ImGui::Indent();
							for (auto& child : mesh.children) {
								render_mesh_ui(child);
							}
							ImGui::Unindent();

							if (ImGui::TreeNode(mu::str_tmpf("Faces: ({})", mesh.faces.size()).c_str())) {
								for (size_t i = 0; i < mesh.faces.size(); i++) {
									if (ImGui::TreeNode(mu::str_tmpf("{}", i).c_str())) {
										ImGui::TextWrapped("Vertices: %s", mu::str_tmpf("{}", mesh.faces[i].vertices_ids).c_str());

										bool changed = false;
										changed = changed || ImGui::DragFloat3("center", glm::value_ptr(mesh.faces[i].center), 0.1, -1, 1);
										changed = changed || ImGui::DragFloat3("normal", glm::value_ptr(mesh.faces[i].normal), 0.1, -1, 1);
										changed = changed || ImGui::ColorEdit4("color", glm::value_ptr(mesh.faces[i].color));
										if (changed) {
											for (auto& mesh : aircraft.model.meshes) {
												mesh_unload_from_gpu(mesh);
												mesh_load_to_gpu(mesh);
											}
										}

										ImGui::TreePop();
									}
								}

								ImGui::TreePop();
							}

							ImGui::TreePop();
						}
					};

					ImGui::Indent();
					for (auto& child : aircraft.model.meshes) {
						render_mesh_ui(child);
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}

			ImGui::Separator();
			ImGui::Text(mu::str_tmpf("Ground Objs {}:", world.ground_objs.size()).c_str());

			{
				static mu::Str gro_obj_to_add = world.ground_obj_templates.begin()->first;
				if (ImGui::BeginCombo("##new_ground_obj", world.ground_obj_templates[gro_obj_to_add].short_name.c_str())) {
					for (const auto& [name, _gro] : world.ground_obj_templates) {
						if (ImGui::Selectable(name.c_str(), name == gro_obj_to_add)) {
							gro_obj_to_add = name;
						}
					}

					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add##gro_obj")) {
					world.ground_objs.push_back(ground_obj_new(world.ground_obj_templates[gro_obj_to_add], {}, {}));
				}
			}

			for (int i = 0; i < world.ground_objs.size(); i++) {
				auto& gro = world.ground_objs[i];

				GroundObjTemplate* gro_obj_template = nullptr;
				for (auto& [_k, a] : world.ground_obj_templates) {
					if (a.short_name == gro.ground_obj_template.short_name) {
						gro_obj_template = &a;
						break;
					}
				}
				mu_assert(gro_obj_template);

				if (ImGui::TreeNode(mu::str_tmpf("[{}] {}", i, gro_obj_template->short_name).c_str())) {
					if (ImGui::BeginCombo("Name", gro.ground_obj_template.short_name.c_str())) {
						for (const auto& [name, gro_obj_template] : world.ground_obj_templates) {
							if (ImGui::Selectable(name.c_str(), gro_obj_template.short_name == gro.ground_obj_template.short_name)) {
								gro.ground_obj_template = gro_obj_template;
								gro.should_be_loaded = true;
							}
						}

						ImGui::EndCombo();
					}

					gro.should_be_loaded = ImGui::Button("Reload");
					gro.should_be_removed = ImGui::Button("Remove");

					static size_t start_info_index = 0;
					const auto& start_infos = world.scenery.start_infos;
					if (ImGui::BeginCombo("Start Pos", start_info_index == -1? "-NULL-" : start_infos[start_info_index].name.c_str())) {
						if (ImGui::Selectable("-NULL-", -1 == start_info_index)) {
							start_info_index = -1;
							gro.translation = {};
						}
						for (size_t j = 0; j < start_infos.size(); j++) {
							if (ImGui::Selectable(start_infos[j].name.c_str(), j == start_info_index)) {
								start_info_index = j;
								gro.translation = start_infos[start_info_index].position;
							}
						}

						ImGui::EndCombo();
					}

					ImGui::Checkbox("visible", &gro.visible);
					ImGui::DragFloat3("translation", glm::value_ptr(gro.translation));

					glm::vec3 now_rotation {
						gro.angles.roll,
						gro.angles.pitch,
						gro.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							gro.angles,
							now_rotation.z - gro.angles.yaw,
							now_rotation.y - gro.angles.pitch,
							now_rotation.x - gro.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(gro.angles.up, gro.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(gro.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(gro.angles.front));
					ImGui::EndDisabled();

					ImGui::DragFloat("Speed", &gro.speed, 0.05f, MIN_SPEED, MAX_SPEED);

					ImGui::Checkbox("Render AABB", &gro.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(gro.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(gro.current_aabb.max));

					size_t light_sources_count = 0;
					meshes_foreach(gro.model.meshes, [&](const Mesh& mesh) {
						if (mesh.is_light_source) {
							light_sources_count++;
						}
						return true;
					});

					ImGui::BulletText(mu::str_tmpf("Meshes: (root: {}, light: {})", gro.model.meshes.size(), light_sources_count).c_str());

					std::function<void(Mesh&)> render_mesh_ui;
					render_mesh_ui = [&gro, &render_mesh_ui, current_angle_max=world.settings.current_angle_max](Mesh& mesh) {
						if (ImGui::TreeNode(mu::str_tmpf("{}", mesh.name).c_str())) {
							ImGui::Checkbox("light source", &mesh.is_light_source);
							ImGui::Checkbox("visible", &mesh.visible);

							ImGui::Checkbox("POS Gizmos", &mesh.render_pos_axis);
							ImGui::Checkbox("CNT Gizmos", &mesh.render_cnt_axis);

							ImGui::BeginDisabled();
								ImGui::DragFloat3("CNT", glm::value_ptr(mesh.cnt), 5, 0, 180);
							ImGui::EndDisabled();

							ImGui::DragFloat3("translation", glm::value_ptr(mesh.translation));
							MyImGui::SliderAngle3("rotation", &mesh.rotation, current_angle_max);

							ImGui::Text(mu::str_tmpf("{}", mesh.animation_type).c_str());

							ImGui::BulletText(mu::str_tmpf("Children: ({})", mesh.children.size()).c_str());
							ImGui::Indent();
							for (auto& child : mesh.children) {
								render_mesh_ui(child);
							}
							ImGui::Unindent();

							if (ImGui::TreeNode(mu::str_tmpf("Faces: ({})", mesh.faces.size()).c_str())) {
								for (size_t i = 0; i < mesh.faces.size(); i++) {
									if (ImGui::TreeNode(mu::str_tmpf("{}", i).c_str())) {
										ImGui::TextWrapped("Vertices: %s", mu::str_tmpf("{}", mesh.faces[i].vertices_ids).c_str());

										bool changed = false;
										changed = changed || ImGui::DragFloat3("center", glm::value_ptr(mesh.faces[i].center), 0.1, -1, 1);
										changed = changed || ImGui::DragFloat3("normal", glm::value_ptr(mesh.faces[i].normal), 0.1, -1, 1);
										changed = changed || ImGui::ColorEdit4("color", glm::value_ptr(mesh.faces[i].color));
										if (changed) {
											for (auto& mesh : gro.model.meshes) {
												mesh_unload_from_gpu(mesh);
												mesh_load_to_gpu(mesh);
											}
										}

										ImGui::TreePop();
									}
								}

								ImGui::TreePop();
							}

							ImGui::TreePop();
						}
					};

					ImGui::Indent();
					for (auto& child : gro.model.meshes) {
						render_mesh_ui(child);
					}
					ImGui::Unindent();

					ImGui::TreePop();
				}
			}
		}
		ImGui::End();
	}

	void loop_timer_update(World& world) {
		DEF_SYSTEM

		auto& self = world.loop_timer;
		auto& settings = world.settings;

		auto now_millis = time_now_millis();
		const uint64_t delta_time_millis = now_millis - self._last_time_millis;
		self._last_time_millis = now_millis;

		if (settings.should_limit_fps) {
			int millis_diff = (1000 / settings.fps_limit) - delta_time_millis;
			self._millis_till_render = clamp<int64_t>(self._millis_till_render - millis_diff, 0, 1000);
			if (self._millis_till_render > 0) {
				self.ready = false;
				return;
			} else {
				self._millis_till_render = 1000 / settings.fps_limit;
				self.delta_time = 1.0f/ settings.fps_limit;
			}
		} else {
			self.delta_time = (double) delta_time_millis / 1000;
		}

		if (self.delta_time < 0.0001f) {
			self.delta_time = 0.0001f;
		}

		self.ready = true;
	}

	void audio_init(World& world) {
		DEF_SYSTEM

		audio_device_init(&world.audio_device);

		// load audio buffers
		auto file_paths = mu::dir_list_files_with(ASSETS_DIR "/sound", [](const auto& s) { return s.ends_with(".wav"); });
		for (const auto& file_path : file_paths) {
			auto base = mu::file_get_base_name(file_path);
			base.remove_suffix(mu::StrView(".wav").length());

			world.audio_buffers[mu::Str(base)] = audio_buffer_from_wav(file_path);
		}
	}

	void audio_free(World& world) {
		DEF_SYSTEM

		for (auto& [_, buf] : world.audio_buffers) {
			audio_buffer_free(buf);
		}

		audio_device_free(world.audio_device);
	}

	void events_collect(World& world) {
		DEF_SYSTEM

		auto& self = world.events;

		self = {};

		const Uint8* sdl_keyb_pressed = SDL_GetKeyboardState(nullptr);
		self.stick_right           = sdl_keyb_pressed[SDL_SCANCODE_RIGHT];
		self.stick_left            = sdl_keyb_pressed[SDL_SCANCODE_LEFT];
		self.stick_front           = sdl_keyb_pressed[SDL_SCANCODE_UP];
		self.stick_back            = sdl_keyb_pressed[SDL_SCANCODE_DOWN];
		self.rudder_right          = sdl_keyb_pressed[SDL_SCANCODE_C];
		self.rudder_left           = sdl_keyb_pressed[SDL_SCANCODE_Z];
		self.throttle_increase     = sdl_keyb_pressed[SDL_SCANCODE_Q];
		self.throttle_decrease     = sdl_keyb_pressed[SDL_SCANCODE_A];

		self.camera_tracking_up    = sdl_keyb_pressed[SDL_SCANCODE_U];
		self.camera_tracking_down  = sdl_keyb_pressed[SDL_SCANCODE_M];
		self.camera_tracking_right = sdl_keyb_pressed[SDL_SCANCODE_K];
		self.camera_tracking_left  = sdl_keyb_pressed[SDL_SCANCODE_H];

		self.camera_flying_up      = sdl_keyb_pressed[SDL_SCANCODE_W];
		self.camera_flying_down    = sdl_keyb_pressed[SDL_SCANCODE_S];
		self.camera_flying_right   = sdl_keyb_pressed[SDL_SCANCODE_D];
		self.camera_flying_left    = sdl_keyb_pressed[SDL_SCANCODE_A];

		bool space_pressed = sdl_keyb_pressed[SDL_SCANCODE_SPACE];
		self.mouse_plane_control_enabled = !space_pressed;

		if (space_pressed && SDL_GetRelativeMouseMode()) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
		} else if (!space_pressed && !SDL_GetRelativeMouseMode()) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
		}

		Uint32 sdl_mouse_state = SDL_GetMouseState(&self.mouse_pos.x, &self.mouse_pos.y);
		self.camera_flying_rotate_enabled = sdl_mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_MOUSEMOTION && self.mouse_plane_control_enabled) {
				self.mouse_dx += event.motion.xrel;
				self.mouse_dy += event.motion.yrel;
			} else if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					signal_fire(world.signals.quit);
					break;
				case SDLK_TAB:
					self.afterburner_toggle = true;
					break;
				case 'g':
					self.landing_gear_toggle = true;
					break;
				case 'b':
					self.brake = true;
					break;
				case SDLK_F10:
					self.camera_cycle = true;
					break;
				case 'f':
					world.settings.fullscreen = !world.settings.fullscreen;
					signal_fire(world.signals.wnd_configs_changed);
					if (world.settings.fullscreen) {
						if (SDL_SetWindowFullscreen(world.sdl_window, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
							mu::panic(SDL_GetError());
						}
					} else {
						if (SDL_SetWindowFullscreen(world.sdl_window, SDL_WINDOW_OPENGL)) {
							mu::panic(SDL_GetError());
						}
					}
					break;
				default:
					break;
				}
			} else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				signal_fire(world.signals.wnd_configs_changed);
			} else if (event.type == SDL_QUIT) {
				signal_fire(world.signals.quit);
			}
		}
	}

}

int main(int argc, char* argv[]) {
	bool run_tests = false;
	for (int i = 1; i < argc; i++) {
		if (argv[i] == mu::StrView("--test")) {
			run_tests = true;
			break;
		}
	}

	if (run_tests) {
		test_parser();
		test_aabbs_intersection();
		test_polygons_to_triangles();
		test_line_segments_to_lines();
		test_rotational_physics();
		return 0;
	}

	World world {};
	mu::log_global_logger = (mu::ILogger*) &world.imgui_window_logger;

	sys::sdl_init(world);
	mu_defer(sys::sdl_free(world));

	sys::projection_init(world);

	sys::imgui_init(world);
	mu_defer(sys::imgui_free(world));

	sys::canvas_init(world);
	mu_defer(sys::canvas_free(world));

	sys::audio_init(world);
	mu_defer(sys::audio_free(world));

	sys::scenery_init(world);
	mu_defer(sys::scenery_free(world));

	sys::aircrafts_init(world);
	mu_defer(sys::aircrafts_free(world));

	sys::ground_objs_init(world);
	mu_defer(sys::ground_objs_free(world));

	signal_listen(world.signals.quit);
	while (!signal_handle(world.signals.quit)) {
		sys::loop_timer_update(world);
		if (!world.loop_timer.ready) {
			time_delay_millis(2);
			continue;
		}
		TEXT_OVERLAY("fps: {:.2f}", 1.0f/world.loop_timer.delta_time);

		sys::events_collect(world);

		sys::projection_update(world);
		sys::camera_update(world);
		sys::cached_matrices_recalc(world);

		sys::scenery_update(world);
		sys::scenery_prepare_render(world);

		sys::aircrafts_update(world);
		sys::aircrafts_prepare_render(world);

		sys::ground_objs_update(world);
		sys::ground_objs_prepare_render(world);

		sys::models_handle_collision(world);

		sys::canvas_rendering_begin(world); {
			sys::canvas_render_ground(world);
			sys::canvas_render_gnd_pictures(world);
			sys::canvas_render_zlpoints(world);
			sys::canvas_render_meshes(world);
			sys::canvas_render_axes(world);
			sys::canvas_render_lines(world);
			sys::canvas_render_text(world);
			sys::canvas_render_hud_text(world);
			if (world.settings.hud.geoms_demo) {
				auto& c = world.canvas;
				canvas_add(c, canvas::hud::Circle{{0.5f, 0.5f}, 0.4f, {0,1,1,0.8f}});
				canvas_add(c, canvas::hud::Circle{{0.2f, 0.2f}, 0.15f, {1,1,0,0.6f}});

				canvas_add(c, canvas::hud::Line{{0.05f,0.05f}, {0.45f,0.45f}, {0,1,0,0.8f}});
				canvas_add(c, canvas::hud::Line{{0.55f,0.05f}, {0.95f,0.45f}, {0,1,0,0.5f}});

				canvas_add(c, canvas::hud::LineStrip{
					{1,0,0,0.8f},
					{{0.05f,0.7f}, {0.25f,0.85f}, {0.45f,0.7f}, {0.65f,0.85f}, {0.85f,0.7f}}
				});

				canvas_add(c, canvas::hud::FilledArc{{0.7f, 0.7f}, 0.2f, 0.0f, glm::pi<float>(), {0.5f,0,0.5f,0.7f}});
				canvas_add(c, canvas::hud::FilledArc{{0.7f, 0.7f}, 0.12f, -glm::pi<float>()/2.0f, glm::pi<float>()/2.0f, {1,0.5f,0,0.7f}});
				canvas_add(c, canvas::hud::FilledArc{{0.7f, 0.7f}, 0.06f, 0.0f, 2.0f*glm::pi<float>(), {1,1,1,0.7f}});

				canvas_add(c, canvas::hud::FilledTriangle{{0.2f,0.55f}, {0.4f,0.95f}, {0.5f,0.6f}, {0,1,0,0.6f}});
				canvas_add(c, canvas::hud::FilledTriangle{{0.55f,0.2f}, {0.65f,0.45f}, {0.9f,0.15f}, {0,0.5f,1,0.6f}});
			}
			sys::canvas_render_hud_geoms(world);

			sys::imgui_rendering_begin(world); {
				sys::imgui_debug_window(world);
				sys::imgui_logs_window(world);
				sys::imgui_overlay_text(world);
			}
			sys::imgui_rendering_end(world);
		}
		sys::canvas_rendering_end(world);

		mu::memory::reset_tmp();
	}

	return 0;
}
