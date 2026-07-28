#include "scene/PrimitiveGeometry.h"

namespace PrimitiveGeometry
{
    void InitFromSize(SdfObjectType type,
                      const std::array<float, 3>& s,
                      PrimitiveParams& p,
                      std::array<float, 3>& scale)
    {
        scale = { 1.0f, 1.0f, 1.0f };

        switch (type)
        {
        case SdfObjectType::Sphere:
            p.radius = s[0];
            break;
        case SdfObjectType::Box:
            // A box maps its scale straight onto the half-extents; roundness is
            // stored but not applied, so it plays no part in the size.
            scale = { s[0], s[1], s[2] };
            break;
        case SdfObjectType::Cylinder:
            p.radius = s[0];
            p.height = s[1];
            break;
        case SdfObjectType::Torus:
            p.radius    = s[0];
            p.thickness = s[1];
            break;
        case SdfObjectType::Link:
            p.radius    = s[0];
            p.length    = s[1];
            p.thickness = s[2];
            break;
        case SdfObjectType::Cone:
            p.height = s[0];
            p.angle  = s[1];
            break;
        case SdfObjectType::Capsule:
            p.radius = s[0];
            p.height = s[1];
            break;
        case SdfObjectType::SolidAngle:
            p.radius = s[0];
            p.angle  = s[1];
            break;
        case SdfObjectType::Pyramid:
            p.radius = s[0];   // base half-width
            p.height = s[1];
            break;
        }
    }

    std::array<float, 3> ComputeSize(SdfObjectType type,
                                     const PrimitiveParams& p,
                                     const std::array<float, 3>& scale)
    {
        switch (type)
        {
        case SdfObjectType::Sphere:   return { p.radius * scale[0], 0.0f, 0.0f };
        case SdfObjectType::Box:      return { scale[0], scale[1], scale[2] };
        case SdfObjectType::Cylinder: return { p.radius * scale[0], p.height * scale[1], 0.0f };
        case SdfObjectType::Torus:    return { p.radius * scale[0], p.thickness, 0.0f };
        // Link/Capsule: radius on scale[0], length/height on scale[1]. Angles are
        // never scaled (a scaled angle is meaningless); passed straight through.
        case SdfObjectType::Link:     return { p.radius * scale[0], p.length * scale[1], p.thickness };
        case SdfObjectType::Cone:     return { p.height * scale[1], p.angle, 0.0f };
        case SdfObjectType::Capsule:  return { p.radius * scale[0], p.height * scale[1], 0.0f };
        case SdfObjectType::SolidAngle: return { p.radius * scale[0], p.angle, 0.0f };
        case SdfObjectType::Pyramid:  return { p.radius * scale[0], p.height * scale[1], 0.0f };
        }
        return { scale[0], scale[1], scale[2] };
    }

    std::array<int, 3> ScaleAxisMap(SdfObjectType type)
    {
        // Read straight off ComputeSize: for each local axis, the scale index
        // that shows up in the term controlling the shape's extent along it.
        // -1 means the extent there is fixed by an unscaled param (a thickness or
        // an angle) and no amount of dragging can change it.
        switch (type)
        {
        // One radius, spherical: every direction is the same knob.
        case SdfObjectType::Sphere:     return { 0, 0, 0 };
        case SdfObjectType::SolidAngle: return { 0, 0, 0 };
        // Half-extents map one-to-one; the only fully independent primitive.
        case SdfObjectType::Box:        return { 0, 1, 2 };
        // Radial (XZ) + axial (Y). Radius is scale[0], height/length scale[1].
        case SdfObjectType::Cylinder:   return { 0, 1, 0 };
        case SdfObjectType::Capsule:    return { 0, 1, 0 };
        case SdfObjectType::Link:       return { 0, 1, 0 };
        case SdfObjectType::Pyramid:    return { 0, 1, 0 };
        // Torus: the major radius is radial, the tube thickness is unscaled.
        case SdfObjectType::Torus:      return { 0, -1, 0 };
        // Cone: only its height scales — the flare is an angle, and scaling an
        // angle is meaningless (ComputeSize passes it through untouched).
        case SdfObjectType::Cone:       return { -1, 1, -1 };
        }
        return { 0, 1, 2 };
    }
}
