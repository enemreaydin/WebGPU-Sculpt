#include "serialization/ExtensionLoader.h"
#include "scene/SceneManager.h"
#include "scene/data/SdfEnums.h"
#include <filesystem>
#include <iostream>
#include <vector>

int main()
{
    std::filesystem::create_directories("assets");

    // Build the scene through SceneManager so each object gets a permanent objectId
    // and is attached under the CSG tree's root Union node.
    SceneManager sceneManager;

    // Mavi kure
    SdfObjectData sphere{};
    sphere.type = static_cast<int32_t>(SdfObjectType::Sphere);
    sphere.parentId = -1;
    sphere.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    sphere.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    sphere.size = { 1.5f, 0.0f, 0.0f, 0.0f };
    sphere.color = { 0.0f, 0.45f, 0.85f, 0.0f };
    sceneManager.AddObject(sphere);

    // Kutu
    SdfObjectData box{};
    box.type = static_cast<int32_t>(SdfObjectType::Box);
    box.parentId = -1;
    box.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    box.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    box.size = { 2.0f, 1.0f, 1.0f, 0.0f };
    box.color = { 0.8f, 0.65f, 0.85f, 0.0f };
    sceneManager.AddObject(box);

    // X ekseni
    SdfObjectData xAxis{};
    xAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    xAxis.parentId = -1;
    xAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    xAxis.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    xAxis.size = { 3.0f, 0.015f, 0.015f, 0.0f };
    xAxis.color = { 1.0f, 0.0f, 0.0f, 0.0f };
    sceneManager.AddObject(xAxis);

    // Y ekseni
    SdfObjectData yAxis{};
    yAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    yAxis.parentId = -1;
    yAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    yAxis.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    yAxis.size = { 0.015f, 3.0f, 0.015f, 0.0f };
    yAxis.color = { 0.0f, 1.0f, 0.0f, 0.0f };
    sceneManager.AddObject(yAxis);

    // Z ekseni
    SdfObjectData zAxis{};
    zAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    zAxis.parentId = -1;
    zAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    zAxis.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    zAxis.size = { 0.015f, 0.015f, 3.0f, 0.0f };
    zAxis.color = { 0.0f, 0.0f, 1.0f, 0.0f };
    sceneManager.AddObject(zAxis);

    const std::string outputPath = "assets/test.sdfo";

    if (!ExtensionLoader::SaveToSDFO(outputPath, sceneManager.GetObjects(), sceneManager.GetCsgTree()))
    {
        std::cerr << "[ERROR] test.sdfo olusturulamadi.\n";
        return 1;
    }

    std::cout << "[OK] test.sdfo olusturuldu: " << outputPath << "\n";
    std::cout << "[OK] Object count: " << sceneManager.GetObjectCount() << "\n";

    return 0;
}
