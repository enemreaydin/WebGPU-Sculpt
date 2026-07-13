#pragma once
#include <vector>
#include <string>
#include <iosfwd>
#include "scene/data/SdfObjectData.h"
#include "scene/csg/CsgTree.h"
#include "scene/SceneManager.h"

class ExtensionLoader {
public:
    // Static so callers don't need an instance.
    static bool SaveToSDFO(const std::string& filepath, const std::vector<SdfObjectData>& objects, const CsgTree& csgTree);
    static bool LoadFromSDFO(const std::string& filepath, SceneManager& sceneManager);

    // .sdfp = the editable PROJECT format (objects + CSG tree = groups/operations).
    // Stream-based so it works against both a real file (native) and an in-memory
    // buffer (web, where there is no disk). Magic tag "SDFP" distinguishes it from
    // the future .sdfo composite-object import format. WriteSDFP does not touch the
    // GPU; ReadSDFP mirrors LoadFromSDFO (clears the scene and rebuilds after load).
    static void WriteSDFP(std::ostream& out, const std::vector<SdfObjectData>& objects, const CsgTree& csgTree);
    static bool ReadSDFP(std::istream& in, SceneManager& sceneManager);
};