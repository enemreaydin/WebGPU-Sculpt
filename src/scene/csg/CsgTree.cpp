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

int32_t CsgTree::AddRoundnessModifier(int32_t targetIndex, float amount)
{
    if (targetIndex < 0 || targetIndex >= static_cast<int32_t>(m_Nodes.size()) ||
        !m_Nodes[targetIndex].alive || !std::isfinite(amount) || amount < 0.0f)
        return -1;

    const int32_t oldParent = m_Nodes[targetIndex].parent;
    const int32_t oldPrev = m_Nodes[targetIndex].previousSibling;
    const int32_t oldNext = m_Nodes[targetIndex].nextSibling;
    CsgNode modifier{};
    modifier.type = CsgNodeType::Modifier;
    modifier.nodeId = m_NextNodeId++;
    modifier.modifierType = ModifierType::Roundness;
    modifier.modifierAmount = amount;
    m_Nodes.push_back(modifier);
    const int32_t modifierIndex = static_cast<int32_t>(m_Nodes.size()) - 1;

    if (oldParent != -1)
    {
        CsgNode& parent = m_Nodes[oldParent];
        if (oldPrev != -1) m_Nodes[oldPrev].nextSibling = modifierIndex; else parent.firstChild = modifierIndex;
        if (oldNext != -1) m_Nodes[oldNext].previousSibling = modifierIndex; else parent.lastChild = modifierIndex;
        m_Nodes[modifierIndex].parent = oldParent;
        m_Nodes[modifierIndex].previousSibling = oldPrev;
        m_Nodes[modifierIndex].nextSibling = oldNext;
    }
    else m_Root = modifierIndex;

    m_Nodes[targetIndex].parent = modifierIndex;
    m_Nodes[targetIndex].previousSibling = -1;
    m_Nodes[targetIndex].nextSibling = -1;
    m_Nodes[modifierIndex].firstChild = targetIndex;
    m_Nodes[modifierIndex].lastChild = targetIndex;
    return m_Nodes[modifierIndex].nodeId;
}

bool CsgTree::RemoveModifierNode(int32_t modifierIndex)
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

    if (parent.type == CsgNodeType::Modifier && parent.firstChild != -1)
        return;

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

std::vector<CsgCommand> CsgTree::CompileToRPN(const std::unordered_map<int32_t, int32_t>& objectIdToIndex) const
{
    std::vector<CsgCommand> program;

    if (m_Root >= 0 && m_Root < static_cast<int32_t>(m_Nodes.size()) && m_Nodes[m_Root].alive)
    {
        CompileNode(m_Root, objectIdToIndex, program);
    }

    return program;
}

void CsgTree::CompileNode(int32_t nodeIndex, const std::unordered_map<int32_t, int32_t>& objectIdToIndex, std::vector<CsgCommand>& outProgram) const
{
    const CsgNode& node = m_Nodes[nodeIndex];
    if (!node.alive)
    {
        return;
    }

    if (node.type == CsgNodeType::Primitive)
    {
        // Translate the permanent objectId to the current real array index.
        // .at() throws on a missing/stale objectId instead of inserting a bogus entry.
        CsgCommand cmd{};
        cmd.cmdType = CsgCmdType::PUSH_OBJECT;
        cmd.operand = objectIdToIndex.at(node.objectId);
        cmd.nodeId = node.nodeId;
        cmd.parameter = 0.0f;
        outProgram.push_back(cmd);
        return;
    }

    if (node.type == CsgNodeType::Modifier)
    {
        if (node.firstChild == -1) return;
        CompileNode(node.firstChild, objectIdToIndex, outProgram);
        if (node.modifierEnabled && node.modifierType == ModifierType::Roundness)
        {
            CsgCommand cmd{};
            cmd.cmdType = CsgCmdType::MOD_ROUNDNESS;
            cmd.nodeId = node.nodeId;
            cmd.parameter = std::max(node.modifierAmount, 0.0f);
            outProgram.push_back(cmd);
        }
        return;
    }

    // Operation node: postorder walk of the child sibling chain. After every child
    // past the first, emit one binary op command. N children -> N-1 op commands
    // (n-ary-to-binary folding).
    const CsgCmdType opCmd = node.blendMode == BlendMode::Smooth
        ? SmoothOpNodeTypeToCmdType(node.type) : OpNodeTypeToCmdType(node.type);
    int32_t compiledChildren = 0;

    for (int32_t child = node.firstChild; child != -1; child = m_Nodes[child].nextSibling)
    {
        if (!m_Nodes[child].alive)
        {
            continue;
        }

        CompileNode(child, objectIdToIndex, outProgram);
        ++compiledChildren;

        if (compiledChildren >= 2)
        {
            CsgCommand cmd{};
            cmd.cmdType = opCmd;
            cmd.operand = 0;
            cmd.nodeId = node.nodeId;
            cmd.parameter = node.blendMode == BlendMode::Smooth ? std::max(node.blendRadius, 0.0f) : 0.0f;
            outProgram.push_back(cmd);
        }
    }
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
