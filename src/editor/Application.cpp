#include "Application.h"
#include <glm/gtc/quaternion.hpp>
#include "utils/FileHelper.h"
#include "serialization/ExtensionLoader.h"
#include "editor/command/SceneEditCommand.h"
#include "scene/SdfObjectFactory.h"
#include "scene/PrimitiveGeometry.h"
#include "interaction/GizmoMath.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <array>
#include <memory>
#include <limits>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static std::array<float, 3> QuatToEulerDegrees(const std::array<float, 4>& r);

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

    // Advance an in-flight banded image export: submit the next band once the
    // previous one's GPU work completed (see SdfRenderer::StepFinalRender).
    renderer.StepFinalRender(graphics.device, graphics.queue);

    // While an export is running the viewport is hidden behind the progress
    // overlay, so drawing it would only steal GPU time from the export — and on
    // a heavy scene the viewport's own multi-second submission counts against
    // the same OS watchdog budget the export bands are sized to respect. Skip
    // the passes entirely; the surface keeps showing its last frame.
    const int exportState = renderer.GetFinalRenderState();
    // Any export state transition means the canvas is about to be shown or hidden
    // (progress overlay -> result -> viewport). Because the viewport passes are
    // skipped for the whole export, the surface still holds a pre-export frame,
    // so redraw on every edge rather than trying to guess which one uncovers it.
    if (exportState != m_LastExportState) {
        RequestRedraw();
        m_LastExportState = exportState;
    }
    if (exportState == 1 || exportState == 2) return;

    // An animated view change advances on wall-clock time, so it is the one piece
    // of state that changes without anyone calling a setter. Nothing else
    // invalidates while it runs, so it has to keep asking for frames itself —
    // otherwise the loop idles mid-flight and the camera arrives in one jump.
    // Must sit above the ShouldRender guard, or the request would never be made.
    if (cameraTransition.IsActive()) RequestRedraw();

    if (!frameScheduler.ShouldRender()) {return;}

    bool isCameraMoving = UpdateCamera();
    // Falling edge camera-moving -> idle. Every frame drawn during a camera move
    // forces taaEnabled = 0 and zeroes the jitter below, so the accumulation
    // layers end up holding four identical un-jittered images. Nothing else
    // schedules the reconvergence, so without this the viewport would stay at
    // that aliased single-sample quality after every orbit until some unrelated
    // edit invalidated it. Continuous rendering used to hide this for free.
    if (m_CameraWasMoving && !isCameraMoving) RequestRedraw();
    m_CameraWasMoving = isCameraMoving;

    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight)
        : 1.0f;
    view = camera.GetViewMatrix();
    proj = camera.GetProjectionMatrix(aspect);

    // Refresh the gizmo geometry from the *same* camera the SDF is about to use,
    // so the shader-drawn gizmo lands in the identical frame — zero lag, even
    // while orbiting/panning.
    UpdateGizmoGlobals(view, proj);

    // Push the user's render settings into the GPU uniform.
    SyncRenderGlobals();
    m_JitterIndex = (m_JitterIndex + 1) & 3;
    // The scene always renders into layer[m_JitterIndex]; fs_resolve reads this
    // index to show the current frame when TAA is off. With TAA off we also skip
    // the sub-pixel jitter so the single shown layer stays pin-sharp.
    globals.jitterLayer = static_cast<int32_t>(m_JitterIndex);
    if (isCameraMoving)
    {
        globals.taaEnabled = 0;
    }
    
    if (globals.taaEnabled) {
        UpdateJitterGlobal(m_JitterIndex);
    } else {
        globals.jitterOffset[0] = 0.0f;
        globals.jitterOffset[1] = 0.0f;
    }
    if (renderer.globalsBuffer) {
        graphics.queue.writeBuffer(renderer.globalsBuffer, 0, &globals, sizeof(GlobalsData));
    }

    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

    CommandEncoderDescriptor encoderDesc = {};
    CommandEncoder encoder = graphics.device.createCommandEncoder(encoderDesc);

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = renderer.GetAccumLayerView(m_JitterIndex);
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

    RenderPassColorAttachment resolveColorAttachment = {};
    resolveColorAttachment.view = targetView;
    resolveColorAttachment.loadOp = LoadOp::Clear;
    resolveColorAttachment.storeOp = StoreOp::Store;

#ifdef __EMSCRIPTEN__
    resolveColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
    RenderPassColorAttachment resolveColorAttachments[1] = {resolveColorAttachment};
    RenderPassDescriptor resolveRenderPassDesc = {};
    resolveRenderPassDesc.colorAttachmentCount = 1;
    resolveRenderPassDesc.colorAttachments = resolveColorAttachments;
    resolveRenderPassDesc.depthStencilAttachment = nullptr;
    resolveRenderPassDesc.timestampWrites = nullptr;

    RenderPassEncoder resolveRenderPass = encoder.beginRenderPass(resolveRenderPassDesc);

    if (renderer.resolvePipeline && renderer.resolveBindGroup) {
        resolveRenderPass.setPipeline(renderer.resolvePipeline);
        resolveRenderPass.setBindGroup(0, renderer.resolveBindGroup, 0, nullptr);
        resolveRenderPass.draw(6, 1, 0, 0);
    }
    resolveRenderPass.end();
    resolveRenderPass.release();

    CommandBufferDescriptor cmdBufDesc = {};
    CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();
    graphics.queue.submit(1, &cmdBuf);
    cmdBuf.release();
    targetView.release();
    frameStats.OnFrameSubmitted(graphics.queue);

#ifndef __EMSCRIPTEN__
    // On the web the browser presents the canvas automatically; present/poll
    // are native-only.
    graphics.surface.present();
    graphics.device.poll(false);
#endif
    frameScheduler.Consume();
}

