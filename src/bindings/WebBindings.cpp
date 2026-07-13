// C bindings that expose the engine's control API to JavaScript (the React
// front-end). Compiled only for the Emscripten/WASM build.
//
// Each function operates on the single global Application instance created in
// main.cpp. String results (scene JSON) are returned as a pointer into a
// static buffer that stays valid until the next call — the JS side copies it
// out immediately via Module.UTF8ToString().

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <string>
#include "editor/Application.h"

// Defined in main.cpp; set once the Application has been constructed.
extern Application* g_App;

extern "C" {

    EMSCRIPTEN_KEEPALIVE
        int sdf_add_primitive(int type) {
        if (!g_App) return -1;
        return g_App->AddPrimitive(static_cast<SdfObjectType>(type));
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_remove_object(int index) {
        if (g_App) g_App->RemoveObject(index);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_select_object(int index) {
        if (g_App) g_App->SelectObject(index);
    }

    // Selection is engine-owned (see SelectionState). These just mutate it.
    // mode: 0 = replace selection with this node, 1 = toggle it in the set.
    EMSCRIPTEN_KEEPALIVE
        void sdf_select_node(int nodeId, int mode) {
        if (g_App) g_App->SelectNode(nodeId, mode);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_clear_selection() {
        if (g_App) g_App->ClearSelection();
    }

    // Viewport pick -> selection. pickIndex is the object vector index from the
    // pick buffer (-1 = empty space); mode 0 = replace, 1 = toggle. Selects the
    // whole top-level group containing the picked primitive (Blender-style).
    EMSCRIPTEN_KEEPALIVE
        void sdf_select_by_pick(int pickIndex, int mode) {
        if (g_App) g_App->SelectByPick(pickIndex, mode);
    }

    // Groups the current selection under a new operation node of the given
    // CsgNodeType (1=Union, 2=Subtract, 3=Intersection); the primary node is the
    // base for Subtract. Returns the new node's id, or -1 on failure.
    EMSCRIPTEN_KEEPALIVE
        int sdf_apply_operation(int opType) {
        if (!g_App) return -1;
        return g_App->ApplyOperation(opType);
    }

    EMSCRIPTEN_KEEPALIVE void sdf_set_operation_type(int nodeId, int opType) { if (g_App) g_App->SetOperationType(nodeId, opType); }
    EMSCRIPTEN_KEEPALIVE void sdf_set_blend_mode(int nodeId, int mode) { if (g_App) g_App->SetBlendMode(nodeId, mode); }
    EMSCRIPTEN_KEEPALIVE void sdf_set_blend_radius(int nodeId, float value) { if (g_App) g_App->SetBlendRadius(nodeId, value); }
    EMSCRIPTEN_KEEPALIVE int sdf_add_roundness_modifier(int targetNodeId) { return g_App ? g_App->AddRoundnessModifier(targetNodeId) : -1; }
    EMSCRIPTEN_KEEPALIVE void sdf_set_roundness_enabled(int nodeId, int enabled) { if (g_App) g_App->SetRoundnessEnabled(nodeId, enabled != 0); }
    EMSCRIPTEN_KEEPALIVE void sdf_set_roundness_amount(int nodeId, float value) { if (g_App) g_App->SetRoundnessAmount(nodeId, value); }
    EMSCRIPTEN_KEEPALIVE void sdf_remove_roundness_modifier(int nodeId) { if (g_App) g_App->RemoveRoundnessModifier(nodeId); }

    // Removes every currently-selected node (primitives + objects, op subtrees).
    EMSCRIPTEN_KEEPALIVE
        void sdf_delete_selection() {
        if (g_App) g_App->DeleteSelection();
    }

    EMSCRIPTEN_KEEPALIVE
        int sdf_pick_object(int x, int y) {
        if (!g_App) return -1;
        uint32_t id = g_App->PickObjectAt(x, y);
        if (id == 0xffffffffu) return -1;
        return static_cast<int>(id);
    }

    EMSCRIPTEN_KEEPALIVE
        int sdf_get_selected_index() {
        return g_App ? g_App->GetSelectedIndex() : -1;
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_position(int index, float x, float y, float z) {
        if (g_App) g_App->SetObjectPosition(index, x, y, z);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_size(int index, float x, float y, float z) {
        if (g_App) g_App->SetObjectSize(index, x, y, z);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_color(int index, float r, float g, float b) {
        if (g_App) g_App->SetObjectColor(index, r, g, b);
    }

    // --- Editor attributes (name / visibility / lock / rotation / scale / params).
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_name(int index, const char* name) {
        if (g_App) g_App->SetObjectName(index, name);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_visible(int index, int visible) {
        if (g_App) g_App->SetObjectVisible(index, visible != 0);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_locked(int index, int locked) {
        if (g_App) g_App->SetObjectLocked(index, locked != 0);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_rotation(int index, float x, float y, float z) {
        if (g_App) g_App->SetObjectRotation(index, x, y, z);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_scale(int index, float x, float y, float z) {
        if (g_App) g_App->SetObjectScale(index, x, y, z);
    }

    // paramId: 0=radius, 1=height, 2=thickness, 3=roundness (see PrimitiveParam).
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_object_param(int index, int paramId, float value) {
        if (g_App) g_App->SetObjectParam(index, paramId, value);
    }

    // ---------------------------------------------------------------------------
    // Undo / Redo
    // ---------------------------------------------------------------------------
    // These are intentionally thin bindings. Undo/redo logic stays in C++ engine
    // code: Application -> UndoStack -> SceneManager. The web layer only calls
    // these functions.

    EMSCRIPTEN_KEEPALIVE
        void sdf_undo() {
        if (g_App) g_App->Undo();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_redo() {
        if (g_App) g_App->Redo();
    }

    EMSCRIPTEN_KEEPALIVE
        int sdf_can_undo() {
        return (g_App && g_App->CanUndo()) ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE
        int sdf_can_redo() {
        return (g_App && g_App->CanRedo()) ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_clear_undo_stack() {
        if (g_App) g_App->ClearUndoStack();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_begin_scene_edit(const char* name) {
        if (g_App) g_App->BeginSceneEdit(name);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_commit_scene_edit() {
        if (g_App) g_App->CommitSceneEdit();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_cancel_scene_edit() {
        if (g_App) g_App->CancelSceneEdit();
    }

    EMSCRIPTEN_KEEPALIVE
        const char* sdf_get_scene_json() {
        static std::string buffer;
        buffer = g_App ? g_App->GetSceneJson() : "{\"selectedIndex\":-1,\"objects\":[]}";
        return buffer.c_str();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_new_scene() {
        if (g_App) g_App->NewScene();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_open_scene() {
        if (g_App) g_App->OpenScene();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_save_scene() {
        if (g_App) g_App->SaveScene();
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_set_show_grid(int show) {
        if (g_App) g_App->SetShowGrid(show != 0);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_resize(int width, int height) {
        if (g_App) g_App->OnResize(width, height);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_camera_orbit(float dx, float dy) {
        if (g_App) g_App->CameraOrbit(dx, dy);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_camera_pan(float dx, float dy) {
        if (g_App) g_App->CameraPan(dx, dy);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_camera_zoom(float delta) {
        if (g_App) g_App->CameraZoom(delta);
    }

    // Set one render setting by RenderSetting id (see Settings.h). Ints are passed
    // as float. Read the current values back from sdf_get_scene_json().render.
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_render_setting(int id, float value) {
        if (g_App) g_App->SetRenderSetting(id, value);
    }

    // --- Translate / rotate gizmo. Mouse coords are render/device pixels,
    // top-left origin.

    EMSCRIPTEN_KEEPALIVE
        int sdf_gizmo_hover(float mouseX, float mouseY) {
        return g_App ? g_App->GizmoHover(mouseX, mouseY) : 0;
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_gizmo_begin_drag(float mouseX, float mouseY) {
        if (g_App) g_App->GizmoBeginDrag(mouseX, mouseY);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_gizmo_drag(float mouseX, float mouseY) {
        if (g_App) g_App->GizmoDrag(mouseX, mouseY);
    }

    EMSCRIPTEN_KEEPALIVE
        void sdf_gizmo_end_drag() {
        if (g_App) g_App->GizmoEndDrag();
    }

    // Cancels the current gizmo transaction. Intended for Esc / right-click cancel.
    // This should restore the transform captured at GizmoBeginDrag and should NOT
    // push anything into the undo stack.
    EMSCRIPTEN_KEEPALIVE
        void sdf_gizmo_cancel_drag() {
        if (g_App) g_App->GizmoCancelDrag();
    }

    // Enable/disable drawing the gizmo (the front-end sets this from its active
    // tool). The gizmo itself is rendered by the SDF shader, so there is no
    // screen-space geometry to hand back — it stays in lockstep with the scene.
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_gizmo_enabled(int enabled) {
        if (g_App) g_App->SetGizmoEnabled(enabled != 0);
    }

    // mode: GizmoMode (0 = translate arrows, 1 = rotate rings). Mirrored in engine.ts.
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_gizmo_mode(int mode) {
        if (g_App) g_App->SetGizmoMode(mode);
    }

    // space: GizmoSpace (0 = world axes, 1 = the object's own axes).
    EMSCRIPTEN_KEEPALIVE
        void sdf_set_gizmo_space(int space) {
        if (g_App) g_App->SetGizmoSpace(space);
    }

} // extern "C"

#endif // __EMSCRIPTEN__
