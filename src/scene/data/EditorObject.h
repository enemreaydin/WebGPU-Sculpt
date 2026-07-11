#pragma once

#include <array>
#include <string>

#include "scene/data/SdfEnums.h"

// Per-primitive shape parameters, interpreted per type. `radius`, `height` and
// `thickness` drive the SDF `size` vector (see PrimitiveGeometry::ComputeSize).
// `roundness` is stored for the UI but not yet applied to the SDF.
struct PrimitiveParams
{
    float radius    = 1.0f;
    float height    = 1.0f;
    float thickness = 0.25f;
    float roundness = 0.02f;
    // Link: straight-segment half-length. Angle (radians): half-angle for cone /
    // solid-angle. Neither is multiplied by scale (see PrimitiveGeometry).
    float length    = 0.5f;
    float angle     = 0.6f;
};

// Identifies a single shape parameter, so the control API can address one field
// without a string. Kept in sync with the web UI's param rows.
enum class PrimitiveParam : int32_t
{
    Radius    = 0,
    Height    = 1,
    Thickness = 2,
    Roundness = 3,
    Length    = 4,
    Angle     = 5
};

// Editor-facing attributes for one scene object. These live only in the engine
// (the GPU-facing SdfObjectData stays a tight 80-byte struct); the web UI reads
// them through GetSceneJson and edits them through the setter API. Held by
// EditorObjectStore, keyed on the object's permanent objectId.
//
// Not persisted to the .sdfo file yet — regenerated with defaults on load, which
// matches the previous behaviour when these lived in the web UI's state.
struct EditorObject
{
    std::string name;
    bool visible = true;
    bool locked  = false;

    // Euler angles in degrees. Stored for the UI; rotation is not yet applied to
    // the SDF (SdfObjectData.rotation stays reserved).
    std::array<float, 3> rotationEuler{ 0.0f, 0.0f, 0.0f };

    // Uniform-per-axis scale multiplier applied on top of the shape params.
    std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };

    PrimitiveParams params;
};
