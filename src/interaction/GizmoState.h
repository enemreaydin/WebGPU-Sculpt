#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

enum class GizmoMode
{
    Translate = 0,
    Rotate = 1,
    Scale = 2
};

// Which frame the gizmo handles are aligned to. World = the fixed world axes;
// Local = the selected object's own axes, i.e. the world axes turned by its
// rotation quaternion. Mirrored in the web UI (engine.ts GizmoSpace).
enum class GizmoSpace
{
    World = 0,
    Local = 1
};

enum class GizmoAxis
{
    None = 0,
    X = 1,
    Y = 2,
    Z = 3
};

struct GizmoState
{
    GizmoMode mode = GizmoMode::Translate;
    GizmoSpace space = GizmoSpace::World;

    GizmoAxis hoveredAxis = GizmoAxis::None;
    GizmoAxis activeAxis = GizmoAxis::None;

    bool isDragging = false;

    glm::vec2 dragStartMouse = glm::vec2(0.0f);
    glm::vec3 dragStartObjectPosition = glm::vec3(0.0f);

    // Latched at BeginDrag. In Local space the object's frame turns while you
    // drag it, so a live axis direction would chase its own tail — the handle
    // must stay pinned to the direction it had when it was grabbed. Anchoring
    // the start rotation (rather than accumulating per-frame deltas) also keeps
    // a long drag free of float drift.
    glm::vec3 dragAxisDir = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::quat dragStartRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 dragStartSpoke = glm::vec3(0.0f);   // rotate only: first spoke of the ring
};
