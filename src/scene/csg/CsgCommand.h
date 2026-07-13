#pragma once
#include <cstdint>

enum class CsgCmdType : int32_t
{
    PUSH_OBJECT = 0,
    OP_UNION = 1,
    OP_SUBTRACT = 2,
    OP_INTERSECTION = 3,
    OP_SMOOTH_UNION = 4,
    OP_SMOOTH_SUBTRACT = 5,
    OP_SMOOTH_INTERSECTION = 6,
    MOD_ROUNDNESS = 7
};

// GPU-facing RPN command — must match the WGSL layout exactly (16 bytes).
struct alignas(16) CsgCommand
{
    CsgCmdType cmdType;
    int32_t operand;   // PUSH_OBJECT: real array index into scene_objects
                       // (NOT objectId — see CsgTree::CompileToRPN).
    int32_t nodeId;    // OP_*: permanent id of the operation node that produced this
                       // result (used for future selection/naming of intermediate
                       // results, e.g. naming a union of two spheres "Head").
    float parameter;
};

static_assert(sizeof(CsgCommand) == 16, "CsgCommand must be exactly 16 bytes.");
