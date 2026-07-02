#pragma once
#include "../ui/Theme.h"

struct Settings
{
	// Settings screen
	bool show = false;
	int selectedTab = 0;

	// App settings
	AppTheme appTheme;

	// Camera settings
	float orbitSensitivity = 0.005f;
	float zoomSensitivity = 0.15f;
	float panSensitivity = 0.05f;
};