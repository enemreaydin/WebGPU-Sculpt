#include "Application.h"
#include "utils/FileHelper.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>
#include <iostream>
#include "ui/Theme.h"
#include <filesystem>
#include <ImGuizmo.h>


bool Application::Initialize() {
    FindProjectRoot();
    window.InitializeWindow(width, height, this);
    graphics.InitializeInstance();
    graphics.InitializeSurface(window.GetHandle());
    graphics.InitializeAdapter();
    graphics.InitializeDevice();
    graphics.InitializeQueue();
    graphics.ConfigureSurface(width, height);
    graphics.adapter.release();
    graphics.instance.release();
    InitializeImgui();
    InitializeRenderer();
    
    return true;
}

void Application::Terminate() {
    renderer.Destroy();
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    graphics.surface.unconfigure();
    graphics.queue.release();
    graphics.surface.release();
    graphics.device.release();
    graphics.adapter.release();
    graphics.instance.release();
    SDL_DestroyWindow(window.GetHandle());
    SDL_Quit();
}

bool Application::IsRunning() {
    return running;
}

void Application::MainLoop() {    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            running = false;
        }
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                running = false;
            }
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                OnResize(event.window.data1, event.window.data2);
            }
        }
    }        
    // Skip drawing until the GPU resources are ready.
    if (!renderer.pipeline) {
        return;
    }

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    // Camera updates, view and projection matrices for Gizmo
    UpdateCamera();
    float aspect = (sceneViewSize.y > 0.0f)
        ? sceneViewSize.x / sceneViewSize.y
        : 1.0f;
    view = camera.GetViewMatrix();
    proj = camera.GetProjectionMatrix(aspect);

    // Keyboard IO 
    noModifiersPressed = !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt;
    if (renderer.globalsBuffer)
    {
        graphics.queue.writeBuffer(
            renderer.globalsBuffer,
            0,
            &globals,
            sizeof(GlobalsData)
        );
    }

    CommandEncoderDescriptor encoderDesc = {};
    CommandEncoder encoder = graphics.device.createCommandEncoder(encoderDesc);

    // Pass 1: render the SDF scene into an offscreen texture.
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
        if (renderer.bindGroup && renderer.sceneBuffer) {
            scenePass.setPipeline(renderer.pipeline);
            scenePass.setBindGroup(0, renderer.bindGroup, 0, nullptr);
            scenePass.draw(6, 1, 0, 0);
        }
        scenePass.end();
        scenePass.release();
    }

    // Pass 2: draw ImGui to the swapchain (the SDF scene is not drawn here).
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

    // ImGui draw calls
    DrawGui();
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);

    renderPass.end();
    renderPass.release();

    CommandBufferDescriptor cmdBufDesc = {};
    CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();
    graphics.queue.submit(1, &cmdBuf);
    cmdBuf.release();
    targetView.release();
    graphics.surface.present();
    graphics.device.poll(false);
}

// Initializer functions

void Application::OnResize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth; height = newHeight;
    graphics.ConfigureSurface(width, height);
}

void Application::InitializeImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyTheme(appTheme); // Theme customization
    ImGui_ImplSDL2_InitForOther(window.GetHandle()); // Glfw initialization

    ImGui_ImplWGPU_InitInfo initInfo;
    initInfo.Device = graphics.device;
    initInfo.NumFramesInFlight = 3;
    initInfo.RenderTargetFormat = (WGPUTextureFormat)graphics.surfaceFormat;
    initInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&initInfo); // wgpu initialization
}

TextureView Application::GetNextSurfaceTextureView() {
    SurfaceTexture surfaceTexture;
    graphics.surface.getCurrentTexture(&surfaceTexture);
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

void Application::InitializeRenderer()
{
    renderer.SetProjectRoot(projectRoot);
    renderer.Initialize(graphics.device, graphics.surfaceFormat);
    renderer.CreateRenderTarget(graphics.device, 1000, 800);

    // Create an empty scene buffer so the shader can render the grid.
    sceneSerializer.RefreshSceneBuffer();
}

void Application::AddObjectToScene(const SdfObjectData& object)
{
    // SceneManager assigns the permanent objectId and attaches the primitive to the
    // CSG tree's root Union node.
    sceneManager.AddObject(object);

    selectionState.selectedObjectIndex = static_cast<int>(sceneManager.GetObjects().size()) - 1;

    sceneSerializer.RefreshSceneBuffer();
}

void Application::UpdateCamera() {
    cameraController.Update(camera, appSettings, sceneViewHovered, noModifiersPressed, sceneCursorDelta, sceneScrollDelta);


    glm::vec3 camPos = camera.GetPosition();
    glm::vec3 camTargetPos = camera.GetTargetPosition();
    globals.selectedIndex = selectionState.selectedObjectIndex;   // -1 means "nothing selected"
    globals.resolution[0] = sceneViewSize.x;
    globals.resolution[1] = sceneViewSize.y;
    globals.showGrid = showGrid ? 1 : 0;
    globals.cameraPos[0] = camPos.x;
    globals.cameraPos[1] = camPos.y;
    globals.cameraPos[2] = camPos.z;
    globals.cameraTarget[0] = camTargetPos.x;
    globals.cameraTarget[1] = camTargetPos.y;
    globals.cameraTarget[2] = camTargetPos.z;

}

