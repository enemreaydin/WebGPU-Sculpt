#include "FrameStats.h"

namespace {
// Pending GPU callbacks that outlived their FrameStats. Freeing one would leave
// the driver holding a raw pointer into released memory, so they are parked here
// forever instead of destroyed — same rule the export path follows in
// SdfRenderer::RetireFinalCallbacks.
std::vector<std::unique_ptr<wgpu::QueueWorkDoneCallback>> g_RetiredCallbacks;
}

void FrameStats::OnFrameSubmitted(wgpu::Queue queue)
{
    frameCount++;

    if (!state) state = std::make_shared<Measurement>();

    // Only ever one measurement outstanding. Arming a new callback while the
    // previous is still pending would replace the unique_ptr and free a closure
    // the driver is about to call; sampling every Nth frame is plenty anyway.
    if (state->inFlight) return;

    state->inFlight = true;
    state->submitTime = std::chrono::steady_clock::now();

    auto measurement = state;
    workDoneCallback = queue.onSubmittedWorkDone(
        [measurement](wgpu::QueueWorkDoneStatus status) {
            measurement->inFlight = false;
            if (status != wgpu::QueueWorkDoneStatus::Success) return;
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - measurement->submitTime).count();
            const float ms = static_cast<float>(seconds * 1000.0);
            // Light smoothing so the readout is legible; frame times are noisy.
            measurement->gpuFrameMs = (measurement->gpuFrameMs > 0.0f)
                ? measurement->gpuFrameMs * 0.8f + ms * 0.2f
                : ms;
        });
}

FrameStats::~FrameStats()
{
    if (state && state->inFlight && workDoneCallback)
        g_RetiredCallbacks.push_back(std::move(workDoneCallback));
}
