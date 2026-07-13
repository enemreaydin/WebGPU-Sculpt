#pragma once
#include <cstdint>

enum class CsgNodeType
{
    Primitive,
    Union,
    Subtract,
    Intersection,
    Group          // Pure container: groups children with no boolean effect. Compiles
                   // exactly like Union (children combined with min), but is a distinct
                   // type so the UI can label it "Group" rather than a boolean op. Kept
                   // last so the existing 1/2/3 values (used in JSON, ApplyOperation, and
                   // the shader command mapping) never shift.
};

// Array-based n-ary tree node using the "first child / next sibling" technique
// (allows any number of children per operation node without variable-size fields,
// which keeps the node itself fixed-size and simple to store in a flat vector).
struct CsgNode
{
    CsgNodeType type;
    int32_t nodeId;              // Permanent identity, assigned from an ever-increasing
                                 // counter (see CsgTree::m_NextNodeId), never reused,
                                 // never equal to the vector index.
    int32_t objectId = -1;       // Valid only when type == Primitive: references
                                 // SdfObjectData.objectId.
    int32_t parent = -1;
    int32_t previousSibling = -1;
    int32_t nextSibling = -1;
    int32_t firstChild = -1;
    int32_t lastChild = -1;
    bool alive = true;           // Tombstone flag — removed nodes are marked dead in
                                 // place, never physically erased from the vector, so
                                 // indices never shift and other nodes' parent/child/
                                 // sibling references stay valid.
};
