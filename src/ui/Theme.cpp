#include <imgui.h>
#include "Theme.h"

void ApplyTheme(AppTheme& theme) {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;
	ImGuiIO& io = ImGui::GetIO();

	theme.fontDefault = io.Fonts->AddFontFromFileTTF("fonts/Inter_18pt-Regular.ttf", 16.0f);
	theme.fontLarge = io.Fonts->AddFontFromFileTTF("fonts/Inter_18pt-Regular.ttf", 30.0f);
	// Genel
	style.FrameBorderSize = 0;
	style.ChildBorderSize = 0;
	style.WindowBorderSize = 0;
	style.PopupBorderSize = 0;

	style.FrameRounding = 2;
	colors[ImGuiCol_WindowBg] = theme.pBackground;
	colors[ImGuiCol_TitleBgActive] = theme.pBackground;
	colors[ImGuiCol_MenuBarBg] = theme.pBackground;
	colors[ImGuiCol_PopupBg] = theme.pBackground;
	// MenuBar
	colors[ImGuiCol_HeaderHovered] = theme.pHovered;
	colors[ImGuiCol_Header] = theme.pHovered;
	colors[ImGuiCol_HeaderActive] = theme.pHovered;

}
