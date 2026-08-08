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
    // shader path that read it (P1a) was removed. Kept for P1b's distance
    // substitution; it is NOT what the per-ray sphere cull uses (see cullMargin).
    float proxyMargin = 0.0f;

    // How far each object's bubble must be inflated before the union of bubbles is
    // guaranteed to contain every surface the CSG program can draw. Same quantity
    // the scene box is padded by, and for the same reason: a smooth blend pushes
    // the surface outside both operands' bubbles.
    //
    // Deliberately NOT proxyMargin. That one carries kProxySafetyDistance (0.5
    // world units), a knob for trusting a bubble as a *distance* — the cull only
    // asks "could this ray touch the object at all", which needs nothing beyond
    // enclosure. Reusing it would start every march up to half a unit early.
    //
    // Published to globals.sceneBoundsMax.w and consumed by intersectObjectSpheres
    // in main.wgsl (CLAUDE.md 7c).
    float cullMargin = 0.0f;

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
