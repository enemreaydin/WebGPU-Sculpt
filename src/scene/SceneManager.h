#pragma once
#include "scene/data/SdfObjectData.h"
#include "scene/csg/CsgTree.h"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "data/ObjectTransformSnapshot.h"
#include <optional>

struct SceneManagerState
{
    std::vector<SdfObjectData> objects;
    CsgTree::State csgTree;
    int32_t nextObjectId = 0;
    int32_t rootNodeIndex = -1;
    std::unordered_map<int32_t, int32_t> objectNodeIndex;
};

class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    // Adds a new user-created primitive: assigns a permanent objectId and attaches
    // it under the CSG tree's root Union node.
    void AddObject(const SdfObjectData& obj);

    // Pushes an object exactly as loaded from disk (no objectId assignment, no tree
    // mutation). Used by the loader; call RebuildAfterLoad() afterwards.
    void RestoreObjectRaw(const SdfObjectData& obj);

    // Removes the object at the given vector index and tombstones its CSG node.
    void RemoveObjectAt(size_t index);

    void ClearScene();
    SceneManagerState CaptureState() const;
    void RestoreState(const SceneManagerState& state);

    std::vector<SdfObjectData>& GetObjects();
    const std::vector<SdfObjectData>& GetObjects() const;
    size_t GetObjectCount() const;

    CsgTree& GetCsgTree();
    const CsgTree& GetCsgTree() const;

    int32_t GetNextObjectId() const;

    // Maps each object's objectId to its current vector index (for CsgTree::CompileToRPN).
    static std::unordered_map<int32_t, int32_t> BuildObjectIdMap(const std::vector<SdfObjectData>& objects);

    // Rebuilds internal bookkeeping (objectId counter, objectId->node index map, and
    // a default root Union tree for legacy files that lack a CSG section) after a load.
    void RebuildAfterLoad();

    int32_t NodeIdForObject(int32_t objectId) const;              // primitive's nodeId, or -1

    // The nodeId of the highest ancestor of this primitive that is still below the
    // structural root Union — i.e. the group the user should select when clicking
    // this primitive in the viewport. If the primitive is a direct child of the
    // root (ungrouped), returns the primitive's own nodeId. -1 if not found.
    int32_t TopLevelNodeIdForObject(int32_t objectId) const;

    // Appends every primitive objectId in the subtree rooted at nodeIndex (a CSG
    // vector index) to `out`. Used both for subtree deletion and for highlighting
    // a whole group's primitives.
    void CollectSubtreeObjectIds(int32_t nodeIndex, std::vector<int32_t>& out) const;

    int32_t GroupNodes(const std::vector<int32_t>& indicesInOrder, // indices[0] = Subtract base
                   CsgNodeType opType);                        // returns new op node's nodeId
    void RemoveNodeSubtree(int32_t nodeIndex);                     // node(s) + their objects

    SdfObjectData* FindObjectById(int32_t objectId);
    const SdfObjectData* FindObjectById(int32_t objectId) const;

    std::optional<ObjectTransformSnapshot> GetObjectTransformSnapshotById(int32_t objectId) const;
    bool SetObjectTransformById(const ObjectTransformSnapshot& snapshot);


private:
    // Ensures the root Union node exists, returning its node index.
    int32_t EnsureRootUnion();

    std::vector<SdfObjectData> m_Objects;
    CsgTree m_CsgTree;
    int32_t m_NextObjectId = 0;
    int32_t m_RootNodeIndex = -1;
    std::unordered_map<int32_t, int32_t> m_ObjectNodeIndex; // objectId -> CSG node index
};
