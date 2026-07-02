#pragma once

#include "../core/SdfDefinitions.h"
#include <glm/glm.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
class SdfObjectFactory
{
public:
    static SdfObjectData CreateSphere(float radius);
    static SdfObjectData CreateBox(float x, float y, float z);
    static SdfObjectData CreateCylinder(float radius, float height);
    static SdfObjectData CreateTorus(float majorRadius, float minorRadius);
    static glm::mat4 BuildModelMatrix(const SdfObjectData& obj, ImGuizmo::OPERATION mode);
    static void ApplyModelMatrix(const glm::mat4& matrix, SdfObjectData& obj, ImGuizmo::OPERATION mode);
    static float EstimateObjectRadius(const SdfObjectData& obj);
};