void Application::OnResize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0) return;
    RequestRedraw();
    width = newWidth; height = newHeight;
    renderWidth = newWidth; renderHeight = newHeight;
    graphics.ConfigureSurface(width, height);
    renderer.CreateRenderTarget(graphics.device, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool Application::StartFinalRender(uint32_t outWidth, uint32_t outHeight) {
    if (outWidth < 1 || outHeight < 1 || outWidth > 4096 || outHeight > 4096) return false;
    const float aspect = static_cast<float>(outWidth) / static_cast<float>(outHeight);
    view = camera.GetViewMatrix();
    proj = camera.GetProjectionMatrix(aspect);

    // Refresh the member globals so the export picks up the current look settings,
    // then branch off a private copy for the offscreen pass.
    SyncRenderGlobals();
    GlobalsData finalGlobals = globals;

    // The export renders at its OWN quality budget, not the viewport's — that is
    // the whole point of the split: work fast on screen, export clean.
    ApplyQuality(appSettings.exportQuality, finalGlobals);
    // Temporal accumulation needs 4 consecutive frames to converge; the export is
    // a single offscreen pass through the scene pipeline (no accumulation array,
    // no resolve), so TAA is meaningless here and the ray must not be jittered.
    // SSAA (aaSamples) is what anti-aliases an export.
    finalGlobals.taaEnabled = 0;
    finalGlobals.jitterOffset[0] = 0.0f;
    finalGlobals.jitterOffset[1] = 0.0f;

    finalGlobals.resolution[0] = static_cast<float>(outWidth);
    finalGlobals.resolution[1] = static_cast<float>(outHeight);
    // No editor furniture in the output image: no grid, no selection highlight,
    // no gizmo.
    finalGlobals.showGrid = 0;
    finalGlobals.selectedIndex = -1;
    finalGlobals.gizmoVisible = 0;
    finalGlobals.qualityPad1 = 1;

    glm::vec3 pos = camera.GetPosition(), target = camera.GetTargetPosition();
    finalGlobals.cameraPos[0]=pos.x; finalGlobals.cameraPos[1]=pos.y; finalGlobals.cameraPos[2]=pos.z;
    finalGlobals.cameraTarget[0]=target.x; finalGlobals.cameraTarget[1]=target.y; finalGlobals.cameraTarget[2]=target.z;
    return renderer.StartFinalRender(graphics.device, graphics.queue, finalGlobals, outWidth, outHeight);
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
    RequestRedraw();
    sceneSerializer.RefreshSceneBuffer();
}

bool Application::UpdateCamera() {
    // A drag beats the animation: an orbit gesture cancels an in-flight snap so
    // the two never fight over yaw/pitch in the same frame. Pan and zoom move the
    // target/distance instead, so they can run alongside it.
    if (viewportInput.orbiting) cameraTransition.Cancel();

    cameraController.Update(camera, appSettings, viewportInput);

    // Sample the eased orientation for this frame, if a view change is running.
    float animatedYaw = 0.0f;
    float animatedPitch = 0.0f;
    const bool animating = cameraTransition.Advance(animatedYaw, animatedPitch);
    if (animating) camera.SetOrientation(animatedYaw, animatedPitch);

    // Reported as "camera moving" so the animation gets the same treatment as a
    // drag: TAA jitter is suppressed while it runs (the accumulation layers would
    // smear four different orientations together) and the moving -> idle edge in
    // MainLoop schedules the reconvergence once it lands.
    bool isMoving = animating || viewportInput.panning || viewportInput.orbiting || viewportInput.scrollDelta;
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
    return isMoving;
}


// Schedule a full convergence run. One rendered frame refreshes exactly one of
// the kAccumLayers accumulation layers, and fs_resolve averages all of them — so
// after any change the image only settles once every layer has been redrawn.
// With TAA off the resolve shows the single current layer, so one frame is the
// finished image. See CLAUDE.md §7a.
void Application::RequestRedraw()
{
    frameScheduler.Request(appSettings.viewportQuality.taaEnabled
        ? SdfRenderer::kAccumLayers
        : 1);
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

void Application::OnSceneChanged()
{   
    RequestRedraw();
    if (m_SceneEditDepth > 0) m_SceneEditChanged = true;
    const auto& objects = sceneManager.GetObjects();

    for (const SdfObjectData& object : objects)
    {
        if (EditorObject* editorObject = editorStore.Find(object.objectId))
        {
            editorObject->rotationEuler = QuatToEulerDegrees(object.rotation);
        }
    }
    sceneSerializer.RefreshSceneBuffer();
}

void Application::OnCsgChanged()
{   
    RequestRedraw();
    if (m_SceneEditDepth > 0) m_SceneEditChanged = true;
    sceneSerializer.RefreshCsgBuffer();
}

SceneEditSnapshot Application::CaptureSceneEditSnapshot() const
{
    SceneEditSnapshot snapshot;
    snapshot.scene = sceneManager.CaptureState();
    snapshot.editorObjects = editorStore.CaptureState();
    snapshot.selection = selectionState;
    snapshot.settings = appSettings;
    return snapshot;
}

bool Application::ShouldRecordSceneEdit() const
{
    return !m_UndoStack.IsApplyingHistory() && m_SceneEditDepth == 0;
}

void Application::FinishSceneEdit(const char* name, const SceneEditSnapshot& before)
{
    if (m_UndoStack.IsApplyingHistory())
        return;

    auto command = std::make_unique<SceneEditCommand>(
        name ? name : "Scene Edit",
        before,
        CaptureSceneEditSnapshot(),
        editorStore,
        selectionState,
        appSettings
    );
    m_UndoStack.PushAlreadyExecuted(std::move(command));
}

void Application::BeginSceneEdit(const char* name)
{
    if (m_UndoStack.IsApplyingHistory())
        return;

    if (m_SceneEditDepth == 0)
    {
        m_SceneEditName = name ? name : "Scene Edit";
        m_SceneEditBefore = CaptureSceneEditSnapshot();
        m_SceneEditChanged = false;
    }
    ++m_SceneEditDepth;
}

void Application::CommitSceneEdit()
{
    if (m_SceneEditDepth <= 0)
        return;

    --m_SceneEditDepth;
    if (m_SceneEditDepth == 0)
    {
        if (m_SceneEditChanged) FinishSceneEdit(m_SceneEditName.c_str(), m_SceneEditBefore);
        m_SceneEditName.clear();
        m_SceneEditChanged = false;
    }
}

void Application::CancelSceneEdit()
{
    if (m_SceneEditDepth <= 0)
        return;

    m_SceneEditDepth = 0;
    sceneManager.RestoreState(m_SceneEditBefore.scene);
    editorStore.RestoreState(m_SceneEditBefore.editorObjects);
    selectionState = m_SceneEditBefore.selection;
    appSettings = m_SceneEditBefore.settings;
    m_SceneEditName.clear();
    m_SceneEditChanged = false;
    OnSceneChanged();
}

void Application::Undo() {
    if (!m_UndoStack.CanUndo())
        return;

    m_UndoStack.Undo(sceneManager);
    UpdateSelectionHighlight();
    OnSceneChanged();
}

void Application::Redo() {
    if (!m_UndoStack.CanRedo())
        return;

    m_UndoStack.Redo(sceneManager);
    UpdateSelectionHighlight();
    OnSceneChanged();
}

bool Application::CanUndo() const {
    return m_UndoStack.CanUndo();
}

bool Application::CanRedo() const {
    return m_UndoStack.CanRedo();
}

void Application::ClearUndoStack() {
    m_UndoStack.Clear();
}

UndoStack& Application::GetUndoStack() {
    return m_UndoStack;
}

const UndoStack& Application::GetUndoStack() const {
    return m_UndoStack;
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
    case SdfObjectType::Link:       object = SdfObjectFactory::CreateLink(0.5f, 0.4f, 0.15f); break;
    case SdfObjectType::Cone:       object = SdfObjectFactory::CreateCone(1.5f, 0.5f); break;
    case SdfObjectType::Capsule:    object = SdfObjectFactory::CreateCapsule(0.4f, 0.7f); break;
    case SdfObjectType::SolidAngle: object = SdfObjectFactory::CreateSolidAngle(1.2f, 0.8f); break;
    case SdfObjectType::Pyramid:    object = SdfObjectFactory::CreatePyramid(0.6f, 1.2f); break;
    default: return -1;
    }

    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    sceneManager.AddObject(object);
    int index = static_cast<int>(sceneManager.GetObjects().size()) - 1;

    // Give the new object its editor record (default name + shape params derived
    // from the factory size).
    const SdfObjectData& created = sceneManager.GetObjects()[index];
    editorStore.Create(created.objectId, type,
                       { created.size[0], created.size[1], created.size[2] });

    int32_t newNodeId = sceneManager.NodeIdForObject(created.objectId);
    selectionState.Clear();
    if (newNodeId != -1) { selectionState.Add(newNodeId); selectionState.primaryNodeId = newNodeId; }
    UpdateSelectionHighlight();

    OnSceneChanged();
    if (record) FinishSceneEdit("Add Object", before);
    return index;
}

void Application::RemoveObject(int index) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;

    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    editorStore.Remove(objects[index].objectId);
    sceneManager.RemoveObjectAt(index);

    // The removed primitive's node is now dead; recompute the highlight so any
    // stale per-object flag / primary index is dropped.
    UpdateSelectionHighlight();

    OnSceneChanged();
    if (record) FinishSceneEdit("Delete Object", before);
}

