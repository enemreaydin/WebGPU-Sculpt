#pragma once
#include <webgpu/webgpu.hpp>

struct SDL_Window;

namespace Platform
{
    // Owns the WebGPU instance/surface/adapter/device/queue and surface config.
    class GraphicsContext
    {
    public:
        void InitializeInstance();
        void InitializeSurface(SDL_Window* window);
        void InitializeAdapter();
        void InitializeDevice();
        void InitializeQueue();
        void ConfigureSurface(int width, int height);

        wgpu::Instance instance = nullptr;
        wgpu::Surface surface = nullptr;
        wgpu::Adapter adapter = nullptr;
        wgpu::Device device = nullptr;
        wgpu::Queue queue = nullptr;
        wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    };
}
