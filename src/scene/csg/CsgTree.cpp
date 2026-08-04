#include "CsgTree.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // File-local helper mapping an operation node type to its RPN command type.
    // Not a class member and not exported.
    static CsgCmdType OpNodeTypeToCmdType(CsgNodeType type)
    {
        switch (type)
        {
            case CsgNodeType::Union:        return CsgCmdType::OP_UNION;
            case CsgNodeType::Subtract:     return CsgCmdType::OP_SUBTRACT;
            case CsgNodeType::Intersection: return CsgCmdType::OP_INTERSECTION;
            case CsgNodeType::Group:        return CsgCmdType::OP_UNION;   // pure container == union
            default:                        return CsgCmdType::OP_UNION;
        }
    }

    static CsgCmdType SmoothOpNodeTypeToCmdType(CsgNodeType type)
    {
        switch (type)
        {
            case CsgNodeType::Union:        return CsgCmdType::OP_SMOOTH_UNION;
            case CsgNodeType::Subtract:     return CsgCmdType::OP_SMOOTH_SUBTRACT;
            case CsgNodeType::Intersection: return CsgCmdType::OP_SMOOTH_INTERSECTION;
            default:                        return CsgCmdType::OP_UNION;
        }
    }

    // Interleave the low 10 bits of v with two zero bits each ("bit expansion"),
    // the standard way to build a 30-bit Morton key from three 10-bit coordinates.
    static uint32_t ExpandBits10(uint32_t v)
    {
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }

    // Z-order key for a point already normalised to the unit cube. Sorting by this
    // key lands spatial neighbours next to each other in the array, which is what
    // lets CompileChildRange treat a contiguous slice as a compact spatial cluster
    // without building an explicit BVH. This is the LBVH construction trick.
    static uint32_t MortonCode01(float nx, float ny, float nz)
    {
        const auto quantize = [](float v) -> uint32_t
        {
            const float scaled = v * 1024.0f;
            // Written as `!(scaled > 0)` so NaN takes this branch too — a NaN
            // centroid must produce a defined key, not an undefined cast.
            if (!(scaled > 0.0f))    return 0u;
            if (scaled >= 1023.0f)   return 1023u;
            return static_cast<uint32_t>(scaled);
        };
        return (ExpandBits10(quantize(nx)) << 2)
             | (ExpandBits10(quantize(ny)) << 1)
             |  ExpandBits10(quantize(nz));
    }

    // memcpy, not a union or a reinterpret_cast: type-punning through either is
    // undefined behaviour in C++, and every compiler here folds this to a move.
    static int32_t FloatBits(float value)
    {
        int32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    // Union of the subtree boxes of children[lo, hi).
    //
    // Returns false if ANY child in the range lacks a usable box, and the caller
    // must then emit that range with no skip header at all. This is the untrusted-
    // input path (CLAUDE.md §11): SceneBounds leaves the box empty for a primitive
    // whose position or size is non-finite, but CompileNode still emits a
    // PUSH_OBJECT for it. Bounding the range as if that child were not there would
    // produce a box that does not contain everything the range draws — the exact
    // under-covering bound that made the first culling attempt clip geometry.
    // An empty group also lands here and merely loses a skip it did not need.
    static bool ComputeRangeBounds(const std::vector<int32_t>& children,
                                   int32_t lo, int32_t hi,
                                   const NodeBoundsTable& bounds,
                                   SceneAABB& out)
    {
        out = SceneAABB{};   // inverted == empty
        for (int32_t i = lo; i < hi; ++i)
        {
            const int32_t child = children[static_cast<size_t>(i)];
            if (!bounds.InRange(child)) return false;

            const SceneAABB& box = bounds.byNodeIndex[static_cast<size_t>(child)];
            if (box.IsEmpty()) return false;

            for (int axis = 0; axis < 3; ++axis)
            {
                out.minBoxDistances[axis] = std::min(out.minBoxDistances[axis], box.minBoxDistances[axis]);
                out.maxBoxDistances[axis] = std::max(out.maxBoxDistances[axis], box.maxBoxDistances[axis]);
            }
        }
        return !out.IsEmpty();
    }

    // SKIP_SUBTREE + the two SKIP_BOUNDS carrying the six AABB floats. The jump
    // distance is left at 0 and patched by the caller once the subtree has been
    // emitted and its length is known — the classic forward-reference backpatch,
    // and the reason the header cannot simply be built after the body.
    static void EmitSkipHeader(const SceneAABB& box, int32_t nodeId, std::vector<CsgCommand>& program)
    {
        CsgCommand header{};
        header.cmdType   = CsgCmdType::SKIP_SUBTREE;
        header.operand   = 0;
        header.nodeId    = nodeId;
        header.parameter = 0.0f;
        program.push_back(header);

        CsgCommand low{};
        low.cmdType   = CsgCmdType::SKIP_BOUNDS;
        low.operand   = FloatBits(box.minBoxDistances[0]);
        low.nodeId    = FloatBits(box.minBoxDistances[1]);
        low.parameter = box.minBoxDistances[2];
        program.push_back(low);

        CsgCommand high{};
        high.cmdType   = CsgCmdType::SKIP_BOUNDS;
        high.operand   = FloatBits(box.maxBoxDistances[0]);
        high.nodeId    = FloatBits(box.maxBoxDistances[1]);
        high.parameter = box.maxBoxDistances[2];
        program.push_back(high);
    }
}

