#include "platform/Window.h"
#include "editor/Application.h"

namespace Platform
{
    void Window::InitializeWindow(int width, int height, void* userPointer) {
#ifdef __EMSCRIPTEN__
    // On the web build SDL's emscripten backend otherwise attaches keydown/keyup/
    // keypress listeners to the whole window and preventDefault()s them, which
    // swallows every keystroke before it reaches our HTML <input> fields (rename,
    // numeric params, color hex, search). "#none" tells SDL to register no keyboard
    // handlers at all — the React UI owns all keyboard input (see App.tsx _kd), so
    // the engine never needs SDL key events here. Must be set before window creation.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#none");
#endif

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
