#pragma once

#include "scene/data/SdfObjectData.h"
#include <glm/glm.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

// Converts between SdfObjectData and the gizmo's model matrix.
class GizmoController
{
public:
    static glm::mat4 BuildModelMatrix(const SdfObjectData& obj, ImGuizmo::OPERATION mode);
    static void ApplyModelMatrix(const glm::mat4& matrix, SdfObjectData& obj, ImGuizmo::OPERATION mode);
};