int32_t CsgTree::AddPrimitiveNode(int32_t objectId, int32_t parent)
{
    CsgNode node;
    node.type = CsgNodeType::Primitive;
    node.nodeId = m_NextNodeId++;   // Never derive nodeId from vector size or index.
    node.objectId = objectId;

    m_Nodes.push_back(node);
    int32_t index = static_cast<int32_t>(m_Nodes.size()) - 1;

    if (parent != -1)
    {
        AttachChild(parent, index);
    }
    else if (m_Root == -1)
    {
        m_Root = index;
    }

    return index;
}

int32_t CsgTree::AddOperationNode(CsgNodeType opType, int32_t parent)
{
    CsgNode node;
    node.type = opType;
    node.nodeId = m_NextNodeId++;   // Never derive nodeId from vector size or index.

    m_Nodes.push_back(node);
    int32_t index = static_cast<int32_t>(m_Nodes.size()) - 1;

    if (parent != -1)
    {
        AttachChild(parent, index);
    }
    else if (m_Root == -1)
    {
        m_Root = index;
    }

    return index;
}

bool CsgTree::RemoveLegacyModifierNode(int32_t modifierIndex)
{
    if (modifierIndex < 0 || modifierIndex >= static_cast<int32_t>(m_Nodes.size())) return false;
    CsgNode& modifier = m_Nodes[modifierIndex];
    if (!modifier.alive || modifier.type != CsgNodeType::Modifier || modifier.firstChild == -1) return false;
    const int32_t child = modifier.firstChild;
    const int32_t parent = modifier.parent;
    const int32_t prev = modifier.previousSibling;
    const int32_t next = modifier.nextSibling;
    CsgNode& childNode = m_Nodes[child];
    childNode.parent = parent;
    childNode.previousSibling = prev;
    childNode.nextSibling = next;
    if (parent != -1)
    {
        CsgNode& parentNode = m_Nodes[parent];
        if (prev != -1) m_Nodes[prev].nextSibling = child; else parentNode.firstChild = child;
        if (next != -1) m_Nodes[next].previousSibling = child; else parentNode.lastChild = child;
    }
    else m_Root = child;
    modifier.firstChild = modifier.lastChild = -1;
    modifier.parent = modifier.previousSibling = modifier.nextSibling = -1;
    modifier.alive = false;
    return true;
}

void CsgTree::AttachChild(int32_t parentIndex, int32_t childIndex)
{
    CsgNode& parent = m_Nodes[parentIndex];
    CsgNode& child = m_Nodes[childIndex];

    child.parent = parentIndex;
    child.nextSibling = -1;
    child.previousSibling = parent.lastChild;   // -1 when parent has no children yet.

    if (parent.firstChild == -1)
    {
        // First child of this parent.
        parent.firstChild = childIndex;
    }
    else
    {
        // Append after the current last child (O(1) thanks to lastChild).
        m_Nodes[parent.lastChild].nextSibling = childIndex;
    }

    parent.lastChild = childIndex;
}

