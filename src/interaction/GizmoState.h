#pragma once

#include <glm/glm.hpp>

enum class GizmoMode
{
    Translate = 0,
    Rotate = 1,
    Scale = 2
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

    GizmoAxis hoveredAxis = GizmoAxis::None;
    GizmoAxis activeAxis = GizmoAxis::None;

    bool isDragging = false;

    glm::vec2 dragStartMouse = glm::vec2(0.0f);
    glm::vec3 dragStartObjectPosition = glm::vec3(0.0f);
};