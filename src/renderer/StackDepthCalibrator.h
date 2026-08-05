#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>

// Decides which CSG_STACK_DEPTH shader variant is fastest ON THIS MACHINE, by
// measuring it instead of hardcoding it.
//
// Why this cannot be a constant
// -----------------------------
// The DECLARED size of the RPN stack arrays in distanceCalc/evaluateScene is
// charged to the whole fragment shader's register/scratch footprint, and that
// footprint sets occupancy — the only mechanism a GPU has for hiding memory
// latency. Cost is therefore not monotonic in the declared size: below the notch
// the array gets scalarised into registers (every access becomes a select chain,
// every write conditionally rewrites all N slots), above it the array spills into
// a fixed per-SM scratch pool. There is a valley, and measurements on this
// project put a factor of ~2 between its floor and its walls.
//
// But WHERE the valley sits is a property of the register file size, the scratch
// pool size and the shader compiler — i.e. of the GPU and driver sitting in front
// of the user, not of the scene or of this codebase. A depth measured on one
// machine is a guess on every other one, and picking wrong costs more than most
// optimisations gain. So the engine probes for it at runtime.
//
// Measuring under drift
// ---------------------
// 🔴 The first version of this got the WRONG ANSWER, and the reason is worth more
// than the fix. It ran all of one candidate's probes, then all of the next's.
// Frame times on a freshly opened project start low and climb for a while before
// settling (queue depth filling, clocks and caches warming), so whichever
// candidate was measured first was measured entirely inside that window — and
// depth 4, first in the list, won every time regardless of merit.
//
// A blocked A/B under a drifting baseline does not measure A against B; it
// measures WHEN A ran against WHEN B ran. Two defences, both required:
//
//   1. WARMUP — discard probes until the reading stops moving, so ranking starts
//      from steady state rather than from the climb.
//   2. INTERLEAVING — probe round-robin (4,8,16,32, 4,8,16,32, ...) instead of in
//      blocks, and rotate which candidate leads each round. Residual drift then
//      lands on every candidate equally instead of on whichever went first, and
//      each candidate spends the same number of rounds in the cheapest slot.
//
// This class deliberately holds no GPU objects and issues no draws. SdfRenderer
// asks it which depth to probe next, runs the probe, and hands back a time; the
// policy, the statistics and the final choice live here.
class StackDepthCalibrator
{
public:
    // Every entry is measured, including 4 and 32. Their presence is not a claim
    // that they are ever good — it is the refusal to assume they are not.
    static constexpr int32_t kCandidates[] = { 4, 8, 16, 32 };
    static constexpr int     kCandidateCount = 4;

    // Warmup runs at one fixed depth and every result is thrown away; it exists
    // only to let the machine reach steady state. It ends early once the reading
    // is stable, and is capped so a permanently noisy machine still calibrates.
    static constexpr int   kMinWarmupProbes = 8;
    static constexpr int   kMaxWarmupProbes = 48;
    static constexpr int   kStableWindow    = 5;
    static constexpr float kStableTolerance = 0.08f;   // 8% spread across the window

    // One probe per candidate per round. More rounds beat more probes-per-visit:
    // rounds are what spread each candidate across the timeline.
    static constexpr int kRounds = 6;

    // What to run BEFORE the sweep has an answer — a bootstrap value, not a
    // policy, and superseded the moment measurement completes.
    //
    // 🔴 It cannot just be "the smallest candidate deep enough", which is what
    // the ladder used to mean. Adding 4 to the candidate list made 4 the smallest,
    // so every scene silently started on the one variant already measured as the
    // worst — a candidate is a hypothesis to be tested, not a default to fall back
    // on. 8 is the best-known-so-far across the machines measured; if this GPU
    // disagrees, the sweep will say so within a second of the first real scene.
    static constexpr int32_t kPreMeasurementDepth = 8;

    void Begin()
    {
        phase = Phase::Warmup;
        warmupCount = 0;
        windowCount = 0;
        round = 0;
        slot = 0;
        for (float& ms : bestMs) ms = 0.0f;
        for (float& ms : window) ms = 0.0f;
    }

    // Give up without a result: the fallback policy stays in force.
    void Abort() { phase = Phase::Idle; }

    bool Active() const     { return phase == Phase::Warmup || phase == Phase::Ranking; }
    bool Calibrated() const { return phase == Phase::Done; }
    bool InWarmup() const   { return phase == Phase::Warmup; }

    // During warmup every probe uses the same variant, so warmup measures the
    // machine settling rather than a mix of variants settling at different rates.
    int32_t PendingDepth() const
    {
        if (phase == Phase::Warmup) return kCandidates[0];
        return kCandidates[(round + slot) % kCandidateCount];
    }

    // Returns true on the probe that completes the whole sweep.
    bool RecordProbe(float ms)
    {
        if (phase == Phase::Warmup)
        {
            RecordWarmup(ms);
            if (warmupCount >= kMaxWarmupProbes || (warmupCount >= kMinWarmupProbes && WindowIsStable()))
            {
                phase = Phase::Ranking;
                round = 0;
                slot = 0;
            }
            return false;
        }

        if (phase != Phase::Ranking) return false;

        if (ms > 0.0f)
        {
            float& best = bestMs[(round + slot) % kCandidateCount];
            // The MINIMUM, not the mean. Every source of noise available here —
            // OS scheduling, browser compositing, another process sharing the
            // GPU, clock ramping — can only ever ADD time to a probe. The fastest
            // run observed is therefore the best estimate of the true cost, and
            // it converges in far fewer samples than an average would, which
            // matters when each sample is a rendered frame the user waits for.
            if (best <= 0.0f || ms < best) best = ms;
        }

        if (++slot < kCandidateCount) return false;
        slot = 0;
        if (++round < kRounds) return false;

        phase = Phase::Done;
        return true;
    }