void CsgTree::RemoveNode(int32_t index)
{
    if (index < 0 || index >= static_cast<int32_t>(m_Nodes.size()))
    {
        return;
    }

    CsgNode& node = m_Nodes[index];
    if (!node.alive)
    {
        return;
    }

    Detach(index);

    // Recursively remove all children first (subtree removal). Capture the next
    // sibling before recursing because RemoveNode mutates sibling links.
    int32_t child = node.firstChild;
    while (child != -1)
    {
        int32_t childNext = m_Nodes[child].nextSibling;
        RemoveNode(child);
        child = childNext;
    }

    node.firstChild = -1;
    node.lastChild = -1;
    node.alive = false;

    if (m_Root == index)
    {
        m_Root = -1;
    }
}

bool CsgTree::ChildOrderIsFree(const CsgNode& node)
{
    // min (Union/Group) and max (Intersection) are associative AND commutative, so
    // any permutation of the children produces the identical distance field.
    //
    // Everything else is excluded on purpose:
    //   Subtract — not commutative in its first operand; the base child must stay
    //              first, and the RPN folding makes later operands positional.
    //   Smooth * — not even associative. opSmoothUnion(opSmoothUnion(a,b),c) is not
    //              opSmoothUnion(a,opSmoothUnion(b,c)); reordering would silently
    //              change the rendered shape, which is exactly the class of bug
    //              G_correctness_17 exists to catch.
    if (node.blendMode == BlendMode::Smooth) return false;
    return node.type == CsgNodeType::Union
        || node.type == CsgNodeType::Group
        || node.type == CsgNodeType::Intersection;
}

int32_t CsgTree::FillSubtreeDepths(int32_t nodeIndex, std::vector<int32_t>& out) const
{
    const CsgNode& node = m_Nodes[nodeIndex];
    if (!node.alive)
    {
        out[static_cast<size_t>(nodeIndex)] = 0;
        return 0;
    }

    if (node.type == CsgNodeType::Primitive)
    {
        out[static_cast<size_t>(nodeIndex)] = 1;
        return 1;
    }

    // Same recurrence as CompileNode, and the same ordering, so the hint reflects
    // what emission will actually do. Children that emit nothing (depth 0) are
    // skipped here exactly as they are there.
    std::vector<int32_t> children;
    for (int32_t child = node.firstChild; child != -1; child = m_Nodes[child].nextSibling)
    {
        if (!m_Nodes[child].alive) continue;
        if (FillSubtreeDepths(child, out) == 0) continue;
        children.push_back(child);
    }

    if (ChildOrderIsFree(node))
    {
        std::stable_sort(children.begin(), children.end(),
            [&out](int32_t a, int32_t b) { return out[static_cast<size_t>(a)] > out[static_cast<size_t>(b)]; });
    }

    int32_t height = 0;
    int32_t peak = 0;
    int32_t compiled = 0;
    for (const int32_t child : children)
    {
        peak = std::max(peak, height + out[static_cast<size_t>(child)]);
        height += 1;
        if (++compiled >= 2) height -= 1;
    }

    out[static_cast<size_t>(nodeIndex)] = peak;
    return peak;
}

CsgTree::CompiledProgram CsgTree::CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex,
                                               const NodeBoundsTable* nodeBounds) const
{
    CompiledProgram compiled;

    if (m_Root >= 0 && m_Root < static_cast<int32_t>(m_Nodes.size()) && m_Nodes[m_Root].alive)
    {
        // One O(nodes) pass to learn every subtree's cost, so emission can order
        // siblings cheapest-last without re-descending per comparison.
        std::vector<int32_t> subtreeDepth(m_Nodes.size(), 0);
        FillSubtreeDepths(m_Root, subtreeDepth);

        // The table must cover the node vector, or an index into it is a lie. A
        // size mismatch means the caller built it against a different tree state,
        // so drop it rather than regroup on stale geometry.
        const NodeBoundsTable* usableBounds =
            (nodeBounds && nodeBounds->byNodeIndex.size() == m_Nodes.size()) ? nodeBounds : nullptr;

        CompileContext ctx;
        ctx.objectIdToIndex = &objectIdToIndex;
        ctx.subtreeDepth    = &subtreeDepth;
        ctx.nodeBounds      = usableBounds;
        ctx.program         = &compiled.commands;
        ctx.skipNodeCount   = &compiled.skipNodeCount;

        // The root's own value is the final distance and is never negated.
        compiled.requiredStackDepth = CompileNode(m_Root, ctx, /*negated=*/false).peak;
    }

    // An empty tree legitimately needs depth 0 — not a sentinel, the shader's
    // `arrayLength(&csg_program) == 0` early-out never touches the stack.
    return compiled;
}