// Rebuild every object's editor record from the current scene — used after a
// load, where objects come straight off disk with no records yet.
// Decompose a unit quaternion back into the Euler degrees the properties panel
// edits. This must invert the composition in SetObjectRotation (R = Rz*Ry*Rx);
// any other axis order silently re-poses loaded scenes. The derivation is in
// row-major R[row][col], but glm is column-major, so R[row][col] == m[col][row].
static std::array<float, 3> QuatToEulerDegrees(const std::array<float, 4>& r) {
    // A zero quaternion (legacy/uninitialized data) would make normalize() NaN.
    const float len2 = r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + r[3]*r[3];
    if (len2 < 1e-8f) return { 0.0f, 0.0f, 0.0f };

    const glm::quat q = glm::normalize(glm::quat(r[3], r[0], r[1], r[2]));  // ctor is (w,x,y,z)
    const glm::mat3 m = glm::mat3_cast(q);

    // R[2][0] == -sin(beta); clamp because float drift past 1.0 makes asin NaN.
    const float sinBeta = std::clamp(-m[0][2], -1.0f, 1.0f);
    const float beta = std::asin(sinBeta);

    float alpha, gamma;
    if (std::abs(sinBeta) > 0.9999f) {
        // Gimbal lock: cos(beta) == 0, so X and Z turn about the same physical
        // axis and only their sum survives. Pin Z, fold the rest into X.
        gamma = 0.0f;
        alpha = std::atan2(sinBeta * m[1][0], m[1][1]);
    }
    else {
        alpha = std::atan2(m[1][2], m[2][2]);
        gamma = std::atan2(m[0][1], m[0][0]);
    }
    return { glm::degrees(alpha), glm::degrees(beta), glm::degrees(gamma) };
}

void Application::RebuildEditorRecords() {
    editorStore.Clear();
    for (const SdfObjectData& o : sceneManager.GetObjects()) {
        EditorObject& eo = editorStore.Create(o.objectId, static_cast<SdfObjectType>(o.type),
                                              { o.size[0], o.size[1], o.size[2] });
        // The quaternion round-trips through .sdfo; the Euler angles do not.
        eo.rotationEuler = QuatToEulerDegrees(o.rotation);
    }
}

void Application::SelectObject(int index) {
    if (index < -1 || index >= static_cast<int>(sceneManager.GetObjects().size())) return;
    selectionState.selectedObjectIndex = index;
    // Feeds globals.selectedIndex in UpdateCamera, i.e. the in-shader highlight.
    // Unlike SelectNode this path never reaches UpdateSelectionHighlight, so it
    // has to invalidate on its own.
    RequestRedraw();
}
void Application::DeleteSelection() {
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    // IndexForNodeId returns -1 for a node already killed as part of an earlier
    // subtree in this same loop, so overlapping selections are handled safely.
    for (int32_t nodeId : selectionState.selectedNodeIds) {
        int32_t idx = sceneManager.GetCsgTree().IndexForNodeId(nodeId);
        if (idx != -1) sceneManager.RemoveNodeSubtree(idx);
    }
    selectionState.Clear();
    UpdateSelectionHighlight();
    OnSceneChanged();
    if (record) FinishSceneEdit("Delete Selection", before);
}


void Application::SetObjectPosition(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    objects[index].position = { x, y, z, 0.0f };
    OnSceneChanged();
    if (record) FinishSceneEdit("Move Object", before);
}

void Application::SetObjectSize(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    objects[index].size = { x, y, z, 0.0f };
    OnSceneChanged();
    if (record) FinishSceneEdit("Resize Object", before);
}

void Application::SetObjectColor(int index, float r, float g, float b) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    objects[index].color = { r, g, b, 1.0f };
    OnSceneChanged();
    if (record) FinishSceneEdit("Change Color", before);
}

// --- Group (CSG subtree) batch edits ----------------------------------------
// Groups own no color/size of their own; these fan an edit out over every
// primitive in the subtree, mutating fields the primitives already have. Both
// run as a single scene-edit so one Ctrl+Z reverts the whole group change.

void Application::SetGroupColor(int nodeId, float r, float g, float b) {
    const int32_t idx = sceneManager.GetCsgTree().IndexForNodeId(nodeId);
    if (idx == -1) return;
    std::vector<int32_t> ids;
    sceneManager.CollectSubtreeObjectIds(idx, ids);
    if (ids.empty()) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    for (int32_t oid : ids) {
        if (SdfObjectData* obj = sceneManager.FindObjectById(oid))
            obj->color = { r, g, b, 1.0f };
    }
    OnSceneChanged();
    if (record) FinishSceneEdit("Set Group Color", before);
}

void Application::ScaleGroup(int nodeId, float factor) {
    if (!std::isfinite(factor) || factor <= 0.0f) return;
    const int32_t idx = sceneManager.GetCsgTree().IndexForNodeId(nodeId);
    if (idx == -1) return;
    std::vector<int32_t> ids;
    sceneManager.CollectSubtreeObjectIds(idx, ids);
    if (ids.empty()) return;

    // Anchor = centroid of the subtree's primitive positions. Recomputed each
    // call from the objects themselves — groups store no center of their own.
    glm::vec3 anchor(0.0f);
    int32_t count = 0;
    for (int32_t oid : ids) {
        if (const SdfObjectData* obj = sceneManager.FindObjectById(oid)) {
            anchor += glm::vec3(obj->position[0], obj->position[1], obj->position[2]);
            ++count;
        }
    }
    if (count == 0) return;
    anchor /= static_cast<float>(count);

    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    for (int32_t oid : ids) {
        SdfObjectData* obj = sceneManager.FindObjectById(oid);
        if (!obj) continue;
        // Move the primitive along the anchor->position ray so the whole group
        // grows/shrinks uniformly about the anchor.
        glm::vec3 p(obj->position[0], obj->position[1], obj->position[2]);
        p = anchor + factor * (p - anchor);
        obj->position = { p.x, p.y, p.z, 0.0f };
        // Scale the persisted editor scale; size re-derives from params * scale,
        // so the change survives save/reload (a raw size write would not).
        if (EditorObject* eo = editorStore.Find(oid)) {
            eo->scale = { eo->scale[0] * factor, eo->scale[1] * factor, eo->scale[2] * factor };
            const auto size = PrimitiveGeometry::ComputeSize(
                static_cast<SdfObjectType>(obj->type), eo->params, eo->scale);
            obj->size = { size[0], size[1], size[2], 0.0f };
        }
    }
    OnSceneChanged();
    if (record) FinishSceneEdit("Scale Group", before);
}

// --- Editor attribute setters -----------------------------------------------
// Each resolves the object's editor record via its permanent objectId; scale
// and shape-param edits then recompute the SDF `size` from the params.

void Application::SetObjectName(int index, const char* name) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->name = name ? name : "";
    if (record) FinishSceneEdit("Rename Object", before);
}

void Application::SetNodeName(int nodeId, const char* name) {
    editorStore.SetNodeName(nodeId, name ? name : "");
}

void Application::SetObjectVisible(int index, bool visible) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->visible = visible;
    if (record) FinishSceneEdit("Set Visibility", before);
}

void Application::SetObjectLocked(int index, bool locked) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->locked = locked;
    if (record) FinishSceneEdit("Set Lock", before);
}

