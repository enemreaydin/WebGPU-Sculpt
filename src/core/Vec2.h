#pragma once

// Minimal 2D float pair. Replaces ImGui's ImVec2 now that the UI layer no
// longer depends on ImGui (the UI lives in the web/React front-end).
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
};
