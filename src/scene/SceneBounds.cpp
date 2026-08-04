#include "SceneBounds.h"
#include "scene/csg/CsgTree.h"

#include <algorithm>
#include <unordered_map>

// ---------------------------------------------------------------------------
// The three safety margins.
//
// They look interchangeable and are not: each covers a DIFFERENT error, on a
// DIFFERENT code path, in DIFFERENT units. The names say which is which — a
// "Factor" multiplies, a "Padding"/"Distance" is in world units.
//
//   1. kRadiusSafetyFactor  (multiplier, per object)
//      "Is this bubble big enough for its object?" Several radii in
//      EnclosingRadius are EXACT fits (sphere = s.x, torus = s.x + s.y), and an
//      exact fit that rounds down by one ULP under-covers. Harmless for the box
//      (one extra step) but fatal for the proxy: an under-covering bubble makes
//      the proxy OVERestimate the distance, and the marcher steps through the
//      surface. Multiplicative so it scales with object size.
//
//   2. kSceneBoxPadding  (world units, per box face)
//      "Is the scene box big enough for all the bubbles?" Absorbs f32 rounding
//      on the CPU -> uniform -> shader round trip for the §7b ray clip.
//
//   3. kProxySafetyDistance  (world units, per evaluation)
//      "Am I far enough from this bubble to trust it?" Guards the CSG operators,
//      not the geometry: subtraction negates its operand, which turns a safe
//      UNDERestimate into an unsafe OVERestimate, and a smooth blend needs both
//      exact values inside the blend zone. This is the P1a tuning knob — start
//      generous, tighten while re-verifying G_correctness_17.
//
// Why (2) cannot cover (3): the shader's proxy path reads obj.position.w
// directly and never consults the scene box, so box padding is nowhere in that
// computation. Why (2) and (3) stay separate despite sharing units: (2) should
// never be touched, (3) is deliberately tuned.
// ---------------------------------------------------------------------------
static constexpr float kRadiusSafetyFactor  = 1.05f;
static constexpr float kSceneBoxPadding     = 0.05f;
static constexpr float kProxySafetyDistance = 0.5f;

float SceneBounds::EnclosingRadius(SdfObjectType type, const std::array<float, 4>& size){
    // Must start at a safe value: an unknown/garbage type falls through to
    // `default` without assigning, and returning an uninitialized float would
    // put whatever was on the stack straight into the scene box.
    float enclosingRadius = 0.0f;
    float x = size[0];
    float y = size[1]; 
    float z = size[2];
    switch (type)
    {
    case SdfObjectType::Sphere:
        enclosingRadius = x;
        break;
    case SdfObjectType::Box:
        enclosingRadius = std::sqrt(x*x+y*y+z*z);
        break;
    case SdfObjectType::Cylinder:
        enclosingRadius = std::hypot(x, 0.5f*y);
        break;
    case SdfObjectType::Torus:
        enclosingRadius = x+y;
        break;    
    case SdfObjectType::Link:
        enclosingRadius = x+y+z;
        break;
    case SdfObjectType::Cone:
    {
        // Apex at the local origin, base rim at (h*tan0, -h), so the farthest
        // surface point is h/cos0. That diverges as 0 -> pi/2, and a single
        // infinite radius would make the whole scene box infinite -- which the
        // shader's slab test then accepts for every ray, silently undoing P4.
        // 85 degrees is well past any usable cone.
        const float coneAngle = std::min(std::abs(y), 1.48f);
        enclosingRadius = x / std::cos(coneAngle);
        break;
    }
    case SdfObjectType::Capsule:
        enclosingRadius = x+y;
        break;
    case SdfObjectType::SolidAngle:
        enclosingRadius = x;
        break; 
    case SdfObjectType::Pyramid:
        enclosingRadius = std::sqrt(2*x*x + y*y);
        break; 
    default:
        break;
    }
    return enclosingRadius * kRadiusSafetyFactor;
}

