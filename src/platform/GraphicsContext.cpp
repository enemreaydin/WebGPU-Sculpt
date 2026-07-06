#include "platform/GraphicsContext.h"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <iostream>

using namespace wgpu;

namespace Platform
{
    void GraphicsContext::InitializeInstance() {
#ifdef __EMSCRIPTEN__
        // Emscripten's wgpuCreateInstance asserts the descriptor is null and
        // ignores it entirely — passing a (non-null) default descriptor aborts.
        instance = wgpuCreateInstance(nullptr);
#else
        instance = createInstance({});
#endif
    }

    void GraphicsContext::InitializeSurface(SDL_Window* window) {
        SDL_PumpEvents();

#ifdef __EMSCRIPTEN__
        // In the browser the WebGPU surface is bound to the HTML canvas that SDL
        // created. SDL's emscripten backend uses the canvas selector "#canvas".
        (void)window;
        SurfaceDescriptorFromCanvasHTMLSelector fromCanvas = {};
        fromCanvas.chain.next = nullptr;
        fromCanvas.chain.sType = SType::SurfaceDescriptorFromCanvasHTMLSelector;
        fromCanvas.selector = "#canvas";

        SurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = &fromCanvas.chain;

        surface = instance.createSurface(surfaceDesc);
#elif defined(_WIN32)
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        );
        HINSTANCE hinstance = GetModuleHandle(nullptr);

        SurfaceDescriptorFromWindowsHWND fromWindowsHWND = {};
        fromWindowsHWND.chain.next = nullptr;
        fromWindowsHWND.chain.sType = SType::SurfaceDescriptorFromWindowsHWND;
        fromWindowsHWND.hwnd = hwnd;
        fromWindowsHWND.hinstance = hinstance;

        SurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = &fromWindowsHWND.chain;

        surface = instance.createSurface(surfaceDesc);
#else
#error "Unsupported platform for GraphicsContext::InitializeSurface"
#endif
        }
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