bool CsgTree::ShouldRegroup(const CsgNode& node, size_t childCount, const CompileContext& ctx)
{
    // ChildOrderIsFree is exactly the right predicate, and it is doing MORE work
    // here than it does for the Sethi-Ullman reorder. Reordering needs only
    // commutativity; regrouping also needs ASSOCIATIVITY, because it changes which
    // operands each binary op sees. min and max have both. Subtract has neither in
    // the relevant sense, and the smooth ops are not associative at all — which is
    // why a smooth chain must keep its authored shape no matter how wide it is.
    return ctx.nodeBounds != nullptr
        && ChildOrderIsFree(node)
        && childCount > static_cast<size_t>(kRegroupLeafMax);
}

void CsgTree::SortChildrenSpatially(std::vector<int32_t>& children, const NodeBoundsTable& bounds)
{
    // Normalise against the extent of just these children, not the whole scene:
    // Morton keys only discriminate to 1/1024 of the range they are quantised
    // against, so using the scene extent for a small cluster would collapse every
    // child onto the same key and the sort would degenerate to a no-op.
    float lo[3] = {  1e30f,  1e30f,  1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const int32_t child : children)
    {
        if (!bounds.InRange(child)) continue;
        const SceneAABB& box = bounds.byNodeIndex[static_cast<size_t>(child)];
        if (box.IsEmpty()) continue;
        for (int axis = 0; axis < 3; ++axis)
        {
            lo[axis] = std::min(lo[axis], box.minBoxDistances[axis]);
            hi[axis] = std::max(hi[axis], box.maxBoxDistances[axis]);
        }
    }

    float inverseExtent[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        const float extent = hi[axis] - lo[axis];
        // A zero-extent axis (every child on one plane, or nothing bounded at all)
        // would divide by zero; 0 collapses that axis to key bit 0 for everyone,
        // which is the correct degenerate answer — it carries no information.
        inverseExtent[axis] = (extent > 1e-6f) ? (1.0f / extent) : 0.0f;
    }

    std::vector<std::pair<uint32_t, int32_t>> keyed;
    keyed.reserve(children.size());
    for (const int32_t child : children)
    {
        uint32_t key = 0u;
        if (bounds.InRange(child))
        {
            const SceneAABB& box = bounds.byNodeIndex[static_cast<size_t>(child)];
            if (!box.IsEmpty())
            {
                float normalised[3];
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float centre = 0.5f * (box.minBoxDistances[axis] + box.maxBoxDistances[axis]);
                    normalised[axis] = (centre - lo[axis]) * inverseExtent[axis];
                }
                key = MortonCode01(normalised[0], normalised[1], normalised[2]);
            }
        }
        keyed.emplace_back(key, child);
    }

    // stable_sort, so children sharing a key keep their authored order and the
    // emitted program stays byte-identical across runs.
    std::stable_sort(keyed.begin(), keyed.end(),
        [](const std::pair<uint32_t, int32_t>& a, const std::pair<uint32_t, int32_t>& b)
        { return a.first < b.first; });

    for (size_t i = 0; i < keyed.size(); ++i) children[i] = keyed[i].second;
}

