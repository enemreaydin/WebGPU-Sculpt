#include "platform/GraphicsContext.h"
#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>
#include <iostream>

using namespace wgpu;

namespace Platform
{
    void GraphicsContext::InitializeInstance() { instance = createInstance({}); }

    void GraphicsContext::InitializeSurface(GLFWwindow* window) { surface = glfwGetWGPUSurface(instance, window); }

    void GraphicsContext::InitializeAdapter() {
        RequestAdapterOptions adapterOpts = {};
        adapterOpts.compatibleSurface = surface;
        adapter = instance.requestAdapter(adapterOpts);
        surfaceFormat = surface.getPreferredFormat(adapter);
    }

    void GraphicsContext::InitializeDevice() {
        DeviceDescriptor deviceDesc = {};
        deviceDesc.label = "Device";
        deviceDesc.requiredLimits = nullptr;
        deviceDesc.defaultQueue.label = "Queue";
        deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void*) {
            std::cout << "Device lost: " << reason;
            if (message) std::cout << " (" << message << ")";
            std::cout << std::endl;
            };
        device = adapter.requestDevice(deviceDesc);
    }

    void GraphicsContext::InitializeQueue() {
        queue = device.getQueue();
    }

    void GraphicsContext::ConfigureSurface(int width, int height) {
        SurfaceConfiguration config = {};
        config.width = width;
        config.height = height;
        config.usage = TextureUsage::RenderAttachment;
        config.format = surfaceFormat;
        config.viewFormatCount = 0;
        config.viewFormats = nullptr;
        config.device = device;
        config.presentMode = PresentMode::Fifo;
        config.alphaMode = CompositeAlphaMode::Auto;
        surface.configure(config);
    }
}
