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

EMSCRIPTEN_KEEPALIVE
const char* sdf_get_scene_json() {
    static std::string buffer;
    buffer = g_App ? g_App->GetSceneJson() : "{\"selectedIndex\":-1,\"objects\":[]}";
    return buffer.c_str();
}

EMSCRIPTEN_KEEPALIVE
void sdf_new_scene()  { if (g_App) g_App->NewScene(); }

EMSCRIPTEN_KEEPALIVE
void sdf_open_scene() { if (g_App) g_App->OpenScene(); }

EMSCRIPTEN_KEEPALIVE
void sdf_save_scene() { if (g_App) g_App->SaveScene(); }

EMSCRIPTEN_KEEPALIVE
void sdf_set_show_grid(int show) { if (g_App) g_App->SetShowGrid(show != 0); }

EMSCRIPTEN_KEEPALIVE
void sdf_resize(int width, int height) { if (g_App) g_App->OnResize(width, height); }

EMSCRIPTEN_KEEPALIVE
void sdf_camera_orbit(float dx, float dy) { if (g_App) g_App->CameraOrbit(dx, dy); }

EMSCRIPTEN_KEEPALIVE
void sdf_camera_pan(float dx, float dy) { if (g_App) g_App->CameraPan(dx, dy); }

EMSCRIPTEN_KEEPALIVE
void sdf_camera_zoom(float delta) { if (g_App) g_App->CameraZoom(delta); }

} // extern "C"

#endif // __EMSCRIPTEN__
