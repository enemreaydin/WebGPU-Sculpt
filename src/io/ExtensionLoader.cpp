#include "ExtensionLoader.h"
#include <fstream>
#include <iostream>
#include <vector>

// 2. SAVE TO BDF (Binary Kaydetme)
bool ExtensionLoader::SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ExtensionLoader] HATA: Dosya yazma hatasi: " << filepath << "\n";
        return false;
    }

    // Header
    char magic[4] = { 'S', 'D', 'F', 'O' };
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
        sceneManager.AddObject(obj);
    }

    file.close();
    std::cout << "[ExtensionLoader] Dosya basariyla yuklendi: " << filepath << "\n";
    return true;
}