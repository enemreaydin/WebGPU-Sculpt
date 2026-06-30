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
    void Destroy();

    // Application'ýn çizebilmesi için public (ya da getter metodlarý yazabilirsin)
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::Buffer sceneBuffer = nullptr;
    wgpu::PipelineLayout pipelineLayout = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::ShaderModule shaderModule = nullptr;

public:
    void SetProjectRoot(const std::filesystem::path& root);

private:
    std::filesystem::path projectRoot;

};