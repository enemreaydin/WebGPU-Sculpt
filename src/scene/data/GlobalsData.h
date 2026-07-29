#pragma once

#include <cstdint>

struct alignas(16) GlobalsData
{
    float resolution[2];  // x, y

    int32_t showGrid;     // 1 = show, 0 = hide
    int32_t selectedIndex;         // alignment padding

    float cameraPos[4];
    float cameraTarget[4];

    // Gizmo, drawn by the SDF fragment shader so it shares the exact same frame
    // (and camera) as the scene — no cross-surface lag. Origin and axis endpoints
    // are world-space; the shader projects them itself. The endpoints already sit
    // at the constant on-screen axis length, and they already point along the
    // handle directions for the active GizmoSpace — so the shader needs to know
    // nothing about world vs local.
    //
    // In rotate mode the same three endpoints do double duty: each ring's plane
    // normal is normalize(gizmoAxis* - gizmoOrigin) and its radius is the length
    // of that same vector. That is why adding rotate cost no new uniform data.
    float gizmoOrigin[4];  // xyz = selected object's world position
    float gizmoAxisX[4];   // xyz = world endpoint of the X axis handle / X ring
    float gizmoAxisY[4];   // xyz = world endpoint of the Y axis handle / Y ring
    float gizmoAxisZ[4];   // xyz = world endpoint of the Z axis handle / Z ring
    int32_t gizmoVisible;  // 1 = draw the gizmo
    int32_t gizmoHovered;  // hovered GizmoAxis (0 none, 1 X, 2 Y, 3 Z)
    int32_t gizmoActive;   // active/dragged GizmoAxis (0..3)
    int32_t gizmoMode;     // GizmoMode: 0 = translate (arrows), 1 = rotate (rings)

    // Lighting / render settings consumed by the SDF shader (assets/shaders).
    // One directional light plus material and shadow/AO "look" controls. The
    // three vec3 values are stored as vec4 (only .xyz is used) so the std140
    // uniform layout stays 16-byte aligned and matches the WGSL Globals struct
    // exactly. Defaults below reproduce the previously hardcoded look, so the
    // render is unchanged until the app writes user/AppSettings values here.
    float lightDirection[4] = { 0.5f, 0.8f, 0.6f, 0.0f };  // xyz = direction TO the light (world space); shader normalizes it
    float lightColor[4]     = { 1.0f, 1.0f, 1.0f, 0.0f };  // xyz = light RGB
    float ambientColor[4]   = { 0.12f, 0.12f, 0.12f, 0.0f };// xyz = ambient / fill-light RGB

    float lightIntensity    = 1.0f;   // scales the direct (diffuse + specular) light
    float specularShininess = 48.0f;  // Blinn-Phong exponent (higher = tighter, glossier highlight)
    float specularStrength  = 0.4f;   // specular highlight scale (reflectivity)
    float shadowSoftness    = 16.0f;  // soft-shadow k (higher = sharper shadows, lower = wider penumbra)

    float aoStrength        = 3.0f;   // ambient-occlusion strength (higher = darker creases)

    // Raymarch quality — let the user trade precision/range for speed by GPU power.
    // (hitEpsilon and maxSteps are coupled: a smaller epsilon usually needs a higher
    // step count to converge, or surfaces develop holes at grazing angles.)
    int32_t maxSteps    = 100;      // primary-ray iteration cap (higher = resolves distant/thin geometry, slower)
    float   hitEpsilon  = 0.001f;   // surface-hit threshold (smaller = sharper silhouettes; needs more maxSteps)
    float   maxDistance = 50.0f;    // far clip for the march (larger = see farther, wastes steps on empty space)

    // Shadow / AO quality.
    int32_t shadowMaxSteps    = 64;     // soft-shadow march iteration cap
    float   shadowMaxDistance = 30.0f;  // how far the shadow ray searches for occluders
    int32_t aoSamples         = 5;      // ambient-occlusion taps along the normal (keep >= 2)
    int32_t aaSamples         = 1;      // supersampling grid dimension N: fs_main fires NxN sub-pixel
                                        // rays and averages them (1 = off / one ray = original look).
                                        // Reuses a former pad slot, so the 240-byte layout is unchanged.

    // Feature toggles — let the user drop the two biggest optional per-pixel costs.
    // The shader guards the shadow/AO marches behind these uniform flags, so an
    // "off" flag skips the whole loop (a coherent branch: every pixel takes the
    // same path). New 16-byte block appended at the end so the existing 224-byte
    // layout is untouched. Buffer size = sizeof(GlobalsData), so this just grows it.
    int32_t shadowsEnabled = 1;   // 1 = cast soft shadows (calcSoftShadow), 0 = fully lit
    int32_t aoEnabled      = 1;   // 1 = ambient occlusion (calcAO), 0 = no occlusion
    int32_t qualityPad1    = 0;   // selection-visuals flag: 1 = suppress the object highlight
                                  // (used for the object-id pick pass); 0 = normal. See SetSelectionVisuals.
    float   gizmoPixelScale = 1.0f; // render-buffer px per display px (web Render Resolution scale);
                                    // scales the shader's gizmo line/dot thickness so the gizmo is a
                                    // constant on-screen size at any Render Resolution. Reuses the last
                                    // pad slot, so the 240-byte layout is unchanged.


    float jitterOffset[2] = {-0.25f, -0.25f};  // per-frame sub-pixel ray offset (px); set each frame in MainLoop
    int32_t taaEnabled = 1;   // 1 = temporal AA (accumulate up to kAccumLayers jittered samples); 0 = single sample
    int32_t accumCount = 1;   // how many accumulation layers hold samples of the CURRENT image, i.e. how many
                              // have been rendered since the last invalidation. fs_resolve averages layers
                              // [0, accumCount) — never a layer left over from a previous camera/scene state,
                              // which is what used to ghost. Layers are always filled from index 0 upward after
                              // an invalidation, so "valid" is a prefix. 1 = show just this frame. See CLAUDE.md 7a.
    float sceneBoundsMin[4] = {  1e30f,  1e30f,  1e30f, 0.0f };
    float sceneBoundsMax[4] = { -1e30f, -1e30f, -1e30f, 0.0f };

};
static_assert(sizeof(GlobalsData) == 288, "GlobalsData must stay in lockstep with the WGSL Globals struct.");
