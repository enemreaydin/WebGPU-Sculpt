#include "platform/Window.h"
#include "editor/Application.h"

namespace Platform
{
    void Window::InitializeWindow(int width, int height, void* userPointer) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(width, height, "SDF Editor", nullptr, nullptr);
        glfwSetWindowSizeLimits(window, 1000, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
        // Route framebuffer resize events back to this instance.
        glfwSetWindowUserPointer(window, userPointer);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int nw, int nh) {
            auto app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            app->OnResize(nw, nh);
            });
    }
}
