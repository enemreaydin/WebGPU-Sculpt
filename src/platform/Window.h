#pragma once
#include <GLFW/glfw3.h>

namespace Platform
{
    // Owns the GLFW window handle and its creation/callbacks.
    class Window
    {
    public:
        void InitializeWindow(int width, int height, void* userPointer);
        GLFWwindow* GetHandle() const { return window; }

    private:
        GLFWwindow* window = nullptr;
    };
}
