#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <iosfwd>
#include "CsgNode.h"
#include "CsgCommand.h"

class CsgTree
{
public:
    int32_t AddPrimitiveNode(int32_t objectId, int32_t parent = -1);
    int32_t AddOperationNode(CsgNodeType opType, int32_t parent = -1);
    void AttachChild(int32_t parentIndex, int32_t childIndex);
    void RemoveNode(int32_t index); // Recursively tombstones the entire subtree; detaches
                                    // from the parent's sibling chain using
                                    // previousSibling/nextSibling/firstChild/lastChild.

    // objectIdToIndex maps each SdfObjectData.objectId to its CURRENT real position
    // in the scene_objects vector at compile time (built fresh by the caller from
    // SceneManager's current object list before every recompile — see Part 4).
    // Use .at() not operator[] when looking up, so a missing/stale objectId throws
    // immediately instead of silently inserting a bogus zero-initialized entry.
    std::vector<CsgCommand> CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex) const;

    void ReadCsgArray(const std::string& filepath);
    void WriteCsgArray(const std::string& filepath) const;

    // Stream variants used to embed the tree as a section inside another file
    // (e.g. the .sdfo scene file). ReadCsgArray/WriteCsgArray delegate to these.
    void ReadFromStream(std::istream& in);
    void WriteToStream(std::ostream& out) const;

    // Root accessors (used by SceneManager to maintain the default root Union node).
    int32_t GetRoot() const { return m_Root; }
    void SetRoot(int32_t index) { m_Root = index; }

    const std::vector<CsgNode>& GetNodes() const { return m_Nodes; }

    void Clear();
    int32_t IndexForNodeId(int32_t nodeId) const;   // vector index of a live node, or -1
    void Detach(int32_t index);                     // unlink from parent, node stays alive
    void Reparent(int32_t childIndex, int32_t newParentIndex);


private:
    void CompileNode(int32_t nodeIndex, const std::unordered_map<int32_t, int32_t>& objectIdToIndex, std::vector<CsgCommand>& outProgram) const;

    std::vector<CsgNode> m_Nodes;
    int32_t m_Root = -1;
    int32_t m_NextNodeId = 0;
};
