#pragma once
#include <imgui.h>

struct AppTheme
{
	ImFont* fontDefault = nullptr;
	ImFont* fontLarge = nullptr;
	ImVec4 pBackground = ImColor(48, 48, 48, 255);
	ImVec4 pHovered = ImColor(64, 64, 64, 255);
	ImVec4 pWarningRed = ImColor(120, 42, 48, 255);
	ImVec4 border = ImColor(64, 64, 64, 255);
};

void ApplyTheme(AppTheme& theme);
