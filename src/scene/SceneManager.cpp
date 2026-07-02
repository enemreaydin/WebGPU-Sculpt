#include "SceneManager.h"

void SceneManager::AddObject(const SdfObjectData& obj) {
    m_Objects.push_back(obj);
}

void SceneManager::ClearScene() {
    m_Objects.clear();
}

std::vector<SdfObjectData>& SceneManager::GetObjects() {
    return m_Objects;
}

const std::vector<SdfObjectData>& SceneManager::GetObjects() const {
    return m_Objects;
}

size_t SceneManager::GetObjectCount() const {
    return m_Objects.size();
}