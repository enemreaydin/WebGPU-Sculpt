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

    // P1d. A SKIP_SUBTREE introduces the next `operand` commands (itself and its
    // two SKIP_BOUNDS included) as ONE skippable unit: if the evaluation point is
    // far enough outside the box those bounds describe, the shader pushes a
    // conservative lower bound for the whole subtree and jumps `operand` commands
    // forward instead of executing them.
    //
    // Both values are >6 on purpose. The shader's dispatch is
    // `if (t == 0) {...} else if (t >= 1 && t <= 6) {...}`, so a build that does
    // not understand these falls through all three commands and executes the
    // subtree normally — correct, just slower. That is what makes emitting them
    // a no-op change and lets the emission be validated before the shader moves.
    SKIP_SUBTREE = 7,
    SKIP_BOUNDS  = 8
};

// GPU-facing RPN command — must match the WGSL layout exactly (16 bytes).
struct alignas(16) CsgCommand
{
    CsgCmdType cmdType;
    int32_t operand;   // PUSH_OBJECT:  real array index into scene_objects
                       //               (NOT objectId — see CsgTree::CompileToRPN).
                       // SKIP_SUBTREE: commands to jump forward, counted from this
                       //               command, when the subtree is rejected.
                       // SKIP_BOUNDS:  a float's BITS (min.x / max.x).
    int32_t nodeId;    // OP_*: permanent id of the operation node that produced this
                       // result (used for future selection/naming of intermediate
                       // results, e.g. naming a union of two spheres "Head").
                       // SKIP_BOUNDS: a float's BITS (min.y / max.y).
    float parameter;   // SKIP_BOUNDS: min.z / max.z — already a float, no bitcast.
};

// Commands a SKIP_SUBTREE header occupies: itself plus the two SKIP_BOUNDS that
// carry the six AABB floats. The bounds ride in the command stream rather than a
// separate storage buffer because that needs no new bind-group entry, no buffer
// sizing and no SdfRenderer change at all — and the stream is already a
// sequential, wave-uniform, cache-friendly read.
static constexpr int32_t kCsgSkipHeaderCommands = 3;

static_assert(sizeof(CsgCommand) == 16, "CsgCommand must be exactly 16 bytes.");
