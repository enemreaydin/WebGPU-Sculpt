#include "platform/GraphicsContext.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>

using namespace wgpu;

namespace Platform
{
    void GraphicsContext::InitializeInstance() { instance = createInstance({}); }

    void GraphicsContext::InitializeSurface(SDL_Window* window) {
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            SDL_GetWindowWMInfo(window, &wmInfo);

            HWND hwnd = wmInfo.info.win.window;
            HINSTANCE hinstance = GetModuleHandle(nullptr);

            SurfaceDescriptorFromWindowsHWND fromWindowsHWND = {};
            fromWindowsHWND.chain.next = nullptr;
            fromWindowsHWND.chain.sType = SType::SurfaceDescriptorFromWindowsHWND;
            fromWindowsHWND.hwnd = hwnd;
            fromWindowsHWND.hinstance = hinstance;

            SurfaceDescriptor surfaceDesc = {};
            surfaceDesc.nextInChain = &fromWindowsHWND.chain;

            surface = instance.createSurface(surfaceDesc);
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
