#pragma once

#include "scene/data/SdfObjectData.h"
#include "scene/data/SdfEnums.h"
class SdfObjectFactory
{
public:
    static SdfObjectData CreateSphere(float radius);
    static SdfObjectData CreateBox(float x, float y, float z);
    static SdfObjectData CreateCylinder(float radius, float height);
    static SdfObjectData CreateTorus(float majorRadius, float minorRadius);
    static SdfObjectData CreateLink(float length, float radius, float thickness);
    static SdfObjectData CreateCone(float height, float angle);
    static SdfObjectData CreateCapsule(float radius, float halfHeight);
    static SdfObjectData CreateSolidAngle(float radius, float angle);
    static SdfObjectData CreatePyramid(float halfBase, float height);
};