#pragma once
#include <vector>
#include <string>
#include "scene/data/SdfObjectData.h"
#include "scene/SceneManager.h"

class ExtensionLoader {
public:
    // Static so callers don't need an instance.
    static bool SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects);
    static bool LoadFromSDFO(const std::string& filepath, SceneManager& sceneManager);
};