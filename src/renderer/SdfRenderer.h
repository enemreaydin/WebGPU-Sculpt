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
    void UpdateSceneBuffer(
        wgpu::Device device,
        wgpu::Queue queue,
        const std::vector<SdfObjectData>& objects,
        const std::vector<CsgCommand>& csgProgram
    );
    void UpdateCsgProgramBuffer(
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

    // One-shot final image render. Reuses this renderer's pipeline, scene/CSG
    // buffers and bind-group layout; only the target and globals snapshot are
    // dedicated to the image request.
    bool StartFinalRender(wgpu::Device device, wgpu::Queue queue,
                          const GlobalsData& globals, uint32_t width, uint32_t height);
    int GetFinalRenderState() const { return finalRenderState; }
    const std::vector<uint8_t>& GetFinalPixels() const { return finalPixels; }
    uint32_t GetFinalWidth() const { return finalWidth; }
    uint32_t GetFinalHeight() const { return finalHeight; }
    wgpu::TextureView GetAccumLayerView(uint32_t i) const;  // returns accumLayerViews[i]
    wgpu::TextureView GetAccumArrayView() const;            // returns accumArrayView
    void ReleaseFinalRender();

    void Destroy();

public:
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::RenderPipeline resolvePipeline = nullptr;  
    wgpu::BindGroup      resolveBindGroup = nullptr;
    wgpu::Buffer sceneBuffer = nullptr;
    wgpu::Buffer csgProgramBuffer = nullptr;
    wgpu::Buffer globalsBuffer = nullptr;
    wgpu::Buffer pickReadbackBuffer = nullptr;

private:
    std::filesystem::path projectRoot;

    wgpu::ShaderModule shaderModule = nullptr;
    wgpu::PipelineLayout pipelineLayout = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::PipelineLayout resolvePipelineLayout = nullptr;
    wgpu::BindGroupLayout resolveBindGroupLayout = nullptr;

    wgpu::Texture renderTargetTexture = nullptr;
    wgpu::TextureView renderTargetView = nullptr;
    wgpu::TextureFormat renderTargetFormat = wgpu::TextureFormat::Undefined;

    static constexpr int32_t kAccumLayers = 4;
    wgpu::Texture accumTexture = nullptr;
    wgpu::TextureView accumLayerViews[kAccumLayers] = {};
    wgpu::TextureView accumArrayView = nullptr;

    wgpu::Texture objectIdTargetTexture = nullptr;
    wgpu::TextureView objectIdTargetView = nullptr;
    wgpu::TextureFormat objectIdTargetFormat = wgpu::TextureFormat::R32Uint;

    // Keeps the heap-allocated mapAsync callback alive until it fires.
    // The webgpu.hpp wrapper hands us ownership via the returned unique_ptr;
    // dropping it frees the std::function while the GPU still holds a pointer
    // to it, which corrupts the indirect call when the map completes.
    std::unique_ptr<wgpu::BufferMapCallback> pickMapCallback = nullptr;
    std::unique_ptr<wgpu::BufferMapCallback> finalMapCallback = nullptr;

    wgpu::Texture finalTexture = nullptr;
    wgpu::TextureView finalView = nullptr;
    wgpu::Texture finalIdTexture = nullptr;
    wgpu::TextureView finalIdView = nullptr;
    wgpu::Buffer finalGlobalsBuffer = nullptr;
    wgpu::BindGroup finalBindGroup = nullptr;
    wgpu::Buffer finalReadbackBuffer = nullptr;
    std::vector<uint8_t> finalPixels;
    int finalRenderState = 0; // 0 idle, 1 rendering, 2 reading, 3 ready, 4 failed
    uint32_t finalWidth = 0;
    uint32_t finalHeight = 0;
    uint32_t finalPaddedBytesPerRow = 0;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint64_t sceneBufferSize = 0;
    uint64_t csgProgramBufferSize = 0;
};
