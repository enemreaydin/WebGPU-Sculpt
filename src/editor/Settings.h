#pragma once

// Engine-side settings. UI-related state (which settings tab is open, theme,
// fonts) now lives in the web front-end; only values the C++ engine actually
// consumes remain here.
struct Settings
{
    // Camera settings
    float orbitSensitivity = 0.005f;
    float zoomSensitivity  = 0.15f;
    float panSensitivity   = 0.025f;
};
