#pragma once

#include "editor/QualitySettings.h"

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
    // Authoritative store for the shader's look knobs. Copied into GlobalsData
    // every frame by Application::SyncRenderGlobals (settings -> globals -> GPU
    // uniform -> shader). Defaults match GlobalsData so the render is unchanged
    // until the user edits them. Mutated via Application::SetRenderSetting(id,
    // value); the ids are the RenderSetting enum below, shared with the web
    // front-end.

    // "Look" settings — artistic, no or low performance cost, and SHARED by the
    // viewport and the export (an exported image must match what was on screen):
    float lightDirection[3] = { 0.5f, 0.8f, 0.6f }; // world direction TO the light (sun angle); normalized in-shader
    float lightColor[3]     = { 1.0f, 1.0f, 1.0f };  // RGB tint of the main light
    float lightIntensity    = 1.0f;   // overall brightness of the main light
    float ambientStrength   = 0.12f;  // base fill light everywhere (even in shadow); higher = flatter
    float shadowSharpness   = 16.0f;  // soft-shadow k: higher = crisper shadow edges, lower = softer penumbra
    float specularGloss     = 48.0f;  // highlight size (Blinn-Phong exponent): higher = tighter/glossier
    float specularStrength  = 0.4f;   // highlight brightness / reflectivity (0 = matte)
    float aoStrength        = 3.0f;   // contact-shadow darkening in creases

    // Quality / performance budgets — one set per context (see QualitySettings.h).
    // These are what the viewport and the export must NOT share: the viewport is
    // tuned for framerate, the export for image quality. Mutated via
    // Application::SetQualitySetting(context, id, value).
    QualitySettings viewportQuality{};
    QualitySettings exportQuality = QualitySettings::ExportDefaults();
};

// Ids for Application::SetRenderSetting — shared with the web front-end
// (web/src/engine.ts RenderSetting). Vector components are individual ids.
// Look settings only; the quality budgets moved to QualitySetting (they exist
// twice now, once per context, so they need the context-taking setter).
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
};
