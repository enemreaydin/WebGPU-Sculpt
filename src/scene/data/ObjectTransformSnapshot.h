#pragma once

#include <array>
#include <cstdint>
#include <cmath>

struct ObjectTransformSnapshot
{
    int32_t objectId = -1;

    std::array<float, 4> position{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 4> size{ 1.0f, 1.0f, 1.0f, 0.0f };
};

inline bool NearlyEqual(float a, float b, float epsilon = 0.00001f)
{
    return std::fabs(a - b) <= epsilon;
}

inline bool SameFloat4(
    const std::array<float, 4>& a,
    const std::array<float, 4>& b,
    float epsilon = 0.00001f
)
{
    for (int i = 0; i < 4; ++i)
    {
        if (!NearlyEqual(a[i], b[i], epsilon))
            return false;
    }

    return true;
}

inline bool SameTransformSnapshot(
    const ObjectTransformSnapshot& a,
    const ObjectTransformSnapshot& b,
    float epsilon = 0.00001f
)
{
    return a.objectId == b.objectId
        && SameFloat4(a.position, b.position, epsilon)
        && SameFloat4(a.rotation, b.rotation, epsilon)
        && SameFloat4(a.size, b.size, epsilon);
}