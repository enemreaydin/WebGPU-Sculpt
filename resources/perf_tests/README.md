# Performance benchmark scenes

Eight `.sdfp` files, each isolating one optimization from
[perf_vuln.md](../perf_vuln.md), plus one all-in stress scene. Open them via
**File → Open** in the web app. Regenerate any time with
`python generate_perf_tests.py` (emsdk's bundled Python works:
`deps/emsdk/python/3.13.3_64bit/python.exe`).

All scenes are authored for the **default camera** (orbit target at the origin,
distance 8). Load the file and **do not move the camera** while reading the FPS
number — "off-screen" placement is computed against that exact view.
Do **not** use frame-all/fit-view on B or H (it would zoom out until the
off-screen objects are visible, defeating the test).

## The files

| File | Objects | Isolates | What to expect after optimizing |
|---|---|---|---|
| `A_baseline_5.sdfp` | 5 | **Reference anchor.** Small mixed-type scene, flat union. | Both before and after, this is your "best case" number. Every other file is compared against it. |
| `B_offscreen_25.sdfp` | 25 | **P1b per-ray / frustum culling.** The *same* 5 visible objects as A + 20 objects behind the camera and far outside the view (placement verified analytically against the shader's projection). | **Before:** much slower than A despite showing the identical image. **After P1:** FPS ≈ A. This is the single clearest pass/fail test in the suite. |
| `C_spread_16.sdfp` | 16 | **P1a bounding-proxy substitution.** A 4×4 grid of small objects, all visible but far apart — every march point is near at most 1–2 objects. | **Before:** ~16× per-step cost everywhere. **After P1a:** most evaluations collapse to one `length()`; FPS should approach A. |
| `D_cluster_12.sdfp` | 12 | **Worst case for culling** — everything overlaps: an 8-primitive smooth-union blob (k=0.35) + smooth subtract + rotated boxes. Culling *can't* help much here; this measures P5 (marcher efficiency) and P6 (per-eval micro-opts) honestly. | Modest gains only. If D barely improves while B/C soar, the optimizations are working exactly as designed. |
| `E_background_10.sdfp` | 10 | **P4 scene-AABB / background early-out.** A tiny cluster; ~80% of pixels hit nothing but still pay the full march today. | **After P4:** background pixels become nearly free → large FPS jump on this file specifically. |
| `F_shadow_12.sdfp` | 12 | **P3 shadow cost.** SDF floor slab + floaters positioned against the default light direction (0.5, 0.8, 0.6) to maximize soft-shadow penumbra marching. | Measure with Shadows ON vs OFF (Properties → Render) before and after — the ON/OFF gap is the shadow share, and P1/P3 should shrink it dramatically. |
| `G_correctness_17.sdfp` | 17 | **Correctness guard, not a speed test.** Every clipping trap that sank the previous culling attempt: rotated primitives of *every* exotic type, hard + smooth subtract, intersection, a k=0.6 smooth chain, roundness modifier, ~6-deep nesting (also guards any RPN-stack shrink). | The image must look **pixel-identical** before and after every optimization. Any missing/eaten geometry here = a conservative-bound bug. Keep a screenshot as reference. |
| `H_ultra_48.sdfp` | 48 | **Everything at once:** smooth blob + subtract, spread grid, floor + shadow floaters, rotated exotics in a group, 16 off-screen objects. | Your headline before/after number. Expect a slideshow today; after Phases 1–2 of the roadmap it should be interactive. |

## Measurement procedure

1. Same machine, same browser, laptop plugged in, same window size every run
   (write the size down — pixel count is the biggest cost factor).
2. Properties → Render: defaults (Balanced-equivalent), Render Resolution
   **100%**, TAA on, Shadows on, AO on — unless the row above says otherwise.
3. Load the file, don't touch anything, wait ~3 s for the FPS meter (top-right)
   to settle, record the number.
4. For interaction feel, also record FPS *while slowly orbiting* — but the
   comparable number is the static one.
5. Record results in the table below (copy per machine).

| Scene | FPS before | FPS after | Notes | Render time before
|---|---|---|---|
| A_baseline_5 | 30 | | | 
| B_offscreen_25 | 6 | | should match A after P1 |
| C_spread_16 | 10 | | |
| D_cluster_12 | 12 | | expected to move least |
| E_background_10 | 26 | | |
| F_shadow_12 (shadows on) | 8 | | |
| F_shadow_12 (shadows off) | 11 | | |
| G_correctness_17 | — | — | image identical? Y/N |
| H_ultra_48 | 1 | | headline number |

## Uncapping the 60 FPS limit

The meter reads `requestAnimationFrame`, which the browser locks to the display
refresh rate — so once a scene is fast, it flatlines at 60 and you can't see
further gains. No code change is needed to fix this: launch the browser with
vsync and the frame-rate limiter disabled. Close **all** Chrome/Edge windows
first (the flags only apply to a fresh process), then:

```
# Chrome
"C:\Program Files\Google\Chrome\Application\chrome.exe" --disable-gpu-vsync --disable-frame-rate-limit

# Edge
"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --disable-gpu-vsync --disable-frame-rate-limit
```

The existing FPS meter then shows the true GPU rate (hundreds of FPS on light
scenes). Expect fan noise and screen tearing — that's normal; use it only for
measuring. If the number still pins at 60, a leftover browser process kept the
old flags — check Task Manager. (Tip: add `--user-data-dir=%TEMP%\bench-profile`
to launch an isolated instance without closing your main browser.)

With vsync disabled, scene **A** gives you the uncapped baseline, and
`fps_A / fps_X` is a clean cost ratio between any two scenes — more informative
than capped numbers. If you prefer thinking in frame time: `ms = 1000 / fps`;
optimizations should be judged by ms saved, not FPS gained (going 30→60 FPS
saves 16.7 ms; 240→300 FPS saves only 0.8 ms).

## What "success" looks like, per roadmap phase

- **Phase 1** (render-on-demand, AABB clip, dynamic res): E jumps; everything
  idles at ~0% GPU when the camera is still (watch Task Manager → GPU).
- **Phase 2** (P1 culling): B ≈ A, C ≈ A, H becomes interactive, **G unchanged
  visually** — check G after *every* culling change, it exists to catch the
  regression that forced the last revert.
- **Phase 3** (marcher + micro): D finally moves; A/uncapped climbs further.
