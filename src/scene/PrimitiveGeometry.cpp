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
        }
        return { scale[0], scale[1], scale[2] };
    }
}
