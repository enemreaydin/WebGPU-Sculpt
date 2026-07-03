#include "serialization/SceneSerializer.h"
#include "serialization/ExtensionLoader.h"
#include <iostream>

void SceneSerializer::RefreshSceneBuffer()
{
    renderer.CreateSceneBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects()
    );
}

void SceneSerializer::NewScene()
{
    sceneManager.ClearScene();

    renderer.CreateSceneBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects()
    );

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

    if (ExtensionLoader::SaveToSDFO(path.string(), sceneManager.GetObjects())) {
        std::cout << "[INFO]: Scene saved to: "
            << path << std::endl;
    }
    else {
        std::cerr << "[WARNING]: SDFO file could not be saved: "
            << path << std::endl;
    }
}
