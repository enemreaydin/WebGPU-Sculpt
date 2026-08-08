#pragma once

#include <webgpu/webgpu.hpp>
#include "scene/data/SdfObjectData.h"
#include "scene/data/GlobalsData.h"
#include "scene/csg/CsgCommand.h"
#include "renderer/StackDepthCalibrator.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
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

    // Point `pipeline` at a scene pipeline whose in-shader RPN stacks are just big
    // enough for the program about to be uploaded, compiling and caching that
    // variant on first use.
    //
    // Why this exists: WGSL function-local array sizes must be const-expressions, so
    // the stacks in distanceCalc/evaluateScene cannot be sized from a uniform. Their
    // DECLARED size is charged to the whole fragment shader's register/scratch
    // footprint, which sets occupancy — measured at 23-62% frame time between depth
    // 16 and depth 8 on identical programs. The only lever is to specialise the
    // shader source per depth and compile a pipeline per variant.
    //
    // requiredStackDepth comes from CsgTree::CompiledProgram; it is rounded UP to a
    // bucket so a scene oscillating between depth 2 and 3 does not thrash the cache.
    void EnsureScenePipeline(wgpu::Device device, int32_t requiredStackDepth);
    int32_t GetActiveCsgStackDepth() const { return activeCsgStackDepth; }

    // ---- Runtime stack-depth calibration (see StackDepthCalibrator.h) --------
    //
    // Arms a sweep: every candidate depth is compiled and timed against the
    // CURRENT scene, and the fastest one wins. Safe to call at any time; the
    // sweep runs itself from StepCalibration and simply restarts if it is armed
    // again mid-flight.
    void RequestCalibration();

    // Runs at most one probe per call, so a sweep costs one extra offscreen draw
    // per frame and never blocks. Returns true on the call that COMPLETES the
    // sweep, which is the caller's cue to request a redraw — the scene pipeline
    // has just been re-selected underneath it.
    //
    // Must be stepped from above MainLoop's redraw guard, for the same reason
    // StepFinalRender is: it is a state machine that advances once per iteration,
    // and an idle viewport is exactly when a sweep should be making progress.
    // `snapshot` is pinned on the first probe of a sweep and reused for every
    // later one. Candidates must be ranked against IDENTICAL work, and the live
    // globals buffer is rewritten every frame — an orbit halfway through a sweep
    // would otherwise charge one variant for a close-up and another for a distant
    // view and call the difference occupancy.
    bool StepCalibration(wgpu::Device device, wgpu::Queue queue, const GlobalsData& snapshot);

    bool IsCalibrating() const { return calibrator.Active(); }

    // 🔴 True only once probes are actually being submitted — NOT merely "a sweep
    // is armed". Callers suppress viewport rendering on this, and a sweep sits
    // armed from startup while StepCalibration waits for its preconditions (a
    // real CSG program, valid globals). Gating the viewport on Active() instead
    // deadlocks: calibration waits for globals, globals are only written by a
    // frame, and the frame is blocked by calibration. Black screen, forever.
    bool IsProbing() const { return calibrator.Active() && calibProbingStarted; }
    bool IsCalibrated() const  { return calibrator.Calibrated(); }
    // 0 when a depth has not been measured (or the sweep has not run).
    float CalibratedMsFor(int32_t depth) const { return calibrator.MeasuredMs(depth); }

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

    // Deepest RPN stack any shader variant provides. A program needing more than
    // this renders with pushes silently dropped (the `sp < CSG_STACK_DEPTH` guard),
    // i.e. missing geometry — EnsureScenePipeline warns loudly when it happens.
    // 32 covers a perfectly balanced binary CSG tree of ~2 billion primitives; only
    // pathological hand-built or hostile nesting can exceed it.
    static constexpr int32_t kMaxCsgStackDepth = 32;

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

    wgpu::ShaderModule shaderModule = nullptr;   // base module; fs_resolve comes from here

    // Raw WGSL kept in memory so a new depth variant costs a string concat, not a
    // file read — the engine reads these from the WASM virtual FS, and a variant can
    // be requested at any scene edit.
    std::string coreShaderSource;
    std::string mainShaderSource;

    // One shader module + one render pipeline per stack-depth bucket, created lazily
    // and never evicted (a handful of buckets, and recompiling is the expensive part).
    // Modules are retained alongside their pipelines: the pipeline keeps the module
    // alive on the API side, but holding the wrapper keeps Destroy symmetrical.
    std::map<int32_t, wgpu::ShaderModule>   sceneShaderModules;
    std::map<int32_t, wgpu::RenderPipeline> scenePipelines;
    int32_t activeCsgStackDepth = 0;

    std::string BuildShaderSource(int32_t stackDepth) const;
    // No longer static: the answer now depends on what calibration measured on
    // this machine, not on a constant baked in at authoring time.
    int32_t BucketForDepth(int32_t requiredStackDepth) const;
    // Cached-or-compiled pipeline for one depth, WITHOUT re-pointing `pipeline`.
    // Calibration needs to draw with a variant the scene is not using, and
    // `pipeline` is a non-owning alias that the viewport reads every frame.
    wgpu::RenderPipeline PipelineForDepth(wgpu::Device device, int32_t bucket);

    // ---- calibration state --------------------------------------------------
    StackDepthCalibrator calibrator;
    // Remembered so the sweep can re-select the scene pipeline when it finishes,
    // without waiting for the next scene edit to supply the depth again.
    int32_t lastRequiredStackDepth = 0;

    // One probe's worth of shared state. Held by shared_ptr and captured BY VALUE
    // by the work-done lambda so it outlives this renderer if the device dies
    // mid-sweep (CLAUDE.md §13).
    struct CalibrationProbe
    {
        bool   inFlight = false;
        bool   ready    = false;   // a result is waiting to be consumed
        bool   failed   = false;
        double ms       = 0.0;
        std::chrono::steady_clock::time_point start{};
    };
    // Start of the current settle window (see kCalibSettleSeconds). Always written
    // by RequestCalibration, which is the only way a sweep becomes Active, so
    // StepCalibration can never read it uninitialised.
    std::chrono::steady_clock::time_point calibSettleStart{};
    bool calibSettleAnnounced = false;   // one-shot log when the window closes

    std::shared_ptr<CalibrationProbe> calibProbe;
    std::unique_ptr<wgpu::QueueWorkDoneCallback> calibCallback;
    std::vector<std::unique_ptr<wgpu::QueueWorkDoneCallback>> calibRetiredCallbacks;

    // A frozen copy of the globals the sweep started with, plus a bind group that
    // reads it instead of the live one. Rebuilt per sweep, which is also what
    // keeps it from outliving the scene buffers it points at.
    wgpu::Buffer      calibGlobalsBuffer = nullptr;
    wgpu::BindGroup   calibBindGroup     = nullptr;
    bool              calibBindGroupReady = false;
    bool EnsureCalibrationBindGroup(wgpu::Device device, wgpu::Queue queue, const GlobalsData& snapshot);
    void ReleaseCalibrationBindGroup();
    // Called from every scene-upload path; drops the pinned bind group and
    // restarts an in-flight sweep. See the comment at the definition.
    void OnSceneUploadChanged();

    wgpu::Texture     calibTexture   = nullptr;
    wgpu::TextureView calibView      = nullptr;
    wgpu::Texture     calibIdTexture = nullptr;
    wgpu::TextureView calibIdView    = nullptr;
    uint32_t calibTargetSize = 0;   // edge length of the allocated probe target
    uint32_t calibProbeSize  = 0;   // edge length the sweep wants

    bool EnsureCalibrationTarget(wgpu::Device device, uint32_t size);
    void ReleaseCalibrationTarget();

    // The probe renders offscreen at its own square size rather than at the
    // canvas size, so a sweep costs the same whatever window the user has open
    // and cannot be skewed by a resize halfway through.
    //
    // The size is MEASURED, not fixed, for the same reason the export sizes its
    // bands: a scene can be 40x heavier than another, and one constant cannot
    // serve both. Too small and the probe is drowned by queue latency; too large
    // and a sweep freezes the viewport for half a minute, or trips the OS GPU
    // watchdog (Windows TDR = 2s) on a single unsplittable draw. Warmup starts
    // deliberately small and converges on kCalibTargetMs before ranking begins.
    static constexpr uint32_t kCalibStartSize = 96;
    static constexpr uint32_t kCalibMinSize   = 64;
    // 🔴 Raised from 512 because on a light scene the sizer HIT this ceiling and
    // the sweep ranked below its target workload. Measured there: 3.2 ms @ 64px,
    // 3.9 @ 128, 4.9 @ 256, ~8.1 @ 512 — 64x the pixels for 2.5x the time. A
    // straight-line fit puts ~3.5 ms of that on a FIXED per-probe cost (submit,
    // queue latency, the onSubmittedWorkDone round trip), i.e. nearly half of each
    // reading was overhead common to all four candidates. That does not bias the
    // ranking, it COMPRESSES it: the real differences get added to a large constant
    // and then compared, which is how a genuine 2x on the shader work arrives
    // looking like the 6% spread the calibrator then correctly calls noise.
    //
    // Diluting the constant is the only fix available (the round trip is the
    // browser's, not ours), and the ceiling only ever binds on scenes cheap enough
    // for it to be safe — the sizer converges DOWNWARD on a heavy scene and never
    // reaches the cap. At kCalibTargetMs the worst case here is ~25 ms against a
    // 2 s TDR budget.
    static constexpr uint32_t kCalibMaxSize   = 1024;
    static constexpr double   kCalibTargetMs  = 25.0;
    // Below this the measurement is mostly queue latency and clock granularity —
    // a fixed additive cost that would drown the differences being ranked.
    static constexpr double kCalibMinProbeMs = 2.0;
    // Generous because the FIRST probe of a sweep still has whatever the viewport
    // had already queued ahead of it; every later probe runs against a drained
    // queue (Application skips the viewport passes while a sweep is in flight).
    static constexpr double kCalibStallSeconds = 10.0;

    // Quiet period between the last scene change and the FIRST probe of a sweep.
    //
    // The calibrator already discards probes until the reading stops moving
    // (StackDepthCalibrator's warmup), but that defence is statistical and can be
    // fooled: a transient that is consistently wrong for five probes in a row
    // passes the 8% spread test and ends warmup early, on the ramp. Right after a
    // page load, a pipeline rebuild or a project open there is a lot of that —
    // shader compilation, first-use allocation, the browser still doing layout —
    // and it is transient in WALL-CLOCK time, which is the one axis a probe-count
    // heuristic cannot see. So the two defences are deliberately different in kind:
    // this one waits out the clock, warmup waits out the variance.
    //
    // 🔴 It is a DEBOUNCE, not a delay. RequestCalibration re-arms it, and every
    // scene upload re-requests while a sweep is live, so the window restarts on
    // each change and the sweep begins 10s after the LAST one. That is what keeps
    // a slider drag from starting a sweep between two frames of the drag — which
    // would pin a snapshot mid-edit and rank candidates against a scene that no
    // longer exists.
    static constexpr double kCalibSettleSeconds = 10.0;

    // Edge length that would land the next probe near kCalibTargetMs. Cost scales
    // with pixel COUNT, so the edge scales with the square root of the ratio.
    uint32_t NextProbeSize(uint32_t size, double ms) const;
    // Set when the first probe of a sweep is actually submitted; see IsProbing().
    bool calibProbingStarted = false;
    // An empty or near-empty program early-outs in the shader before it ever
    // touches the RPN stacks, so every variant would time identically and the
    // "winner" would be pure noise.
    static constexpr uint64_t kCalibMinProgramCommands = 8;

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
    // The scene pipeline this export started with. An export spans many frames and
    // many band submits, while `pipeline` can be re-pointed at a different stack-depth
    // variant by any scene edit in between — bands must all run the same shader, so
    // the export pins its own.
    wgpu::RenderPipeline finalPipeline = nullptr;
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
