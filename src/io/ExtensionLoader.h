#pragma once
#include <vector>
#include <string>
#include "../core/SdfDefinitions.h"
#include "../scene/SceneManager.h"

class ExtensionLoader {
public:
    // Fonksiyonlarýn baþýnda static olmasý þart, çünkü bir nesne üretmeden çaðýrýyorsun.
    static bool LoadMockFile(const std::string& filepath, SceneManager& sceneManager);
    static bool SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects);
    static bool LoadFromSDFO(const std::string& filepath, SceneManager& sceneManager);
};