#pragma once

#include <webgpu/webgpu.hpp>
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"
#include "scene/csg/CsgCommand.h"

#include <chrono>
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
    // dedicated to the image request. The image is rendered in horizontal
    // scissor bands (one queue submit each) instead of a single draw, so the
    // GPU is never blocked for the whole image at once: progress is
    // observable, the viewport stays responsive between bands, and a heavy
    // export can't hit the OS GPU-watchdog timeout that a monolithic
    // multi-second submission risks. StartFinalRender submits band 0;
    // StepFinalRender (called once per MainLoop frame) submits each following
    // band only after the previous band's onSubmittedWorkDone fired, then
    // kicks the readback after the last band.
    bool StartFinalRender(wgpu::Device device, wgpu::Queue queue,
                          const GlobalsData& globals, uint32_t width, uint32_t height);
    void StepFinalRender(wgpu::Device device, wgpu::Queue queue);
    // 0..100, measured in GPU-completed scanlines (not submitted ones), so the
    // bar tracks real work; reports 100 during the readback/map phase.
    int GetFinalRenderProgress() const;
    int GetFinalRenderState() const { return finalRenderState; }
    const std::vector<uint8_t>& GetFinalPixels() const { return finalPixels; }
    uint32_t GetFinalWidth() const { return finalWidth; }
    uint32_t GetFinalHeight() const { return finalHeight; }
    wgpu::TextureView GetAccumLayerView(uint32_t i) const;  // returns accumLayerViews[i]
    wgpu::TextureView GetAccumArrayView() const;            // returns accumArrayView
    void ReleaseFinalRender();

    void Destroy();

public:
    // Number of temporal-accumulation layers fs_resolve averages (CLAUDE.md §7a).
    // Also the number of consecutive frames the image needs to converge after a
    // change, which is why Application::RequestRedraw reads it.
    static constexpr int32_t kAccumLayers = 4;

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
    bool finalMapInFlight = false;

    wgpu::Texture finalTexture = nullptr;
    wgpu::TextureView finalView = nullptr;
    wgpu::Texture finalIdTexture = nullptr;
    wgpu::TextureView finalIdView = nullptr;
    wgpu::Buffer finalGlobalsBuffer = nullptr;
    wgpu::BindGroup finalBindGroup = nullptr;
    wgpu::Buffer finalReadbackBuffer = nullptr;
    std::vector<uint8_t> finalPixels;
    int finalRenderState = 0; // 0 idle, 1 rendering bands, 2 reading, 3 ready, 4 failed
    uint32_t finalWidth = 0;
    uint32_t finalHeight = 0;
    uint32_t finalPaddedBytesPerRow = 0;

    // Band-tiled export state. The next band is submitted only from
    // StepFinalRender (never from inside the workDone callback — submitting
    // there would replace finalBandCallback while its std::function is still
    // executing, freeing the closure under our own feet).
    void SubmitNextFinalBand(wgpu::Device device, wgpu::Queue queue);
    void StartFinalReadback(wgpu::Device device, wgpu::Queue queue);
    uint32_t NextBandRows(uint32_t rows, double seconds) const;
    void RetireFinalCallbacks();
    std::unique_ptr<wgpu::QueueWorkDoneCallback> finalBandCallback = nullptr;

    // Adaptive band sizing. Every band is timed and the next one is scaled to
    // land near kBandTargetSeconds. This is a hard safety requirement, not a
    // nicety: a submission that runs longer than the OS GPU watchdog window
    // (Windows TDR = 2s by default) gets the driver reset out from under it,
    // which loses the device and kills the export. Bands therefore shrink
    // immediately but grow at most 2x at a time.
    // 0.25s against a 2s watchdog is deliberate headroom: consecutive bands are
    // not equally expensive (a band where geometry first enters the frame can
    // cost several times the empty-sky band above it), and the sizer only
    // learns that AFTER the band has run.
    static constexpr double   kBandTargetSeconds = 0.25;
    static constexpr double   kStallSeconds      = 15.0; // no callback by then => device lost
    static constexpr uint32_t kBandFirstRows     = 16;   // conservative probe band
    static constexpr uint32_t kBandMinRows       = 4;
    static constexpr uint32_t kBandMaxRows       = 256;
    uint32_t finalRowsDone     = 0;  // scanlines whose GPU work completed
    uint32_t finalBandRows     = 0;  // scanlines in the band currently in flight
    uint32_t finalNextBandRows = 0;  // measured estimate for the next band
    bool     finalBandInFlight = false;
    // Start of the phase we are waiting on (band submit, or the readback map),
    // used both for the adaptive timing and for the stall watchdog.
    std::chrono::steady_clock::time_point finalPhaseStart{};
    // Cleared to false when an export ends, so a callback that fires late (a
    // reset GPU can still deliver one) knows its export is gone.
    std::shared_ptr<bool> finalToken;
    // A pending callback must never be freed: the driver holds a raw pointer
    // to the std::function and would call released memory when it finally
    // fires. Aborted exports park theirs here instead of destroying them.
    std::vector<std::unique_ptr<wgpu::QueueWorkDoneCallback>> finalRetiredWorkCallbacks;
    std::vector<std::unique_ptr<wgpu::BufferMapCallback>> finalRetiredMapCallbacks;

    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint64_t sceneBufferSize = 0;
    uint64_t csgProgramBufferSize = 0;
};
