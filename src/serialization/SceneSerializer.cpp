#include "serialization/SceneSerializer.h"
#include "serialization/ExtensionLoader.h"
#include <iostream>

void SceneSerializer::RefreshSceneBuffer()
{
    // Rebuild the objectId map and recompile the CSG tree into an RPN program, then
    // upload both the scene objects and the program to the GPU.
    auto objectIdMap = SceneManager::BuildObjectIdMap(sceneManager.GetObjects());
    std::vector<CsgCommand> program = sceneManager.GetCsgTree().CompileToRPN(objectIdMap);

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
