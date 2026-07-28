#pragma once
#include <webgpu/webgpu.hpp>
#include <chrono>
#include <memory>
#include <vector>

// Per-frame performance accounting for the viewport.
//
// Two different numbers, because they answer two different questions:
//
//   FrameCount()  — how many frames the engine actually submitted. The web FPS
//                   meter samples this instead of counting its own rAF ticks:
//                   under render-on-demand the browser keeps calling back at the
//                   refresh rate while the engine draws nothing, so a rAF-based
//                   meter reads 60 on a completely idle engine. Sampling this
//                   makes "0" a real, correct reading.
//
//   GpuFrameMs()  — how long the GPU took on the last measured frame, timed from
//                   submit to the queue's onSubmittedWorkDone. Frame *rate* can
//                   never exceed the display refresh (the loop is driven by
//                   requestAnimationFrame), so it saturates at 60 and cannot show
//                   headroom. Frame *time* is not vsync-bound, so it keeps
//                   falling as the scene gets cheaper — this is the number to A/B
//                   an optimisation with. It is approximate: it includes queue
//                   latency and excludes presentation, and browser WebGPU exposes
//                   no real GPU timestamp queries.
class FrameStats
{
public:
    // Call once per submitted frame, right after queue.submit. Bumps the counter
    // and, if no measurement is outstanding, times this frame's GPU work.
    void OnFrameSubmitted(wgpu::Queue queue);

    unsigned long long FrameCount() const { return frameCount; }
    float GpuFrameMs() const { return state ? state->gpuFrameMs : 0.0f; }

    ~FrameStats();

private:
    // Held by shared_ptr so the in-flight callback writes into storage that
    // outlives this object — a pending GPU callback must stay valid even if the
    // owner goes away (see CLAUDE.md §13).
    struct Measurement
    {
        bool inFlight = false;
        std::chrono::steady_clock::time_point submitTime;
        float gpuFrameMs = 0.0f;
    };

    unsigned long long frameCount = 0;
    std::shared_ptr<Measurement> state;
    std::unique_ptr<wgpu::QueueWorkDoneCallback> workDoneCallback;
};
