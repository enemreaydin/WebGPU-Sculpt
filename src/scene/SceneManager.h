#pragma once
#include "../core/SdfDefinitions.h"
#include <vector>

class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    // Sahnemize yeni bir SDF nesnesi (Küre, Kutu vb.) eklemek için
    void AddObject(const SdfObjectData& obj);

    // Sahneyi tamamen temizlemek için (Yeni dosya yüklenirken gerekecek)
    void ClearScene();

    // GPU tarafýna (Renderer'a) verileri ham þekilde teslim etmek için
    const std::vector<SdfObjectData>& GetObjects() const;

    // Sahnede þu an kaç nesne olduðunu dönmek için
    size_t GetObjectCount() const;

private:
    // Sahnemizin kalbi: Nesnelerin dinamik listesi (Sahne Aðacý Altyapýsý)
    std::vector<SdfObjectData> m_Objects;
};