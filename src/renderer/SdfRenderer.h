#pragma once

#include <webgpu/webgpu.hpp>
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"

#include <cstdint>
#include <filesystem>
#include <vector>

class SdfRenderer
{
public:
    void SetProjectRoot(const std::filesystem::path& root);

    void Initialize(wgpu::Device device, wgpu::TextureFormat format);

    void CreateSceneBuffer(
        wgpu::Device device,
        wgpu::Queue queue,
        const std::vector<SdfObjectData>& objects
    );

    void ClearSceneBuffer();

    void CreateRenderTarget(
        wgpu::Device device,
        uint32_t width,
        uint32_t height
    );

    wgpu::TextureView GetRenderTargetView() const;
    uint32_t GetRenderWidth() const;
    uint32_t GetRenderHeight() const;

    void Destroy();

public:
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::Buffer sceneBuffer = nullptr;
    wgpu::Buffer globalsBuffer = nullptr;

private:
    std::filesystem::path projectRoot;

    wgpu::ShaderModule shaderModule = nullptr;
    wgpu::PipelineLayout pipelineLayout = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;

    wgpu::Texture renderTargetTexture = nullptr;
    wgpu::TextureView renderTargetView = nullptr;
    wgpu::TextureFormat renderTargetFormat = wgpu::TextureFormat::Undefined;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
};