CsgTree::CompileResult CsgTree::CompileNode(int32_t nodeIndex, const CompileContext& ctx, bool negated) const
{
    const CsgNode& node = m_Nodes[nodeIndex];
    if (!node.alive)
    {
        return {};   // emits nothing, leaves nothing
    }

    if (node.type == CsgNodeType::Primitive)
    {
        // Translate the permanent objectId to the current real array index.
        // .at() throws on a missing/stale objectId instead of inserting a bogus entry.
        CsgCommand cmd{};
        cmd.cmdType = CsgCmdType::PUSH_OBJECT;
        cmd.operand = ctx.objectIdToIndex->at(node.objectId);
        cmd.nodeId = node.nodeId;
        cmd.parameter = node.modifierEnabled && node.modifierType == ModifierType::Roundness
            ? std::max(node.modifierAmount, 0.0f) : 0.0f;
        ctx.program->push_back(cmd);
        return { 1, 1 };   // one push: peaks at 1, leaves 1
    }

    // Operation node: postorder walk of the child sibling chain. After every child
    // past the first, emit one binary op command. N children -> N-1 op commands
    // (n-ary-to-binary folding).
    const CsgCmdType opCmd = node.blendMode == BlendMode::Smooth
        ? SmoothOpNodeTypeToCmdType(node.type) : OpNodeTypeToCmdType(node.type);

    std::vector<int32_t> children;
    for (int32_t child = node.firstChild; child != -1; child = m_Nodes[child].nextSibling)
    {
        if (m_Nodes[child].alive) children.push_back(child);
    }
    if (children.empty()) return {};

    if (ShouldRegroup(node, children.size(), ctx))
    {
        // Spatial order REPLACES the depth order below rather than composing with
        // it. The two want opposite things — Sethi-Ullman wants the deepest child
        // first, Morton wants spatial neighbours adjacent — and here spatial wins,
        // because a sub-range that is not compact in space has a bounding box too
        // loose for any runtime test to reject. The depth the regrouping costs is
        // bounded and known (one slot per level); the looseness is not.
        SortChildrenSpatially(children, *ctx.nodeBounds);
    }
    else if (ChildOrderIsFree(node))
    {
        // Sibling order changes the peak: the `height + childResult.peak` term in
        // CompileChildRange charges a compound child for everything its earlier
        // siblings left on the stack, so a deep child costs nothing when it goes
        // FIRST and costs the full running height when it goes last. Emitting
        // deepest-first therefore minimises the peak — Sethi-Ullman ordering.
        //
        // Ordering only; the hint is never used to decide whether a child emits
        // (that stays with the authoritative childResult.yield), so a stale or
        // wrong hint can only cost depth, never correctness. FillSubtreeDepths does
        // not model regrouping, so on a regrouped subtree the hint now UNDERSTATES
        // the real depth — which is fine for exactly that reason, and why the
        // reported requiredStackDepth still comes from the emission itself.
        const std::vector<int32_t>& subtreeDepth = *ctx.subtreeDepth;
        std::stable_sort(children.begin(), children.end(),
            [&subtreeDepth](int32_t a, int32_t b)
            { return subtreeDepth[static_cast<size_t>(a)] > subtreeDepth[static_cast<size_t>(b)]; });
    }

    return CompileChildRange(children, 0, static_cast<int32_t>(children.size()), node, opCmd, ctx, negated);
}