// Fail-safe radius for an object we cannot vouch for. A bubble this large means
// the shader's dBound is hugely negative, never exceeds delta, and the object
// therefore ALWAYS takes the exact path. Note 0.0f would be the worst possible
// choice here: it makes the proxy OVERestimate the distance, which is exactly
// how the marcher tunnels through a surface.
static constexpr float kAlwaysExactRadius = 1e30f;

// Publish each primitive's bubble radius into the unused position.w.
//
// !! STAGED, NOT YET CONSUMED. The per-object proxy substitution that read this
// (P1a) was removed: skipping an object outright beats discounting it, and it
// only bought ~12%. The radii stay because the *next* optimisation needs exactly
// this data — per-ray sphere culling tests each ray against these bubbles once,
// builds a bitmask, and skips masked objects entirely (including the 80-byte
// object load, which P1a still had to pay). Do not delete as dead code.
//
// Derived data, recomputed on every refresh — the position setters in
// Application zero this slot, so it can only live on the refresh path.
void SceneBounds::WriteEnclosingRadii(std::vector<SdfObjectData>& objects, const CsgTree& tree){

    std::unordered_map<int32_t, size_t> objectIdToIndex;
    objectIdToIndex.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i)
        objectIdToIndex[objects[i].objectId] = i;

    for (const CsgNode& node : tree.GetNodes()){
        if (!node.alive) continue;
        if (node.type == CsgNodeType::Primitive)
        {
            const auto it = objectIdToIndex.find(node.objectId);
            if (it == objectIdToIndex.end()) continue;   // corrupt file: node points at a missing object
            SdfObjectData& object = objects[it->second];

            // EnclosingRadius already applies kRadiusSafetyFactor — do not scale again.
            const float radius = SceneBounds::EnclosingRadius(
                static_cast<SdfObjectType>(object.type), object.size);

            object.position[3] = (std::isfinite(radius) && radius > 0.0f)
                ? radius
                : kAlwaysExactRadius;
        }
    }
}

// Inverted (min > max) is how an empty scene is represented: it is the identity
// element of the union below, so "no objects" needs no separate flag, and the
// slab test in the shader rejects every ray against it for free.
bool SceneAABB::IsEmpty() const {
    return minBoxDistances[0] > maxBoxDistances[0]
        || minBoxDistances[1] > maxBoxDistances[1]
        || minBoxDistances[2] > maxBoxDistances[2];
}

