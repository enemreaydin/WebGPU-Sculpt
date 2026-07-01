#pragma once
#include <webgpu/webgpu.hpp>
#include "../scene/SceneManager.h"
#include "../core/SdfDefinitions.h"
#include <vector>
#include <filesystem>

class SdfRenderer {
public:
    void Initialize(wgpu::Device device, wgpu::TextureFormat format);
    void CreateSceneBuffer(wgpu::Device device, wgpu::Queue queue, const std::vector<SdfObjectData>& objects);
    
    void CreateRenderTarget(wgpu::Device device, uint32_t width, uint32_t height);
    wgpu::TextureView GetRenderTargetView() const { return renderTargetView; }
    uint32_t GetRenderWidth() const { return renderWidth; }
    uint32_t GetRenderHeight() const { return renderHeight; }

    void Destroy();

    // Application'ýn çizebilmesi için public (ya da getter metodlarý yazabilirsin)
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::Buffer sceneBuffer = nullptr;
    wgpu::Buffer globalsBuffer = nullptr;
    wgpu::PipelineLayout pipelineLayout = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::ShaderModule shaderModule = nullptr;
    void SetProjectRoot(const std::filesystem::path& root);

private:
    std::filesystem::path projectRoot;
    wgpu::Texture renderTargetTexture = nullptr;
    wgpu::TextureView renderTargetView = nullptr;
    wgpu::TextureFormat renderTargetFormat = wgpu::TextureFormat::Undefined;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

};

