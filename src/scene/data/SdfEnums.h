#pragma once

#include <cstdint>

enum class SdfObjectType : int32_t
{
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Torus = 3
};

enum class SdfOperationType : int32_t
{
    None = 0,
    Union = 1,
    Subtract = 2,
    Intersection = 3
};
