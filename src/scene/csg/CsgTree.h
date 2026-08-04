#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <iosfwd>
#include "CsgNode.h"
#include "CsgCommand.h"
#include "scene/SceneBounds.h"   // NodeBoundsTable, used by value in CompileToRPN

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
        int32_t skipNodeCount = 0;        // SKIP_SUBTREE headers emitted (diagnostics)
    };

    // objectIdToIndex maps each SdfObjectData.objectId to its CURRENT real position
    // in the scene_objects vector at compile time (built fresh by the caller from
    // SceneManager's current object list before every recompile — see Part 4).
    // Use .at() not operator[] when looking up, so a missing/stale objectId throws
    // immediately instead of silently inserting a bogus zero-initialized entry.
    //
    // nodeBounds enables SPATIAL REGROUPING (P1d): a wide flat operation node is
    // emitted as a synthetic k-ary hierarchy of its children instead of one long
    // left-deep fold, so the program gains subtrees a runtime test can skip whole.
    // Pass nullptr to get the exact pre-P1d emission.
    //
    // Regrouping is only ever applied where ChildOrderIsFree says any permutation
    // is provably identity — hard Union/Group/Intersection. min and max are
    // associative AND commutative, so the regrouped program computes a bit-identical
    // distance; nothing about the rendered image may change until the skip commands
    // are actually consumed by the shader.
    CompiledProgram CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex,
                                 const NodeBoundsTable* nodeBounds = nullptr) const;

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

    // Everything the emission recursion needs that does not change during a single
    // compile. Bundled rather than passed as six parameters, because the recursion
    // threads all of it through every level and the long signature buried the two
    // that actually vary (the node, and the child range).
    struct CompileContext
    {
        const std::unordered_map<int32_t, int32_t>* objectIdToIndex = nullptr;
        const std::vector<int32_t>*                 subtreeDepth    = nullptr;   // ordering hint
        const NodeBoundsTable*                      nodeBounds      = nullptr;   // null = no regrouping
        std::vector<CsgCommand>*                    program         = nullptr;
        int32_t*                                    skipNodeCount   = nullptr;
    };

    // Computed DURING emission, never by a second traversal: a separate depth walk
    // would be a second copy of the emission rules, and the moment the two disagree
    // the bound is silently wrong and the shader guard silently eats geometry.
    // `negated` = this subtree's value will be sign-flipped an ODD number of times
    // before it reaches the final distance, i.e. it sits under the subtrahend side
    // of a Subtract (opSubtract(dB, dA) = max(-dB, dA), so every child past the
    // first is negated; opSmoothSubtract inherits that through opSmoothIntersection
    // with -dB).
    //
    // 🔴 This is the correctness condition for the whole optimisation. A skip
    // substitutes an UNDERESTIMATE of the subtree's distance, which is safe under
    // every operator we emit — min, max and both smooth variants are monotonically
    // increasing in each operand, so an underestimate in gives an underestimate
    // out, and the marcher only ever takes a shorter step than it could have.
    // Negation inverts exactly that: -underestimate is an OVERestimate, and an
    // overestimate is what lets a sphere-tracing step jump straight through a
    // surface. So skips are emitted only where `negated` is false. It is an XOR,
    // not a latch: A - (B - C) negates C twice and is safe again.
    CompileResult CompileNode(int32_t nodeIndex, const CompileContext& ctx, bool negated) const;

    // Emits children[lo, hi) as operands of opNode, folding one binary op command
    // after every operand past the first. Either flat (the pre-P1d behaviour) or,
    // when ShouldRegroup allows it, split into kRegroupArity contiguous sub-ranges
    // that each recurse — which is what turns a flat union into a hierarchy.
    //
    // Splitting a range into k sub-ranges at ONE level costs exactly +1 stack slot
    // regardless of k, because the fold is left-deep: peak = max(peak_0, 1 + max of
    // the rest). Depth is therefore (levels + leaf peak), and levels = log_k(N).
    // That is the whole reason the arity is 8 and not 2 — binary would need ~14
    // slots for 5000 objects, far past the measured occupancy valley at 8
    // (SdfRenderer's kCsgStackDepthBuckets), and cost more than the skip saves.
    CompileResult CompileChildRange(const std::vector<int32_t>& children,
                                    int32_t lo, int32_t hi,
                                    const CsgNode& opNode, CsgCmdType opCmd,
                                    const CompileContext& ctx, bool negated) const;

    // True iff this node's children may be emitted as a synthetic hierarchy.
    static bool ShouldRegroup(const CsgNode& node, size_t childCount, const CompileContext& ctx);

    // Reorders children into Z-order (Morton) of their subtree-bound centroids, so
    // that a contiguous sub-range of the array is also compact in space — which is
    // what makes the sub-range's bounding box tight enough to be worth testing.
    // Only ever called under ShouldRegroup, i.e. where reordering is identity.
    static void SortChildrenSpatially(std::vector<int32_t>& children, const NodeBoundsTable& bounds);

    // Children per internal level of the synthetic hierarchy, and the largest child
    // range still emitted flat. See the depth arithmetic on CompileChildRange.
    static constexpr int32_t kRegroupArity   = 8;
    static constexpr int32_t kRegroupLeafMax = 8;

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
