#pragma once

#include "../core/SdfDefinitions.h"

class SdfObjectFactory
{
public:
    static SdfObjectData CreateSphere(float radius);
    static SdfObjectData CreateBox(float x, float y, float z);
    static SdfObjectData CreateCylinder(float radius, float height);
    static SdfObjectData CreateTorus(float majorRadius, float minorRadius);
};