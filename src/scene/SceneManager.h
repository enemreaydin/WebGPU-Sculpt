#pragma once
#include "scene/data/SdfObjectData.h"
#include <vector>

class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    void AddObject(const SdfObjectData& obj);
    void ClearScene();

    std::vector<SdfObjectData>& GetObjects();
    const std::vector<SdfObjectData>& GetObjects() const;
    size_t GetObjectCount() const;

private:
    std::vector<SdfObjectData> m_Objects;
};