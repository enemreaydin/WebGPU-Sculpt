#include "platform/Window.h"
#include "editor/Application.h"

namespace Platform
{
    void Window::InitializeWindow(int width, int height, void* userPointer) {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "SDF Editor",
        width, height,
        SDL_WINDOW_RESIZABLE
    );

    SDL_SetWindowMinimumSize(window, 1000, 600);

    SDL_SetPointerProperty(SDL_GetWindowProperties(window), "app", userPointer);
}

}
