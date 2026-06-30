#pragma once
#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
#include <filesystem>

#include "core/Settings.h"
#include "scene/SceneManager.h"
#include "render/SdfRenderer.h"     // Renderer modulumuzu dahil ediyoruz
#include "core/SdfDefinitions.h"

using namespace wgpu;

class Application {
public:
    Application(int width = 1280, int height = 720) : width(width), height(height) {}

    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

private:
    // --- Baþlangýç zinciri (her biri Initialize içinde sýrayla çaðrýlýr) ---
    void InitializeWindow();
    void InitializeInstance();
    void InitializeSurface();
    void InitializeAdapter();
    void InitializeDevice();
    void InitializeQueue();
    void ConfigureSurface();
    void InitializeImgui();

    // --- Proje / dosya sistemi ---
    void FindProjectRoot();

    // --- Frame yardýmcýlarý ---
    TextureView GetNextSurfaceTextureView();
    void OnResize(int width, int height); // Pencere yeniden boyutlandýðýnda surface'ý yeniden configure et

    // --- GUI çizimi ---
    void DrawGui();
    void DrawMainMenuBar();
    void DrawSettings();
    void DrawWorkspace();

private:
    // Window & WGPU temel altyapýsý
    GLFWwindow* window = nullptr;
    Instance instance = nullptr;
    Surface surface = nullptr;
    Adapter adapter = nullptr;
    Device device = nullptr;
    Queue queue = nullptr;
    TextureFormat surfaceFormat = TextureFormat::Undefined;
    int width;
    int height;

    // Modüller (sorumluluklar ayrýldý)
    SceneManager sceneManager;
    SdfRenderer renderer;       // Tüm pipeline, buffer ve shader iþleri burada

    // GUI durumu
    Settings appSettings;
    AppTheme appTheme;

    // Proje dosya sistemi
    std::filesystem::path projectRoot;
};