void Application::SetObjectRotation(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    EditorObject* eo = editorStore.Find(objects[index].objectId);
    if (!eo) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    eo->rotationEuler = { x, y, z };

    // Euler degrees -> local-to-world quaternion. Extrinsic X, then Y, then Z:
    // composition applies right-to-left, so the product is qz * qy * qx. This
    // order is part of the .sdfo format — changing it re-poses every saved scene.
    // The shader rotates the query point by the conjugate (rotateInverse).
    const glm::quat qx = glm::angleAxis(glm::radians(x), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat qy = glm::angleAxis(glm::radians(y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat qz = glm::angleAxis(glm::radians(z), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat q  = glm::normalize(qz * qy * qx);

    // Component-wise: glm's in-memory quat order is not (x,y,z,w).
    objects[index].rotation = { q.x, q.y, q.z, q.w };
    OnSceneChanged();
    if (record) FinishSceneEdit("Rotate Object", before);
}

void Application::SetObjectScale(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    EditorObject* eo = editorStore.Find(objects[index].objectId);
    if (!eo) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    eo->scale = { x, y, z };
    auto size = PrimitiveGeometry::ComputeSize(
        static_cast<SdfObjectType>(objects[index].type), eo->params, eo->scale);
    objects[index].size = { size[0], size[1], size[2], 0.0f };
    OnSceneChanged();
    if (record) FinishSceneEdit("Scale Object", before);
}

void Application::SetObjectParam(int index, int paramId, float value) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    EditorObject* eo = editorStore.Find(objects[index].objectId);
    if (!eo) return;
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    switch (static_cast<PrimitiveParam>(paramId)) {
    case PrimitiveParam::Radius:    eo->params.radius = value; break;
    case PrimitiveParam::Height:    eo->params.height = value; break;
    case PrimitiveParam::Thickness: eo->params.thickness = value; break;
    case PrimitiveParam::Roundness: eo->params.roundness = value; break;
    case PrimitiveParam::Length:    eo->params.length = value; break;
    case PrimitiveParam::Angle:     eo->params.angle = value; break;
    default: return;
    }
    auto size = PrimitiveGeometry::ComputeSize(
        static_cast<SdfObjectType>(objects[index].type), eo->params, eo->scale);
    objects[index].size = { size[0], size[1], size[2], 0.0f };
    OnSceneChanged();
    if (record) FinishSceneEdit("Change Shape Parameter", before);
}

void Application::ClearSelection(){
    selectionState.Clear();
    UpdateSelectionHighlight();
}

// Viewport pick -> selection. Selects the whole top-level group containing the
// picked primitive (Blender-style); individual nodes stay selectable from the
// outliner. -1 / out-of-range = empty space: a plain click clears, additive keeps.
void Application::SelectByPick(int pickIndex, int mode) {
    const auto& objects = sceneManager.GetObjects();
    if (pickIndex < 0 || pickIndex >= static_cast<int>(objects.size())) {
        if (mode == 0) {
            selectionState.Clear();
            UpdateSelectionHighlight();
        }
        return;
    }

    int32_t objectId = objects[pickIndex].objectId;
    int32_t nodeId = sceneManager.TopLevelNodeIdForObject(objectId);
    if (nodeId == -1) return;
    SelectNode(nodeId, mode);   // handles replace/toggle + UpdateSelectionHighlight
}

// Recompute the selection highlight after any selection change:
//  (1) selectedObjectIndex = the primary node's primitive vector index (or -1 if
//      the primary is a group) — still used by the single-object gizmo/properties
//      path; and
//  (2) the per-object `selected` flag on every primitive under ANY selected node,
//      so a whole group lights up. The flag lives in the scene buffer, so we
//      re-upload it here (selection is user-driven, not per-frame, so this is cheap).
void Application::UpdateSelectionHighlight() {
    const CsgTree& tree = sceneManager.GetCsgTree();
    auto& objects = sceneManager.GetObjects();

    // (1) primary primitive index
    selectionState.selectedObjectIndex = -1;
    if (selectionState.primaryNodeId != -1) {
        int32_t idx = tree.IndexForNodeId(selectionState.primaryNodeId);
        if (idx != -1 && tree.GetNodes()[idx].type == CsgNodeType::Primitive) {
            int32_t oid = tree.GetNodes()[idx].objectId;
            for (int32_t i = 0; i < static_cast<int32_t>(objects.size()); ++i)
                if (objects[i].objectId == oid) { selectionState.selectedObjectIndex = i; break; }
        }
    }

    // (2) per-object highlight flags for the whole selection
    for (SdfObjectData& o : objects) o.selected = 0;

    std::vector<int32_t> subtreeIds;
    for (int32_t nodeId : selectionState.selectedNodeIds) {
        int32_t idx = tree.IndexForNodeId(nodeId);
        if (idx == -1) continue;
        subtreeIds.clear();
        sceneManager.CollectSubtreeObjectIds(idx, subtreeIds);
        for (int32_t oid : subtreeIds)
            if (SdfObjectData* o = sceneManager.FindObjectById(oid)) o->selected = 1;
    }
    RequestRedraw();
    sceneSerializer.RefreshSceneBuffer();
}


void Application::SelectNode(int nodeId, int mode) {
    if (sceneManager.GetCsgTree().IndexForNodeId(nodeId) == -1) return;  // unknown/dead

    if (mode == 1) {                    // toggle
        if (selectionState.Contains(nodeId)) {
            selectionState.Remove(nodeId);
            if (selectionState.primaryNodeId == nodeId)
                selectionState.primaryNodeId =
                    selectionState.selectedNodeIds.empty() ? -1 : selectionState.selectedNodeIds.back();
        }
        else {
            selectionState.Add(nodeId);
            selectionState.primaryNodeId = nodeId;
        }
    }
    else {                            // replace
        selectionState.Clear();
        selectionState.Add(nodeId);
        selectionState.primaryNodeId = nodeId;
    }
    UpdateSelectionHighlight();
}
int Application::ApplyOperation(int opType) {
    CsgNodeType t;
    switch (opType) {
        case 1: t = CsgNodeType::Union;        break;
        case 2: t = CsgNodeType::Subtract;     break;
        case 3: t = CsgNodeType::Intersection; break;
        case 4: t = CsgNodeType::Group;        break;   // pure container (no boolean effect)
        default: return -1;
    }

    // Order children by SELECTION order: the first-selected node is the base
    // (it becomes firstChild in the RPN fold), and for Subtract the remaining
    // selected nodes are carved out of it. So "click A, then B, Subtract" gives
    // A − B (the second selection is removed from the first). selectedNodeIds is
    // maintained in click order by SelectionState::Add, so we use it directly
    // rather than putting the primary/last-clicked node first.
    const CsgTree& tree = sceneManager.GetCsgTree();
    std::vector<int32_t> indices;
    for (int32_t nid : selectionState.selectedNodeIds) {
        int32_t idx = tree.IndexForNodeId(nid);
        if (idx != -1) indices.push_back(idx);
    }
    if (indices.size() < 2) return -1;

    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    int32_t newNodeId = sceneManager.GroupNodes(indices, t);
    if (newNodeId != -1) {
        selectionState.Clear();
        selectionState.Add(newNodeId);
        selectionState.primaryNodeId = newNodeId;
        UpdateSelectionHighlight();
    }
    OnSceneChanged();   // recompile RPN + upload
    if (record) FinishSceneEdit(t == CsgNodeType::Group ? "Group Objects" : "Apply CSG Operation", before);
    return newNodeId;
}

void Application::SetBlendMode(int nodeId, int mode)
{
    if (mode != 0 && mode != 1) return;
    CsgNode* node = sceneManager.GetCsgTree().FindNodeById(nodeId);
    const BlendMode value = static_cast<BlendMode>(mode);
    if (!node || (node->type != CsgNodeType::Union && node->type != CsgNodeType::Subtract && node->type != CsgNodeType::Intersection) || node->blendMode == value) return;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->blendMode = value;
    OnCsgChanged();
    if (record) FinishSceneEdit("Change Blend Mode", before);
}

void Application::SetBlendRadius(int nodeId, float value)
{
    if (!std::isfinite(value) || value < 0.0f) return;
    CsgNode* node = sceneManager.GetCsgTree().FindNodeById(nodeId);
    if (!node || (node->type != CsgNodeType::Union && node->type != CsgNodeType::Subtract && node->type != CsgNodeType::Intersection) || node->blendRadius == value) return;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->blendRadius = value;
    OnCsgChanged();
    if (record) FinishSceneEdit("Change Blend Radius", before);
}

int Application::AddRoundnessModifier(int targetNodeId)
{
    CsgTree& tree = sceneManager.GetCsgTree();
    CsgNode* node = tree.FindNodeById(targetNodeId);
    if (!node || node->type != CsgNodeType::Primitive) return -1;
    const SdfObjectData* object = sceneManager.FindObjectById(node->objectId);
    if (!object || object->type != static_cast<int32_t>(SdfObjectType::Box) ||
        node->modifierType == ModifierType::Roundness) return -1;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->modifierType = ModifierType::Roundness;
    node->modifierEnabled = true;
    node->modifierAmount = 0.0f;
    OnCsgChanged();
    if (record) FinishSceneEdit("Add Roundness Modifier", before);
    return targetNodeId;
}

void Application::SetRoundnessEnabled(int modifierNodeId, bool enabled)
{
    CsgNode* node = sceneManager.GetCsgTree().FindNodeById(modifierNodeId);
    if (!node || node->type != CsgNodeType::Primitive || node->modifierType != ModifierType::Roundness || node->modifierEnabled == enabled) return;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->modifierEnabled = enabled; OnCsgChanged();
    if (record) FinishSceneEdit("Toggle Roundness Modifier", before);
}

void Application::SetRoundnessAmount(int modifierNodeId, float value)
{
    if (!std::isfinite(value) || value < 0.0f) return;
    CsgNode* node = sceneManager.GetCsgTree().FindNodeById(modifierNodeId);
    if (!node || node->type != CsgNodeType::Primitive || node->modifierType != ModifierType::Roundness) return;
    const SdfObjectData* object = sceneManager.FindObjectById(node->objectId);
    if (!object || object->type != static_cast<int32_t>(SdfObjectType::Box)) return;
    const float maxRadius = std::max(0.0f, std::min({ object->size[0], object->size[1], object->size[2] }) - 0.00001f);
    value = std::min(value, maxRadius);
    if (node->modifierAmount == value) return;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->modifierAmount = value; OnCsgChanged();
    if (record) FinishSceneEdit("Change Roundness Amount", before);
}

void Application::RemoveRoundnessModifier(int modifierNodeId)
{
    CsgNode* node = sceneManager.GetCsgTree().FindNodeById(modifierNodeId);
    if (!node || node->type != CsgNodeType::Primitive || node->modifierType != ModifierType::Roundness) return;
    const bool record = ShouldRecordSceneEdit(); SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();
    node->modifierType = ModifierType::None;
    node->modifierAmount = 0.0f;
    node->modifierEnabled = false;
    OnCsgChanged();
    if (record) FinishSceneEdit("Remove Roundness Modifier", before);
}



const char* Application::TypeName(int32_t type) {
    switch (static_cast<SdfObjectType>(type)) {
    case SdfObjectType::Sphere:   return "Sphere";
    case SdfObjectType::Box:      return "Box";
    case SdfObjectType::Cylinder: return "Cylinder";
    case SdfObjectType::Torus:    return "Torus";
    case SdfObjectType::Link:       return "Link";
    case SdfObjectType::Cone:       return "Cone";
    case SdfObjectType::Capsule:    return "Capsule";
    case SdfObjectType::SolidAngle: return "SolidAngle";
    case SdfObjectType::Pyramid:    return "Pyramid";
    default:                      return "Object";
    }
}

// Minimal JSON string escaping for user-entered object names.
static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

std::string Application::GetSceneJson() const {
    const auto& objects = sceneManager.GetObjects();

    std::ostringstream out;
    out << "{\"selectedIndex\":" << selectionState.selectedObjectIndex
        << ",\"showGrid\":" << (showGrid ? "true" : "false")
        << ",\"objects\":[";

    for (size_t i = 0; i < objects.size(); ++i) {
        const SdfObjectData& o = objects[i];
        const EditorObject* eo = editorStore.Find(o.objectId);
        EditorObject fallback;  // defaults if a record is somehow missing
        const EditorObject& e = eo ? *eo : fallback;
        if (i > 0) out << ",";
        out << "{\"index\":" << i
            << ",\"id\":" << o.objectId
            << ",\"type\":" << o.type
            << ",\"typeName\":\"" << TypeName(o.type) << "\""
            << ",\"position\":[" << o.position[0] << "," << o.position[1] << "," << o.position[2] << "]"
            << ",\"size\":[" << o.size[0] << "," << o.size[1] << "," << o.size[2] << "]"
            << ",\"color\":[" << o.color[0] << "," << o.color[1] << "," << o.color[2] << "]"
            << ",\"name\":\"" << JsonEscape(e.name) << "\""
            << ",\"visible\":" << (e.visible ? "true" : "false")
            << ",\"locked\":" << (e.locked ? "true" : "false")
            << ",\"rotation\":[" << e.rotationEuler[0] << "," << e.rotationEuler[1] << "," << e.rotationEuler[2] << "]"
            << ",\"scale\":[" << e.scale[0] << "," << e.scale[1] << "," << e.scale[2] << "]"
            << ",\"params\":{\"radius\":" << e.params.radius
            << ",\"height\":" << e.params.height
            << ",\"thickness\":" << e.params.thickness
            << ",\"roundness\":" << e.params.roundness
            << ",\"length\":" << e.params.length
            << ",\"angle\":" << e.params.angle << "}"
            << "}";
    }
    out << "]";

    // Engine-owned selection (nodeIds). primaryNodeId is the active node (the
    // last-clicked one, used for the 3D highlight); -1 when nothing is selected.
    // The Subtract base is the FIRST selected node, not the primary (see
    // ApplyOperation).
    out << ",\"primaryNodeId\":" << selectionState.primaryNodeId
        << ",\"selectedNodeIds\":[";
    for (size_t i = 0; i < selectionState.selectedNodeIds.size(); ++i) {
        if (i) out << ",";
        out << selectionState.selectedNodeIds[i];
    }
    out << "]";

    // CSG hierarchy. `root` and every `children` entry are permanent nodeIds
    // (NOT vector indices) — the front-end keys the outliner entirely off nodeId.
    const CsgTree& tree = sceneManager.GetCsgTree();
    const auto& nodes = tree.GetNodes();
    int32_t rootIdx = tree.GetRoot();
    int32_t rootNodeId = (rootIdx >= 0 && rootIdx < static_cast<int32_t>(nodes.size()))
                       ? nodes[rootIdx].nodeId : -1;

    out << ",\"tree\":{\"root\":" << rootNodeId << ",\"nodes\":[";
    bool firstNode = true;
    for (const CsgNode& n : nodes) {
        if (!n.alive) continue;
        if (!firstNode) out << ",";
        firstNode = false;
        const std::string* nodeName = editorStore.FindNodeName(n.nodeId);
        out << "{\"nodeId\":" << n.nodeId
            << ",\"type\":" << static_cast<int>(n.type)
            << ",\"objectId\":" << (n.type == CsgNodeType::Primitive ? n.objectId : -1)
            << ",\"name\":\"" << JsonEscape(nodeName ? *nodeName : std::string()) << "\""
            << ",\"blendMode\":" << static_cast<int>(n.blendMode)
            << ",\"blendRadius\":" << n.blendRadius
            << ",\"modifierType\":" << static_cast<int>(n.modifierType)
            << ",\"modifierId\":" << ((n.modifierType != ModifierType::None) ? n.nodeId : -1)
            << ",\"modifierAmount\":" << n.modifierAmount
            << ",\"modifierEnabled\":" << (n.modifierEnabled ? "true" : "false")
            << ",\"children\":[";
        bool firstChild = true;
        for (int32_t c = n.firstChild; c != -1; c = nodes[c].nextSibling) {
            if (!nodes[c].alive) continue;
            if (!firstChild) out << ",";
            firstChild = false;
            out << nodes[c].nodeId;
        }
        out << "]}";
    }
    out << "]}";

    // Render settings mirror (so the front-end panel reflects engine state).
    const Settings& rs = appSettings;
    out << ",\"render\":{"
        << "\"lightDirX\":" << rs.lightDirection[0]
        << ",\"lightDirY\":" << rs.lightDirection[1]
        << ",\"lightDirZ\":" << rs.lightDirection[2]
        << ",\"lightColR\":" << rs.lightColor[0]
        << ",\"lightColG\":" << rs.lightColor[1]
        << ",\"lightColB\":" << rs.lightColor[2]
        << ",\"lightIntensity\":" << rs.lightIntensity
        << ",\"ambientStrength\":" << rs.ambientStrength
        << ",\"shadowSharpness\":" << rs.shadowSharpness
        << ",\"specularGloss\":" << rs.specularGloss
        << ",\"specularStrength\":" << rs.specularStrength
        << ",\"aoStrength\":" << rs.aoStrength
        << "}";

    // The two quality budgets, mirrored separately so the front-end's Render and
    // Export tabs each reflect their own engine state.
    const auto writeQuality = [&out](const char* key, const QualitySettings& q) {
        out << ",\"" << key << "\":{"
            << "\"maxSteps\":" << q.maxSteps
            << ",\"hitEpsilon\":" << q.hitEpsilon
            << ",\"maxDistance\":" << q.maxDistance
            << ",\"shadowSteps\":" << q.shadowSteps
            << ",\"shadowDistance\":" << q.shadowDistance
            << ",\"aoSamples\":" << q.aoSamples
            << ",\"aaSamples\":" << q.aaSamples
            << ",\"taaEnabled\":" << (q.taaEnabled ? "true" : "false")
            << ",\"shadowsEnabled\":" << (q.shadowsEnabled ? "true" : "false")
            << ",\"aoEnabled\":" << (q.aoEnabled ? "true" : "false")
            << "}";
    };
    writeQuality("viewportQuality", rs.viewportQuality);
    writeQuality("exportQuality", rs.exportQuality);

    out << "}";
    return out.str();
}

void Application::NewScene() {
    sceneSerializer.NewScene();
    RequestRedraw();
    editorStore.Clear();
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    m_UndoStack.Clear();
    OnSceneChanged();
}

void Application::OpenScene() {
    sceneSerializer.OpenSceneFromSDFO();    
    RequestRedraw();
    RebuildEditorRecords();
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    m_UndoStack.Clear();
    OnSceneChanged();
}

void Application::SaveScene() { sceneSerializer.SaveSceneToSDFO(); }

std::string Application::SerializeProject() {
    std::ostringstream out(std::ios::binary);
    ExtensionLoader::WriteSDFP(out, sceneManager.GetObjects(), sceneManager.GetCsgTree());
    // Editor attributes (names / visibility / rotation / scale / params) follow the
    // CSG-tree section as their own block; see EditorObjectStore::WriteToStream.
    editorStore.WriteToStream(out);
    return out.str();
}

bool Application::DeserializeProject(const std::string& bytes) {
    std::istringstream in(bytes, std::ios::binary);
    bool ok = ExtensionLoader::ReadSDFP(in, sceneManager);

    // Whether or not the header parsed, resync everything derived from the scene so
    // we never leave the GPU / editor / selection pointing at stale data.
    RequestRedraw();
    sceneSerializer.RefreshSceneBuffer();
    RebuildEditorRecords();
    // Overlay the saved editor attributes on top of the freshly rebuilt defaults.
    // Reads the section ReadSDFP left the stream positioned at; EOF-tolerant, so a
    // legacy .sdfp without the block just keeps the defaults.
    if (ok) editorStore.ReadFromStream(in);
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    m_UndoStack.Clear();
    OnSceneChanged();
    return ok;
}

void Application::CameraOrbit(float dx, float dy) {
    RequestRedraw();
    viewportInput.orbiting = true;
    viewportInput.mouseDelta.x += dx;
    viewportInput.mouseDelta.y += dy;
}

void Application::CameraPan(float dx, float dy) {
    RequestRedraw();
    viewportInput.panning = true;
    viewportInput.mouseDelta.x += dx;
    viewportInput.mouseDelta.y += dy;
}

void Application::CameraZoom(float delta) {
    RequestRedraw();
    viewportInput.scrollDelta += delta;
}

// Navigation-gizmo axis view. The camera eases into the requested direction over
// CameraTransition::DEFAULT_DURATION_S instead of teleporting, so the user can
// see how the scene rotated to get there. The transition is advanced in
// UpdateCamera and kept fed with frames from MainLoop.
void Application::CameraSnap(float x, float y, float z) {
    RequestRedraw();
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    if (!Camera::OrientationForDirection(glm::vec3(x, y, z), targetYaw, targetPitch)) return;
    // Ease from wherever the camera is *now* (possibly mid-animation from a
    // previous click). A move too small to see is applied directly rather than
    // animated, so a repeat click on the current view costs nothing.
    if (!cameraTransition.Start(camera.GetYaw(), camera.GetPitch(), targetYaw, targetPitch))
        camera.SetOrientation(targetYaw, targetPitch);
}

namespace {
bool ComputeFrameBounds(const std::vector<SdfObjectData>& objects,
                        const EditorObjectStore& store, bool selectedOnly,
                        glm::vec3& center, float& radius) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const SdfObjectData& object : objects) {
        const EditorObject* editorObject = store.Find(object.objectId);
        if ((selectedOnly && object.selected == 0) || (!selectedOnly && editorObject && !editorObject->visible)) continue;
        const glm::vec3 position(object.position[0], object.position[1], object.position[2]);
        const glm::vec3 extent(glm::abs(object.size[0]), glm::abs(object.size[1]), glm::abs(object.size[2]));
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
            !std::isfinite(extent.x) || !std::isfinite(extent.y) || !std::isfinite(extent.z)) continue;
        lo = glm::min(lo, position - extent);
        hi = glm::max(hi, position + extent);
        found = true;
    }
    if (!found) return false;
    center = (lo + hi) * 0.5f;
    radius = glm::length(hi - lo) * 0.5f;
    return std::isfinite(radius);
}
}

