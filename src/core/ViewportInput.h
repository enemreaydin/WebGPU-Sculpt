#pragma once
#include "core/Vec2.h"

// Per-frame viewport input, fed in from the outside (the web front-end via the
// bindings layer, or a native event pump). The engine no longer polls ImGui for
// mouse state, so all interaction arrives through this struct.
struct ViewportInput
{
    bool  orbiting    = false;  // left-drag with no modifiers
    bool  panning     = false;  // left-drag while panning modifier held
    Vec2  mouseDelta;           // pixels moved since last frame
    float scrollDelta = 0.0f;   // wheel ticks since last frame
};
