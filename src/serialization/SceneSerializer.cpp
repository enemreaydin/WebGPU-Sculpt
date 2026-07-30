#include "serialization/SceneSerializer.h"
#include "serialization/ExtensionLoader.h"
#include <iostream>

// Everything the GPU upload depends on, derived from the current objects + tree.
//
// Both Refresh* entry points below route through here, which is load-bearing:
// RefreshCsgBuffer can ALSO re-upload the object array (SdfRenderer's
// CreateSceneBuffer fallback when the program size changes), so derived per-object
// data living in only one entry point would ship stale on the other path.
std::vector<CsgCommand> SceneSerializer::PrepareSceneForUpload()
{
    // Per-object bubble radii into position.w (the P1a proxy), then the whole-scene
    // box built from those same bubbles (the P4 ray clip). Radii first: they are
    // independent today, but this is the order that stays correct if the box is
    // ever changed to read position.w instead of recomputing.
    SceneBounds::WriteEnclosingRadii(sceneManager.GetObjects(), sceneManager.GetCsgTree());
    sceneBounds = SceneBounds::Compute(sceneManager.GetObjects(), sceneManager.GetCsgTree());

    // objectId -> current array index, then the CSG tree compiled into the RPN
    // program the shader interprets.
    const auto objectIdMap = SceneManager::BuildObjectIdMap(sceneManager.GetObjects());
    return sceneManager.GetCsgTree().CompileToRPN(objectIdMap);
}

void SceneSerializer::RefreshSceneBuffer()
{
    // Upload both the scene objects and the recompiled program.
    const std::vector<CsgCommand> program = PrepareSceneForUpload();
    renderer.UpdateSceneBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects(),
        program
    );
}

void SceneSerializer::RefreshCsgBuffer()
{
    // Upload the program only (unless the size changed — see the note above).
    const std::vector<CsgCommand> program = PrepareSceneForUpload();
    renderer.UpdateCsgProgramBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects(),
        program
    );
}

void SceneSerializer::NewScene()
{
    sceneManager.ClearScene();

    RefreshSceneBuffer();

    std::cout << "[INFO]: New empty scene created.\n";
}

void SceneSerializer::OpenSceneFromSDFO()
{
    std::filesystem::path path = projectRoot / "assets/test.sdfo";

    sceneManager.ClearScene();

    if (ExtensionLoader::LoadFromSDFO(path.string(), sceneManager)) {
        RefreshSceneBuffer();

        std::cout << "[INFO]: Scene loaded from: "
            << path << std::endl;
    }
    else {
        RefreshSceneBuffer();

        std::cerr << "[WARNING]: SDFO file could not be loaded: "
            << path << std::endl;
    }
}

void SceneSerializer::SaveSceneToSDFO()
{
    std::filesystem::path path = projectRoot / "assets/test.sdfo";

    if (ExtensionLoader::SaveToSDFO(path.string(), sceneManager.GetObjects(), sceneManager.GetCsgTree())) {
        std::cout << "[INFO]: Scene saved to: "
            << path << std::endl;
    }
    else {
        std::cerr << "[WARNING]: SDFO file could not be saved: "
            << path << std::endl;
    }
}
