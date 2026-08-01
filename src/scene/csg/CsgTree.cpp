#include "CsgTree.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

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

CsgTree::CompiledProgram CsgTree::CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex) const
{
    CompiledProgram compiled;

    if (m_Root >= 0 && m_Root < static_cast<int32_t>(m_Nodes.size()) && m_Nodes[m_Root].alive)
    {
        // One O(nodes) pass to learn every subtree's cost, so emission can order
        // siblings cheapest-last without re-descending per comparison.
        std::vector<int32_t> subtreeDepth(m_Nodes.size(), 0);
        FillSubtreeDepths(m_Root, subtreeDepth);

        compiled.requiredStackDepth = CompileNode(m_Root, objectIdToIndex, subtreeDepth, compiled.commands).peak;
    }

    // An empty tree legitimately needs depth 0 — not a sentinel, the shader's
    // `arrayLength(&csg_program) == 0` early-out never touches the stack.
    return compiled;
}

CsgTree::CompileResult CsgTree::CompileNode(int32_t nodeIndex, const std::unordered_map<int32_t, int32_t>& objectIdToIndex, const std::vector<int32_t>& subtreeDepth, std::vector<CsgCommand>& outProgram) const
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
        cmd.operand = objectIdToIndex.at(node.objectId);
        cmd.nodeId = node.nodeId;
        cmd.parameter = node.modifierEnabled && node.modifierType == ModifierType::Roundness
            ? std::max(node.modifierAmount, 0.0f) : 0.0f;
        outProgram.push_back(cmd);
        return { 1, 1 };   // one push: peaks at 1, leaves 1
    }

    // Operation node: postorder walk of the child sibling chain. After every child
    // past the first, emit one binary op command. N children -> N-1 op commands
    // (n-ary-to-binary folding).
    const CsgCmdType opCmd = node.blendMode == BlendMode::Smooth
        ? SmoothOpNodeTypeToCmdType(node.type) : OpNodeTypeToCmdType(node.type);
    int32_t compiledChildren = 0;

    // Stack bookkeeping, mirroring the emission below exactly.
    //   height — values this node's children have left on the stack so far
    //   peak   — highest height reached anywhere in this subtree
    int32_t height = 0;
    int32_t peak = 0;

    std::vector<int32_t> children;
    for (int32_t child = node.firstChild; child != -1; child = m_Nodes[child].nextSibling)
    {
        if (m_Nodes[child].alive) children.push_back(child);
    }

    // Sibling order changes the peak: the `height + childResult.peak` term below
    // charges a compound child for everything its earlier siblings left on the
    // stack, so a deep child costs nothing when it goes FIRST and costs the full
    // running height when it goes last. Emitting deepest-first therefore minimises
    // the peak — this is Sethi-Ullman register-minimising evaluation order.
    //
    // Ordering only; the hint is never used to decide whether a child emits (that
    // stays with the authoritative childResult.yield below), so a stale or wrong
    // hint can only cost depth, never correctness.
    if (ChildOrderIsFree(node))
    {
        std::stable_sort(children.begin(), children.end(),
            [&subtreeDepth](int32_t a, int32_t b)
            { return subtreeDepth[static_cast<size_t>(a)] > subtreeDepth[static_cast<size_t>(b)]; });
    }

    for (const int32_t child : children)
    {
        const CompileResult childResult = CompileNode(child, objectIdToIndex, subtreeDepth, outProgram);

        // Alive but emitted nothing (an empty group, or one holding only dead nodes).
        // Counting it would emit an op with only one value beneath it — harmless at
        // runtime because the shader guards with `sp >= 2`, but it leaves a dead
        // command in a program the shader walks linearly at every march step.
        if (childResult.yield == 0)
        {
            continue;
        }

        // The child's peak is measured from zero, but its commands execute on top of
        // whatever its earlier siblings left behind — so offset it by the current
        // height. This term is the entire reason a compound child costs depth when it
        // is NOT first and costs none when it is.
        peak = std::max(peak, height + childResult.peak);
        height += 1;
        ++compiledChildren;

        if (compiledChildren >= 2)
        {
            CsgCommand cmd{};
            cmd.cmdType = opCmd;
            cmd.operand = 0;
            cmd.nodeId = node.nodeId;
            cmd.parameter = node.blendMode == BlendMode::Smooth ? std::max(node.blendRadius, 0.0f) : 0.0f;
            outProgram.push_back(cmd);
            height -= 1;   // the op pops two and pushes one
        }
    }

    return { peak, compiledChildren > 0 ? 1 : 0 };
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
