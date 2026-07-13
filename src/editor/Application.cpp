#include "Application.h"
#include <glm/gtc/quaternion.hpp>
#include "utils/FileHelper.h"
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

    UpdateCamera();

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
// Undo / Redo
// ---------------------------------------------------------------------------

void Application::OnSceneChanged()
{
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
        FinishSceneEdit(m_SceneEditName.c_str(), m_SceneEditBefore);
        m_SceneEditName.clear();
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
        out << "{\"nodeId\":" << n.nodeId
            << ",\"type\":" << static_cast<int>(n.type)   // 0=prim 1=union 2=sub 3=intersect
            << ",\"objectId\":" << (n.type == CsgNodeType::Primitive ? n.objectId : -1)
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
        << ",\"maxSteps\":" << rs.maxSteps
        << ",\"hitEpsilon\":" << rs.hitEpsilon
        << ",\"maxDistance\":" << rs.maxDistance
        << ",\"shadowSteps\":" << rs.shadowSteps
        << ",\"shadowDistance\":" << rs.shadowDistance
        << ",\"aoSamples\":" << rs.aoSamples
        << "}";

    out << "}";
    return out.str();
}

void Application::NewScene() {
    sceneSerializer.NewScene();
    editorStore.Clear();
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    m_UndoStack.Clear();
    OnSceneChanged();
}

void Application::OpenScene() {
    sceneSerializer.OpenSceneFromSDFO();
    RebuildEditorRecords();
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    m_UndoStack.Clear();
    OnSceneChanged();
}

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
    GizmoTarget target = ComputeGizmoTarget();
    if (!target.valid) return 0;

    glm::mat4 v, p;
    GizmoViewProj(camera, renderWidth, renderHeight, v, p);
    return gizmoController.Hover(glm::vec2(mouseX, mouseY), target.origin, target.rotation, v, p,
        static_cast<float>(renderWidth), static_cast<float>(renderHeight));
}

void Application::GizmoBeginDrag(float mouseX, float mouseY) {
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
    gizmoController.EndDrag();
    m_TransformTransaction.Commit(sceneManager, m_UndoStack);
    m_GizmoDrag = GizmoDragState{};
    OnSceneChanged();
}

void Application::GizmoCancelDrag() {
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
    globals.maxSteps          = s.maxSteps;
    globals.hitEpsilon        = s.hitEpsilon;
    globals.maxDistance       = s.maxDistance;
    globals.shadowMaxSteps    = s.shadowSteps;
    globals.shadowMaxDistance = s.shadowDistance;
    globals.aoSamples         = s.aoSamples;
}

// Update one render setting by RenderSetting id (see Settings.h). Ints arrive as
// float and are rounded. Values are assumed pre-clamped by the UI, but the int
// counts are floored to sane minimums here as a safety net.
void Application::SetRenderSetting(int id, float value) {
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
    case RenderSetting::MaxSteps:         appSettings.maxSteps = std::max(1, static_cast<int>(value + 0.5f)); break;
    case RenderSetting::HitEpsilon:       appSettings.hitEpsilon = value; break;
    case RenderSetting::MaxDistance:      appSettings.maxDistance = value; break;
    case RenderSetting::ShadowSteps:      appSettings.shadowSteps = std::max(1, static_cast<int>(value + 0.5f)); break;
    case RenderSetting::ShadowDistance:   appSettings.shadowDistance = value; break;
    case RenderSetting::AoSamples:        appSettings.aoSamples = std::max(2, static_cast<int>(value + 0.5f)); break;
    default: break;
    }

    if (record) FinishSceneEdit("Change Render Setting", before);
}
