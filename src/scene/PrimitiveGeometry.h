#pragma once

#include <array>

#include "scene/data/SdfEnums.h"
#include "scene/data/EditorObject.h"

// Domain math that maps a primitive's editor parameters to the GPU `size`
// vector, and derives sensible default parameters for a freshly created
// primitive. This used to live in the web UI (computeSize); it belongs with the
// engine so the SDF meaning of each parameter is defined in one place.
namespace PrimitiveGeometry
{
    // Fill default shape params + scale for a new primitive whose GPU `size`
    // vector is `initialSize`. Chosen so ComputeSize() reproduces `initialSize`.
    void InitFromSize(SdfObjectType type,
                      const std::array<float, 3>& initialSize,
                      PrimitiveParams& outParams,
                      std::array<float, 3>& outScale);

    // Derive the GPU `size` vector from the editor shape params + scale.
    std::array<float, 3> ComputeSize(SdfObjectType type,
                                     const PrimitiveParams& params,
                                     const std::array<float, 3>& scale);

    // Which EditorObject::scale component each LOCAL gizmo axis (X, Y, Z) drives
    // for a given primitive type, or -1 when that direction cannot change this
    // shape's size at all. It is the inverse view of ComputeSize above, and the
    // scale gizmo is built from it: an axis mapped to -1 gets no handle drawn and
    // no hit test, so the gizmo can only offer transforms the model can express.
    //
    // Every axial primitive is built around Y with XZ radial (see the sdf*
    // functions in sdf_core.wgsl), so e.g. a cylinder maps X and Z to the same
    // radius component: dragging either handle fattens it, dragging Y makes it
    // taller. A sphere maps all three to its single radius, so it always scales
    // uniformly no matter which handle is grabbed. Only the box is genuinely
    // three-way independent.
    std::array<int, 3> ScaleAxisMap(SdfObjectType type);
}
