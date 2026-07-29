#pragma once
#include <filesystem>
#include "scene/SceneManager.h"
#include "renderer/SdfRenderer.h"
#include "platform/GraphicsContext.h"
#include "scene/SceneBounds.h"
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
    void RefreshCsgBuffer();

    // World-space bound of everything the CSG program can draw, recomputed by the
    // two Refresh* functions above so it is always derived from exactly the scene
    // state that was compiled into the uploaded RPN program. Application mirrors
    // it into the per-frame `globals` uniform (CLAUDE.md 6).
    const SceneAABB& GetSceneBounds() const { return sceneBounds; }

private:
    // Owned by value: this is derived state this class computes, not a handle to
    // something Application owns (unlike every reference member below).
    SceneAABB sceneBounds;

    SceneManager& sceneManager;
    SdfRenderer& renderer;
    Platform::GraphicsContext& graphics;
    std::filesystem::path& projectRoot;
};
