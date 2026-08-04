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

    // How far outside an object's bubble a point must be before that bubble may
    // stand in for the object at all. Scene-dependent: it has to exceed the widest
    // smooth blend in the scene, because inside a blend zone the surface is shaped
    // by both operands, and because subtraction negates its operand (turning a safe
    // underestimate into an unsafe overestimate). See the constants block in
    // SceneBounds.cpp.
    //
    // Published to globals.sceneBoundsMin.w. !! STAGED, NOT YET CONSUMED — the
    // shader path that read it (P1a) was removed; per-ray sphere culling will use
    // the same value to inflate the bubbles it tests against.
    float proxyMargin = 0.0f;

    bool IsEmpty() const;
};

// One AABB per CSG node: the world-space bound of everything THAT SUBTREE can
// draw. Scene-wide Compute() above is exactly the root entry of this table.
//
// Indexed by node VECTOR INDEX, not nodeId — the compiler walks indices, and a
// nodeId lookup per node would make the build O(nodes^2). Nodes that are dead,
// unreachable from the root, or bound nothing keep the inverted "empty" box.
//
// Used by CsgTree::CompileToRPN to regroup a wide flat operation node into a
// spatial hierarchy, so the emitted program can carry per-subtree skip bounds
// (P1d). A missing or wrong entry can only cost performance, never correctness:
// it feeds the ORDER children are emitted in and the bounds a runtime skip test
// consults, and a skip whose box is too large simply never fires.
struct NodeBoundsTable
{
    std::vector<SceneAABB> byNodeIndex;

    // delta, already folded into every box above. Kept alongside because the
    // shader-side skip threshold must use the same number the boxes were
    // inflated by, and deriving it twice is how the two drift apart.
    float margin = 0.0f;

    bool InRange(int32_t nodeIndex) const
    {
        return nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < byNodeIndex.size();
    }
};

namespace SceneBounds {
    // Radius of a ball centred on position.xyz that fully contains the primitive.
    // Rotation-invariant by construction, and already carries its safety factor.
    float     EnclosingRadius(SdfObjectType type, const std::array<float, 4>& size);

    // World-space bound of everything the CSG program can draw, plus proxyMargin.
    SceneAABB Compute(const std::vector<SdfObjectData>& objects, const CsgTree& tree);

    // Per-subtree version of Compute(). One post-order walk from the root, so it
    // is O(nodes) for the whole table rather than O(nodes) per node.
    NodeBoundsTable ComputeNodeBounds(const std::vector<SdfObjectData>& objects, const CsgTree& tree);

    void WriteEnclosingRadii(std::vector<SdfObjectData>& objects, const CsgTree& tree);
}
