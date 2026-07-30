#pragma once

#include <array>                        // std:: types: real include, never forward-declared
#include <vector>
#include <cmath>
#include "scene/data/SdfObjectData.h"   // used BY VALUE inside the vector -> needs definition
#include "scene/data/SdfEnums.h"        // SdfObjectType is an enum -> needs definition

class CsgTree;                          // only ever a const& below -> name is enough

struct SceneAABB {
    float minBoxDistances[3] = {  1e30f,  1e30f,  1e30f };   // inverted = "empty", the union identity
    float maxBoxDistances[3] = { -1e30f, -1e30f, -1e30f };

    // Delta for the per-object proxy substitution (P1a): a point must be MORE than
    // this far outside an object's bubble before the bubble's distance may be
    // trusted in place of the exact SDF. Published to globals.sceneBoundsMin.w.
    // Scene-dependent, because it has to exceed the widest smooth blend in the
    // scene — see the constants block in SceneBounds.cpp.
    float proxyMargin = 0.0f;

    bool IsEmpty() const;
};

namespace SceneBounds {
    // Radius of a ball centred on position.xyz that fully contains the primitive.
    // Rotation-invariant by construction, and already carries its safety factor.
    float     EnclosingRadius(SdfObjectType type, const std::array<float, 4>& size);

    // World-space bound of everything the CSG program can draw, plus proxyMargin.
    SceneAABB Compute(const std::vector<SdfObjectData>& objects, const CsgTree& tree);

    void WriteEnclosingRadii(std::vector<SdfObjectData>& objects, const CsgTree& tree);
}
