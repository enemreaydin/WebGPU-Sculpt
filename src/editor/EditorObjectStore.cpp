#include "editor/EditorObjectStore.h"

#include <cstdio>
#include <utility>

#include "scene/PrimitiveGeometry.h"

namespace
{
    const char* TypeLabel(SdfObjectType type)
    {
        switch (type)
        {
        case SdfObjectType::Sphere:   return "Sphere";
        case SdfObjectType::Box:      return "Box";
        case SdfObjectType::Cylinder: return "Cylinder";
        case SdfObjectType::Torus:    return "Torus";
        }
        return "Object";
    }
}

std::string EditorObjectStore::MakeDefaultName(SdfObjectType type)
{
    int32_t n = ++m_TypeCounters[static_cast<int32_t>(type)];
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "%03d", n); // zero-padded, e.g. "007"
    return std::string(TypeLabel(type)) + "." + suffix;
}

EditorObject& EditorObjectStore::Create(int32_t objectId,
                                        SdfObjectType type,
                                        const std::array<float, 3>& initialSize)
{
    EditorObject obj;
    obj.name = MakeDefaultName(type);
    PrimitiveGeometry::InitFromSize(type, initialSize, obj.params, obj.scale);

    auto it = m_Objects.insert_or_assign(objectId, std::move(obj)).first;
    return it->second;
}

void EditorObjectStore::Remove(int32_t objectId)
{
    m_Objects.erase(objectId);
}

void EditorObjectStore::Clear()
{
    m_Objects.clear();
    m_TypeCounters.clear();
}

EditorObject* EditorObjectStore::Find(int32_t objectId)
{
    auto it = m_Objects.find(objectId);
    return it == m_Objects.end() ? nullptr : &it->second;
}

const EditorObject* EditorObjectStore::Find(int32_t objectId) const
{
    auto it = m_Objects.find(objectId);
    return it == m_Objects.end() ? nullptr : &it->second;
}

EditorObjectStoreState EditorObjectStore::CaptureState() const
{
    EditorObjectStoreState state;
    state.objects = m_Objects;
    state.typeCounters = m_TypeCounters;
    return state;
}

void EditorObjectStore::RestoreState(const EditorObjectStoreState& state)
{
    m_Objects = state.objects;
    m_TypeCounters = state.typeCounters;
}
