#pragma once

#include <array>
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

struct alignas(16) SdfObjectData
{
    int32_t type;
    int32_t op;

    int32_t pad0;
    int32_t pad1;

    std::array<float, 4> position;
    std::array<float, 4> size;
    std::array<float, 4> color;
};

static_assert(sizeof(SdfObjectData) == 64,
    "SdfObjectData must be exactly 64 bytes.");


struct GlobalsData {
    float resolution[2];  // x, y
    float _pad[2];        // 16 byte'a tamamla
    float cameraPos[4];
    float cameraTarget[4];
};
