#pragma once
#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
#include <filesystem>

#include "core/Settings.h"
#include "scene/SceneManager.h"
#include "render/SdfRenderer.h"    
#include "core/SdfDefinitions.h"
#include "scene/Camera.h"
#include "ui/AddObjectPanel.h"

using namespace wgpu;

class Application {
public:
    Application(int width = 1280, int height = 720) : width(width), height(height) {}

    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

private:
    int width;
    int height;
    std::filesystem::path projectRoot;

    // Declarations
    GLFWwindow* window = nullptr;
    Instance instance = nullptr;
    Surface surface = nullptr;
    Adapter adapter = nullptr;
    Device device = nullptr;
    Queue queue = nullptr;
    TextureFormat surfaceFormat = TextureFormat::Undefined;
    SceneManager sceneManager;
    SdfRenderer renderer;      
    Settings appSettings;
    AppTheme appTheme;
    AddObjectPanel addObjectPanel;

    // Scene 
    GlobalsData globals;
    ImVec2 sceneViewSize = { 1000, 800 };
    Camera camera;
    bool sceneViewHovered = false;
    ImVec2 sceneCursorDelta;
    void UpdateGlobals();

    // Initializers
    void InitializeWindow();
    void InitializeInstance();
    void InitializeSurface();
    void InitializeAdapter();
    void InitializeDevice();
    void InitializeQueue();
    void ConfigureSurface();
    void InitializeImgui();
    void InitializeRenderer();

    // Helpers
    void FindProjectRoot();
    TextureView GetNextSurfaceTextureView();
    void OnResize(int width, int height);

    // GUI functions
    void DrawGui();
    void DrawMainMenuBar();
    void DrawSettings();
    void DrawWorkspace();

    void AddObjectToScene(const SdfObjectData& object);

    void NewScene();
    void OpenSceneFromSDFO();
    void SaveSceneToSDFO();
    void RefreshSceneBuffer();

};