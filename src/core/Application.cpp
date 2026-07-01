#include "Application.h"
#include "utils/FileHelper.h"
#include "io/ExtensionLoader.h"
#include <glfw3webgpu.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <iostream>
#include "ui/Theme.h"
#include <filesystem>


bool Application::Initialize() {
    FindProjectRoot();
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
    InitializeRenderer();
    
    return true;
}

void Application::Terminate() {
    renderer.Destroy();
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
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
    // Globals'ý da AYNI boyutla, AYNI yerde güncelle
    GlobalsData globals{};
    globals.resolution[0] = sceneViewSize.x;
    globals.resolution[1] = sceneViewSize.y;
    queue.writeBuffer(renderer.globalsBuffer, 0, &globals, sizeof(GlobalsData));

    CommandEncoderDescriptor encoderDesc = {};
    CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

    // --- YENÝ: 1. PASS - SDF sahnesini offscreen texture'a çiz ---
    {
        RenderPassColorAttachment sceneAttachment = {};
        sceneAttachment.view = renderer.GetRenderTargetView();
        sceneAttachment.resolveTarget = nullptr;
        sceneAttachment.loadOp = LoadOp::Clear;
        sceneAttachment.storeOp = StoreOp::Store;
        sceneAttachment.clearValue = WGPUColor{ 0.1, 0.1, 0.12, 1.0 };

        RenderPassDescriptor scenePassDesc = {};
        scenePassDesc.colorAttachmentCount = 1;
        scenePassDesc.colorAttachments = &sceneAttachment;
        scenePassDesc.depthStencilAttachment = nullptr;
        scenePassDesc.timestampWrites = nullptr;

        RenderPassEncoder scenePass = encoder.beginRenderPass(scenePassDesc);
        scenePass.setPipeline(renderer.pipeline);
        scenePass.setBindGroup(0, renderer.bindGroup, 0, nullptr);
        scenePass.draw(6, 1, 0, 0);
        scenePass.end();
        scenePass.release();
    }

    // --- 2. PASS - ImGui'yi swapchain'e çiz (SDF çizimi artýk burada YOK) ---
    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

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

// Imitializer functions

void Application::InitializeWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, "SDF Editor", nullptr, nullptr);
    glfwSetWindowSizeLimits(window, 1000, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    // Resizable property
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int nw, int nh) {
        auto app = static_cast<Application*>(glfwGetWindowUserPointer(w));
        app->OnResize(nw, nh);
        });
}

void Application::InitializeInstance() { instance = createInstance({}); }

void Application::InitializeSurface() { surface = glfwGetWGPUSurface(instance, window); }

void Application::InitializeAdapter() {
    RequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    adapter = instance.requestAdapter(adapterOpts);
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
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth; height = newHeight;
    ConfigureSurface();
}

void Application::InitializeImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyTheme(appTheme); // Theme customization
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

void Application::FindProjectRoot()
    {
    namespace fs = std::filesystem;
    fs::path current = fs::current_path();
    
    while (!current.empty())
    {
        if (fs::exists(current / "assets") &&
            fs::exists(current / "src"))
        {
            projectRoot = current;

            std::cout << "Project Root found:\n"
                << projectRoot << std::endl;
            return;
        }
        current = current.parent_path();
    }
    throw std::runtime_error("Project Root cannot be found!");
}

void Application::InitializeRenderer() {
    namespace fs = std::filesystem;

    // Current working directory (debug)
    fs::path cwd = fs::current_path();
    std::cout << cwd << std::endl;

    // Set project root for shader and asset loading
    renderer.SetProjectRoot(projectRoot);
    renderer.Initialize(device, surfaceFormat);
    renderer.CreateRenderTarget(device, 1000, 800);

    std::filesystem::path bdfPath = projectRoot / "assets/test.sdfo";

    // Load scene from BDF file
    if (ExtensionLoader::LoadFromSDFO(bdfPath.string(), sceneManager)) {
        // Upload scene objects to GPU
        renderer.CreateSceneBuffer(device, queue, sceneManager.GetObjects());
    }
    else {
        std::cerr << "[WARNING]: BDF file can not be loaded "
            << bdfPath << std::endl;
    }
}

void Application::AddObjectToScene(const SdfObjectData& object)
{
    SdfObjectData obj = object;

    if (sceneManager.GetObjects().empty())
    {
        obj.op = static_cast<int32_t>(SdfOperationType::None);
    }

    sceneManager.AddObject(obj);

    renderer.CreateSceneBuffer(
        device,
        queue,
        sceneManager.GetObjects()
    );
}
