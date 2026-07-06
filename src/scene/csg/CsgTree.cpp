#include "CsgTree.h"

#include <fstream>
#include <iostream>

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
        cmd.pad0 = 0;
        outProgram.push_back(cmd);
        return;
    }

    // Operation node: postorder walk of the child sibling chain. After every child
    // past the first, emit one binary op command. N children -> N-1 op commands
    // (n-ary-to-binary folding).
    const CsgCmdType opCmd = OpNodeTypeToCmdType(node.type);
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
            cmd.pad0 = 0;
            outProgram.push_back(cmd);
        }
    }
}

void CsgTree::Clear()
{
    m_Nodes.clear();
    m_Root = -1;
    m_NextNodeId = 0;
}

void CsgTree::WriteToStream(std::ostream& out) const
{
    // Size prefix (node count) followed by the raw node array, then the two scalars.
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
    int32_t nodeCount = 0;
    in.read(reinterpret_cast<char*>(&nodeCount), sizeof(int32_t));
    if (!in || nodeCount < 0)
    {
        Clear();
        return;
    }

    m_Nodes.assign(static_cast<size_t>(nodeCount), CsgNode{});
    if (nodeCount > 0)
    {
        in.read(reinterpret_cast<char*>(m_Nodes.data()), nodeCount * sizeof(CsgNode));
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
