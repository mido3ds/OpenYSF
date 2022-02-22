#pragma once

#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "math.hpp"

namespace MyImGui {
	template<typename T>
	void EnumsCombo(const char* label, T* p_enum, const std::initializer_list<std::pair<T, const char*>>& enums) {
		int var_i = -1;
		const char* preview = "- Invalid Value -";
		for (const auto& [type, type_str] : enums) {
			var_i++;
			if (type == *p_enum) {
				preview = type_str;
				break;
			}
		}

		if (ImGui::BeginCombo(label, preview)) {
			for (const auto& [type, type_str] : enums) {
				if (ImGui::Selectable(type_str,  type == *p_enum)) {
					*p_enum = type;
				}
			}

			ImGui::EndCombo();
		}
	}

	void SliderAngle(const char* label, float* radians, float angle_max) {
		float angle = *radians / RADIANS_MAX * angle_max;
		ImGui::DragFloat(label, &angle, 0.01f * angle_max, -angle_max, angle_max);
		*radians = angle / angle_max * RADIANS_MAX;
	}

	void SliderAngle3(const char* label, glm::vec3* radians, float angle_max) {
		glm::vec3 angle = *radians / RADIANS_MAX * angle_max;
		ImGui::DragFloat3(label, glm::value_ptr(angle), 0.01f * angle_max, -angle_max, angle_max);
		*radians = angle / angle_max * RADIANS_MAX;
	}
}
