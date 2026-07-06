#include "platform/Window.h"
#include "editor/Application.h"

namespace Platform
{
    void Window::InitializeWindow(int width, int height, void* userPointer) {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "SDF Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_RESIZABLE
    );

    SDL_SetWindowMinimumSize(window, 1000, 600);
    // GLFW_DONT_CARE karşılığı: maksimum boyut sınırlaması koymuyoruz,
    // bu yüzden SDL_SetWindowMaximumSize hiç çağrılmıyor.

    // GLFW'nin glfwSetWindowUserPointer karşılığı: SDL_SetWindowData
    // ile window'a bir "isim -> pointer" kaydı yapıyoruz.
    SDL_SetWindowData(window, "app", userPointer);
}
}
