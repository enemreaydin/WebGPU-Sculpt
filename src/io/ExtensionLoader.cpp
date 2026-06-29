#include "ExtensionLoader.h"
#include <fstream>
#include <iostream>
#include <vector>

// 1. MOCK FILE (Test Ýçin)
bool ExtensionLoader::LoadMockFile(const std::string& filepath, SceneManager& sceneManager) {
    std::cout << "[ExtensionLoader] Mock dosya hazirlaniyor...\n";

    sceneManager.ClearScene();

    // Mavi Küre
    SdfObjectData sphere;
    sphere.type = 0; // Sphere
    sphere.op = 0;   // None
    sphere.position = { 0.0f, 0.0f, 0.0f, 0.0f }; // Array'in 4. elemaný padding
    sphere.size = { 1.5f, 0.0f, 0.0f, 0.0f };
    sphere.color = { 0.0f, 0.45f, 0.85f, 0.0f };
    sceneManager.AddObject(sphere);

    // Kýrmýzý Kutu
    SdfObjectData box;
    box.type = 1; // Box
    box.op = 2;   // Subtract
    box.position = { 1.0f, 1.0f, 0.0f, 0.0f };
    box.size = { 1.0f, 1.0f, 1.0f, 0.0f };
    box.color = { 0.8f, 0.1f, 0.1f, 0.0f };
    sceneManager.AddObject(box);

    std::cout << "[ExtensionLoader] Mock nesneler sahneye eklendi.\n";
    return true;
}

// 2. SAVE TO BDF (Binary Kaydetme)
bool ExtensionLoader::SaveToBDF(const std::string& filepath, const std::vector<SdfObjectData>& objects) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ExtensionLoader] HATA: Dosya yazma hatasi: " << filepath << "\n";
        return false;
    }

    // Header
    char magic[4] = { 'B', 'D', 'F', '!' };
    int32_t count = (int32_t)objects.size();

    file.write(magic, 4);
    file.write((char*)&count, sizeof(int32_t));

    // Veri
    file.write((char*)objects.data(), objects.size() * sizeof(SdfObjectData));

    file.close();
    std::cout << "[ExtensionLoader] Dosya basariyla kaydedildi: " << filepath << "\n";
    return true;
}

// 3. LOAD FROM BDF (Binary Okuma)
bool ExtensionLoader::LoadFromBDF(const std::string& filepath, SceneManager& sceneManager) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ExtensionLoader] HATA: Dosya okuma hatasi: " << filepath << "\n";
        return false;
    }

    char magic[4];
    int32_t count;
    file.read(magic, 4);
    file.read((char*)&count, sizeof(int32_t));

    if (std::string(magic, 4) != "BDF!") {
        std::cerr << "[ExtensionLoader] HATA: Gecersiz dosya formati!\n";
        return false;
    }

    sceneManager.ClearScene();
    for (int i = 0; i < count; ++i) {
        SdfObjectData obj;
        file.read((char*)&obj, sizeof(SdfObjectData));
        sceneManager.AddObject(obj);
    }

    file.close();
    std::cout << "[ExtensionLoader] Dosya basariyla yuklendi: " << filepath << "\n";
    return true;
}