#include "Application.h"
#include "utils/FileHelper.h"
#include "io/ExtensionLoader.h"
#include <glfw3webgpu.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <iostream>
#include "ui/Theme.h"

bool Application::Initialize() {
    InitializeWindow();
    InitializeInstance();
    InitializeSurface();
    InitializeAdapter();
    InitializeDevice();
    InitializeQueue();
    ConfigureSurface();
    InitializeImgui();

    // 1. Renderer'ý baþlat (Pipeline ve Shader kurulumu burada)
    renderer.Initialize(device, surfaceFormat);

    // 2. Sahneyi yükle ve GPU Buffer'larýný hazýrla
    if (ExtensionLoader::LoadMockFile("test.bdf", sceneManager)) {
        renderer.CreateSceneBuffer(device, queue, sceneManager.GetObjects());
    }
    else {
        std::cerr << "[UYARI]: Sahne yüklenemedi, boþ sahnede baþlatýlýyor.\n";
    }

    return true;
}

void Application::Terminate() {
    // Renderer kaynaklarýný temizle
    renderer.Destroy();

    // ImGui temizle
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // WGPU ve GLFW temizle
    surface.unconfigure();
    queue.release();
    surface.release();
    device.release();
    adapter.release();
    instance.release();

    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Application::IsRunning() {
    return !glfwWindowShouldClose(window);
}

void Application::MainLoop() {
    // GPU kaynaklarý hazýr deðilse çizim yapma
    if (!renderer.pipeline || !renderer.bindGroup || !renderer.sceneBuffer) {
        return;
    }

    glfwPollEvents();

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

    CommandEncoder encoder = device.createCommandEncoder({});

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = { 0.1, 0.1, 0.12, 1.0 };

    RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

    // ÇÝZÝM
    renderPass.setPipeline(renderer.pipeline);
    renderPass.setBindGroup(0, renderer.bindGroup, 0, nullptr);
    renderPass.draw(6, 1, 0, 0);

    DrawGui();

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);

    renderPass.end();
    renderPass.release();

    CommandBuffer cmdBuf = encoder.finish({});
    encoder.release();
    queue.submit(1, &cmdBuf);
    cmdBuf.release();

    targetView.release();
    surface.present();
    device.poll(false);
}

// ============================================================
// YARDIMCI ÝLK YÜKLEME FONKSÝYONLARI
// ============================================================

void Application::InitializeWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, "SDF Editor", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int nw, int nh) {
        auto app = static_cast<Application*>(glfwGetWindowUserPointer(w));
        app->OnResize(nw, nh);
        });
}

void Application::InitializeInstance() { instance = createInstance({}); }

void Application::InitializeSurface() { surface = glfwGetWGPUSurface(instance, window); }

void Application::InitializeAdapter() {
    RequestAdapterOptions opts{};
    opts.compatibleSurface = surface;
    adapter = instance.requestAdapter(opts);
    surfaceFormat = surface.getPreferredFormat(adapter);
}

void Application::InitializeDevice() {
    device = adapter.requestDevice({});
    queue = device.getQueue();
}

void Application::InitializeQueue() { /* queue device'tan alýndý */ }

void Application::ConfigureSurface() {
    SurfaceConfiguration config{};
    config.width = width;
    config.height = height;
    config.usage = TextureUsage::RenderAttachment;
    config.format = surfaceFormat;
    config.device = device;
    config.presentMode = PresentMode::Fifo;
    surface.configure(config);
}

void Application::OnResize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth; height = newHeight;
    ConfigureSurface();
}

void Application::InitializeImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyTheme(); // Theme customization
    ImGui_ImplGlfw_InitForOther(window, true); // Glfw initialization

    ImGui_ImplWGPU_InitInfo initInfo;
    initInfo.Device = device;
    initInfo.NumFramesInFlight = 3;
    initInfo.RenderTargetFormat = (WGPUTextureFormat)surfaceFormat;
    initInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&initInfo); // wgpu initialization
}

TextureView Application::GetNextSurfaceTextureView() {
    SurfaceTexture surfaceTexture;
    surface.getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success)
        return nullptr;

    Texture texture = surfaceTexture.texture;

    TextureViewDescriptor viewDesc = {};
    viewDesc.label = "Surface texture view";
    viewDesc.format = texture.getFormat();
    viewDesc.dimension = TextureViewDimension::_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = TextureAspect::All;

    return texture.createView(viewDesc);
}

// Boþ býrakýlanlarý doldurabilirsin
void Application::DrawGui() {}
void Application::DrawMainMenuBar() {}