CsgTree::CompileResult CsgTree::CompileChildRange(const std::vector<int32_t>& children,
                                                  int32_t lo, int32_t hi,
                                                  const CsgNode& opNode, CsgCmdType opCmd,
                                                  const CompileContext& ctx, bool negated) const
{
    const int32_t count = hi - lo;

    // Stack bookkeeping, mirroring the emission exactly.
    //   height   — values this range's operands have left on the stack so far
    //   peak     — highest height reached anywhere inside it
    //   operands — how many actually emitted something (drives the op count)
    int32_t height   = 0;
    int32_t peak     = 0;
    int32_t operands = 0;

    // Folding one operand in, identical whether that operand is a real child node
    // or a synthetic sub-range. Keeping it in one place is what makes the two
    // branches below provably equivalent in stack behaviour.
    const auto fold = [&](const CompileResult& operand)
    {
        // Alive but emitted nothing (an empty group, or one holding only dead
        // nodes). Counting it would emit an op with only one value beneath it —
        // harmless at runtime because the shader guards with `sp >= 2`, but it
        // leaves a dead command in a program the shader walks at every march step.
        if (operand.yield == 0) return;

        // The operand's peak is measured from zero, but its commands execute on top
        // of whatever earlier operands left behind — so offset it by the current
        // height. This term is the entire reason a compound operand costs depth
        // when it is NOT first and costs none when it is.
        peak = std::max(peak, height + operand.peak);
        height += 1;
        ++operands;

        if (operands >= 2)
        {
            CsgCommand cmd{};
            cmd.cmdType = opCmd;
            cmd.operand = 0;
            cmd.nodeId = opNode.nodeId;
            cmd.parameter = opNode.blendMode == BlendMode::Smooth
                ? std::max(opNode.blendRadius, 0.0f) : 0.0f;
            ctx.program->push_back(cmd);
            height -= 1;   // the op pops two and pushes one
        }
    };

    if (!ShouldRegroup(opNode, static_cast<size_t>(count), ctx))
    {
        // A flat leaf range has no sub-ranges, so ordering inside it cannot affect
        // what a runtime skip test can reject — only the stack peak. So the
        // Sethi-Ullman order is restored HERE, on the slice, undoing locally what
        // the Morton sort had to do globally.
        //
        // This is not cosmetic. G_correctness_17 mixes a ~6-deep chain with eight
        // shallow siblings; leaving that chain in Z-order can put it last in its
        // bucket, which adds the full running height to the deepest subtree in the
        // scene and pushes the program from depth 6 to depth 8. One more and it
        // would cross into the depth-16 shader variant, which §6b measured at
        // 1.5-2x the frame time — a regression far larger than any skip could pay
        // back, arriving from a sort order nobody would think to look at.
        // Only Subtract flips the sign, and only for operands past the first: the
        // RPN fold makes the first EMITTED operand the base (see SelectionState —
        // "click A, then B, Subtract" is A - B). `operands` is incremented inside
        // fold(), so reading it before the call gives exactly "how many operands
        // already landed", which is the positional test the base rule needs.
        const bool subtractsOperands = (opNode.type == CsgNodeType::Subtract);

        if (count > 1 && ChildOrderIsFree(opNode) && ctx.subtreeDepth)
        {
            std::vector<int32_t> slice(children.begin() + lo, children.begin() + hi);
            const std::vector<int32_t>& subtreeDepth = *ctx.subtreeDepth;
            std::stable_sort(slice.begin(), slice.end(),
                [&subtreeDepth](int32_t a, int32_t b)
                { return subtreeDepth[static_cast<size_t>(a)] > subtreeDepth[static_cast<size_t>(b)]; });
            // ChildOrderIsFree already excludes Subtract, so nothing here is ever
            // negated by this node — but the flag still has to carry down from
            // above, because this range may sit inside someone else's subtrahend.
            for (const int32_t child : slice) fold(CompileNode(child, ctx, negated));
        }
        else
        {
            for (int32_t i = lo; i < hi; ++i)
            {
                const bool childNegated = negated != (subtractsOperands && operands > 0);
                fold(CompileNode(children[static_cast<size_t>(i)], ctx, childNegated));
            }
        }
    }
    else
    {
        // Splitting count=9 into 8 buckets would make seven of them hold a single
        // primitive, and a one-primitive bucket is not a hierarchy — it is P1a,
        // which was measured at ~12% and removed for being a poor return. Worse,
        // each bucket will carry a fixed command overhead once the skip metadata
        // lands, so tiny buckets cost program size for nothing. Aim every bucket at
        // roughly kRegroupLeafMax entries and let the arity fall out of that; the
        // floor of 2 is what guarantees the recursion still terminates.
        // Rounded UP, so that whenever count <= kRegroupArity * kRegroupLeafMax the
        // split lands entirely on flat leaves and costs exactly one level. Rounding
        // down would leave an oversized remainder that has to recurse again, adding
        // a level (and a stack slot) purely to arithmetic.
        const int32_t bucketsNeeded = (count + kRegroupLeafMax - 1) / kRegroupLeafMax;
        const int32_t arity = std::max(2, std::min(kRegroupArity, bucketsNeeded));

        // Even contiguous slices of an array already sorted into Z-order. Computing
        // the split points from the running index (rather than a fixed slice size)
        // keeps every bucket within one element of the others when count is not a
        // multiple of the arity — an even split is what bounds the depth at
        // log_arity(count); a lopsided one degenerates toward a linear chain.
        for (int32_t bucket = 0; bucket < arity; ++bucket)
        {
            const int64_t wide = static_cast<int64_t>(count);
            const int32_t bucketLo = lo + static_cast<int32_t>((wide *  bucket)      / arity);
            const int32_t bucketHi = lo + static_cast<int32_t>((wide * (bucket + 1)) / arity);
            if (bucketHi <= bucketLo) continue;

            // Where the header would start, so it can be patched — or unwound.
            const int32_t headerIndex = static_cast<int32_t>(ctx.program->size());

            SceneAABB bucketBox;
            const bool skippable = !negated
                && ComputeRangeBounds(children, bucketLo, bucketHi, *ctx.nodeBounds, bucketBox);

            if (skippable) EmitSkipHeader(bucketBox, opNode.nodeId, *ctx.program);

            const CompileResult emitted =
                CompileChildRange(children, bucketLo, bucketHi, opNode, opCmd, ctx, negated);

            if (emitted.yield == 0)
            {
                // The bucket emitted nothing (every child dead or empty), so the
                // header now introduces a subtree that does not exist. yield == 0
                // means literally no commands were appended, so truncating back to
                // headerIndex removes exactly the header and nothing else.
                ctx.program->resize(static_cast<size_t>(headerIndex));
                continue;
            }

            if (skippable)
            {
                // Backpatch: jump from the header to just past the subtree, and NOT
                // past the fold's op command below — a skip still leaves one value
                // on the stack, so the op that consumes it must always run.
                (*ctx.program)[static_cast<size_t>(headerIndex)].operand =
                    static_cast<int32_t>(ctx.program->size()) - headerIndex;
                if (ctx.skipNodeCount) ++(*ctx.skipNodeCount);
            }

            fold(emitted);
        }
    }

    return { peak, operands > 0 ? 1 : 0 };
}

