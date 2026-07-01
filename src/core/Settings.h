#pragma once
#include "../ui/Theme.h"

struct Settings
{
	// Settings screen
	bool show = false;
	int selectedTab = 0;

	// App settings
	AppTheme appTheme;
	float orbitSensitivity = 0.005f;
};