bool Application::FrameSelected() {
    RequestRedraw();
    glm::vec3 center;
    float radius = 0.0f;
    if (!ComputeFrameBounds(sceneManager.GetObjects(), editorStore, true, center, radius)) return false;
    camera.FrameBounds(center, glm::max(radius, 0.1f));
    return true;
}

bool Application::FrameAll() {
    RequestRedraw();
    glm::vec3 center;
    float radius = 0.0f;
    if (!ComputeFrameBounds(sceneManager.GetObjects(), editorStore, false, center, radius)) return false;
    camera.FrameBounds(center, glm::max(radius, 0.1f));
    return true;
}

// ---------------------------------------------------------------------------
// Translate gizmo
// ---------------------------------------------------------------------------
// The interaction state machine lives in GizmoController; these methods only
// gather the camera/selection inputs and forward to it. The gizmo is drawn by
// the SDF fragment shader (see UpdateGizmoGlobals + main.wgsl), not as a
// front-end overlay, so it never trails the scene.

// A stored (x,y,z,w) rotation array as a unit quaternion. Degenerate (zero)
// quaternions — legacy/uninitialized data — fall back to identity, not NaN.
static glm::quat QuatFromArray(const std::array<float, 4>& r) {
    const float len2 = r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + r[3]*r[3];
    if (len2 < 1e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return glm::normalize(glm::quat(r[3], r[0], r[1], r[2]));  // ctor is (w,x,y,z)
}

// Resolve the primary selected node into a gizmo target. A primitive transforms
// about its own centre/axes (unchanged behaviour); a group/operation node
// transforms every descendant primitive together about their centroid ("median
// point"), world-aligned. Returns valid=false when nothing usable is selected.
Application::GizmoTarget Application::ComputeGizmoTarget() const {
    GizmoTarget t;
    if (selectionState.primaryNodeId == -1) return t;

    const CsgTree& tree = sceneManager.GetCsgTree();
    int32_t idx = tree.IndexForNodeId(selectionState.primaryNodeId);
    if (idx == -1) return t;
    const CsgNode& n = tree.GetNodes()[idx];

    if (n.type == CsgNodeType::Primitive) {
        const SdfObjectData* o = sceneManager.FindObjectById(n.objectId);
        if (!o) return t;
        t.valid    = true;
        t.isGroup  = false;
        t.origin   = glm::vec3(o->position[0], o->position[1], o->position[2]);
        t.rotation = QuatFromArray(o->rotation);
        t.objectIds.push_back(n.objectId);
        return t;
    }

    // Group node: pivot on the centroid of its descendant primitives' positions.
    std::vector<int32_t> ids;
    sceneManager.CollectSubtreeObjectIds(idx, ids);
    glm::vec3 sum(0.0f);
    int count = 0;
    for (int32_t oid : ids) {
        const SdfObjectData* o = sceneManager.FindObjectById(oid);
        if (!o) continue;
        sum += glm::vec3(o->position[0], o->position[1], o->position[2]);
        ++count;
        t.objectIds.push_back(oid);
    }
    if (count == 0) return t;

    t.valid    = true;
    t.isGroup  = true;
    t.origin   = sum / static_cast<float>(count);
    t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // groups are world-aligned
    return t;
}

// Current camera view/proj for gizmo hit-testing (matches the render frame).
static inline void GizmoViewProj(Camera& camera, int renderWidth, int renderHeight,
                                 glm::mat4& v, glm::mat4& p) {
    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight) : 1.0f;
    v = camera.GetViewMatrix();
    p = camera.GetProjectionMatrix(aspect);
}