SceneAABB SceneBounds::Compute(const std::vector<SdfObjectData>& objects, const CsgTree& tree){
    SceneAABB bounds;

    // A CsgNode references a primitive by its PERMANENT objectId, never by the
    // (transient) vector index, so the walk below needs a lookup. Build it once:
    // O(objects + nodes) instead of the O(objects * nodes) a FindObjectById per
    // node would cost.
    std::unordered_map<int32_t, size_t> objectIdToIndex;
    objectIdToIndex.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i)
        objectIdToIndex[objects[i].objectId] = i;

    // Measured, not a tuning constant: the widest smooth blend alive in the scene.
    // Feeds BOTH derived margins at the bottom of this function.
    float maxBlendRadius = 0.0f;

    // Walk the CSG TREE, not the object vector. The tree is what CompileToRPN
    // turns into the program the GPU executes, so it is the same source of truth
    // as the render itself: an object sitting in m_Objects with no alive node
    // emits no PUSH_OBJECT and draws nothing, and bounding it would inflate the
    // box around geometry that isn't on screen.
    //
    // Safe direction check: this is a SUPERSET of what renders, never a subset.
    // CompileToRPN traverses from the root, so an alive-but-orphaned node is
    // counted here and skipped there — erring large, which only costs steps.
    // The single `alive` test also covers both jobs, since a tombstoned node must
    // contribute neither its geometry nor its blend radius.
    for (const CsgNode& node : tree.GetNodes())
    {
        if (!node.alive) continue;

        if (node.type == CsgNodeType::Primitive)
        {
            const auto it = objectIdToIndex.find(node.objectId);
            if (it == objectIdToIndex.end()) continue;   // corrupt file: node points at a missing object
            const SdfObjectData& object = objects[it->second];

            // Each primitive contributes the AABB of its enclosing sphere. The
            // sphere is centred on position.xyz, which makes it rotation-invariant:
            // a rotated primitive needs no re-fit at all. That costs looseness (a
            // 1x1x1 box needs radius sqrt(3), 73% wider than the box itself) and
            // buys exactness under a transform the editor applies constantly.
            const float radius = EnclosingRadius(static_cast<SdfObjectType>(object.type), object.size);

            // A corrupt .sdfp blits SdfObjectData/CsgNode straight off disk, and
            // the load-time validation checks CSG links only -- never the floats. A
            // single NaN here would propagate into the box and then into the
            // shader's slab test, where every NaN comparison is false and the
            // behaviour is anyone's guess. Dropping the object only shrinks the
            // box, so guard and skip.
            if (!std::isfinite(radius) || radius < 0.0f) continue;
            if (!std::isfinite(object.position[0]) ||
                !std::isfinite(object.position[1]) ||
                !std::isfinite(object.position[2])) continue;

            for (int axis = 0; axis < 3; ++axis)
            {
                const float center = object.position[axis];
                bounds.minBoxDistances[axis] = std::min(bounds.minBoxDistances[axis], center - radius);
                bounds.maxBoxDistances[axis] = std::max(bounds.maxBoxDistances[axis], center + radius);
            }
        }

        // Smooth union bulges OUTWARD by up to k/4 (opSmoothUnion subtracts
        // k*h*(1-h), maximal at h = 0.5), so the blended surface can sit outside
        // both operands' spheres. Smooth intersection/subtract add that term
        // instead and can only pull the surface inward -- but folding every Smooth
        // node in is free and saves re-deriving that case split later. Using the
        // full k rather than k/4 is likewise deliberate slack.
        if (node.blendMode == BlendMode::Smooth &&
            std::isfinite(node.blendRadius) && node.blendRadius > 0.0f)
        {
            maxBlendRadius = std::max(maxBlendRadius, node.blendRadius);
        }
    }

    // Both derived margins are built from the same measured quantity, because a
    // smooth blend genuinely pushes surfaces outward: the box must contain that
    // bulge, and the proxy must not be trusted anywhere inside a blend zone.
    // Set before the early return below so delta is valid in every scene state.
    bounds.proxyMargin = maxBlendRadius + kProxySafetyDistance;

    // Nothing contributed: leave it inverted. Inflating here would turn an empty
    // scene into a small valid box at the origin, and every ray through it would
    // march for nothing.
    if (bounds.IsEmpty()) return bounds;

    const float boxMargin = maxBlendRadius + kSceneBoxPadding;
    for (int axis = 0; axis < 3; ++axis)
    {
        bounds.minBoxDistances[axis] -= boxMargin;
        bounds.maxBoxDistances[axis] += boxMargin;
    }

    return bounds;
}

namespace {

void ExpandToInclude(SceneAABB& target, const SceneAABB& other)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        target.minBoxDistances[axis] = std::min(target.minBoxDistances[axis], other.minBoxDistances[axis]);
        target.maxBoxDistances[axis] = std::max(target.maxBoxDistances[axis], other.maxBoxDistances[axis]);
    }
}

