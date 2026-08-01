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
    struct State
    {
        std::vector<CsgNode> nodes;
        int32_t root = -1;
        int32_t nextNodeId = 0;
    };

    int32_t AddPrimitiveNode(int32_t objectId, int32_t parent = -1);
    int32_t AddOperationNode(CsgNodeType opType, int32_t parent = -1);
    void AttachChild(int32_t parentIndex, int32_t childIndex);
    void RemoveNode(int32_t index); // Recursively tombstones the entire subtree; detaches
                                    // from the parent's sibling chain using
                                    // previousSibling/nextSibling/firstChild/lastChild.

    // The RPN program plus the peak stack height it reaches.
    //
    // The shader's RPN stacks are fixed-size (CSG_STACK_DEPTH in sdf_core.wgsl) and
    // guard every push with `if (sp < CSG_STACK_DEPTH)`. A program whose peak exceeds
    // that has its extra pushes silently DROPPED — the scene renders with missing
    // geometry and no error anywhere. requiredStackDepth is what makes that
    // checkable: the program is safe iff CSG_STACK_DEPTH >= requiredStackDepth.
    //
    // Depth grows with CSG *nesting*, not object count: a flat union of N primitives
    // peaks at 2 for any N, because CompileNode emits an op after every child past
    // the first. A balanced binary tree of height k peaks at k+1.
    struct CompiledProgram
    {
        std::vector<CsgCommand> commands;
        int32_t requiredStackDepth = 0;   // 0 = empty program
    };

    // objectIdToIndex maps each SdfObjectData.objectId to its CURRENT real position
    // in the scene_objects vector at compile time (built fresh by the caller from
    // SceneManager's current object list before every recompile — see Part 4).
    // Use .at() not operator[] when looking up, so a missing/stale objectId throws
    // immediately instead of silently inserting a bogus zero-initialized entry.
    CompiledProgram CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex) const;

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
    State CaptureState() const;
    void RestoreState(const State& state);
    int32_t IndexForNodeId(int32_t nodeId) const;   // vector index of a live node, or -1
    void Detach(int32_t index);                     // unlink from parent, node stays alive
    void Reparent(int32_t childIndex, int32_t newParentIndex);
    CsgNode* FindNodeById(int32_t nodeId);
    const CsgNode* FindNodeById(int32_t nodeId) const;


private:
    // What one subtree's emitted subprogram does to the stack.
    //   peak  — highest stack height reached anywhere inside it, measured from 0
    //   yield — 1 if it left a value behind, 0 if it emitted nothing at all
    //           (an operation node whose children are all dead or all empty emits
    //           no commands; treating that as "left a value" is what would make the
    //           depth wrong and the op count wrong — see CompileNode).
    struct CompileResult
    {
        int32_t peak = 0;
        int32_t yield = 0;
    };

    // Computed DURING emission, never by a second traversal: a separate depth walk
    // would be a second copy of the emission rules, and the moment the two disagree
    // the bound is silently wrong and the shader guard silently eats geometry.
    //
    // subtreeDepth is the sibling-ordering hint from FillSubtreeDepths (below),
    // indexed by node vector index.
    CompileResult CompileNode(int32_t nodeIndex, const std::unordered_map<int32_t, int32_t>& objectIdToIndex, const std::vector<int32_t>& subtreeDepth, std::vector<CsgCommand>& outProgram) const;

    // Peak stack height each subtree would reach, written into out[nodeIndex] by a
    // single O(nodes) post-order pass before emission. Returns the same value for
    // nodeIndex; 0 means "this subtree emits nothing at all".
    //
    // This mirrors the recurrence in CompileNode and exists ONLY to order siblings.
    // If the two ever drift apart the ordering merely becomes suboptimal — the depth
    // CompileToRPN reports still comes from the real emission, so a mismatch here can
    // never under-size the shader's stack.
    int32_t FillSubtreeDepths(int32_t nodeIndex, std::vector<int32_t>& out) const;

    // True iff reordering this node's children is provably identity, i.e. the
    // operator is both associative and commutative. Only hard Union/Group/
    // Intersection qualify — see the comment at the call site.
    static bool ChildOrderIsFree(const CsgNode& node);
    void NormalizeLegacyModifierNodes();
    bool RemoveLegacyModifierNode(int32_t modifierIndex);
    // True iff m_Root and every node link (parent/child/sibling) is either -1 or a
    // valid vector index. Deserialized (untrusted) files can carry out-of-range
    // links that would be dereferenced with operator[] later -> heap OOB read.
    bool LinksAreValid() const;

    std::vector<CsgNode> m_Nodes;
    int32_t m_Root = -1;
    int32_t m_NextNodeId = 0;
};
