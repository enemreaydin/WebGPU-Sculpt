#pragma once

#include <cstdint>

struct alignas(16) GlobalsData
{
    float resolution[2];  // x, y

    int32_t showGrid;     // 1 = show, 0 = hide
    int32_t selectedIndex;         // alignment padding

    float cameraPos[4];
    float cameraTarget[4];

    // Translate gizmo, drawn by the SDF fragment shader so it shares the exact
    // same frame (and camera) as the scene — no cross-surface lag. Origin and
    // axis endpoints are world-space; the shader projects them itself. The
    // endpoints already sit at the constant on-screen axis length.
    float gizmoOrigin[4];  // xyz = selected object's world position
    float gizmoAxisX[4];   // xyz = world endpoint of the +X axis handle
    float gizmoAxisY[4];   // xyz = world endpoint of the +Y axis handle
    float gizmoAxisZ[4];   // xyz = world endpoint of the +Z axis handle
    int32_t gizmoVisible;  // 1 = draw the gizmo
    int32_t gizmoHovered;  // hovered GizmoAxis (0 none, 1 X, 2 Y, 3 Z)
    int32_t gizmoActive;   // active/dragged GizmoAxis (0..3)
    int32_t gizmoPad;
};
