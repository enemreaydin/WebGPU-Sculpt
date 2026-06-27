#include "ExtensionLoader.h"
#include <iostream>

bool ExtensionLoader::LoadMockFile(const std::string& filepath, SceneManager& sceneManager) {
    std::cout << "[ExtensionLoader] '" << filepath << "' dosyasi analiz ediliyor...\n";

    // Eski sahneyi temizle
    sceneManager.ClearScene();

    // DOSYADAN OKUNMUÞ GÝBÝ YAPILAN 1. NESNE (Mavi Bir Küre)
    SdfObjectData sphere;
    sphere.type = SdfObjectType::Sphere;
    sphere.op = SdfOperationType::None;      // Ýlk nesne tabandýr
    sphere.position = { 0.0f, 0.0f, 0.0f };    // Tam merkezde
    sphere.size = { 1.5f, 0.0f, 0.0f };        // Yarýçap = 1.5
    sphere.color = { 0.0f, 0.45f, 0.85f };     // Baykar Mavisi
    sceneManager.AddObject(sphere);

    // DOSYADAN OKUNMUÞ GÝBÝ YAPILAN 2. NESNE (Oyan Kýrmýzý Kutu)
    SdfObjectData box;
    box.type = SdfObjectType::Box;
    box.op = SdfOperationType::Subtract;     // OYMA (SUBTRACT) ÝÞLEMÝ!
    box.position = { 1.0f, 1.0f, 0.0f };       // Sað üste doðru kaykýlmýþ
    box.size = { 1.0f, 1.0f, 1.0f };           // Boyutlarý 1x1x1
    box.color = { 0.8f, 0.1f, 0.1f };          // Kýrmýzý
    sceneManager.AddObject(box);

    std::cout << "[ExtensionLoader] Dosya basariyla okundu. Sahneye nesneler dizildi.\n";
    return true;
}