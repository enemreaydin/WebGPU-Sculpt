#pragma once

// Engine-side settings. UI-related state (which settings tab is open, theme,
// fonts) now lives in the web front-end; only values the C++ engine actually
// consumes remain here.
struct Settings
{
    // Camera settings
    float orbitSensitivity = 0.005f;
    float zoomSensitivity  = 0.15f;
    float panSensitivity   = 0.025f;

    // --- Render settings -----------------------------------------------------
    // Authoritative store for the shader's look + quality knobs. Copied into
    // GlobalsData every frame by Application::SyncRenderGlobals (settings ->
    // globals -> GPU uniform -> shader). Defaults match GlobalsData so the
    // render is unchanged until the user edits them. Mutated via
    // Application::SetRenderSetting(id, value); the ids are the RenderSetting
    // enum below, shared with the web front-end.

    // "Look" settings (no or low performance cost — safe for any user):
    float lightDirection[3] = { 0.5f, 0.8f, 0.6f }; // world direction TO the light (sun angle); normalized in-shader
    float lightColor[3]     = { 1.0f, 1.0f, 1.0f };  // RGB tint of the main light
    float lightIntensity    = 1.0f;   // overall brightness of the main light
    float ambientStrength   = 0.12f;  // base fill light everywhere (even in shadow); higher = flatter
    float shadowSharpness   = 16.0f;  // soft-shadow k: higher = crisper shadow edges, lower = softer penumbra
    float specularGloss     = 48.0f;  // highlight size (Blinn-Phong exponent): higher = tighter/glossier
    float specularStrength  = 0.4f;   // highlight brightness / reflectivity (0 = matte)
    float aoStrength        = 3.0f;   // contact-shadow darkening in creases

    // Quality / performance settings (advanced — trade speed for precision/range):
    int   maxSteps          = 100;    // primary-ray step budget (main performance dial)
    float hitEpsilon        = 0.001f; // surface-hit threshold (smaller = crisper, needs more maxSteps)
    float maxDistance       = 50.0f;  // how far primary rays travel before giving up
    int   shadowSteps       = 64;     // shadow-ray step budget
    float shadowDistance    = 30.0f;  // how far shadow rays search for occluders
    int   aoSamples         = 5;      // ambient-occlusion taps (>= 2)
};

// Ids for Application::SetRenderSetting — shared with the web front-end
// (web/src/engine.ts RenderSetting). Vector components are individual ids.
enum class RenderSetting : int
{
    LightDirX = 0, LightDirY = 1, LightDirZ = 2,
    LightColorR = 3, LightColorG = 4, LightColorB = 5,
    LightIntensity = 6,
    AmbientStrength = 7,
    ShadowSharpness = 8,
    SpecularGloss = 9,
    SpecularStrength = 10,
    AoStrength = 11,
    MaxSteps = 12,
    HitEpsilon = 13,
    MaxDistance = 14,
    ShadowSteps = 15,
    ShadowDistance = 16,
    AoSamples = 17,
};
