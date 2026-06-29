#include "Application.h"
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

    adapter.release();
    instance.release();
    InitializeImgui();

    return true;
}

void Application::Terminate() {
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    surface.unconfigure();
    queue.release();
    surface.release();
    device.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Application::IsRunning() {
    return !glfwWindowShouldClose(window);
}

void Application::MainLoop() {
    glfwPollEvents();

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

    CommandEncoderDescriptor encoderDesc = {};
    CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.resolveTarget = nullptr;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = WGPUColor{ 0.1, 0.1, 0.12, 1.0 };

    RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;

    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    // Renderpass çizimleri


    // Imgui çizimleri
    DrawGui();
    
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
    renderPass.end();
    renderPass.release();

    CommandBufferDescriptor cmdBufDesc = {};
    CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();

    queue.submit(1, &cmdBuf);
    cmdBuf.release();

    targetView.release();
    surface.present();
    device.poll(false);
}

// Initialize chain
void Application::InitializeWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, "SDF Editor", nullptr, nullptr);

    // Resizable property
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int newWidth, int newHeight) {
        auto app = static_cast<Application*>(glfwGetWindowUserPointer(w));
        app->OnResize(newWidth, newHeight);
        });
}

void Application::InitializeInstance() {
    instance = wgpuCreateInstance(nullptr);
}

void Application::InitializeSurface() {
    surface = glfwGetWGPUSurface(instance, window);
}

void Application::InitializeAdapter() {
    RequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    adapter = instance.requestAdapter(adapterOpts);

    // surfaceFormat'ý adapter varken al
    surfaceFormat = surface.getPreferredFormat(adapter);
}

void Application::InitializeDevice() {
    DeviceDescriptor deviceDesc = {};
    deviceDesc.label = "Device";
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = "Queue";
    deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void*) {
        std::cout << "Device lost: " << reason;
        if (message) std::cout << " (" << message << ")";
        std::cout << std::endl;
        };
    device = adapter.requestDevice(deviceDesc);
}

void Application::InitializeQueue() {
    queue = device.getQueue();
}

void Application::ConfigureSurface() {
    SurfaceConfiguration config = {};
    config.width = width;
    config.height = height;
    config.usage = TextureUsage::RenderAttachment;
    config.format = surfaceFormat;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.device = device;
    config.presentMode = PresentMode::Fifo;
    config.alphaMode = CompositeAlphaMode::Auto;
    surface.configure(config);
}

void Application::OnResize(int newWidth, int newHeight) {
    if (newWidth == 0 || newHeight == 0) return;

    width = newWidth;
    height = newHeight;

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
