#include "ExtensionLoader.h"
#include <fstream>
#include <iostream>
#include <ostream>
#include <istream>
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

// Write the project to any binary output stream (file or in-memory).
void ExtensionLoader::WriteSDFP(std::ostream& out, const std::vector<SdfObjectData>& objects, const CsgTree& csgTree) {
    // Header: magic tag + object count. Same payload layout as SDFO, but the magic
    // is "SDFP" so a project file and a (future) composite-object .sdfo never mix.
    char magic[4] = { 'S', 'D', 'F', 'P' };
    int32_t count = static_cast<int32_t>(objects.size());

    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&count), sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(objects.data()), objects.size() * sizeof(SdfObjectData));

    // CSG tree (groups / boolean operations): size-prefixed section.
    csgTree.WriteToStream(out);
}

// Read a project from any binary input stream (file or in-memory).
bool ExtensionLoader::ReadSDFP(std::istream& in, SceneManager& sceneManager) {
    char magic[4] = { 0, 0, 0, 0 };
    int32_t count = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&count), sizeof(int32_t));

    if (!in || std::string(magic, 4) != "SDFP" || count < 0) {
        std::cerr << "[ExtensionLoader] HATA: Gecersiz .sdfp formati!\n";
        return false;
    }

    sceneManager.ClearScene();
    for (int i = 0; i < count; ++i) {
        SdfObjectData obj;
        in.read(reinterpret_cast<char*>(&obj), sizeof(SdfObjectData));
        sceneManager.RestoreObjectRaw(obj);
    }

    // CSG tree section (EOF-tolerant, same as SDFO), then rebuild derived state.
    sceneManager.GetCsgTree().ReadFromStream(in);
    sceneManager.RebuildAfterLoad();
    return true;
}