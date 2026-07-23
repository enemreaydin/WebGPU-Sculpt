#pragma once
#include <webgpu/webgpu.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>

#include "platform/Window.h"
#include "platform/GraphicsContext.h"
#include "editor/Settings.h"
#include "scene/SceneManager.h"
#include "renderer/SdfRenderer.h"
#include "scene/data/SdfEnums.h"
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"
#include "camera/Camera.h"
#include "camera/CameraController.h"
#include "core/ViewportInput.h"
#include "interaction/SelectionState.h"
#include "interaction/GizmoState.h"
#include "interaction/GizmoController.h"
#include "serialization/SceneSerializer.h"
#include "editor/EditorObjectStore.h"
#include "editor/command/UndoStack.h"
#include "editor/TransformTransaction.h"
#include "editor/SceneEditSnapshot.h"

using namespace wgpu;

// The engine application. It owns the window, WebGPU context, scene and
// renderer, and drives the SDF render loop. All user interaction (menus,
// property panels, object list) now lives in the web/React front-end, which
// drives this class through the public control API below (see src/bindings).
class Application {
public:
    Application(int width = 1280, int height = 720)
        : width(width), height(height),
          renderWidth(width), renderHeight(height),
          sceneSerializer(sceneManager, renderer, graphics, projectRoot) {}

    // Lifecycle
    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

    // Called when the drawing surface changes size (native window resize or the
    // web canvas being resized from JS).
    void OnResize(int width, int height);

    // ---- Control API (called from the web bindings / front-end) ------------

    // Scene editing
    int  AddPrimitive(SdfObjectType type);   // returns the new object's index
    void RemoveObject(int index);
    void SelectObject(int index);
    uint32_t PickObjectAt(int x, int y);

    // Viewport pick -> selection. `pickIndex` is the object vector index the pick
    // buffer returned (-1 = empty space); `mode` is 0 = replace, 1 = toggle. Unlike
    // SelectNode, this selects the whole top-level group containing the picked
    // primitive (see SceneManager::TopLevelNodeIdForObject) — Blender-style.
    void SelectByPick(int pickIndex, int mode);
    int  GetSelectedIndex() const { return selectionState.selectedObjectIndex; }

    void SetObjectPosition(int index, float x, float y, float z);
    void SetObjectSize(int index, float x, float y, float z);
    void SetObjectColor(int index, float r, float g, float b);

    // Group (CSG subtree) batch edits, keyed by CSG nodeId. Fan out over every
    // primitive in the subtree; a group owns no color/size of its own. Uniform
    // factor only (no per-axis, which would distort the SDF). See Application.cpp.
    void SetGroupColor(int nodeId, float r, float g, float b);
    void ScaleGroup(int nodeId, float factor);

    // Editor attributes (owned by the engine via EditorObjectStore). Scale and
    // shape-param edits recompute the object's SDF `size`; the rest are stored.
    void SetObjectName(int index, const char* name);
    // Name a CSG operation/group node (groups have no objectId, so their names are
    // stored per-nodeId in EditorObjectStore and serialized with the project).
    void SetNodeName(int nodeId, const char* name);
    void SetObjectVisible(int index, bool visible);
    void SetObjectLocked(int index, bool locked);
    void SetObjectRotation(int index, float x, float y, float z);
    void SetObjectScale(int index, float x, float y, float z);
    void SetObjectParam(int index, int paramId, float value);
    void SelectNode(int nodeId, int mode);   // mode 0 = replace, 1 = toggle
    void ClearSelection();
    int  ApplyOperation(int opType);        
    void SetBlendMode(int nodeId, int mode);
    void SetBlendRadius(int nodeId, float value);
    int  AddRoundnessModifier(int targetNodeId);
    void SetRoundnessEnabled(int modifierNodeId, bool enabled);
    void SetRoundnessAmount(int modifierNodeId, float value);
    void RemoveRoundnessModifier(int modifierNodeId);
    void DeleteSelection();
    void BeginSceneEdit(const char* name);
    void CommitSceneEdit();
    void CancelSceneEdit();


    // Serialized snapshot of the scene as JSON (object list + selection), for
    // the front-end to render its panels from.
    std::string GetSceneJson() const;

    // File operations
    void NewScene();
    void OpenScene();
    void SaveScene();

    // Project (.sdfp) I/O for the web front-end. The browser owns the actual file
    // (via the File System Access API); the engine only produces/consumes the raw
    // project bytes. SerializeProject returns the current scene as a .sdfp byte
    // string; DeserializeProject loads one back (and refreshes the GPU + editor
    // records). Returns false on a malformed buffer.
    std::string SerializeProject();
    bool DeserializeProject(const std::string& bytes);

    // View
    void SetShowGrid(bool show) { showGrid = show; }
    bool GetShowGrid() const { return showGrid; }
    void SetSelectionVisuals(bool show) { globals.qualityPad1 = show ? 0 : 1; }

    // Camera interaction (deltas in pixels / wheel ticks, accumulated per frame)
    void CameraOrbit(float dx, float dy);
    void CameraPan(float dx, float dy);
    void CameraZoom(float delta);
    void CameraSnap(float x, float y, float z);
    bool FrameSelected();
    bool FrameAll();
    float CameraYaw() const { return camera.GetYaw(); }
    float CameraPitch() const { return camera.GetPitch(); }