CsgNode* CsgTree::FindNodeById(int32_t nodeId)
{
    const int32_t index = IndexForNodeId(nodeId);
    return index == -1 ? nullptr : &m_Nodes[index];
}

const CsgNode* CsgTree::FindNodeById(int32_t nodeId) const
{
    const int32_t index = IndexForNodeId(nodeId);
    return index == -1 ? nullptr : &m_Nodes[index];
}

void CsgTree::Clear()
{
    m_Nodes.clear();
    m_Root = -1;
    m_NextNodeId = 0;
}

CsgTree::State CsgTree::CaptureState() const
{
    State state;
    state.nodes = m_Nodes;
    state.root = m_Root;
    state.nextNodeId = m_NextNodeId;
    return state;
}

void CsgTree::RestoreState(const State& state)
{
    m_Nodes = state.nodes;
    m_Root = state.root;
    m_NextNodeId = state.nextNodeId;
}

void CsgTree::WriteToStream(std::ostream& out) const
{
    // Negative marker distinguishes the extended node layout from legacy raw arrays.
    const int32_t formatMarker = -2;
    out.write(reinterpret_cast<const char*>(&formatMarker), sizeof(formatMarker));
    int32_t nodeCount = static_cast<int32_t>(m_Nodes.size());
    out.write(reinterpret_cast<const char*>(&nodeCount), sizeof(int32_t));
    if (nodeCount > 0)
    {
        out.write(reinterpret_cast<const char*>(m_Nodes.data()), nodeCount * sizeof(CsgNode));
    }
    out.write(reinterpret_cast<const char*>(&m_Root), sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(&m_NextNodeId), sizeof(int32_t));
}

void CsgTree::ReadFromStream(std::istream& in)
{
    // Tolerate EOF gracefully: if the section is absent, leave the tree empty.
    int32_t markerOrCount = 0;
    in.read(reinterpret_cast<char*>(&markerOrCount), sizeof(int32_t));
    if (!in)
    {
        Clear();
        return;
    }

    const bool extended = markerOrCount == -2;
    int32_t nodeCount = markerOrCount;
    if (extended) in.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    if (!in || nodeCount < 0) { Clear(); return; }

    m_Nodes.assign(static_cast<size_t>(nodeCount), CsgNode{});
    if (extended && nodeCount > 0)
    {
        in.read(reinterpret_cast<char*>(m_Nodes.data()), nodeCount * sizeof(CsgNode));
    }
    else if (nodeCount > 0)
    {
        struct LegacyCsgNode
        {
            CsgNodeType type;
            int32_t nodeId;
            int32_t objectId;
            int32_t parent;
            int32_t previousSibling;
            int32_t nextSibling;
            int32_t firstChild;
            int32_t lastChild;
            bool alive;
        };
        std::vector<LegacyCsgNode> legacy(static_cast<size_t>(nodeCount));
        in.read(reinterpret_cast<char*>(legacy.data()), nodeCount * sizeof(LegacyCsgNode));
        for (int32_t i = 0; i < nodeCount; ++i)
        {
            CsgNode& n = m_Nodes[static_cast<size_t>(i)];
            const LegacyCsgNode& old = legacy[static_cast<size_t>(i)];
            n.type = old.type; n.nodeId = old.nodeId; n.objectId = old.objectId;
            n.parent = old.parent; n.previousSibling = old.previousSibling;
            n.nextSibling = old.nextSibling; n.firstChild = old.firstChild;
            n.lastChild = old.lastChild; n.alive = old.alive;
        }
    }
    in.read(reinterpret_cast<char*>(&m_Root), sizeof(int32_t));
    in.read(reinterpret_cast<char*>(&m_NextNodeId), sizeof(int32_t));

    if (!in)
    {
        // Truncated/partial section — fall back to an empty tree.
        Clear();
    }
    else if (!LinksAreValid())
    {
        // Corrupt/hostile file: a link points outside the node vector. Dropping the
        // tree here means SceneManager::RebuildAfterLoad rebuilds a flat default
        // union from the objects, so the scene still loads (just ungrouped) instead
        // of dereferencing an out-of-range index in CompileNode / GetSceneJson.
        Clear();
    }
    else
    {
        NormalizeLegacyModifierNodes();
    }
}

