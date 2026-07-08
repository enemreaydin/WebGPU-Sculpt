#include "Application.h"
#include "utils/FileHelper.h"
#include "scene/SdfObjectFactory.h"
#include <iostream>
#include <sstream>
#include <filesystem>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
    InitializeRenderer();

    return true;
}

void Application::Terminate() {
    renderer.Destroy();
    graphics.surface.unconfigure();
    graphics.queue.release();
    graphics.surface.release();
    graphics.device.release();
    SDL_DestroyWindow(window.GetHandle());
    SDL_Quit();
}

bool Application::IsRunning() {
    return running;
}

void Application::MainLoop() {
    // Skip drawing until the GPU pipeline is ready.
    if (!renderer.pipeline) {
        return;
    }

    if (!renderer.GetObjectIdTargetView()) {
    return; 
    }

    UpdateCamera();

    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight)
        : 1.0f;
    view = camera.GetViewMatrix();
    proj = camera.GetProjectionMatrix(aspect);

    if (renderer.globalsBuffer) {
        graphics.queue.writeBuffer(renderer.globalsBuffer, 0, &globals, sizeof(GlobalsData));
    }

    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

    CommandEncoderDescriptor encoderDesc = {};
    CommandEncoder encoder = graphics.device.createCommandEncoder(encoderDesc);

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.resolveTarget = nullptr;
#ifdef __EMSCRIPTEN__
    // Browser WebGPU (Dawn) requires depthSlice to be "undefined" for a 2D
    // color attachment; the zero-initialized default (0) is rejected. This
    // field only exists in the newer emscripten header, not the pinned native one.
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = WGPUColor{ 0.1, 0.1, 0.12, 1.0 };

    RenderPassColorAttachment objectIdAttachment = {};
    objectIdAttachment.view = renderer.GetObjectIdTargetView();
    objectIdAttachment.resolveTarget = nullptr;
    #ifdef __EMSCRIPTEN__
        objectIdAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    #endif
    objectIdAttachment.loadOp = LoadOp::Clear;
    objectIdAttachment.storeOp = StoreOp::Store;
    objectIdAttachment.clearValue = WGPUColor{ 4294967295.0, 0.0, 0.0, 0.0 };  // 0xffffffff sentinel: "nothing"

    RenderPassColorAttachment colorAttachments[2] = { colorAttachment, objectIdAttachment };
    RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 2;
    renderPassDesc.colorAttachments = colorAttachments;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;

    // The SDF is drawn as a full-screen quad straight onto the swap-chain
    // (canvas) surface. The React UI is composited over it by the browser, so
    // there is no longer an intermediate offscreen texture.
    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    if (renderer.bindGroup && renderer.sceneBuffer) {
        renderPass.setPipeline(renderer.pipeline);
        renderPass.setBindGroup(0, renderer.bindGroup, 0, nullptr);
        renderPass.draw(6, 1, 0, 0);
    }
    renderPass.end();
    renderPass.release();

    CommandBufferDescriptor cmdBufDesc = {};
    CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();
    graphics.queue.submit(1, &cmdBuf);
    cmdBuf.release();
    targetView.release();

#ifndef __EMSCRIPTEN__
    // On the web the browser presents the canvas automatically; present/poll
    // are native-only.
    graphics.surface.present();
    graphics.device.poll(false);
#endif
}

