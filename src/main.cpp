#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
// The webgpu.hpp wrapper implementation is emitted once in WebGPUImpl.cpp.
#include "editor/Application.h"

// The single Application instance. Exposed globally so the web bindings
// (src/bindings/WebBindings.cpp) can drive it from JavaScript.
Application* g_App = nullptr;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    Application* app = new Application();
    app->Initialize();
    g_App = app;
    *appstate = app;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    Application* app = static_cast<Application*>(appstate);
    app->MainLoop();
    return app->IsRunning() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    Application* app = static_cast<Application*>(appstate);

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        app->OnResize(event->window.data1, event->window.data2);
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    Application* app = static_cast<Application*>(appstate);
    g_App = nullptr;
    app->Terminate();
    delete app;
}