bool CsgTree::LinksAreValid() const
{
    const int32_t n = static_cast<int32_t>(m_Nodes.size());
    const auto inRange = [n](int32_t idx) { return idx >= -1 && idx < n; };

    if (!inRange(m_Root)) return false;
    for (const CsgNode& node : m_Nodes)
    {
        if (!inRange(node.parent) || !inRange(node.firstChild) || !inRange(node.lastChild) ||
            !inRange(node.nextSibling) || !inRange(node.previousSibling))
        {
            return false;
        }
    }
    return true;
}

void CsgTree::NormalizeLegacyModifierNodes()
{
    for (int32_t i = 0; i < static_cast<int32_t>(m_Nodes.size()); ++i)
    {
        CsgNode& legacy = m_Nodes[i];
        if (!legacy.alive || legacy.type != CsgNodeType::Modifier || legacy.firstChild == -1) continue;
        CsgNode& target = m_Nodes[legacy.firstChild];
        target.modifierType = legacy.modifierType;
        target.modifierAmount = legacy.modifierAmount;
        target.modifierEnabled = legacy.modifierEnabled;
        RemoveLegacyModifierNode(i);
    }
}

void CsgTree::WriteCsgArray(const std::string& filepath) const
{
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "[CsgTree] Failed to open file for writing: " << filepath << "\n";
        return;
    }
    WriteToStream(file);
}

void CsgTree::ReadCsgArray(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "[CsgTree] Failed to open file for reading: " << filepath << "\n";
        return;
    }
    ReadFromStream(file);
}

int32_t CsgTree::IndexForNodeId(int32_t nodeID) const{
    for (int32_t i = 0; i < static_cast<int32_t>(m_Nodes.size()); i++)
    {
        // Skip tombstoned nodes: a dead nodeId must resolve to -1, otherwise
        // callers act on a removed node.
        if (m_Nodes[i].alive && nodeID == m_Nodes[i].nodeId)
        {
            return i;
        }
    }
    return -1;
}

void CsgTree::Detach(int32_t index){

    CsgNode& node = m_Nodes[index];
    // Detach this node from its parent's sibling chain, handling first-child and
    // last-child edge cases (previousSibling / nextSibling being -1).
    int32_t parentIndex = node.parent;
    int32_t prev = node.previousSibling;
    int32_t next = node.nextSibling;

    if (prev != -1)
    {
        m_Nodes[prev].nextSibling = next;
    }
    if (next != -1)
    {
        m_Nodes[next].previousSibling = prev;
    }
    if (parentIndex != -1)
    {
        CsgNode& parent = m_Nodes[parentIndex];
        if (parent.firstChild == index)
        {
            parent.firstChild = next;
        }
        if (parent.lastChild == index)
        {
            parent.lastChild = prev;
        }
    }

    node.parent = -1;
    node.previousSibling = -1;
    node.nextSibling = -1;

}

void CsgTree::Reparent(int32_t childIndex, int32_t newParentIndex){
    Detach(childIndex);
    AttachChild(newParentIndex, childIndex);
}
