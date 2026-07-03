#pragma once
#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
#include <filesystem>

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
#include "interaction/SelectionState.h"
#include "serialization/SceneSerializer.h"
#include "ui/AddObjectPanel.h"

using namespace wgpu;

class Application {
public:
    Application(int width = 1280, int height = 720)
        : width(width), height(height),
          sceneSerializer(sceneManager, renderer, graphics, projectRoot) {}

    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

    void OnResize(int width, int height);

private:
    int width;
    int height;
    std::filesystem::path projectRoot;

    // Declarations
    Platform::Window window;
    Platform::GraphicsContext graphics;
    SceneManager sceneManager;
    SdfRenderer renderer;
    SceneSerializer sceneSerializer;
    Settings appSettings;
    AppTheme appTheme;
    AddObjectPanel addObjectPanel;
    bool showGrid = true;

    SelectionState selectionState;

    // Scene
    GlobalsData globals;
    ImVec2 sceneViewSize = { 1000, 800 };
    Camera camera;
    CameraController cameraController;
    bool sceneViewHovered = false;
    ImVec2 sceneCursorDelta;
    float sceneScrollDelta;
    void UpdateCamera();
    glm::mat4 view;
    glm::mat4 proj;

    // Shortcuts
    bool noModifiersPressed;
          
    // Initializers
    void InitializeImgui();
    void InitializeRenderer();

    // Helpers
    void FindProjectRoot();
    TextureView GetNextSurfaceTextureView();

    // GUI functions
    void DrawGui();
    void DrawMainMenuBar();
    void DrawSettings();
    void DrawWorkspace();


    void AddObjectToScene(const SdfObjectData& object);

};