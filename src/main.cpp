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

constexpr float ZL_SCALE = 0.151f;

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
		SDL_Quit();
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

					glm::vec3 now_rotation {
						aircraft.angles.roll,
						aircraft.angles.pitch,
						aircraft.angles.yaw,
					};
					if (MyImGui::SliderAngle3("rotation", &now_rotation, world.settings.current_angle_max)) {
						local_euler_angles_rotate(
							aircraft.angles,
							now_rotation.z - aircraft.angles.yaw,
							now_rotation.y - aircraft.angles.pitch,
							now_rotation.x - aircraft.angles.roll
						);
					}

					ImGui::BeginDisabled();
					auto x = glm::cross(aircraft.angles.up, aircraft.angles.front);
					ImGui::DragFloat3("right", glm::value_ptr(x));
					ImGui::DragFloat3("up", glm::value_ptr(aircraft.angles.up));
					ImGui::DragFloat3("front", glm::value_ptr(aircraft.angles.front));
					ImGui::EndDisabled();

					ImGui::Checkbox("Render AABB", &aircraft.render_aabb);
					ImGui::DragFloat3("AABB.min", glm::value_ptr(aircraft.current_aabb.min));
					ImGui::DragFloat3("AABB.max", glm::value_ptr(aircraft.current_aabb.max));

					ImGui::Checkbox("Render Axes", &aircraft.render_axes);

					if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::Checkbox("Burner", &aircraft.engine.burner_enabled);

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

				uniform mat4 projection_view_model;
				uniform mat3 model_normal;

				out float vs_vertex_y;
				out vec4 vs_color;
				out vec3 vs_normal;
				out float vs_depth;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position, 1.0);
					vs_color = attr_color;
					vs_vertex_y = attr_position.y;
					vs_normal = normalize(model_normal * attr_normal);
					vs_depth = gl_Position.w;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core
				in float vs_vertex_y;
				in vec4 vs_color;
				in vec3 vs_normal;
				in float vs_depth;

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

				uniform mat4 projection_view_model;

				out float vs_vertex_id;
				out float vs_depth;

				void main() {
					gl_Position = projection_view_model * vec4(attr_position.x, 0.0, attr_position.y, 1.0);
					vs_vertex_id = gl_VertexID % 6;
					vs_depth = gl_Position.w;
				}
			)GLSL",

			// fragment shader
			R"GLSL(
				#version 330 core

				in float vs_vertex_id;
				in float vs_depth;

				uniform vec3 primitive_color[2];
				uniform bool gradient_enabled;
				uniform sampler2D groundtile;

				uniform bool fog_enabled;
				uniform float fog_density;
				uniform vec3 fog_color;

				out vec4 out_fragcolor;

				const int color_indices[6] = int[] (
					0, 1, 1,
					0, 0, 1
				);

				const vec2 tex_coords[3] = vec2[] (
					vec2(0, 0), vec2(1, 0), vec2(1, 1)
				);

				void main() {
					int color_index = 0;
					if (gradient_enabled) {
						color_index = color_indices[int(vs_vertex_id)];
					}
					out_fragcolor = texture(groundtile, tex_coords[int(vs_vertex_id) % 3]).r * vec4(primitive_color[color_index], 1.0);
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
		self.gnd_pics.list         = mu::Vec<canvas::GndPic>(&self.arena);
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

		Uint32 sdl_mouse_state = SDL_GetMouseState(&self.mouse_pos.x, &self.mouse_pos.y);
		self.camera_flying_rotate_enabled = sdl_mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					signal_fire(world.signals.quit);
					break;
				case SDLK_TAB:
					self.afterburner_toggle = true;
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

	void models_handle_collision(World& world) {
		DEF_SYSTEM

		if (!world.settings.handle_collision) {
			return;
		}

		// adhoc entity query
		struct Entity {
			AABB* aabb;
			const char* name;
			bool render_aabb, visible, is_aircraft, collided;
		};
		mu::Vec<Entity> e(mu::memory::tmp());
		for (auto& a : world.aircrafts) {
			e.push_back(Entity {
				.aabb = &a.current_aabb,
				.name = a.aircraft_template.short_name.c_str(),
				.render_aabb = a.render_aabb,
				.visible = a.visible,
				.is_aircraft = true,
				.collided = false,
			});
		}
		for (auto& g : world.ground_objs) {
			e.push_back(Entity {
				.aabb = &g.current_aabb,
				.name = g.ground_obj_template.short_name.c_str(),
				.render_aabb = g.render_aabb,
				.visible = g.visible,
				.is_aircraft = false,
				.collided = false,
			});
		}

		// test collision
		for (uint32_t i = 0; e.size() > 1 && i < e.size()-1 && e[i].is_aircraft; i++) {
			if (e[i].visible == false) {
				continue;
			}

			for (uint32_t j = i+1; j < e.size(); j++) {
				if (e[j].visible && aabbs_intersect(*e[i].aabb, *e[j].aabb)) {
					e[i].collided = true;
					e[j].collided = true;
					TEXT_OVERLAY("{}[air] collided with {}[{}]", e[i].name, e[j].name, e[j].is_aircraft ? "air":"gro");
				}
			}
		}

		// render boxes as lines (12 edges per aabb)
		constexpr glm::vec3 RED {1,0,0};
		constexpr glm::vec3 BLU {0,0,1};
		for (int i = 0; i < e.size(); i++) {
			if (e[i].visible && e[i].render_aabb) {
				canvas_add(world.canvas, canvas::Box {
					.translation = e[i].aabb->min,
					.scale = e[i].aabb->max - e[i].aabb->min,
					.color = e[i].collided ? RED : BLU,
				});
			}
		}
	}

	void scenery_init(World& world) {
		DEF_SYSTEM

		world.scenery_templates = scenery_templates_from_dir(ASSETS_DIR "/scenery");
		world.scenery = scenery_new(world.scenery_templates["SMALL_MAP"]);
	}

	void scenery_free(World& world) {
		DEF_SYSTEM

		field_unload_from_gpu(world.scenery.root_fld);
	}

	void scenery_update(World& world) {
		DEF_SYSTEM

		auto& self = world.scenery;
		const auto all_fields = field_list_recursively(self.root_fld, mu::memory::tmp());

		if (self.should_be_loaded) {
			scenery_unload(self);
			scenery_load(self);
			signal_fire(world.signals.scenery_loaded);
		}

		if (self.root_fld.should_be_transformed) {
			self.root_fld.should_be_transformed = false;

			// transform fields
			self.root_fld.transformation = glm::identity<glm::mat4>();
			for (Field* fld : all_fields) {
				if (fld->visible == false) {
					continue;
				}

				fld->transformation = glm::translate(fld->transformation, fld->translation);
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[2], glm::vec3{0, 0, 1});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[1], glm::vec3{1, 0, 0});
				fld->transformation = glm::rotate(fld->transformation, fld->rotation[0], glm::vec3{0, 1, 0});

				for (auto& subfield : fld->subfields) {
					subfield.transformation = fld->transformation;
				}

				meshes_foreach(fld->meshes, [&](Mesh& mesh) {
					if (mesh.render_cnt_axis) {
						canvas_add(world.canvas, canvas::Axis { glm::translate(glm::identity<glm::mat4>(), mesh.cnt) });
					}

					// apply mesh transformation
					mesh.transformation = fld->transformation;
					mesh.transformation = glm::translate(mesh.transformation, mesh.translation);
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[2], glm::vec3{0, 0, 1});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[1], glm::vec3{1, 0, 0});
					mesh.transformation = glm::rotate(mesh.transformation, mesh.rotation[0], glm::vec3{0, 1, 0});

					if (mesh.render_pos_axis) {
						canvas_add(world.canvas, canvas::Axis { mesh.transformation });
					}

					return true;
				});
			}
		}
	}

	void scenery_prepare_render(World& world) {
		DEF_SYSTEM

		const auto all_fields = field_list_recursively(world.scenery.root_fld, mu::memory::tmp());

		for (const Field* fld : all_fields) {
			if (fld->visible == false) {
				continue;
			}

			// ground
			canvas_add(world.canvas, canvas::Ground {
				.color = fld->ground_color,
			});

			// pictures
			for (const auto& picture : fld->pictures) {
				if (picture.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, picture.translation);
				model_transformation = glm::rotate(model_transformation, picture.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, picture.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, picture.rotation[0], glm::vec3{0, 1, 0});

				auto gnd_pic = canvas::GndPic {
					.projection_view_model = world.mats.projection_view * model_transformation,
					.list_primitives = mu::Vec<canvas::GndPic::Primitive>(&world.canvas.arena),
				};

				for (const auto& primitive : picture.primitives) {
					GLenum gl_primitive_type;
					switch (primitive.kind) {
					case Primitive2D::Kind::POINTS:
						gl_primitive_type = GL_POINTS;
						break;
					case Primitive2D::Kind::LINES:
						gl_primitive_type = GL_LINES;
						break;
					case Primitive2D::Kind::LINE_SEGMENTS:
						gl_primitive_type = GL_LINE_STRIP;
						break;
					case Primitive2D::Kind::TRIANGLES:
					case Primitive2D::Kind::QUAD_STRIPS:
					case Primitive2D::Kind::QUADRILATERAL:
					case Primitive2D::Kind::POLYGON:
					case Primitive2D::Kind::GRADATION_QUAD_STRIPS:
						gl_primitive_type = GL_TRIANGLES;
						break;
					default: mu_unreachable();
					}

					gnd_pic.list_primitives.push_back(canvas::GndPic::Primitive {
						.vao = primitive.gl_buf.vao,
						.buf_len = primitive.gl_buf.len,
						.gl_primitive_type = gl_primitive_type,

						.color = primitive.color,
						.gradient_enabled = primitive.kind == Primitive2D::Kind::GRADATION_QUAD_STRIPS,
						.gradient_color2 = primitive.gradient_color2,
					});
				}

				canvas_add(world.canvas, std::move(gnd_pic));
			}

			// terrains
			for (const auto& terr_mesh : fld->terr_meshes) {
				if (terr_mesh.visible == false) {
					continue;
				}

				auto model_transformation = fld->transformation;
				model_transformation = glm::translate(model_transformation, terr_mesh.translation);
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[2], glm::vec3{0, 0, 1});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[1], glm::vec3{1, 0, 0});
				model_transformation = glm::rotate(model_transformation, terr_mesh.rotation[0], glm::vec3{0, 1, 0});

				if (terr_mesh.gradient.enabled) {
					canvas_add(world.canvas, canvas::GradientMesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,
						.model_normal = glm::transpose(glm::inverse(glm::mat3(model_transformation))),

						.gradient_bottom_y = terr_mesh.gradient.bottom_y,
						.gradient_top_y = terr_mesh.gradient.top_y,
						.gradient_bottom_color = terr_mesh.gradient.bottom_color,
						.gradient_top_color = terr_mesh.gradient.top_color,
					});
				} else {
					canvas_add(world.canvas, canvas::Mesh {
						.vao = terr_mesh.gl_buf.vao,
						.buf_len = terr_mesh.gl_buf.len,
						.projection_view_model = world.mats.projection_view * model_transformation,
						.model_normal = glm::transpose(glm::inverse(glm::mat3(model_transformation)))
					});
				}
			}

			// meshes
			meshes_foreach(fld->meshes, [&](const Mesh& mesh) {
				if (mesh.visible == false) {
					return false;
				}

				canvas_add(world.canvas, canvas::Mesh {
					.vao = mesh.gl_buf.vao,
					.buf_len = mesh.gl_buf.len,
					.projection_view_model =
						world.mats.projection_view
						* mesh.transformation
						* fld->transformation,
					.model_normal = glm::transpose(glm::inverse(
						glm::mat3(mesh.transformation * fld->transformation)))
				});

				return true;
			});
		}
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

		// regular
		for (const auto& mesh : world.canvas.meshes.list_regular) {
			gl_program_uniform_set(world.canvas.meshes.program, "projection_view_model", mesh.projection_view_model);
			gl_program_uniform_set(world.canvas.meshes.program, "model_normal", mesh.model_normal);
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

				glBindVertexArray(primitives.vao);
				glDrawArrays(primitives.gl_primitive_type, 0, primitives.buf_len);
			}
		}

		glEnable(GL_DEPTH_TEST);
	}
}

int main() {
	World world {};
	mu::log_global_logger = (mu::ILogger*) &world.imgui_window_logger;

	test_parser();
	test_aabbs_intersection();
	test_polygons_to_triangles();
	test_line_segments_to_lines();

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