void Application::OnResize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    width = newWidth; height = newHeight;
    renderWidth = newWidth; renderHeight = newHeight;
    graphics.ConfigureSurface(width, height);
    renderer.CreateRenderTarget(graphics.device, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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

void Application::FindProjectRoot() {
    namespace fs = std::filesystem;

#ifdef __EMSCRIPTEN__
    // In the browser the assets are preloaded into the virtual filesystem at
    // "/assets" (see the --preload-file link flag). Use the absolute mount root
    // so shader paths don't depend on the WASM FS current directory.
    projectRoot = "/";
    return;
#else
    fs::path current = fs::current_path();

    while (!current.empty()) {
        if (fs::exists(current / "assets") && fs::exists(current / "src")) {
            projectRoot = current;
            std::cout << "Project Root found:\n" << projectRoot << std::endl;
            return;
        }
        current = current.parent_path();
    }
    throw std::runtime_error("Project Root cannot be found!");
#endif
}

void Application::InitializeRenderer() {
    renderer.SetProjectRoot(projectRoot);
    renderer.Initialize(graphics.device, graphics.surfaceFormat);
    renderer.CreateRenderTarget(graphics.device, static_cast<uint32_t>(width), static_cast<uint32_t>(height));


    // Create an empty scene buffer so the shader can render the grid.
    sceneSerializer.RefreshSceneBuffer();
}

void Application::UpdateCamera() {
    cameraController.Update(camera, appSettings, viewportInput);
    // Input is per-frame; consume it so a gesture doesn't repeat.
    viewportInput = ViewportInput{};

    glm::vec3 camPos = camera.GetPosition();
    glm::vec3 camTargetPos = camera.GetTargetPosition();

    globals.selectedIndex = selectionState.selectedObjectIndex;
    globals.resolution[0] = static_cast<float>(renderWidth);
    globals.resolution[1] = static_cast<float>(renderHeight);
    globals.showGrid = showGrid ? 1 : 0;
    globals.cameraPos[0] = camPos.x;
    globals.cameraPos[1] = camPos.y;
    globals.cameraPos[2] = camPos.z;
    globals.cameraTarget[0] = camTargetPos.x;
    globals.cameraTarget[1] = camTargetPos.y;
    globals.cameraTarget[2] = camTargetPos.z;
}

// ---------------------------------------------------------------------------
// Control API
// ---------------------------------------------------------------------------
uint32_t Application::PickObjectAt(int x, int y) {
    if (x < 0 || y < 0 || x >= renderWidth || y >= renderHeight) {
        return 0xffffffffu;
    }
    if (!renderer.GetObjectIdTargetView()) {
        return 0xffffffffu;
    }

    bool mapDone = false;
    uint32_t result = 0xffffffffu;

    renderer.ReadObjectIdAt(graphics.device, graphics.queue, x, y, &mapDone, &result);

#ifdef __EMSCRIPTEN__
    while (!mapDone) { emscripten_sleep(0); }
#else
    while (!mapDone) { graphics.device.poll(true); }
#endif

    return result;
}
int Application::AddPrimitive(SdfObjectType type) {
    SdfObjectData object{};
    switch (type) {
    case SdfObjectType::Sphere:   object = SdfObjectFactory::CreateSphere(1.0f); break;
    case SdfObjectType::Box:      object = SdfObjectFactory::CreateBox(1.0f, 1.0f, 1.0f); break;
    case SdfObjectType::Cylinder: object = SdfObjectFactory::CreateCylinder(0.7f, 1.5f); break;
    case SdfObjectType::Torus:    object = SdfObjectFactory::CreateTorus(1.0f, 0.25f); break;
    default: return -1;
    }

    sceneManager.AddObject(object);
    int index = static_cast<int>(sceneManager.GetObjects().size()) - 1;
    selectionState.selectedObjectIndex = index;
    sceneSerializer.RefreshSceneBuffer();
    return index;
}

void Application::RemoveObject(int index) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;

    sceneManager.RemoveObjectAt(index);

    if (objects.empty())
        selectionState.selectedObjectIndex = -1;
    else if (selectionState.selectedObjectIndex >= static_cast<int>(objects.size()))
        selectionState.selectedObjectIndex = static_cast<int>(objects.size()) - 1;

    sceneSerializer.RefreshSceneBuffer();
}

void Application::SelectObject(int index) {
    if (index < -1 || index >= static_cast<int>(sceneManager.GetObjects().size())) return;
    selectionState.selectedObjectIndex = index;
}

void Application::SetObjectPosition(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    objects[index].position = { x, y, z, 0.0f };
    sceneSerializer.RefreshSceneBuffer();
}

void Application::SetObjectSize(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    objects[index].size = { x, y, z, 0.0f };
    sceneSerializer.RefreshSceneBuffer();
}

void Application::SetObjectColor(int index, float r, float g, float b) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    objects[index].color = { r, g, b, 1.0f };
    sceneSerializer.RefreshSceneBuffer();
}

const char* Application::TypeName(int32_t type) {
    switch (static_cast<SdfObjectType>(type)) {
    case SdfObjectType::Sphere:   return "Sphere";
    case SdfObjectType::Box:      return "Box";
    case SdfObjectType::Cylinder: return "Cylinder";
    case SdfObjectType::Torus:    return "Torus";
    default:                      return "Object";
    }
}

std::string Application::GetSceneJson() const {
    const auto& objects = sceneManager.GetObjects();

    std::ostringstream out;
    out << "{\"selectedIndex\":" << selectionState.selectedObjectIndex
        << ",\"showGrid\":" << (showGrid ? "true" : "false")
        << ",\"objects\":[";

    for (size_t i = 0; i < objects.size(); ++i) {
        const SdfObjectData& o = objects[i];
        if (i > 0) out << ",";
        out << "{\"index\":" << i
            << ",\"id\":" << o.objectId
            << ",\"type\":" << o.type
            << ",\"typeName\":\"" << TypeName(o.type) << "\""
            << ",\"position\":[" << o.position[0] << "," << o.position[1] << "," << o.position[2] << "]"
            << ",\"size\":[" << o.size[0] << "," << o.size[1] << "," << o.size[2] << "]"
            << ",\"color\":[" << o.color[0] << "," << o.color[1] << "," << o.color[2] << "]"
            << "}";
    }

    out << "]}";
    return out.str();
}

void Application::NewScene()  { sceneSerializer.NewScene();  selectionState.selectedObjectIndex = -1; }
void Application::OpenScene() { sceneSerializer.OpenSceneFromSDFO(); selectionState.selectedObjectIndex = -1; }
void Application::SaveScene() { sceneSerializer.SaveSceneToSDFO(); }

void Application::CameraOrbit(float dx, float dy) {
    viewportInput.orbiting = true;
    viewportInput.mouseDelta.x += dx;
    viewportInput.mouseDelta.y += dy;
}

void Application::CameraPan(float dx, float dy) {
    viewportInput.panning = true;
    viewportInput.mouseDelta.x += dx;
    viewportInput.mouseDelta.y += dy;
}

void Application::CameraZoom(float delta) {
    viewportInput.scrollDelta += delta;
}
