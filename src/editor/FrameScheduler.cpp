#include "FrameScheduler.h"
#include <algorithm>

// Saturating, not accumulating: twenty invalidations in one tick still only mean
// "the image is stale, converge again", not eighty frames of work.
void FrameScheduler::Request(int framesRequested){
    frameCount = std::max(framesRequested, frameCount);
}

// Floored so the counter can never go negative even if a caller consumes a frame
// it never actually submitted — the class holds the invariant, not the caller.
void FrameScheduler::Consume(){
    if (frameCount > 0) frameCount--;
}

bool FrameScheduler::ShouldRender() const{
    return forceContinuous || frameCount > 0;
}

// Debug / bisect escape hatch: pin the loop back to unconditional per-vsync
// rendering. If a bug disappears when this is on, the cause is a missing
// RequestRedraw somewhere, not the thing being investigated.
void FrameScheduler::ForceContinuous(bool enabled){
    forceContinuous = enabled;
}
