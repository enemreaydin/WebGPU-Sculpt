#pragma once
#include <imgui.h>

struct AppTheme
{
	ImFont* fontDefault = nullptr;
	ImFont* fontLarge = nullptr;
	ImVec4 pBackground = ImColor(0, 30, 60, 255);
	ImVec4 pHovered = ImColor(0, 60, 140, 255);
	ImVec4 pWarningRed = ImColor(120, 0, 0, 255);
	ImVec4 border = ImColor(0, 45, 90, 255);
};

void ApplyTheme(AppTheme& theme);
