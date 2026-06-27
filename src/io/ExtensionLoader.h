#pragma once
#include "../scene/SceneManager.h"
#include <string>

class ExtensionLoader {
public:
    // Ýleride burasý gerçek bir dosya okuyacak, þimdilik sahte (mock) veri üretecek.
    static bool LoadMockFile(const std::string& filepath, SceneManager& sceneManager);
};