// Post-order: a node's box is the union of its children's, so children must be
// finished first. Recursion mirrors CsgTree::CompileNode rather than iterating
// the node vector, because only a walk from the root establishes that order —
// and because an alive-but-orphaned node must NOT inflate any ancestor's box.
//
// Boolean type is deliberately ignored. Subtract only removes and Intersection
// only keeps what both operands cover, so bounding every operator by the union
// of its children is a superset in all three cases. A tighter rule (Subtract =
// base child only, Intersection = smallest child) is a real future win but a
// separate change: it must hold for the RUNTIME skip test too, and erring large
// only ever costs march steps.
void AccumulateNodeBounds(const std::vector<SdfObjectData>& objects,
                          const std::vector<CsgNode>& nodes,
                          const std::unordered_map<int32_t, size_t>& objectIdToIndex,
                          int32_t nodeIndex,
                          std::vector<SceneAABB>& out)
{
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.size()) return;

    const CsgNode& node = nodes[static_cast<size_t>(nodeIndex)];
    if (!node.alive) return;

    SceneAABB box;   // starts inverted == empty

    if (node.type == CsgNodeType::Primitive)
    {
        const auto it = objectIdToIndex.find(node.objectId);
        if (it != objectIdToIndex.end())
        {
            const SdfObjectData& object = objects[it->second];
            const float radius = SceneBounds::EnclosingRadius(
                static_cast<SdfObjectType>(object.type), object.size);

            // Same non-finite guards as Compute(), and for the same reason: a
            // corrupt .sdfp blits these floats straight off disk and load-time
            // validation checks CSG links only. Leaving the box empty here makes
            // the subtree unskippable, which is the safe direction.
            const bool finite = std::isfinite(radius) && radius >= 0.0f
                && std::isfinite(object.position[0])
                && std::isfinite(object.position[1])
                && std::isfinite(object.position[2]);
            if (finite)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    box.minBoxDistances[axis] = object.position[axis] - radius;
                    box.maxBoxDistances[axis] = object.position[axis] + radius;
                }
            }
        }
    }
    else
    {
        for (int32_t child = node.firstChild; child != -1; child = nodes[static_cast<size_t>(child)].nextSibling)
        {
            AccumulateNodeBounds(objects, nodes, objectIdToIndex, child, out);
            ExpandToInclude(box, out[static_cast<size_t>(child)]);
        }
    }

    out[static_cast<size_t>(nodeIndex)] = box;
}

}   // namespace

NodeBoundsTable SceneBounds::ComputeNodeBounds(const std::vector<SdfObjectData>& objects, const CsgTree& tree)
{
    const std::vector<CsgNode>& nodes = tree.GetNodes();

    NodeBoundsTable table;
    table.byNodeIndex.assign(nodes.size(), SceneAABB{});

    std::unordered_map<int32_t, size_t> objectIdToIndex;
    objectIdToIndex.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i)
        objectIdToIndex[objects[i].objectId] = i;

    // delta must exceed the widest smooth blend ANYWHERE in the tree, not just
    // inside the subtree being bounded: a blend is a property of the operator, and
    // an ancestor's blend can push this subtree's contribution outward. Taking the
    // global max is the conservative reading and matches Compute()'s proxyMargin,
    // which is what keeps the CPU box and the shader's skip threshold consistent.
    float maxBlendRadius = 0.0f;
    for (const CsgNode& node : nodes)
    {
        if (!node.alive) continue;
        if (node.blendMode == BlendMode::Smooth &&
            std::isfinite(node.blendRadius) && node.blendRadius > 0.0f)
        {
            maxBlendRadius = std::max(maxBlendRadius, node.blendRadius);
        }
    }
    table.margin = maxBlendRadius + kProxySafetyDistance;

    AccumulateNodeBounds(objects, nodes, objectIdToIndex, tree.GetRoot(), table.byNodeIndex);

    // Inflate after the union, not per primitive: doing it per leaf would compound
    // the margin once per tree level on the way up.
    for (SceneAABB& box : table.byNodeIndex)
    {
        if (box.IsEmpty()) continue;   // inverted stays inverted; see SceneAABB
        for (int axis = 0; axis < 3; ++axis)
        {
            box.minBoxDistances[axis] -= table.margin;
            box.maxBoxDistances[axis] += table.margin;
        }
    }

    return table;
}