    // Render settings. `id` is a RenderSetting (see Settings.h); ints are passed
    // as float and rounded. Writes appSettings; SyncRenderGlobals mirrors it into
    // the GPU uniform each frame.
    void SetRenderSetting(int id, float value);
    bool StartFinalRender(uint32_t width, uint32_t height);
    int FinalRenderState() const { return renderer.GetFinalRenderState(); }
    const std::vector<uint8_t>& FinalRenderPixels() const { return renderer.GetFinalPixels(); }
    uint32_t FinalRenderWidth() const { return renderer.GetFinalWidth(); }
    uint32_t FinalRenderHeight() const { return renderer.GetFinalHeight(); }
    void ReleaseFinalRender() { renderer.ReleaseFinalRender(); }

    // Gizmo. Mouse coords are in render (device) pixels, origin top-left.
    // GizmoHover returns the hit GizmoAxis (0 = none). GizmoDrag translates or
    // rotates the selection depending on the active mode.
    int  GizmoHover(float mouseX, float mouseY);
    void GizmoBeginDrag(float mouseX, float mouseY);
    void GizmoDrag(float mouseX, float mouseY);
    void GizmoEndDrag();
    bool IsGizmoDragging() const { return gizmoController.IsDragging(); }

    // Whether the gizmo should be drawn (the front-end sets this from its active
    // tool). The gizmo is rendered by the SDF shader, so it stays perfectly in
    // sync with the scene; there is no screen-space overlay JSON.
    void SetGizmoEnabled(bool enabled) { gizmoEnabled = enabled; }

    // `mode` is a GizmoMode (0 translate, 1 rotate), `space` a GizmoSpace
    // (0 world, 1 local). Both are ignored mid-drag; see GizmoController.
    void SetGizmoMode(int mode)   { gizmoController.SetMode(static_cast<GizmoMode>(mode)); }
    void SetGizmoSpace(int space) { gizmoController.SetSpace(static_cast<GizmoSpace>(space)); }

    // Web Render Resolution scale (render-buffer px per display px). Keeps the
    // gizmo a constant on-screen size when the backing store is up/down-scaled;
    // it is a viewport display preference, not scene state (see §10).
    void SetGizmoPixelScale(float scale) { gizmoController.SetPixelScale(scale); }

    void Undo();
    void Redo();

    bool CanUndo() const;
    bool CanRedo() const;
    void ClearUndoStack();

    UndoStack& GetUndoStack();
    const UndoStack& GetUndoStack() const;

    void GizmoCancelDrag();

private:
    bool running = true;
    int width;
    int height;
    int renderWidth;
    int renderHeight;
    std::filesystem::path projectRoot;

    // Owned subsystems
    Platform::Window window;
    Platform::GraphicsContext graphics;
    SceneManager sceneManager;
    SdfRenderer renderer;
    SceneSerializer sceneSerializer;
    Settings appSettings;
    bool showGrid = true;

    SelectionState selectionState;
    EditorObjectStore editorStore;
    GizmoController gizmoController;
    bool gizmoEnabled = false;   // set true by the front-end when the move tool is active

    // Scene / camera state
    GlobalsData globals{};
    Camera camera;
    CameraController cameraController;
    ViewportInput viewportInput;
    glm::mat4 view{};
    glm::mat4 proj{};

    // Initializers / helpers
    void UpdateSelectionHighlight();  // recompute selectedObjectIndex (primary primitive)
                                      // + per-object highlight flags for the whole
                                      // selection, then re-upload the scene buffer
    void FindProjectRoot();
    void InitializeRenderer();
    bool UpdateCamera();
    void UpdateGizmoGlobals(const glm::mat4& v, const glm::mat4& p);  // fill gizmo* in globals
    void SyncRenderGlobals();  // copy appSettings render values into globals each frame
    void RebuildEditorRecords();   // recreate editor records after a scene load
    TextureView GetNextSurfaceTextureView();

    // The gizmo target derived from the primary selected node: a single primitive
    // (transform about its own centre/axes) or a group/operation node (transform
    // every descendant primitive together about their centroid, world-aligned).
    // One route drives hover, drag, and the shader-drawn gizmo for both cases.
    struct GizmoTarget {
        bool valid = false;
        bool isGroup = false;
        glm::vec3 origin{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        std::vector<int32_t> objectIds;   // affected primitives (1 for a primitive)
    };
    GizmoTarget ComputeGizmoTarget() const;

    static const char* TypeName(int32_t type);

    UndoStack m_UndoStack;

    void OnSceneChanged();
    void OnCsgChanged();

    TransformTransaction m_TransformTransaction;

    // Start-of-drag snapshot for the gizmo. The controller reports translate/rotate
    // as absolute-from-start, so we capture each affected object's starting pose and
    // the pivot once at BeginDrag and recompute from them every frame.
    struct GizmoDragState {
        bool active = false;
        bool isGroup = false;
        glm::vec3 pivot{ 0.0f };
        std::vector<int32_t> objectIds;
        std::vector<glm::vec3> startPos;
        std::vector<glm::quat> startRot;
    };
    GizmoDragState m_GizmoDrag;

    int m_SceneEditDepth = 0;
    std::string m_SceneEditName;
    SceneEditSnapshot m_SceneEditBefore{};
    bool m_SceneEditChanged = false;

    uint32_t m_JitterIndex = 0;
    void UpdateJitterGlobal(uint32_t jitterIndex);

    SceneEditSnapshot CaptureSceneEditSnapshot() const;
    void FinishSceneEdit(const char* name, const SceneEditSnapshot& before);
    bool ShouldRecordSceneEdit() const;
};
