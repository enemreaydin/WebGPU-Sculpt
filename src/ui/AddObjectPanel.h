#pragma once

#include "scene/data/SdfObjectData.h"
#include "scene/data/SdfEnums.h"

class AddObjectPanel
{
public:
    void Open(SdfObjectType type);
    bool Draw(SdfObjectData& outObject);

private:
    bool isOpen = false;
    bool requestOpen = false;

    SdfObjectType selectedType = SdfObjectType::Sphere;

    float sphereRadius = 1.0f;
    float boxSize[3] = { 1.0f, 1.0f, 1.0f };
    float cylinderRadius = 0.7f;
    float cylinderHeight = 1.5f;
    float torusMajorRadius = 1.0f;
    float torusMinorRadius = 0.25f;
};