    // Abandon the current candidate without a measurement and move on. Used when
    // a variant will not compile on this device: leaving it unmeasured means the
    // selector can never choose it, which is the correct outcome.
    // Returns true if that completed the sweep.
    bool SkipCandidate()
    {
        if (phase == Phase::Warmup) { phase = Phase::Ranking; round = 0; slot = 0; return false; }
        if (phase != Phase::Ranking) return false;
        skipped[(round + slot) % kCandidateCount] = true;
        return RecordProbe(0.0f);
    }

    // Cheapest MEASURED candidate that is still deep enough for the program.
    //
    // The depth floor is not negotiable: a variant shallower than the program
    // needs has its extra pushes eaten by the shader's `sp < CSG_STACK_DEPTH`
    // guard, which renders missing geometry silently. Speed only ever chooses
    // among the candidates that are already correct.
    //
    // Before the sweep finishes this returns the smallest valid candidate — never
    // a partially measured ranking, or the pipeline would flip between variants
    // mid-sweep and the sweep would be measuring its own churn.
    int32_t BestBucketAtLeast(int32_t required) const
    {
        // Two different floors. Once measured, correctness is the only constraint
        // and the cheapest deep-enough candidate wins. Before that, the bootstrap
        // depth is held as well, so an untested candidate is never the default.
        const int32_t floor = (phase == Phase::Done)
            ? required
            : std::max(required, kPreMeasurementDepth);

        int32_t smallestValid = -1;
        int32_t cheapest = -1;
        float   cheapestMs = 0.0f;

        // kCandidates is ascending, so the first one clearing the floor is also
        // the smallest.
        for (int i = 0; i < kCandidateCount; ++i)
        {
            const int32_t candidate = kCandidates[i];
            if (candidate < floor) continue;
            if (smallestValid < 0) smallestValid = candidate;

            if (phase != Phase::Done || bestMs[i] <= 0.0f) continue;
            if (cheapest < 0 || bestMs[i] < cheapestMs)
            {
                cheapest = candidate;
                cheapestMs = bestMs[i];
            }
        }

        if (cheapest >= 0) return cheapest;
        if (smallestValid >= 0) return smallestValid;
        return kCandidates[kCandidateCount - 1];
    }

    float MeasuredMs(int32_t bucket) const
    {
        for (int i = 0; i < kCandidateCount; ++i)
            if (kCandidates[i] == bucket) return bestMs[i];
        return 0.0f;
    }

    void Log() const
    {
        std::cout << "[Calibration] CSG stack depth, best of " << kRounds
                  << " interleaved rounds:\n";
        int best = -1, second = -1;
        for (int i = 0; i < kCandidateCount; ++i)
        {
            std::cout << "    depth " << kCandidates[i] << ": ";
            if (bestMs[i] > 0.0f) std::cout << bestMs[i] << " ms";
            else std::cout << (skipped[i] ? "(would not compile)" : "(not measured)");
            std::cout << "\n";
            if (bestMs[i] <= 0.0f) continue;
            if (best < 0 || bestMs[i] < bestMs[best]) { second = best; best = i; }
            else if (second < 0 || bestMs[i] < bestMs[second]) { second = i; }
        }
        if (best < 0) { std::cout << "[Calibration] no candidate measured.\n"; return; }

        std::cout << "[Calibration] fastest on this GPU: depth " << kCandidates[best]
                  << " (" << bestMs[best] << " ms)";
        float slowest = 0.0f;
        for (int i = 0; i < kCandidateCount; ++i) slowest = std::max(slowest, bestMs[i]);
        if (bestMs[best] > 0.0f) std::cout << ", " << (slowest / bestMs[best]) << "x faster than the worst";
        std::cout << "\n";

        // Say so when the result is not actually a result. A margin inside the
        // noise floor means this machine does not care which variant it runs, and
        // reporting that honestly is more useful than dressing it up as a finding.
        if (second >= 0 && bestMs[second] > 0.0f &&
            (bestMs[second] - bestMs[best]) / bestMs[best] < kStableTolerance)
        {
            std::cout << "[Calibration] note: depth " << kCandidates[second] << " is within "
                      << (kStableTolerance * 100.0f) << "% of the winner — this GPU is"
                      << " largely indifferent, and the choice is inside the noise floor.\n";
        }
    }

private:
    enum class Phase { Idle, Warmup, Ranking, Done };

    void RecordWarmup(float ms)
    {
        ++warmupCount;
        window[windowCount % kStableWindow] = ms;
        ++windowCount;
    }

    // Stable == the last kStableWindow probes agree to within kStableTolerance.
    // Deliberately a SPREAD test, not "the last two are close": a monotonic climb
    // produces adjacent samples that look similar at every point along it, so a
    // pairwise test would declare the middle of the ramp settled.
    bool WindowIsStable() const
    {
        if (windowCount < kStableWindow) return false;
        float lo = window[0], hi = window[0];
        for (int i = 1; i < kStableWindow; ++i)
        {
            lo = std::min(lo, window[i]);
            hi = std::max(hi, window[i]);
        }
        if (lo <= 0.0f) return false;
        return (hi - lo) / lo <= kStableTolerance;
    }

    Phase phase = Phase::Idle;
    int   warmupCount = 0;
    int   windowCount = 0;
    float window[kStableWindow] = {};
    int   round = 0;
    int   slot = 0;
    float bestMs[kCandidateCount] = {};    // 0 == not measured
    bool  skipped[kCandidateCount] = {};
};
