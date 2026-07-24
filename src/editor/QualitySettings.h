#pragma once

// One raymarch quality budget. Two independent instances live in Settings: one
// drives the interactive viewport, one drives the offscreen image export. They
// are deliberately separate so the user can work at a cheap viewport quality and
// still export a high-quality image without re-tuning anything first.
//
// Only the *cost* knobs live here. The "look" values (light, ambient, gloss,
// shadow sharpness, AO strength) stay in Settings and are shared by both
// contexts — the export must look like what the viewport showed, just cleaner.
struct QualitySettings
{
    // Raymarch budgets.
    int   maxSteps       = 100;    // primary-ray step budget (the main performance dial)
    float hitEpsilon     = 0.001f; // surface-hit threshold (smaller = crisper, needs more maxSteps)
    float maxDistance    = 50.0f;  // how far primary rays travel before giving up
    int   shadowSteps    = 64;     // shadow-ray step budget
    float shadowDistance = 30.0f;  // how far shadow rays search for occluders
    int   aoSamples      = 5;      // ambient-occlusion taps (>= 2)

    // Anti-aliasing. The two techniques are complementary and belong to opposite
    // contexts: SSAA costs N*N rays per pixel every frame (unusable in a live
    // viewport, ideal for a one-off export), while temporal accumulation is free
    // per frame but needs 4 consecutive frames of a still camera to converge —
    // so it does nothing for the export's single offscreen pass. See CLAUDE.md §7a.
    int   aaSamples      = 1;      // supersampling grid dimension N: NxN sub-pixel rays (1 = off)
    bool  taaEnabled     = true;   // temporal accumulation AA — viewport only, ignored by the export

    // Feature toggles — the two biggest optional per-pixel marches. Per-context
    // so "shadows off while I model, shadows on when I export" is one setting,
    // not a thing the user has to remember to flip back.
    bool  shadowsEnabled = true;   // soft shadows (calcSoftShadow)
    bool  aoEnabled      = true;   // ambient occlusion (calcAO)

    // Defaults for the export context: the viewport trades quality for framerate,
    // the export has no framerate to protect, so it starts where the viewport's
    // "Ultra" preset ends and leans on SSAA instead of temporal accumulation.
    static QualitySettings ExportDefaults()
    {
        QualitySettings q;
        q.maxSteps       = 256;
        q.hitEpsilon     = 0.0004f;
        q.maxDistance    = 80.0f;
        q.shadowSteps    = 96;
        q.shadowDistance = 40.0f;
        q.aoSamples      = 8;
        q.aaSamples      = 3;
        q.taaEnabled     = false;
        return q;
    }
};

// Ids for Application::SetQualitySetting(context, id, value) — mirrored in
// web/src/engine.ts (QualitySetting). Keep the two in sync.
enum class QualitySetting : int
{
    MaxSteps = 0,
    HitEpsilon = 1,
    MaxDistance = 2,
    ShadowSteps = 3,
    ShadowDistance = 4,
    AoSamples = 5,
    AaSamples = 6,
    Taa = 7,
    ShadowsEnabled = 8,
    AoEnabled = 9,
};

// Which QualitySettings instance a setter addresses. Mirrored in engine.ts.
enum class QualityContext : int
{
    Viewport = 0,
    Export = 1,
};
