#pragma once
#include <vector>
#include <string>
#include "scene/data/SdfObjectData.h"
#include "scene/csg/CsgTree.h"
#include "scene/SceneManager.h"

class ExtensionLoader {
public:
    // Static so callers don't need an instance.
    static bool SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects, const CsgTree& csgTree);
    static bool LoadFromSDFO(const std::string& filepath, SceneManager& sceneManager);
};