#pragma once

#include <webgpu/webgpu.hpp>
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"
#include "scene/csg/CsgCommand.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class SdfRenderer
{
public:
    void SetProjectRoot(const std::filesystem::path& root);

    void Initialize(wgpu::Device device, wgpu::TextureFormat format);

    void CreateSceneBuffer(
        wgpu::Device device,
        wgpu::Queue queue,
        const std::vector<SdfObjectData>& objects,
        const std::vector<CsgCommand>& csgProgram
    );

    void ClearSceneBuffer();

    void CreateRenderTarget(
        wgpu::Device device,
        uint32_t width,
        uint32_t height
    );

    wgpu::TextureView GetObjectIdTargetView() const;
    wgpu::TextureView GetRenderTargetView() const;
    void ReadObjectIdAt(wgpu::Device device, wgpu::Queue queue, int x, int y,
                     bool* outDone, uint32_t* outResult);
    uint32_t GetRenderWidth() const;
    uint32_t GetRenderHeight() const;

    void Destroy();

public:
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::Buffer sceneBuffer = nullptr;
    wgpu::Buffer csgProgramBuffer = nullptr;
    wgpu::Buffer globalsBuffer = nullptr;
    wgpu::Buffer pickReadbackBuffer = nullptr;

private:
    std::filesystem::path projectRoot;

    wgpu::ShaderModule shaderModule = nullptr;
    wgpu::PipelineLayout pipelineLayout = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;

    wgpu::Texture renderTargetTexture = nullptr;
    wgpu::TextureView renderTargetView = nullptr;
    wgpu::TextureFormat renderTargetFormat = wgpu::TextureFormat::Undefined;

    wgpu::Texture objectIdTargetTexture = nullptr;
    wgpu::TextureView objectIdTargetView = nullptr;
    wgpu::TextureFormat objectIdTargetFormat = wgpu::TextureFormat::R32Uint;

    // Keeps the heap-allocated mapAsync callback alive until it fires.
    // The webgpu.hpp wrapper hands us ownership via the returned unique_ptr;
    // dropping it frees the std::function while the GPU still holds a pointer
    // to it, which corrupts the indirect call when the map completes.
    std::unique_ptr<wgpu::BufferMapCallback> pickMapCallback = nullptr;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
};