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
}
