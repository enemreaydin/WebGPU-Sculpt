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
    bool IsEmpty() const;
};

namespace SceneBounds {
    float     EnclosingRadius(SdfObjectType type, const std::array<float, 4>& size);
    SceneAABB Compute(const std::vector<SdfObjectData>& objects, const CsgTree& tree);
}
