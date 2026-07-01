#pragma once

#include <array>
#include <cstdint>

//====================================================
// SDF Primitive Types
//====================================================

enum class SdfObjectType : int32_t
{
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Torus = 3
};

//====================================================
// Boolean Operations
//====================================================

enum class SdfOperationType : int32_t
{
    None = 0,
    Union = 1,
    Subtract = 2,
    Intersection = 3
};

//====================================================
// GPU STRUCTURE
//
// WGSL ile %100 ayný hizalamaya sahiptir.
// Her vec3 yerine vec4 mantýðý kullanýlýr.
//====================================================

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
};