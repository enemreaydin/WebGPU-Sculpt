#include "ExtensionLoader.h"
#include <fstream>
#include <iostream>
#include <vector>

// Write the scene to a binary .sdfo file.
bool ExtensionLoader::SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects, const CsgTree& csgTree) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ExtensionLoader] HATA: Dosya yazma hatasi: " << filepath << "\n";
        return false;
    }

    // Header: magic tag + object count
    char magic[4] = { 'S', 'D', 'F', 'O' };
    int32_t count = (int32_t)objects.size();

    file.write(magic, 4);
    file.write((char*)&count, sizeof(int32_t));

    // Payload
    file.write((char*)objects.data(), objects.size() * sizeof(SdfObjectData));

    // CSG tree section (size prefix + data). Appended after the object payload so old
    // files without this section stay loadable.
    csgTree.WriteToStream(file);

    file.close();
    std::cout << "[ExtensionLoader] Dosya basariyla kaydedildi: " << filepath << "\n";
    return true;
}

// Read a binary .sdfo file back into the scene.
bool ExtensionLoader::LoadFromSDFO(const std::string& filepath, SceneManager& sceneManager) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ExtensionLoader] HATA: Dosya okuma hatasi: " << filepath << "\n";
        return false;
    }

    char magic[4];
    int32_t count;
    file.read(magic, 4);
    file.read((char*)&count, sizeof(int32_t));

    if (std::string(magic, 4) != "SDFO") {
        std::cerr << "[ExtensionLoader] HATA: Gecersiz dosya formati!\n";
        return false;
    }

    sceneManager.ClearScene();
    for (int i = 0; i < count; ++i) {
        SdfObjectData obj;
        file.read((char*)&obj, sizeof(SdfObjectData));
        // Restore raw (no objectId reassignment, no tree mutation) — the CSG tree is
        // restored from its own section below.
        sceneManager.RestoreObjectRaw(obj);
    }

    // CSG tree section. Tolerates EOF gracefully: legacy files without this section
    // leave the tree empty, and RebuildAfterLoad() then rebuilds a default tree.
    sceneManager.GetCsgTree().ReadFromStream(file);
    sceneManager.RebuildAfterLoad();

    file.close();
    std::cout << "[ExtensionLoader] Dosya basariyla yuklendi: " << filepath << "\n";
    return true;
}