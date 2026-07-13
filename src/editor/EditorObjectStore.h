#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "scene/data/SdfEnums.h"
#include "scene/data/EditorObject.h"

struct EditorObjectStoreState
{
    std::unordered_map<int32_t, EditorObject> objects;
    std::unordered_map<int32_t, int32_t> typeCounters;
};

// Owns the editor-only attributes (name, visibility, lock, rotation, scale,
// shape params) for every scene object, keyed by permanent objectId. Also
// assigns default display names ("Sphere.001", ...). This keeps naming and
// per-object editor state inside the engine rather than the web UI.
class EditorObjectStore
{
public:
    // Create the record for a new object, deriving default params/scale from its
    // initial GPU `size` and assigning a unique default name. Returns it so the
    // caller can read the derived params back if needed.
    EditorObject& Create(int32_t objectId,
                          SdfObjectType type,
                          const std::array<float, 3>& initialSize);

    void Remove(int32_t objectId);
    void Clear();

    // nullptr when there is no record for this objectId.
    EditorObject* Find(int32_t objectId);
    const EditorObject* Find(int32_t objectId) const;

    EditorObjectStoreState CaptureState() const;
    void RestoreState(const EditorObjectStoreState& state);

private:
    std::string MakeDefaultName(SdfObjectType type);

    std::unordered_map<int32_t, EditorObject> m_Objects;
    std::unordered_map<int32_t, int32_t> m_TypeCounters; // type -> count, for naming
};
