#pragma once
#include <filesystem>
#include "scene/SceneManager.h"
#include "renderer/SdfRenderer.h"
#include "platform/GraphicsContext.h"

// New / open / save of the scene, plus refreshing the GPU scene buffer.
class SceneSerializer
{
public:
    SceneSerializer(SceneManager& sceneManager, SdfRenderer& renderer, Platform::GraphicsContext& graphics, std::filesystem::path& projectRoot)
        : sceneManager(sceneManager), renderer(renderer), graphics(graphics), projectRoot(projectRoot) {}

    void NewScene();
    void OpenSceneFromSDFO();
    void SaveSceneToSDFO();
    void RefreshSceneBuffer();

private:
    SceneManager& sceneManager;
    SdfRenderer& renderer;
    Platform::GraphicsContext& graphics;
    std::filesystem::path& projectRoot;
};
