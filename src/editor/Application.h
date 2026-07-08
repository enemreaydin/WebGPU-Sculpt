#pragma once
#include <webgpu/webgpu.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>

#include "platform/Window.h"
#include "platform/GraphicsContext.h"
#include "editor/Settings.h"
#include "scene/SceneManager.h"
#include "renderer/SdfRenderer.h"
#include "scene/data/SdfEnums.h"
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"
#include "camera/Camera.h"
#include "camera/CameraController.h"
#include "core/ViewportInput.h"
#include "interaction/SelectionState.h"
#include "serialization/SceneSerializer.h"
#include "interaction/GizmoState.h"

using namespace wgpu;

// The engine application. It owns the window, WebGPU context, scene and
// renderer, and drives the SDF render loop. All user interaction (menus,
// property panels, object list) now lives in the web/React front-end, which
// drives this class through the public control API below (see src/bindings).
class Application {
public:
    Application(int width = 1280, int height = 720)
        : width(width), height(height),
          renderWidth(width), renderHeight(height),
          sceneSerializer(sceneManager, renderer, graphics, projectRoot) {}

    // Lifecycle
    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

    // Called when the drawing surface changes size (native window resize or the
    // web canvas being resized from JS).
    void OnResize(int width, int height);

    // ---- Control API (called from the web bindings / front-end) ------------

    // Scene editing
    int  AddPrimitive(SdfObjectType type);   // returns the new object's index
    void RemoveObject(int index);
    void SelectObject(int index);
    int  GetSelectedIndex() const { return selectionState.selectedObjectIndex; }

    void SetObjectPosition(int index, float x, float y, float z);
    void SetObjectSize(int index, float x, float y, float z);
    void SetObjectColor(int index, float r, float g, float b);

    // Serialized snapshot of the scene as JSON (object list + selection), for
    // the front-end to render its panels from.
    std::string GetSceneJson() const;

    // File operations
    void NewScene();
    void OpenScene();
    void SaveScene();

    // View
    void SetShowGrid(bool show) { showGrid = show; }
    bool GetShowGrid() const { return showGrid; }

    // Camera interaction (deltas in pixels / wheel ticks, accumulated per frame)
    void CameraOrbit(float dx, float dy);
    void CameraPan(float dx, float dy);
    void CameraZoom(float delta);

    // Gizmo interaction
    int  GizmoHover(float mouseX, float mouseY);
    void GizmoBeginDrag(float mouseX, float mouseY);
    void GizmoDrag(float mouseX, float mouseY);
    void GizmoEndDrag();
    bool IsGizmoDragging() const { return gizmoState.isDragging; }

private:
    bool running = true;
    int width;
    int height;
    int renderWidth;
    int renderHeight;
    std::filesystem::path projectRoot;

    // Owned subsystems
    Platform::Window window;
    Platform::GraphicsContext graphics;
    SceneManager sceneManager;
    SdfRenderer renderer;
    SceneSerializer sceneSerializer;
    Settings appSettings;
    bool showGrid = true;

    SelectionState selectionState;
    GizmoState gizmoState;

    // Scene / camera state
    GlobalsData globals{};
    Camera camera;
    CameraController cameraController;
    ViewportInput viewportInput;
    glm::mat4 view{};
    glm::mat4 proj{};

    // Initializers / helpers
    void FindProjectRoot();
    void InitializeRenderer();
    void UpdateCamera();
    TextureView GetNextSurfaceTextureView();

    static const char* TypeName(int32_t type);

    bool GetSelectedObjectPosition(glm::vec3& outPosition) const;
    void UpdateSceneBufferAfterTransform();
};
