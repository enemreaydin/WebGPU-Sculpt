#include "Application.h"
#include "utils/FileHelper.h"
#include "scene/SdfObjectFactory.h"
#include "scene/PrimitiveGeometry.h"
#include "interaction/GizmoMath.h"
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

    // Give the new object its editor record (default name + shape params derived
    // from the factory size).
    const SdfObjectData& created = sceneManager.GetObjects()[index];
    editorStore.Create(created.objectId, type,
                       { created.size[0], created.size[1], created.size[2] });

    int32_t newNodeId = sceneManager.NodeIdForObject(created.objectId);
    selectionState.Clear();
    if (newNodeId != -1) { selectionState.Add(newNodeId); selectionState.primaryNodeId = newNodeId; }
    UpdateHighlightIndex();

    sceneSerializer.RefreshSceneBuffer();
    return index;
}

void Application::RemoveObject(int index) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;

    editorStore.Remove(objects[index].objectId);
    sceneManager.RemoveObjectAt(index);

    if (objects.empty())
        selectionState.selectedObjectIndex = -1;
    else if (selectionState.selectedObjectIndex >= static_cast<int>(objects.size()))
        selectionState.selectedObjectIndex = static_cast<int>(objects.size()) - 1;

    sceneSerializer.RefreshSceneBuffer();
}

// Rebuild every object's editor record from the current scene — used after a
// load, where objects come straight off disk with no records yet.
void Application::RebuildEditorRecords() {
    editorStore.Clear();
    for (const SdfObjectData& o : sceneManager.GetObjects()) {
        editorStore.Create(o.objectId, static_cast<SdfObjectType>(o.type),
                           { o.size[0], o.size[1], o.size[2] });
    }
}

void Application::SelectObject(int index) {
    if (index < -1 || index >= static_cast<int>(sceneManager.GetObjects().size())) return;
    selectionState.selectedObjectIndex = index;
}
void Application::DeleteSelection() {
    // IndexForNodeId returns -1 for a node already killed as part of an earlier
    // subtree in this same loop, so overlapping selections are handled safely.
    for (int32_t nodeId : selectionState.selectedNodeIds) {
        int32_t idx = sceneManager.GetCsgTree().IndexForNodeId(nodeId);
        if (idx != -1) sceneManager.RemoveNodeSubtree(idx);
    }
    selectionState.Clear();
    selectionState.selectedObjectIndex = -1;
    sceneSerializer.RefreshSceneBuffer();
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

// --- Editor attribute setters -----------------------------------------------
// Each resolves the object's editor record via its permanent objectId; scale
// and shape-param edits then recompute the SDF `size` from the params.

void Application::SetObjectName(int index, const char* name) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->name = name ? name : "";
}

void Application::SetObjectVisible(int index, bool visible) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->visible = visible;
}

void Application::SetObjectLocked(int index, bool locked) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->locked = locked;
}

void Application::SetObjectRotation(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    if (EditorObject* eo = editorStore.Find(objects[index].objectId))
        eo->rotationEuler = { x, y, z };  // stored; not yet applied to the SDF
}

void Application::SetObjectScale(int index, float x, float y, float z) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    EditorObject* eo = editorStore.Find(objects[index].objectId);
    if (!eo) return;
    eo->scale = { x, y, z };
    auto size = PrimitiveGeometry::ComputeSize(
        static_cast<SdfObjectType>(objects[index].type), eo->params, eo->scale);
    objects[index].size = { size[0], size[1], size[2], 0.0f };
    sceneSerializer.RefreshSceneBuffer();
}

void Application::SetObjectParam(int index, int paramId, float value) {
    auto& objects = sceneManager.GetObjects();
    if (index < 0 || index >= static_cast<int>(objects.size())) return;
    EditorObject* eo = editorStore.Find(objects[index].objectId);
    if (!eo) return;
    switch (static_cast<PrimitiveParam>(paramId)) {
    case PrimitiveParam::Radius:    eo->params.radius = value; break;
    case PrimitiveParam::Height:    eo->params.height = value; break;
    case PrimitiveParam::Thickness: eo->params.thickness = value; break;
    case PrimitiveParam::Roundness: eo->params.roundness = value; break;
    default: return;
    }
    auto size = PrimitiveGeometry::ComputeSize(
        static_cast<SdfObjectType>(objects[index].type), eo->params, eo->scale);
    objects[index].size = { size[0], size[1], size[2], 0.0f };
    sceneSerializer.RefreshSceneBuffer();
}

