#include "editor/EditorObjectStore.h"

#include <cstdio>
#include <istream>
#include <ostream>
#include <utility>
#include <vector>

#include "scene/PrimitiveGeometry.h"

namespace
{
    template <typename T>
    void WritePod(std::ostream& out, const T& v)
    {
        out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T>
    bool ReadPod(std::istream& in, T& v)
    {
        in.read(reinterpret_cast<char*>(&v), sizeof(T));
        return static_cast<bool>(in);
    }
}

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
        case SdfObjectType::Link:       return "Link";
        case SdfObjectType::Cone:       return "Cone";
        case SdfObjectType::Capsule:    return "Capsule";
        case SdfObjectType::SolidAngle: return "SolidAngle";
        case SdfObjectType::Pyramid:    return "Pyramid";
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
    m_NodeNames.clear();
}

void EditorObjectStore::SetNodeName(int32_t nodeId, const std::string& name)
{
    m_NodeNames[nodeId] = name;
}

void EditorObjectStore::RemoveNode(int32_t nodeId)
{
    m_NodeNames.erase(nodeId);
}

const std::string* EditorObjectStore::FindNodeName(int32_t nodeId) const
{
    auto it = m_NodeNames.find(nodeId);
    return it == m_NodeNames.end() ? nullptr : &it->second;
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
    state.nodeNames = m_NodeNames;
    return state;
}

void EditorObjectStore::RestoreState(const EditorObjectStoreState& state)
{
    m_Objects = state.objects;
    m_TypeCounters = state.typeCounters;
    m_NodeNames = state.nodeNames;
}

void EditorObjectStore::WriteToStream(std::ostream& out) const
{
    // Records: count, then per object [objectId, visible, locked, rotationEuler(3),
    // scale(3), params(6), name(length-prefixed)].
    int32_t objCount = static_cast<int32_t>(m_Objects.size());
    WritePod(out, objCount);
    for (const auto& [objectId, obj] : m_Objects)
    {
        WritePod(out, objectId);
        WritePod<uint8_t>(out, obj.visible ? 1u : 0u);
        WritePod<uint8_t>(out, obj.locked ? 1u : 0u);
        out.write(reinterpret_cast<const char*>(obj.rotationEuler.data()), 3 * sizeof(float));
        out.write(reinterpret_cast<const char*>(obj.scale.data()), 3 * sizeof(float));
        WritePod(out, obj.params.radius);
        WritePod(out, obj.params.height);
        WritePod(out, obj.params.thickness);
        WritePod(out, obj.params.roundness);
        WritePod(out, obj.params.length);
        WritePod(out, obj.params.angle);

        int32_t nameLen = static_cast<int32_t>(obj.name.size());
        WritePod(out, nameLen);
        if (nameLen > 0) out.write(obj.name.data(), nameLen);
    }

    // Naming counters, so the next created object keeps counting (e.g. Sphere.004).
    int32_t counterCount = static_cast<int32_t>(m_TypeCounters.size());
    WritePod(out, counterCount);
    for (const auto& [type, count] : m_TypeCounters)
    {
        WritePod(out, type);
        WritePod(out, count);
    }

    // CSG group/node names, keyed by nodeId. Appended last so older .sdfp files
    // (which stop after the counters) still read via the EOF-tolerant reader.
    int32_t nodeNameCount = static_cast<int32_t>(m_NodeNames.size());
    WritePod(out, nodeNameCount);
    for (const auto& [nodeId, name] : m_NodeNames)
    {
        WritePod(out, nodeId);
        int32_t len = static_cast<int32_t>(name.size());
        WritePod(out, len);
        if (len > 0) out.write(name.data(), len);
    }
}

void EditorObjectStore::ReadFromStream(std::istream& in)
{
    int32_t objCount = 0;
    if (!ReadPod(in, objCount) || objCount < 0) return;  // absent/legacy section

    for (int32_t i = 0; i < objCount; ++i)
    {
        int32_t objectId = 0;
        uint8_t visible = 1, locked = 0;
        std::array<float, 3> rotationEuler{ 0.0f, 0.0f, 0.0f };
        std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
        PrimitiveParams params;

        if (!ReadPod(in, objectId)) return;
        ReadPod(in, visible);
        ReadPod(in, locked);
        in.read(reinterpret_cast<char*>(rotationEuler.data()), 3 * sizeof(float));
        in.read(reinterpret_cast<char*>(scale.data()), 3 * sizeof(float));
        ReadPod(in, params.radius);
        ReadPod(in, params.height);
        ReadPod(in, params.thickness);
        ReadPod(in, params.roundness);
        ReadPod(in, params.length);
        ReadPod(in, params.angle);

        int32_t nameLen = 0;
        ReadPod(in, nameLen);
        std::string name;
        if (nameLen > 0)
        {
            std::vector<char> buf(static_cast<size_t>(nameLen));
            in.read(buf.data(), nameLen);
            name.assign(buf.data(), static_cast<size_t>(nameLen));
        }
        if (!in) return;  // truncated — stop overlaying, keep what we have

        // Overlay onto the default record RebuildEditorRecords already created for
        // this objectId. Objects no longer in the scene are simply skipped.
        if (EditorObject* eo = Find(objectId))
        {
            eo->name = std::move(name);
            eo->visible = (visible != 0);
            eo->locked = (locked != 0);
            eo->rotationEuler = rotationEuler;
            eo->scale = scale;
            eo->params = params;
        }
    }

    int32_t counterCount = 0;
    if (!ReadPod(in, counterCount) || counterCount < 0) return;
    for (int32_t i = 0; i < counterCount; ++i)
    {
        int32_t type = 0, count = 0;
        if (!ReadPod(in, type)) return;
        ReadPod(in, count);
        if (!in) return;
        m_TypeCounters[type] = count;
    }

    // CSG group/node names (absent in pre-node-name .sdfp files — EOF-tolerant).
    int32_t nodeNameCount = 0;
    if (!ReadPod(in, nodeNameCount) || nodeNameCount < 0) return;
    for (int32_t i = 0; i < nodeNameCount; ++i)
    {
        int32_t nodeId = 0, len = 0;
        if (!ReadPod(in, nodeId)) return;
        ReadPod(in, len);
        std::string name;
        if (len > 0)
        {
            std::vector<char> buf(static_cast<size_t>(len));
            in.read(buf.data(), len);
            name.assign(buf.data(), static_cast<size_t>(len));
        }
        if (!in) return;
        m_NodeNames[nodeId] = std::move(name);
    }
}
