#pragma once
#include <SDL.h>

namespace Platform
{
    // Owns the GLFW window handle and its creation/callbacks.
    class Window
    {
    public:
        void InitializeWindow(int width, int height, void* userPointer);
        SDL_Window* GetHandle() const { return window; }

    private:
        SDL_Window* window = nullptr;
    };
}
