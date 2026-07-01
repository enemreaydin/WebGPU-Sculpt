#include "../io/ExtensionLoader.h"
#include <filesystem>
#include <iostream>
#include <vector>

int main()
{
    std::filesystem::create_directories("assets");

    std::vector<SdfObjectData> objects;

    // Mavi kure
    SdfObjectData sphere{};
    sphere.type = static_cast<int32_t>(SdfObjectType::Sphere);
    sphere.op = static_cast<int32_t>(SdfOperationType::None);
    sphere.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    sphere.size = { 1.5f, 0.0f, 0.0f, 0.0f };
    sphere.color = { 0.0f, 0.45f, 0.85f, 0.0f };
    objects.push_back(sphere);

    // Kutu
    SdfObjectData box{};
    box.type = static_cast<int32_t>(SdfObjectType::Box);
    box.op = static_cast<int32_t>(SdfOperationType::Union);
    box.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    box.size = { 2.0f, 1.0f, 1.0f, 0.0f };
    box.color = { 0.8f, 0.65f, 0.85f, 0.0f };
    objects.push_back(box);

    // X ekseni
    SdfObjectData xAxis{};
    xAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    xAxis.op = static_cast<int32_t>(SdfOperationType::Union);
    xAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    xAxis.size = { 3.0f, 0.015f, 0.015f, 0.0f };
    xAxis.color = { 1.0f, 0.0f, 0.0f, 0.0f };
    objects.push_back(xAxis);

    // Y ekseni
    SdfObjectData yAxis{};
    yAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    yAxis.op = static_cast<int32_t>(SdfOperationType::Union);
    yAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    yAxis.size = { 0.015f, 3.0f, 0.015f, 0.0f };
    yAxis.color = { 0.0f, 1.0f, 0.0f, 0.0f };
    objects.push_back(yAxis);

    // Z ekseni
    SdfObjectData zAxis{};
    zAxis.type = static_cast<int32_t>(SdfObjectType::Box);
    zAxis.op = static_cast<int32_t>(SdfOperationType::Union);
    zAxis.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    zAxis.size = { 0.015f, 0.015f, 3.0f, 0.0f };
    zAxis.color = { 0.0f, 0.0f, 1.0f, 0.0f };
    objects.push_back(zAxis);

    const std::string outputPath = "assets/test.sdfo";

    if (!ExtensionLoader::SaveToSDFO(outputPath, objects))
    {
        std::cerr << "[ERROR] test.bdf olusturulamadi.\n";
        return 1;
    }

    std::cout << "[OK] test.bdf olusturuldu: " << outputPath << "\n";
    std::cout << "[OK] Object count: " << objects.size() << "\n";

    return 0;
}