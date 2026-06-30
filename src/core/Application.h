#pragma once

#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
<<<<<<< HEAD
#include <core/Settings.h>
=======
#include "scene/SceneManager.h"
#include "render/SdfRenderer.h" // Renderer modülümüzü dahil ediyoruz
#include "core/SdfDefinitions.h"
#include <filesystem>

>>>>>>> origin/zeynep
using namespace wgpu;

class Application {
public:
    Application(int width = 1280, int height = 720) : width(width), height(height) {}

    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

private:
    // --- Baþlangýç zinciri ---
    void InitializeWindow();
    void InitializeInstance();
    void InitializeSurface();
    void InitializeAdapter();
    void InitializeDevice();
    void InitializeQueue();
    void ConfigureSurface();
    void InitializeImgui();
<<<<<<< HEAD

    // GUI fonksiyon ve deðiþkenleri
    Settings appSettings;
    AppTheme appTheme;

    void DrawGui();
    void DrawMainMenuBar();
    void DrawSettings();
    void DrawWorkspace();


=======
>>>>>>> origin/zeynep

    // --- Frame yardýmcýlarý ---
    TextureView GetNextSurfaceTextureView();
    void OnResize(int width, int height);
    void DrawGui();
    void DrawMainMenuBar();

private:
    // Window & WGPU Temel Altyapýsý
    GLFWwindow* window = nullptr;
    Instance instance = nullptr;
    Surface surface = nullptr;
    Adapter adapter = nullptr;
    Device device = nullptr;
    Queue queue = nullptr;
    TextureFormat surfaceFormat = TextureFormat::Undefined;

    int width;
    int height;

    // Modüllerimiz (Sorumluluklar ayrýldý)
    SceneManager sceneManager;
    SdfRenderer renderer; // Artýk tüm pipeline, buffer ve shader iþleri burada

    std::filesystem::path projectRoot;
    void FindProjectRoot();
};