#include "SceneBounds.h"
#include "scene/csg/CsgTree.h"

#include <algorithm>
#include <unordered_map>

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
    return enclosingRadius;
}
// Extra slack on every face, on top of the smooth-blend margin: absorbs f32
// rounding on the CPU->uniform->shader round trip. Cheap insurance — a slightly
// larger box costs a few march steps, a slightly smaller one eats geometry.
static constexpr float kBoundsSlack = 0.05f;

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

    // Nothing contributed: leave it inverted. Inflating here would turn an empty
    // scene into a small valid box at the origin, and every ray through it would
    // march for nothing.
    if (bounds.IsEmpty()) return bounds;

    const float margin = maxBlendRadius + kBoundsSlack;
    for (int axis = 0; axis < 3; ++axis)
    {
        bounds.minBoxDistances[axis] -= margin;
        bounds.maxBoxDistances[axis] += margin;
    }

    return bounds;
}