#include "SceneManager.h"

void SceneManager::AddObject(const SdfObjectData& obj) {
    // Listeye yeni nesneyi ekle (Uzantýdan okundukça buraya dolacak)
    m_Objects.push_back(obj);
}

void SceneManager::ClearScene() {
    m_Objects.clear();
}

const std::vector<SdfObjectData>& SceneManager::GetObjects() const {
    return m_Objects;
}

size_t SceneManager::GetObjectCount() const {
    return m_Objects.size();
}