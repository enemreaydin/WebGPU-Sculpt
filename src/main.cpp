#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>  // glfwGetWGPUSurface için
#include <webgpu/webgpu.h>
#include <iostream>
#include <cassert>

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif

int main() {
	std::cout << "Program Started!" << std::endl;
}