void Application::ClearSelection(){
    selectionState.selectedObjectIndex = -1;
    selectionState.Clear();
}

// Primary primitive -> its object vector index (for the shader highlight).
void Application::UpdateHighlightIndex() {
    selectionState.selectedObjectIndex = -1;
    if (selectionState.primaryNodeId == -1) return;

    const CsgTree& tree = sceneManager.GetCsgTree();
    int32_t idx = tree.IndexForNodeId(selectionState.primaryNodeId);
    if (idx == -1) return;
    const CsgNode& n = tree.GetNodes()[idx];
    if (n.type != CsgNodeType::Primitive) return;   // groups don't highlight in 3D

    const auto& objs = sceneManager.GetObjects();
    for (int32_t i = 0; i < static_cast<int32_t>(objs.size()); ++i)
        if (objs[i].objectId == n.objectId) { selectionState.selectedObjectIndex = i; return; }
}


void Application::SelectNode(int nodeId, int mode) {
    if (sceneManager.GetCsgTree().IndexForNodeId(nodeId) == -1) return;  // unknown/dead

    if (mode == 1) {                    // toggle
        if (selectionState.Contains(nodeId)) {
            selectionState.Remove(nodeId);
            if (selectionState.primaryNodeId == nodeId)
                selectionState.primaryNodeId =
                    selectionState.selectedNodeIds.empty() ? -1 : selectionState.selectedNodeIds.back();
        } else {
            selectionState.Add(nodeId);
            selectionState.primaryNodeId = nodeId;
        }
    } else {                            // replace
        selectionState.Clear();
        selectionState.Add(nodeId);
        selectionState.primaryNodeId = nodeId;
    }
    UpdateHighlightIndex();
}
int Application::ApplyOperation(int opType) {
    CsgNodeType t;
    switch (opType) {
        case 1: t = CsgNodeType::Union;        break;
        case 2: t = CsgNodeType::Subtract;     break;
        case 3: t = CsgNodeType::Intersection; break;
        default: return -1;
    }

    // Build indices, primary first (= Subtract base), then the rest in order.
    const CsgTree& tree = sceneManager.GetCsgTree();
    std::vector<int32_t> indices;
    if (selectionState.primaryNodeId != -1) {
        int32_t p = tree.IndexForNodeId(selectionState.primaryNodeId);
        if (p != -1) indices.push_back(p);
    }
    for (int32_t nid : selectionState.selectedNodeIds) {
        if (nid == selectionState.primaryNodeId) continue;
        int32_t idx = tree.IndexForNodeId(nid);
        if (idx != -1) indices.push_back(idx);
    }
    if (indices.size() < 2) return -1;

    int32_t newNodeId = sceneManager.GroupNodes(indices, t);
    if (newNodeId != -1) {
        selectionState.Clear();
        selectionState.Add(newNodeId);
        selectionState.primaryNodeId = newNodeId;
        UpdateHighlightIndex();
    }
    sceneSerializer.RefreshSceneBuffer();   // recompile RPN + upload
    return newNodeId;
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
            << ",\"roundness\":" << e.params.roundness << "}"
            << "}";
    }
    out << "]";

    // Engine-owned selection (nodeIds). primaryNodeId is the active node / the
    // base for Subtract; -1 when nothing is selected.
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

    out << "}";
    return out.str();
}

void Application::NewScene()  { sceneSerializer.NewScene();  editorStore.Clear(); selectionState.selectedObjectIndex = -1; }
void Application::OpenScene() { sceneSerializer.OpenSceneFromSDFO(); RebuildEditorRecords(); selectionState.selectedObjectIndex = -1; }
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

// Screen-length of the gizmo axes (in render pixels) and the pick tolerance.
namespace {
    constexpr float GIZMO_SCREEN_LENGTH_PX = 96.0f;
    constexpr float GIZMO_HIT_THRESHOLD_PX = 10.0f;
}

bool Application::GetSelectedObjectPosition(glm::vec3& outPosition) const {
    const auto& objects = sceneManager.GetObjects();
    int index = selectionState.selectedObjectIndex;
    if (index < 0 || index >= static_cast<int>(objects.size()))
        return false;
    const auto& p = objects[index].position;
    outPosition = glm::vec3(p[0], p[1], p[2]);
    return true;
}

