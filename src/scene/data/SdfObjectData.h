#pragma once

#include <array>
#include <cstdint>

struct alignas(16) SdfObjectData
{
    int32_t type;        // SdfObjectType (Sphere/Box/Cylinder/Torus...)
    int32_t objectId;    // Permanent identity so CSG tree nodes can reference this
                         // primitive even after vector reordering/deletion. Assigned
                         // from an ever-increasing counter, never reused, never equal
                         // to the array index.
    int32_t parentId;    // Reserved for a future transform hierarchy (NOT the CSG tree).
                         // -1 = no parent. No hierarchy logic yet, field is reserved.
    int32_t pad0;

    std::array<float, 4> position;   // xyz used, w = 0

    std::array<float, 4> rotation;   // Quaternion (x,y,z,w), identity = (0,0,0,1).
                                     // Reserved for a future gizmo rotate mode.

    std::array<float, 4> size;
    // .xyz interpreted per-primitive-type (sphere: x=radius, box: xyz=half-extents, etc.)
    // .w reserved for a future primitive-specific extra parameter.

    std::array<float, 4> color;
    // .rgb = surface color
    // .a reserved for a future opacity/alpha value.
};

static_assert(sizeof(SdfObjectData) == 80,
    "SdfObjectData must be exactly 80 bytes.");
