#include "SceneManager.h"

int32_t SceneManager::EnsureRootUnion()
{
    if (m_RootNodeIndex == -1)
    {
        // The root is always a Union node; primitives attach as its children.
        m_RootNodeIndex = m_CsgTree.AddOperationNode(CsgNodeType::Union, -1);
    }
    return m_RootNodeIndex;
}

void SceneManager::AddObject(const SdfObjectData& obj) {
    SdfObjectData copy = obj;
    copy.objectId = m_NextObjectId++;   // Permanent, never reused, never == array index.
    m_Objects.push_back(copy);

    int32_t root = EnsureRootUnion();
    int32_t nodeIndex = m_CsgTree.AddPrimitiveNode(copy.objectId, root);
    m_ObjectNodeIndex[copy.objectId] = nodeIndex;
}

void SceneManager::RestoreObjectRaw(const SdfObjectData& obj) {
    m_Objects.push_back(obj);
}

void SceneManager::RemoveObjectAt(size_t index) {
    if (index >= m_Objects.size())
    {
        return;
    }

    int32_t objectId = m_Objects[index].objectId;

    auto it = m_ObjectNodeIndex.find(objectId);
    if (it != m_ObjectNodeIndex.end())
    {
        m_CsgTree.RemoveNode(it->second);
        m_ObjectNodeIndex.erase(it);
    }

    m_Objects.erase(m_Objects.begin() + index);
}

void SceneManager::ClearScene() {
    m_Objects.clear();
    m_CsgTree.Clear();
    m_ObjectNodeIndex.clear();
    m_RootNodeIndex = -1;
    m_NextObjectId = 0;
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

CsgTree& SceneManager::GetCsgTree() {
    return m_CsgTree;
}

const CsgTree& SceneManager::GetCsgTree() const {
    return m_CsgTree;
}

int32_t SceneManager::GetNextObjectId() const {
    return m_NextObjectId;
}

std::unordered_map<int32_t, int32_t> SceneManager::BuildObjectIdMap(const std::vector<SdfObjectData>& objects) {
    std::unordered_map<int32_t, int32_t> map;
    map.reserve(objects.size());
    for (int32_t i = 0; i < static_cast<int32_t>(objects.size()); ++i)
    {
        map[objects[i].objectId] = i;
    }
    return map;
}

void SceneManager::RebuildAfterLoad() {
    // Restore the objectId counter from the maximum loaded id so new ids never collide.
    m_NextObjectId = 0;
    for (const auto& o : m_Objects)
    {
        if (o.objectId + 1 > m_NextObjectId)
        {
            m_NextObjectId = o.objectId + 1;
        }
    }

    m_ObjectNodeIndex.clear();
    m_RootNodeIndex = m_CsgTree.GetRoot();

    if (m_RootNodeIndex == -1 && !m_Objects.empty())
    {
        // Legacy file without a CSG section: rebuild a default root Union tree so the
        // scene still renders (matches the auto root-Union default behavior).
        m_CsgTree.Clear();
        m_RootNodeIndex = m_CsgTree.AddOperationNode(CsgNodeType::Union, -1);
        for (const auto& o : m_Objects)
        {
            int32_t nodeIndex = m_CsgTree.AddPrimitiveNode(o.objectId, m_RootNodeIndex);
            m_ObjectNodeIndex[o.objectId] = nodeIndex;
        }
        return;
    }

    // Rebuild the objectId -> node index map from the loaded tree.
    const auto& nodes = m_CsgTree.GetNodes();
    for (int32_t i = 0; i < static_cast<int32_t>(nodes.size()); ++i)
    {
        if (nodes[i].alive && nodes[i].type == CsgNodeType::Primitive)
        {
            m_ObjectNodeIndex[nodes[i].objectId] = i;
        }
    }
}