int Application::GizmoHover(float mouseX, float mouseY) {
    RequestRedraw();
    GizmoTarget target = ComputeGizmoTarget();
    if (!target.valid) return 0;

    glm::mat4 v, p;
    GizmoViewProj(camera, renderWidth, renderHeight, v, p);
    return gizmoController.Hover(glm::vec2(mouseX, mouseY), target.origin, target.rotation, v, p,
        static_cast<float>(renderWidth), static_cast<float>(renderHeight));
}

void Application::GizmoBeginDrag(float mouseX, float mouseY) {
    RequestRedraw();
    GizmoTarget target = ComputeGizmoTarget();
    if (!target.valid) return;

    glm::mat4 v, p;
    GizmoViewProj(camera, renderWidth, renderHeight, v, p);

    gizmoController.BeginDrag(
        glm::vec2(mouseX, mouseY),
        target.origin,
        target.rotation,
        v,
        p,
        static_cast<float>(renderWidth),
        static_cast<float>(renderHeight)
    );

    // BeginDrag refuses (e.g. no axis grabbed, or an unresolvable rotate spoke),
    // so only arm the transaction/snapshot once a drag actually latched.
    if (!gizmoController.IsDragging()) return;

    // Snapshot the pivot and every affected object's start pose. DragTranslate/
    // DragRotate report absolute-from-start values, so each frame recomputes from
    // these — no accumulated drift, and a clean base for the group math.
    m_GizmoDrag = GizmoDragState{};
    m_GizmoDrag.active   = true;
    m_GizmoDrag.isGroup  = target.isGroup;
    m_GizmoDrag.pivot    = target.origin;
    m_GizmoDrag.objectIds = target.objectIds;
    for (int32_t oid : target.objectIds) {
        const SdfObjectData* o = sceneManager.FindObjectById(oid);
        m_GizmoDrag.startPos.push_back(o ? glm::vec3(o->position[0], o->position[1], o->position[2])
                                         : glm::vec3(0.0f));
        m_GizmoDrag.startRot.push_back(o ? QuatFromArray(o->rotation)
                                         : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    // Undo covers every affected primitive (one for a primitive, all descendants
    // for a group).
    m_TransformTransaction.Begin(sceneManager, target.objectIds);
}

void Application::GizmoDrag(float mouseX, float mouseY) {
    RequestRedraw();
    if (!m_GizmoDrag.active) return;

    glm::mat4 v, p;
    GizmoViewProj(camera, renderWidth, renderHeight, v, p);
    const float w = static_cast<float>(renderWidth);
    const float h = static_cast<float>(renderHeight);

    if (gizmoController.Mode() == GizmoMode::Rotate) {
        glm::quat outRotation;
        if (!gizmoController.DragRotate(glm::vec2(mouseX, mouseY), v, p, w, h, outRotation))
            return;

        if (m_GizmoDrag.isGroup) {
            // The group's start rotation was identity, so outRotation IS the delta.
            // Turn each descendant about the pivot and reorient it by the same delta.
            const glm::quat delta = outRotation;
            for (size_t i = 0; i < m_GizmoDrag.objectIds.size(); ++i) {
                SdfObjectData* o = sceneManager.FindObjectById(m_GizmoDrag.objectIds[i]);
                if (!o) continue;
                glm::vec3 newPos = m_GizmoDrag.pivot + delta * (m_GizmoDrag.startPos[i] - m_GizmoDrag.pivot);
                glm::quat newRot = glm::normalize(delta * m_GizmoDrag.startRot[i]);
                o->position = { newPos.x, newPos.y, newPos.z, 0.0f };
                o->rotation = { newRot.x, newRot.y, newRot.z, newRot.w };
                if (EditorObject* eo = editorStore.Find(o->objectId))
                    eo->rotationEuler = QuatToEulerDegrees(o->rotation);
            }
        } else {
            // Single primitive: rotate in place about its own centre (position fixed).
            SdfObjectData* o = sceneManager.FindObjectById(m_GizmoDrag.objectIds[0]);
            if (o) {
                o->rotation = { outRotation.x, outRotation.y, outRotation.z, outRotation.w };
                if (EditorObject* eo = editorStore.Find(o->objectId))
                    eo->rotationEuler = QuatToEulerDegrees(o->rotation);
            }
        }
        OnSceneChanged();
        return;
    }

    glm::vec3 outOrigin;
    if (!gizmoController.DragTranslate(glm::vec2(mouseX, mouseY), v, p, w, h, outOrigin))
        return;

    // Shift every affected object by the same pivot delta (for a lone primitive
    // this reduces to setting its position to outOrigin).
    const glm::vec3 delta = outOrigin - m_GizmoDrag.pivot;
    for (size_t i = 0; i < m_GizmoDrag.objectIds.size(); ++i) {
        SdfObjectData* o = sceneManager.FindObjectById(m_GizmoDrag.objectIds[i]);
        if (!o) continue;
        glm::vec3 newPos = m_GizmoDrag.startPos[i] + delta;
        o->position = { newPos.x, newPos.y, newPos.z, 0.0f };
    }
    OnSceneChanged();
}

void Application::GizmoEndDrag() {
    RequestRedraw();
    gizmoController.EndDrag();
    m_TransformTransaction.Commit(sceneManager, m_UndoStack);
    m_GizmoDrag = GizmoDragState{};
    OnSceneChanged();
}

void Application::GizmoCancelDrag() {
    RequestRedraw();
    gizmoController.EndDrag();
    m_TransformTransaction.Cancel(sceneManager);
    m_GizmoDrag = GizmoDragState{};
    OnSceneChanged();
}

// Fill the gizmo* fields of `globals` so the SDF shader can draw the gizmo in
// the same frame as the scene. Called every frame from MainLoop with the live
// camera matrices.
void Application::UpdateGizmoGlobals(const glm::mat4& v, const glm::mat4& p) {
    // Recompute the target every frame: during a group translate the centroid
    // rides along with the objects (the gizmo follows the cursor); during a rotate
    // it is preserved (rotation about the centroid), so the gizmo stays put.
    GizmoTarget target = ComputeGizmoTarget();
    bool visible = gizmoEnabled && target.valid;
    glm::vec3 origin   = target.origin;
    glm::quat rotation = target.rotation;

    globals.gizmoVisible = visible ? 1 : 0;
    globals.gizmoHovered = static_cast<int32_t>(gizmoController.HoveredAxis());
    globals.gizmoActive  = static_cast<int32_t>(gizmoController.ActiveAxis());
    globals.gizmoMode    = static_cast<int32_t>(gizmoController.Mode());
    // Same scale the axis endpoints were sized with, so the shader's line/dot
    // thickness tracks the handle length and the gizmo stays a constant size.
    globals.gizmoPixelScale = gizmoController.PixelScale();
    if (!visible)
        return;

    // The endpoints carry the space (world vs local) into the shader implicitly:
    // AxisEndWorld already turns each axis by the object's rotation in Local mode.
    float h = static_cast<float>(renderHeight);
    glm::vec3 ex = gizmoController.AxisEndWorld(GizmoAxis::X, origin, rotation, v, p, h);
    glm::vec3 ey = gizmoController.AxisEndWorld(GizmoAxis::Y, origin, rotation, v, p, h);
    glm::vec3 ez = gizmoController.AxisEndWorld(GizmoAxis::Z, origin, rotation, v, p, h);

    globals.gizmoOrigin[0] = origin.x; globals.gizmoOrigin[1] = origin.y; globals.gizmoOrigin[2] = origin.z; globals.gizmoOrigin[3] = 0.0f;
    globals.gizmoAxisX[0] = ex.x; globals.gizmoAxisX[1] = ex.y; globals.gizmoAxisX[2] = ex.z; globals.gizmoAxisX[3] = 0.0f;
    globals.gizmoAxisY[0] = ey.x; globals.gizmoAxisY[1] = ey.y; globals.gizmoAxisY[2] = ey.z; globals.gizmoAxisY[3] = 0.0f;
    globals.gizmoAxisZ[0] = ez.x; globals.gizmoAxisZ[1] = ez.y; globals.gizmoAxisZ[2] = ez.z; globals.gizmoAxisZ[3] = 0.0f;
}

// Mirror the user's render settings (appSettings) into the GPU uniform. Cheap;
// runs every frame from MainLoop, same pattern as the camera/gizmo copies.
void Application::SyncRenderGlobals() {
    const Settings& s = appSettings;
    globals.lightDirection[0] = s.lightDirection[0];
    globals.lightDirection[1] = s.lightDirection[1];
    globals.lightDirection[2] = s.lightDirection[2];
    globals.lightColor[0] = s.lightColor[0];
    globals.lightColor[1] = s.lightColor[1];
    globals.lightColor[2] = s.lightColor[2];
    // Ambient is exposed to the user as a single grayscale strength.
    globals.ambientColor[0] = s.ambientStrength;
    globals.ambientColor[1] = s.ambientStrength;
    globals.ambientColor[2] = s.ambientStrength;
    globals.lightIntensity    = s.lightIntensity;
    globals.specularShininess = s.specularGloss;
    globals.specularStrength  = s.specularStrength;
    globals.shadowSoftness    = s.shadowSharpness;
    globals.aoStrength        = s.aoStrength;
    // The live viewport always renders at the viewport budget; the export builds
    // its own globals from exportQuality in StartFinalRender.
    ApplyQuality(s.viewportQuality, globals);
}

// Copy one quality budget into a globals block (the only place QualitySettings
// maps onto the GPU uniform, so viewport and export stay consistent).
void Application::ApplyQuality(const QualitySettings& q, GlobalsData& g) {
    g.maxSteps          = q.maxSteps;
    g.hitEpsilon        = q.hitEpsilon;
    g.maxDistance       = q.maxDistance;
    g.shadowMaxSteps    = q.shadowSteps;
    g.shadowMaxDistance = q.shadowDistance;
    g.aoSamples         = q.aoSamples;
    g.aaSamples         = q.aaSamples;
    g.shadowsEnabled    = q.shadowsEnabled ? 1 : 0;
    g.aoEnabled         = q.aoEnabled ? 1 : 0;
    g.taaEnabled        = q.taaEnabled ? 1 : 0;
}

QualitySettings& Application::QualityFor(int context) {
    return static_cast<QualityContext>(context) == QualityContext::Export
        ? appSettings.exportQuality
        : appSettings.viewportQuality;
}

// Update one render setting by RenderSetting id (see Settings.h). Ints arrive as
// float and are rounded. Values are assumed pre-clamped by the UI, but the int
// counts are floored to sane minimums here as a safety net.
void Application::SetRenderSetting(int id, float value) {
    RequestRedraw();
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    switch (static_cast<RenderSetting>(id)) {
    case RenderSetting::LightDirX:        appSettings.lightDirection[0] = value; break;
    case RenderSetting::LightDirY:        appSettings.lightDirection[1] = value; break;
    case RenderSetting::LightDirZ:        appSettings.lightDirection[2] = value; break;
    case RenderSetting::LightColorR:      appSettings.lightColor[0] = value; break;
    case RenderSetting::LightColorG:      appSettings.lightColor[1] = value; break;
    case RenderSetting::LightColorB:      appSettings.lightColor[2] = value; break;
    case RenderSetting::LightIntensity:   appSettings.lightIntensity = value; break;
    case RenderSetting::AmbientStrength:  appSettings.ambientStrength = value; break;
    case RenderSetting::ShadowSharpness:  appSettings.shadowSharpness = value; break;
    case RenderSetting::SpecularGloss:    appSettings.specularGloss = value; break;
    case RenderSetting::SpecularStrength: appSettings.specularStrength = value; break;
    case RenderSetting::AoStrength:       appSettings.aoStrength = value; break;
    default: break;
    }

    if (record) FinishSceneEdit("Change Render Setting", before);
}

// Update one quality budget by (QualityContext, QualitySetting) — see
// QualitySettings.h. Same shape as SetRenderSetting, plus the context that picks
// which of the two budgets is written. Ints arrive as float and are rounded.
void Application::SetQualitySetting(int context, int id, float value) {
    RequestRedraw();
    const bool record = ShouldRecordSceneEdit();
    SceneEditSnapshot before;
    if (record) before = CaptureSceneEditSnapshot();

    QualitySettings& q = QualityFor(context);
    switch (static_cast<QualitySetting>(id)) {
    // Hard safety caps (well above the UI slider maxes, so they never interfere
    // with legitimate values) guard against a rogue binding call passing a value
    // that would hang the GPU or make the march never terminate a hit.
    case QualitySetting::MaxSteps:       q.maxSteps = std::clamp(static_cast<int>(value + 0.5f), 1, 2048); break;
    case QualitySetting::HitEpsilon:     q.hitEpsilon = std::clamp(value, 1e-6f, 1.0f); break;
    case QualitySetting::MaxDistance:    q.maxDistance = std::clamp(value, 0.1f, 100000.0f); break;
    case QualitySetting::ShadowSteps:    q.shadowSteps = std::clamp(static_cast<int>(value + 0.5f), 1, 1024); break;
    case QualitySetting::ShadowDistance: q.shadowDistance = std::clamp(value, 0.1f, 100000.0f); break;
    case QualitySetting::AoSamples:      q.aoSamples = std::clamp(static_cast<int>(value + 0.5f), 2, 128); break;
    // NxN supersampling grid dimension. Cost is O(N^2) rays/pixel, so the safety cap is low
    // (well above the UI max of 8) to keep a rogue call from stalling the GPU.
    case QualitySetting::AaSamples:      q.aaSamples = std::clamp(static_cast<int>(value + 0.5f), 1, 16); break;
    case QualitySetting::Taa:            q.taaEnabled = value != 0.0f; break;
    case QualitySetting::ShadowsEnabled: q.shadowsEnabled = value != 0.0f; break;
    case QualitySetting::AoEnabled:      q.aoEnabled = value != 0.0f; break;
    default: break;
    }

    if (record) FinishSceneEdit("Change Quality Setting", before);
}

void Application::UpdateJitterGlobal(uint32_t i){

    float offsets[4][2] = {{-0.25f, 0.25f}, {0.25f, 0.25f}, {-0.25f, -0.25f}, {0.25f, -0.25f}};
    globals.jitterOffset[0] = offsets[i][0];
    globals.jitterOffset[1] = offsets[i][1];

}
