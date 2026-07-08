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

int32_t SceneManager::NodeIdForObject(int32_t objectId) const{
    auto it = m_ObjectNodeIndex.find(objectId);
    if (it == m_ObjectNodeIndex.end()) return -1;
    return m_CsgTree.GetNodes()[it->second].nodeId;
}

int32_t SceneManager::GroupNodes(const std::vector<int32_t>& indices, CsgNodeType opType)
{
    if (indices.size() < 2) return -1;

    // Host the new group under the primary node's current parent (usually root).
    int32_t parent = m_CsgTree.GetNodes()[indices[0]].parent;
    if (parent == -1) parent = m_CsgTree.GetRoot();

    // Create the op node detached (parent -1), then hang it, then move the
    // selected nodes under it — primary first so it becomes firstChild = the
    // base child in the RPN fold (see CompileNode).
    int32_t opIndex = m_CsgTree.AddOperationNode(opType, -1);
    m_CsgTree.AttachChild(parent, opIndex);
    for (int32_t idx : indices)
        m_CsgTree.Reparent(idx, opIndex);

    return m_CsgTree.GetNodes()[opIndex].nodeId;
}


void SceneManager::CollectSubtreeObjectIds(int32_t nodeIndex, std::vector<int32_t>& out) const
{
    const auto& nodes = m_CsgTree.GetNodes();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes.size())) return;
    const CsgNode& n = nodes[nodeIndex];
    if (!n.alive) return;
    if (n.type == CsgNodeType::Primitive) out.push_back(n.objectId);
    for (int32_t c = n.firstChild; c != -1; c = nodes[c].nextSibling)
        CollectSubtreeObjectIds(c, out);
}

void SceneManager::RemoveNodeSubtree(int32_t nodeIndex)
{
    std::vector<int32_t> objectIds;
    CollectSubtreeObjectIds(nodeIndex, objectIds);   // gather BEFORE tombstoning

    m_CsgTree.RemoveNode(nodeIndex);                 // tombstones the whole subtree

    for (int32_t oid : objectIds) {
        for (size_t i = 0; i < m_Objects.size(); ++i)
            if (m_Objects[i].objectId == oid) { m_Objects.erase(m_Objects.begin() + i); break; }
        m_ObjectNodeIndex.erase(oid);
    }
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
