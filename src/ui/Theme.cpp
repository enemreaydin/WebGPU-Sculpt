#include "Theme.h"
#include <imgui.h>

void ApplyTheme() {
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;
	ImGuiIO& io = ImGui::GetIO();


	io.Fonts->AddFontFromFileTTF("fonts/Inter_18pt-Regular.ttf", 16.0f);
	style.FrameRounding = 4;
	

}