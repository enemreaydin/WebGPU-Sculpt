#include "serialization/SceneSerializer.h"
#include "serialization/ExtensionLoader.h"
#include <iostream>

// TEMPORARY — Stage 1 (P4) verification only. Delete this function and its two
// call sites once the scene bounds are trusted.
static void LogSceneBounds(const SceneAABB& b, size_t objectCount)
{
    std::cout << "[bounds] objects=" << objectCount
              << (b.IsEmpty() ? " EMPTY" : "")
              << " min=(" << b.minBoxDistances[0] << ", " << b.minBoxDistances[1] << ", " << b.minBoxDistances[2] << ")"
              << " max=(" << b.maxBoxDistances[0] << ", " << b.maxBoxDistances[1] << ", " << b.maxBoxDistances[2] << ")"
              << std::endl;
}

void SceneSerializer::RefreshSceneBuffer()
{
    // Rebuild the objectId map and recompile the CSG tree into an RPN program, then
    // upload both the scene objects and the program to the GPU.
    
    auto objectIdMap = SceneManager::BuildObjectIdMap(sceneManager.GetObjects());
    std::vector<CsgCommand> program = sceneManager.GetCsgTree().CompileToRPN(objectIdMap);
    sceneBounds = SceneBounds::Compute(sceneManager.GetObjects(), sceneManager.GetCsgTree());
    LogSceneBounds(sceneBounds, sceneManager.GetObjects().size());  // TEMPORARY
    renderer.UpdateSceneBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects(),
        program
    );
}

void SceneSerializer::RefreshCsgBuffer()
{
    auto objectIdMap = SceneManager::BuildObjectIdMap(sceneManager.GetObjects());
    std::vector<CsgCommand> program = sceneManager.GetCsgTree().CompileToRPN(objectIdMap);
    sceneBounds = SceneBounds::Compute(sceneManager.GetObjects(), sceneManager.GetCsgTree());
    LogSceneBounds(sceneBounds, sceneManager.GetObjects().size());  // TEMPORARY
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