int Application::GizmoHover(float mouseX, float mouseY) {
    if (gizmoState.isDragging)
        return static_cast<int>(gizmoState.activeAxis);

    glm::vec3 selectedPos;
    if (!GetSelectedObjectPosition(selectedPos)) {
        gizmoState.hoveredAxis = GizmoAxis::None;
        return 0;
    }

    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight) : 1.0f;
    glm::mat4 v = camera.GetViewMatrix();
    glm::mat4 p = camera.GetProjectionMatrix(aspect);

    GizmoAxis hovered = GizmoMath::HitTestTranslateAxes(
        glm::vec2(mouseX, mouseY), selectedPos, v, p,
        static_cast<float>(renderWidth), static_cast<float>(renderHeight),
        GIZMO_SCREEN_LENGTH_PX, GIZMO_HIT_THRESHOLD_PX);

    gizmoState.hoveredAxis = hovered;
    return static_cast<int>(hovered);
}

void Application::GizmoBeginDrag(float mouseX, float mouseY) {
    glm::vec3 selectedPos;
    if (!GetSelectedObjectPosition(selectedPos)) {
        gizmoState.activeAxis = GizmoAxis::None;
        gizmoState.isDragging = false;
        return;
    }

    GizmoHover(mouseX, mouseY);
    if (gizmoState.hoveredAxis == GizmoAxis::None) {
        gizmoState.activeAxis = GizmoAxis::None;
        gizmoState.isDragging = false;
        return;
    }

    gizmoState.activeAxis = gizmoState.hoveredAxis;
    gizmoState.isDragging = true;
    gizmoState.dragStartMouse = glm::vec2(mouseX, mouseY);
    gizmoState.dragStartObjectPosition = selectedPos;
}

void Application::GizmoDrag(float mouseX, float mouseY) {
    if (!gizmoState.isDragging || gizmoState.activeAxis == GizmoAxis::None)
        return;

    auto& objects = sceneManager.GetObjects();
    int index = selectionState.selectedObjectIndex;
    if (index < 0 || index >= static_cast<int>(objects.size()))
        return;

    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight) : 1.0f;
    glm::mat4 v = camera.GetViewMatrix();
    glm::mat4 p = camera.GetProjectionMatrix(aspect);

    glm::vec3 newPosition;
    bool ok = GizmoMath::ComputeTranslateDragPosition(
        gizmoState.activeAxis, gizmoState.dragStartMouse, glm::vec2(mouseX, mouseY),
        gizmoState.dragStartObjectPosition, v, p,
        static_cast<float>(renderWidth), static_cast<float>(renderHeight),
        GIZMO_SCREEN_LENGTH_PX, newPosition);
    if (!ok)
        return;

    objects[index].position = { newPosition.x, newPosition.y, newPosition.z, 0.0f };
    sceneSerializer.RefreshSceneBuffer();
}

void Application::GizmoEndDrag() {
    gizmoState.isDragging = false;
    gizmoState.activeAxis = GizmoAxis::None;
    gizmoState.hoveredAxis = GizmoAxis::None;
}

std::string Application::GetGizmoJson() {
    std::ostringstream out;

    glm::vec3 selectedPos;
    if (!GetSelectedObjectPosition(selectedPos)) {
        return "{\"visible\":false}";
    }

    float aspect = (renderHeight > 0)
        ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight) : 1.0f;
    glm::mat4 v = camera.GetViewMatrix();
    glm::mat4 p = camera.GetProjectionMatrix(aspect);

    glm::vec2 origin;
    if (!GizmoMath::WorldToScreen(selectedPos, v, p,
            static_cast<float>(renderWidth), static_cast<float>(renderHeight), origin)) {
        return "{\"visible\":false}";
    }

    out << "{\"visible\":true"
        << ",\"hovered\":" << static_cast<int>(gizmoState.hoveredAxis)
        << ",\"active\":" << static_cast<int>(gizmoState.activeAxis)
        << ",\"origin\":[" << origin.x << "," << origin.y << "]"
        << ",\"axes\":[";

    const GizmoAxis axes[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    bool first = true;
    for (GizmoAxis axis : axes) {
        GizmoMath::AxisScreenSegment seg = GizmoMath::BuildAxisSegment(
            axis, selectedPos, v, p,
            static_cast<float>(renderWidth), static_cast<float>(renderHeight),
            GIZMO_SCREEN_LENGTH_PX);
        if (!first) out << ",";
        first = false;
        out << "{\"axis\":" << static_cast<int>(axis)
            << ",\"valid\":" << (seg.valid ? "true" : "false")
            << ",\"end\":[" << seg.screenEnd.x << "," << seg.screenEnd.y << "]}";
    }
    out << "]}";
    return out.str();
}
