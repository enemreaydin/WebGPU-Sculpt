#pragma once

#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
#include <core/Settings.h>
using namespace wgpu;

class Application {
public:
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

    // GUI fonksiyon ve deðiþkenleri
    Settings appSettings;
    void DrawGui();
    void DrawMainMenuBar();
    void DrawSettings();



    // --- Frame yardýmcýlarý ---
    TextureView GetNextSurfaceTextureView();

    // Pencere yeniden boyutlandýðýnda surface'ý yeniden configure et
    void OnResize(int width, int height);

private:
    GLFWwindow* window = nullptr;
    Instance instance = nullptr;
    Adapter adapter = nullptr;
    Device device = nullptr;
    Queue queue = nullptr;
    Surface surface = nullptr;
    TextureFormat surfaceFormat = TextureFormat::Undefined;

    int width = 1280;
    int height = 720;
};