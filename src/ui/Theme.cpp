#include "Theme.h"
#include <imgui.h>

void ApplyTheme() {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;
	ImGuiIO& io = ImGui::GetIO();

	// Genel
	io.Fonts->AddFontFromFileTTF("fonts/Inter_18pt-Regular.ttf", 16.0f);
	style.FrameRounding = 4;
	colors[ImGuiCol_WindowBg] = ImColor(40, 40, 40, 255);
	colors[ImGuiCol_TitleBgActive] = ImColor(40, 40, 40, 255);

	// MenuBar
	colors[ImGuiCol_MenuBarBg] = ImColor(40, 40, 40, 255);
	colors[ImGuiCol_PopupBg] = ImColor(40, 40, 40, 255);
	colors[ImGuiCol_HeaderHovered] = ImColor(64, 64, 64, 255);
	colors[ImGuiCol_Header] = ImColor(64, 64, 64, 255);
	colors[ImGuiCol_HeaderActive] = ImColor(64, 64, 64, 255);

}

ImVec4 RGB(int r, int g, int b, float a = 1.0f) {
	return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}