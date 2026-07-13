#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>

#include "scene/data/SdfEnums.h"
#include "scene/data/EditorObject.h"

struct EditorObjectStoreState
{
    std::unordered_map<int32_t, EditorObject> objects;
    std::unordered_map<int32_t, int32_t> typeCounters;
    // Custom names for CSG operation/group nodes, keyed by permanent CSG nodeId.
    // Groups have no objectId, so they can't live in `objects`.
    std::unordered_map<int32_t, std::string> nodeNames;
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

    // Custom name for a CSG operation/group node (keyed by nodeId). Groups have no
    // objectId, so their names live here rather than on an EditorObject.
    void SetNodeName(int32_t nodeId, const std::string& name);
    void RemoveNode(int32_t nodeId);
    const std::string* FindNodeName(int32_t nodeId) const;  // nullptr if unnamed

    EditorObjectStoreState CaptureState() const;
    void RestoreState(const EditorObjectStoreState& state);

    // Binary (de)serialization for the .sdfp project file. Unlike SdfObjectData
    // these records hold a variable-length name, so they can't be blitted raw.
    // WriteToStream emits every record keyed by objectId plus the naming counters.
    // ReadFromStream OVERLAYS onto existing records (so it runs after the loader's
    // default RebuildEditorRecords): it updates the record for each saved objectId
    // that still exists and restores the counters. EOF-tolerant — an absent section
    // (legacy/other-format file) leaves the current records untouched.
    void WriteToStream(std::ostream& out) const;
    void ReadFromStream(std::istream& in);

private:
    std::string MakeDefaultName(SdfObjectType type);

    std::unordered_map<int32_t, EditorObject> m_Objects;
    std::unordered_map<int32_t, int32_t> m_TypeCounters; // type -> count, for naming
    std::unordered_map<int32_t, std::string> m_NodeNames; // CSG nodeId -> custom name
};
