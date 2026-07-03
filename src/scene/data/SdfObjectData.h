#pragma once

#include <array>
#include <cstdint>

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
