# Performance Vulnerability Report — SDF Editor

> Symptom: FPS collapses past ~5 objects; ~10 objects makes the app unusable — even when
> objects are off-screen. This document ranks every performance vulnerability found in the
> engine, **most important first**, explains *why* each one costs what it costs (with the
> math), and lays out concrete solution options for each. No code was changed.

---

## 0. TL;DR — ranked list

| # | Vulnerability | Cost scaling | Expected win | Effort |
|---|---|---|---|---|
| P1 | Every object's exact SDF evaluated at **every march step of every ray** — no culling, no bounds | O(pixels × steps × objects) | **The fix for your symptom.** Near-flat cost in object count for typical scenes | Medium |
| P2 | Full raymarch re-rendered **every vsync, even when nothing changed** | 100% of GPU, 100% of the time | Idle GPU ≈ 0; editor feels instantly usable | Low |
| P3 | Shadow / AO / normal each re-run the full O(N) evaluator — secondary rays ~2–3× the primary cost | multiplies P1 | 2–3× during interaction | Low–Medium |
| P4 | No whole-scene bounds — background & grazing rays pay full march cost | ~30–60% of pixels are background | Big for sparse scenes | Low |
| P5 | Naive marcher: fixed `hitEpsilon`, no over-relaxation → more steps than necessary | multiplies everything | 20–50% fewer steps | Low–Medium |
| P6 | Per-evaluation constant math: quaternion rotate always runs, `sin/cos` per cone eval, etc. | per object per step | 10–30% of eval cost | Low |
| P7 | Register pressure: three 16-deep stacks in `evaluateScene` + 16-deep in `distanceCalc` may cap GPU occupancy for the whole shader | global multiplier | Unknown until profiled; possibly large | Medium |
| P8 | Interaction-time quality not auto-scaled (resolution/shadows stay at full while orbiting); SSAA multiplies everything ×N² | UX | Smooth camera at any scene size | Low |
| P9 | Architectural ceiling: re-evaluating the CSG per pixel per frame at all — vs. Dreams/Claybook "evaluate once per **edit**, march a cached field per frame" | O(1) in object count per frame | Hundreds of objects; the endgame | High |
| P10 | Minor: object-id target written every frame, un-cleared accum layers, `maxDistance` generous default | small | small | Trivial |

Recommended attack order: **P2 → P4 → P8 (days, makes the app usable) → P1 + P3 (the real fix) → P5/P6/P7 (polish) → P9 (long-term project)**.

---

## 1. Where the milliseconds go — the cost model

Sphere tracing cost is a product of four factors:

```
GPU work per frame ≈ Pixels × StepsPerRay × Objects × CostPerObjectEval
```

Current numbers, from the code:

- **Pixels** — full-screen quad at canvas backing-store size. At 1600×900 → **1.44M rays**
  ([fs_main](assets/shaders/main.wgsl#L155)). Render Resolution 50% divides this by 4.
- **StepsPerRay** — primary march budget `maxSteps = 100`
  ([Settings.h:37](src/editor/Settings.h#L37), loop at [main.wgsl:101](assets/shaders/main.wgsl#L101)).
  Real rays take ~20–100 steps; *grazing* rays (passing close to a surface) crawl toward the
  full 100 because the step size is the distance to the nearest surface.
  On top of the primary march, **every hit pixel** adds:
  - `calcNormal` — 4 more full-scene evaluations ([sdf_core.wgsl:232](assets/shaders/sdf_core.wgsl#L232))
  - `calcSoftShadow` — up to `shadowSteps = 64` more ([sdf_core.wgsl:246](assets/shaders/sdf_core.wgsl#L246))
  - `calcAO` — 5 more ([sdf_core.wgsl:272](assets/shaders/sdf_core.wgsl#L272))
  - `evaluateScene` — 1 more, the heavyweight color variant ([sdf_core.wgsl:289](assets/shaders/sdf_core.wgsl#L289))
- **Objects** — this is the vulnerability: `distanceCalc` walks the **entire RPN program**
  and evaluates **every primitive's exact SDF** at every call
  ([sdf_core.wgsl:157-230](assets/shaders/sdf_core.wgsl#L157-L230)). There is no
  bounding-volume early-out, no per-ray culling, nothing. An object behind the camera costs
  exactly as much as one filling the screen.
- **CostPerObjectEval** — `rotateInverse` (two cross products, always executed even for
  identity rotation, [sdf_core.wgsl:30](assets/shaders/sdf_core.wgsl#L30)) + the primitive
  SDF (some with transcendentals: `sdfCone` computes `sin`/`cos` **per evaluation**,
  [sdf_core.wgsl:70](assets/shaders/sdf_core.wgsl#L70)) + a storage-buffer read of the
  96 bytes of command+object data. Roughly 40–70 ALU ops.

**Worked example, 10 objects at 1600×900:** an average of ~100 `distanceCalc` calls per
pixel (march + shadow + normal + AO) × 10 objects × 1.44M pixels ≈ **1.4 billion primitive
SDF evaluations per frame** — order 100 GFLOPs plus memory traffic, *per frame*, ×60 for
60fps. That saturates even a good discrete GPU; on an integrated GPU it is seconds-per-frame
territory.

**Why 5 → 10 objects feels like a cliff, not 2× slower:** the per-step cost is linear in N,
but (a) more objects → a "busier" distance field → shorter safe steps → *more steps too*,
so total cost grows superlinearly; (b) once frame time crosses 16.6ms, vsync quantizes you
down 60 → 30 → 20 → 15; (c) shadow rays hit the same superlinear curve. Linear cause,
nonlinear symptom.

**And the key insight for the fix:** your instinct is exactly right — the millions of
evaluations are spent on objects that *cannot possibly matter* for the ray being marched.
For any point `p`, an object whose bounding sphere is 20 units away contributes a distance
≥ 20 to the `min()` — computing its exact torus/cone/link SDF to learn "it's far" is pure
waste. Everything in P1 is about exploiting that.

### How to confirm the bottleneck before optimizing

- FPS recovers when you shrink the window or set Render Resolution 50% → **GPU-bound,
  fragment-limited** (this is almost certainly the case).
- Set `maxSteps` to 10 in the Advanced panel: if FPS jumps, the march dominates. Toggle
  Shadows off: the delta is the shadow march share. Same for AO.
- For real numbers, use the **native build** (wgpu-native) under RenderDoc / Nsight /
  Radeon GPU Profiler — browser WebGPU exposes no GPU timers to you. Keep a small
  benchmark scene (e.g. 12 objects, fixed camera) to compare before/after in ms, not FPS.

---

## P1 — No spatial culling: every object, every step, every ray  🔴 the scaling wall

**Where:** [distanceCalc](assets/shaders/sdf_core.wgsl#L157) and
[evaluateScene](assets/shaders/sdf_core.wgsl#L289) — the `for` loop over the whole
`csg_program`, executing every `PUSH_OBJECT` with the exact SDF.

**Why it's the #1 issue:** it makes per-frame cost proportional to *total scene size*
instead of *what the ray can actually see*. Every other fix is a constant-factor
improvement; this one changes the complexity class for real scenes.

**Important context:** CLAUDE.md records that a bounding-sphere skip was tried before and
**reverted because it clipped geometry**. That attempt failed on correctness, not on the
idea — the idea is standard practice (IQ has an article on exactly this; Dreams, Claybook
and the 2025 "Synchronized Tracing" paper are all built on pruning). Below is the math that
makes it safe, which is worth understanding before re-attempting.

### The safety math (why the old attempt clipped, and how not to)

Sphere tracing is only correct if the distance function **never overestimates** the true
distance (an overestimate → step too far → tunnel through a surface = clipped geometry).
Underestimates are always safe — they just cost extra steps.

**Enclosure lemma.** If object `O` fits entirely inside a ball `B(c, r)`, then for every
point `p`: `dist_O(p) ≥ dist_B(p) = |p − c| − r`. So replacing an object's exact SDF with
its *enclosing-sphere* distance is a conservative **underestimate** — safe by construction.

How that interacts with each CSG op:

- **Union `min(a,b)`** and **Intersection `max(a,b)`**: replacing either operand with an
  underestimate keeps the result an underestimate. Safe, unconditionally.
- **Subtraction `max(a, −b)`** is the trap: negating flips an underestimate of `b` into an
  **overestimate** of `−b`. If the subtrahend's proxy is used near/inside the carved
  region, `max` can overestimate → the marcher steps through the carved surface. This is
  very likely what clipped last time (along with any under-covering radii).
- **Smooth ops** blend whenever `|a − b| < k`, and smooth union bulges *outward* by up to
  `k/4` — a proxy used inside the blend zone distorts the surface.

**The rule that makes it safe:** substitute the proxy **only when the point is farther
than a margin δ from the bound**, i.e. `if (distToBound > δ) use proxy else evaluate exact`,
with `δ ≥ max(largest smooth-blend k on the node's path to the root, hitEpsilon) + slack`.
Outside δ, subtraction and smooth blending mathematically cannot be influenced by the exact
value (for subtract: `−b < −δ < 0`, so it only wins `max` at points already deep inside the
result — points the marcher never visits from outside; for smooth: the blend zone requires
`|a−b| < k < δ`). A single global `δ = maxBlendRadiusInScene + ε` is the simplest correct
choice; per-node δ is tighter.

**The bounds must truly enclose.** Second suspected cause of the old clipping. The sphere
must be computed per primitive type from the *actual* size fields — centered at
`position.xyz` it is rotation-invariant, so rotation is free. Enclosing radii for the
current types ([the shader switch](assets/shaders/sdf_core.wgsl#L177-L189)):

| Type | size meaning | enclosing radius (add ~5% slack) |
|---|---|---|
| Sphere | x = r | `s.x` |
| Box (rounded, bounds-preserving) | half-extents | `length(s.xyz)` — roundness stays inside, conveniently |
| Cylinder | r, h | `length(vec2(s.x, 0.5·s.y))` |
| Torus | R, r | `s.x + s.y` |
| Link | r1, le, r2 | `s.y + s.x + s.z` |
| Cone | h, angle | `√(h² + (h·tan θ)²)` |
| Capsule | r, halfH | `s.x + s.y` |
| SolidAngle | ra, angle | `s.x` |
| Pyramid | halfBase, h | `√(2·s.x² + s.y²)` |

Compute these **on the CPU** whenever the scene buffer is refreshed (in
`SceneSerializer::RefreshSceneBuffer`) and store the radius in the unused
[`position.w`](src/scene/data/SdfObjectData.h#L20) — zero struct growth, zero new
bindings, the shader just reads `obj.position.w`.

### Solution options (they compose; recommended order a → b → c)

**a) In-evaluator proxy substitution** (smallest change, always-on):
in the `PUSH_OBJECT` case, first compute `dBound = length(p − center) − radius`; if
`dBound > δ`, push `dBound` and skip the exact SDF + `rotateInverse` entirely. Cost when
far: one `length` instead of the full eval. Objects behind the camera or across the scene
collapse to ~6 ops each. Expected: large win the moment objects don't all overlap on
screen; degrades gracefully (never wrong, per the math above). One caveat: proxies are
*underestimates*, so rays passing near a bound take slightly more steps — the slack margin
keeps this negligible.

**b) Per-ray pruning (the direct fix for "off-screen objects still cost"):**
once per pixel, before the march, intersect the ray with each object's bounding sphere
**inflated by δ**. Objects the ray misses entirely can be *fully skipped* for that ray:
their `PUSH` pushes a large constant (+∞ stand-in). This is correct because a ray that
never enters the inflated bound can neither hit the object nor see any surface the object
influences (carves/blends) — all of that lies inside the inflated bound. Store the result
as a **u32 bitmask** (fine up to 32 objects; two u32s for 64). The march loop then costs
O(objects-the-ray-can-see) — for a camera looking at 2 of 10 objects, most pixels evaluate
1–2 SDFs instead of 10. Off-screen and behind-camera objects: **zero per-step cost**.
Nuance: the mask is only valid for *that ray* — shadow rays (different origin/direction)
and normal/AO taps (slightly off-ray; covered by δ-slack) should either recompute a mask
or just fall back to option (a)'s substitution, which needs no mask.

**c) Screen-tile culling prepass** (the "engine-grade" version of b):
a tiny compute pass bins objects into 8×8 or 16×16 pixel tiles (frustum-vs-sphere per
tile), writing one bitmask per tile to a storage buffer; `fs_main` reads its tile's mask.
Amortizes the per-pixel sphere tests and also lets you *sort* per tile. Same idea as tiled
deferred lighting. Do this only if (b)'s per-pixel tests show up in profiles.

**d) Hierarchical pruning via the CSG tree** (scales to big scenes):
compute bounds **per CSG node** bottom-up (union/group = enclosing sphere of children + k;
intersection = smaller child's bound; subtract = base child's bound). Then a whole group
that's far away is one sphere test instead of N. This matches the existing tree structure
and is what the literature does — see *Synchronized Tracing of Primitive-based Implicit
Volumes* (ACM TOG 2025), which gets order-of-magnitude wins from exactly this bottom-up
tree pruning. Requires emitting node bounds alongside the RPN program (e.g. a second small
storage buffer indexed by `nodeId`, or extra commands that "skip the next M commands if
the bound test fails" — a `JUMP_IF_FAR` command fits the existing interpreter cleanly).

---

## P2 — Full re-render every vsync, even when nothing changes  🔴 biggest win-per-effort

**Where:** [main.cpp:19](src/main.cpp#L19) → `SDL_AppIterate` calls
[Application::MainLoop](src/editor/Application.cpp#L54) unconditionally; `MainLoop` always
encodes both passes and redraws. There is no dirty flag anywhere in the frame path.

**Why it matters:** this is an *editor*. The user spends most of their time looking at a
static scene with a static camera — and the engine re-marches every pixel 60×/sec anyway,
producing an identical image (after the 4 TAA frames converge, *bit-identical*). All that
sustained GPU load also throttles the very interactions you care about (the browser
compositor, React, and the raymarch fight for the same GPU), and it burns
laptop battery for nothing.

**Solution — render-on-demand:** keep a `framesToRender` counter in `Application`. Set it
to 4 (one full TAA cycle) from every place that changes what's on screen — `OnSceneChanged`,
`OnCsgChanged`, camera movement (`UpdateCamera` already returns `isCameraMoving`),
gizmo hover/drag state changes, selection changes, render-setting writes, canvas resize.
In `MainLoop`: if the counter is 0, skip encoding entirely (just return); else decrement
and draw. Details to get right:

- The object-id target must stay valid for picking — it does, since you simply keep the
  last-rendered one (don't clear it when skipping).
- Continuous-input paths (orbit drag, gizmo drag) refresh the counter every event, so
  interaction is indistinguishable from today.
- The FPS meter will read the compositor's rAF, not engine frames — either freeze it or
  show "idle".

**Expected result:** idle GPU cost drops to ~0 regardless of scene size. A 10-object scene
is instantly "usable" whenever you're not actively moving — which is most of the time. This
does *not* fix orbit-drag framerate (P1/P3/P8 do), but it transforms the felt experience
for a day of work.

---

## P3 — Secondary rays multiply everything: shadows, AO, normals

**Where:** on every hit pixel: `calcSoftShadow` marches up to **64 steps**
([sdf_core.wgsl:255](assets/shaders/sdf_core.wgsl#L255)), `calcNormal` adds 4 full-scene
evals, `calcAO` adds 5 — each one a full O(N) `distanceCalc`. With defaults, a hit pixel's
secondary work (≈73 evals) usually **exceeds its primary march**.

The existing guards are good (shadows skipped at zero light intensity, AO toggleable,
coherent branches) — the issue is that when enabled, they pay the same unculled O(N) price,
and they're enabled by default.

**Solutions:**

1. **P1 fixes these for free** — proxy substitution (option a) needs no per-ray mask and
   drops straight into the shadow/AO/normal calls. Do nothing extra.
2. **Interactive degradation:** while the camera moves or a gizmo drag is active, force
   `shadow = 1.0`, `ao = 1.0` (skip both marches) and restore on release — the TAA
   disable-while-moving precedent ([Application.cpp:84-87](src/editor/Application.cpp#L84-L87))
   already establishes this exact pattern. Shadows popping in on mouse-up is a completely
   standard editor behavior (Blender does it).
3. **Cheaper budgets:** the shadow march can use a coarser epsilon and `shadowSteps = 32`
   with little visible change; AO at 3 taps is nearly indistinguishable. These are already
   user-facing sliders — consider just lowering the *defaults* and letting Ultra restore them.
4. **Normals for ~free:** switch `calcNormal` from 4 taps to forward differences (3 taps)
   — 25% off the normal cost, slight quality loss at silhouettes; or keep 4-tap
   (tetrahedron is better) and rely on culling. Low priority next to 1–2.

---

## P4 — No whole-scene bound: background and grazing rays pay full price

**Where:** the march ([main.wgsl:101-111](assets/shaders/main.wgsl#L101-L111)) starts at
`t = 0` and walks until `t > maxDistance (50)` for every pixel, including the typically
30–60% of pixels that hit nothing. A background pixel's step size equals the distance to
the nearest object — a few units in a typical scene — so a miss ray still takes ~15–40
full O(N) steps just to escape to `t = 50`.

**Solutions (cheap, land together):**

1. **Scene AABB ray clip.** CPU computes the whole scene's bounding box (union of the P1
   per-object spheres, +δ) each `RefreshSceneBuffer`, uploads it in two free `globals`
   slots (needs one new 16-byte block, or reuse `jitterPad0/1` for a bounding-sphere
   center-packed form). In the shader, do an analytic ray-vs-AABB (slab test, ~10 ops):
   miss → background color immediately, **0 march steps**; hit → start the march at
   `t = tEnter` and stop at `t = tExit`. Empty space before and after the scene becomes
   free. This helps *every* pixel of *every* scene and is ~20 lines of WGSL.
2. **Tighter `maxDistance` default** — 50 units is generous for the scenes this editor
   produces; the AABB clip makes this moot, otherwise 30 is safer.
3. (After P1's per-ray mask: "mask == 0" is the same early-out for free.)

---

## P5 — Naive marching: fixed epsilon, unit relaxation

**Where:** the march uses a constant `hitEpsilon = 0.001` at any distance and steps by
exactly the SDF value ([main.wgsl:105-110](assets/shaders/main.wgsl#L105-L110)).

Two well-studied upgrades, both multiplicative with everything above:

1. **Distance-proportional epsilon ("cone" epsilon).** A pixel at distance `t` covers a
   world-space footprint of `t × (2/resolution.y) / focalLength`. Using
   `eps = hitEpsilon × max(1, t·pixelScale)` stops the marcher as soon as the surface is
   sub-pixel — exactly what grazing rays need (they currently crawl at 0.001 precision for
   surfaces 40 units away that occupy half a pixel). Typically 20–40% fewer steps with zero
   visible difference, *and* it reduces the far-surface shimmer TAA has to clean up.
2. **Over-relaxation (Enhanced Sphere Tracing, Keinert et al. 2014).** Step by `ω·d` with
   `ω ≈ 1.2–1.6`; if the next sample shows the step was unsafe (`d_next < (ω−1)·d`), fall
   back and re-step conservatively. ~20–30% fewer steps on typical scenes, fully safe with
   the fallback. Segment tracing (Galin et al. 2020) is the stronger academic version —
   big step reductions, but it needs per-primitive local Lipschitz machinery; not worth it
   here until P1/P9 are done.

---

## P6 — Per-evaluation constant recomputation

Small individually, but they sit inside the innermost loop that runs ~10⁹ times/frame:

1. **`rotateInverse` always runs** ([sdf_core.wgsl:175](assets/shaders/sdf_core.wgsl#L175)),
   even for identity rotation (the default for every new object). Two cross products × 2
   evaluators × every step. Fix: CPU sets a flag (spare int: `parentId` is reserved, or
   sign-pack into `position.w` alongside the P1 radius); shader branches — uniform per
   object, so no divergence cost. Or better: skip is subsumed by P1's proxy path for far
   objects; for near ones it's still ~15% of the eval.
2. **`sdfCone` computes `sin(angle)`/`cos(angle)` every evaluation**
   ([sdf_core.wgsl:70](assets/shaders/sdf_core.wgsl#L70)); same pattern in `sdfSolidAngle`.
   These are constants of the object — precompute on CPU into the free `size.w` (store
   `tan`- or `sin/cos`-packed) and pass down. Transcendentals are 4–16× the cost of a mul.
3. **`sdfPyramid`'s `sqrt(h² + hb²)`** and capsule endpoint construction — same treatment.
4. **Storage-buffer traffic:** every eval re-reads 16B of command + 80B of object per
   object per step. It caches well (uniform across the warp), but the interpreter loop
   also re-reads `arrayLength` and re-decodes per step. Minor; only worth touching if a
   profile says so (e.g. copy the program into a fixed-size uniform array when small).

---

## P7 — Register pressure from the RPN stacks (needs profiling, potentially big)

**Where:** `distanceCalc` holds `array<f32, 16>`; `evaluateScene` holds **three** parallel
16-deep stacks (`f32` + `vec3f` + `u32` ≈ 80 scalars)
([sdf_core.wgsl:301-303](assets/shaders/sdf_core.wgsl#L301-L303)). Dynamically-indexed
local arrays usually live in registers only if the compiler can promote them; otherwise
they spill to scratch memory (slow), and either way the **peak** register demand of the
whole fragment shader sets its occupancy — fewer resident warps → the GPU can't hide the
memory latency of the storage-buffer reads → everything slows down, even the parts that
don't touch the stacks.

**Solutions:**

1. **Shrink the stacks.** Depth 16 supports 16-deep *nesting*, which the UI can't
   realistically produce (depth grows with nesting, not object count — a flat union of 100
   objects needs depth 2). Depth **8** is still generous and halves the pressure. Keep the
   existing overflow guards; optionally have `CompileToRPN` compute the actual required
   depth and warn.
2. **Pack `evaluateScene`'s color stack**: color as one packed u32 (`pack4x8unorm`) instead
   of `vec3f` — 3 stacks × 16 → effectively half the scalars. `evaluateScene` runs once
   per pixel, so its *runtime* is irrelevant — but its *register footprint* is charged to
   the whole shader.
3. **Verify before investing:** on the native build, RenderDoc/Nsight will show occupancy
   and scratch usage. If occupancy is already fine, skip this entirely.

---

## P8 — Interaction-time quality doesn't scale down

The engine already disables TAA while the camera moves — but TAA is nearly free either
way; the *expensive* things (resolution, shadows, AO) stay at full quality during the
exact moments (orbiting, dragging) when framerate matters most and image quality matters
least.

**Solutions (all web-side or trivial engine flags, riding existing chains):**

1. **Dynamic resolution during interaction:** while orbit/pan/gizmo drag is active, drop
   `renderScale` one notch (100 → 50%: 4× fewer rays), restore on release. All plumbing
   exists (`syncCanvasSize`, `setGizmoPixelScale`); TAA-off-while-moving already hides the
   quality dip. This alone makes a 10-object orbit smooth even before P1 lands.
2. **Shadows/AO off during interaction** — P3 option 2, same trigger.
3. **SSAA warning:** `aaSamples = N` runs `shadeRay` N² times per pixel
   ([main.wgsl:183-189](assets/shaders/main.wgsl#L183-L189)) — at N=2 that's 4× the *entire*
   cost stack. With TAA providing 4× accumulation for free on static frames, SSAA is nearly
   redundant; consider capping it or excluding it from any preset.

---

## P9 — The architectural ceiling, and what Dreams/Claybook actually do

Everything above optimizes the current architecture: **re-evaluate the analytic CSG at
every sample of every frame**. That architecture has a hard ceiling — cost forever tied to
scene complexity — and the two famous SDF games both escaped it the same way:

> **Evaluate the CSG once per *edit* into a cached spatial structure; per *frame*, march
> the cache — O(1) in object count.**

- **Dreams (Media Molecule)** — Alex Evans' SIGGRAPH 2015 talk *Learning from Failure*
  (your YouTube link is this talk). The edit history is a CSG list ("edit list") that a
  GPU **evaluator** (a pipeline of ~40 compute shaders) bakes into **sparse SDF bricks**
  (~8³ voxel blocks around surfaces only), incrementally — only bricks touched by an edit
  re-evaluate. The renderer never sees the CSG at all; shipping Dreams rasterizes dense
  **point clouds/splats** generated from the bricks (they abandoned per-pixel raymarching
  of bricks after trying it). Result: thousands of primitives with per-frame cost
  independent of the edit count.
- **Claybook (Second Order)** — Sebastian Aaltonen's GDC 2018 talk. Bakes everything into
  a **global distance field volume texture** (plus per-object SDF volumes), updated
  incrementally on change, then sphere-traces the *texture* — each step is one trilinear
  fetch regardless of scene complexity. Hits 60fps on a base PS4 with fully deformable
  worlds, ray-traced shadows and AO against the same volume.

**What this means for this editor (staged, don't jump to the end):**

1. **Hybrid accelerator (recommended eventual target):** keep the analytic evaluators, add
   a coarse **global distance volume** (e.g. 128³ R16F ≈ 4MB) re-baked by a compute pass on
   `OnSceneChanged` (the bake itself is object-count-dependent but runs *once per edit*,
   not per pixel — and can be brick-sparse later). Per frame: march the volume while far
   (one texture fetch per step, minus a conservative error margin of ~1 voxel diagonal to
   stay an underestimate), switch to the exact analytic SDF only within a few voxels of the
   surface for crisp hits/normals. Shadows and AO can sample the volume *only* — they don't
   need crisp. This keeps exact silhouettes, kills the O(N) inner loop, and reuses the
   existing pipeline layout (one extra binding + one compute pipeline).
2. **Full brick/point pipeline (Dreams-style):** sparse bricks + incremental re-eval +
   splatting. A multi-month research project — only worth it if the editor's ambition is
   Dreams-scale sculpting with thousands of strokes. The CSG-as-edit-list design of this
   engine is actually well-shaped for it, but do P1–P8 first; they may be all you ever need
   at "dozens of objects" scale.

---

## P10 — Minor items (log for completeness)

1. **Object-id target rendered every frame** ([Application.cpp:118-131](src/editor/Application.cpp#L118-L131))
   though it's only read on click — ~4 bytes/px of write bandwidth. Could be a separate
   on-demand pass, but it shares the expensive march, so splitting would *double* march
   cost on pick frames — leave as-is; P2 reduces it naturally.
2. **Un-cleared accumulation layers** flash for ~3 frames after load/resize (documented).
   Clear once on creation; irrelevant to steady-state perf.
3. **`maxDistance = 50`** default is generous — subsumed by P4.
4. **FPS meter rAF loop** — negligible, and it's the only per-frame JS. CPU side is clean:
   steady-state per-frame upload is a single 256-byte uniform write; React never renders
   per frame. The bottleneck is entirely GPU-side — which is good news: it's all fixable
   in the shader + a few CPU-side precomputations.

---

## Suggested roadmap

| Phase | Items | Outcome |
|---|---|---|
| 1 — days | P2 (render-on-demand), P4 (scene AABB clip), P8.1 (dynamic res on interaction), P3.2/3.3 (interaction degradation, cheaper defaults) | 10-object scenes usable today; idle cost ≈ 0 |
| 2 — the real fix | P1a (bounding radii in `position.w` + proxy substitution with δ-margin), then P1b (per-ray bitmask pruning) | Cost tracks *visible* objects; off-screen objects ≈ free; the 5→10 cliff gone |
| 3 — polish | P5 (adaptive epsilon + over-relaxation), P6 (precompute constants), P7 (stack/occupancy, after profiling on native) | 1.5–2.5× on top |
| 4 — endgame | P9.1 (global distance volume hybrid), optionally P1d (hierarchical node bounds) first | Hundreds of objects, Dreams-direction architecture |

A note on process: land **one phase at a time and measure** (fixed benchmark scene,
frame-time in ms via the native build or A/B toggles in the Advanced panel). P1's proxy
substitution in particular should ship behind a render-setting toggle at first, so any
reappearance of the old clipping bug is one click to isolate — and verify it against
rotated/scaled primitives, subtract nodes, and large smooth-blend radii, which are exactly
the cases that sank the previous attempt.

## References

- Alex Evans — *Learning from Failure: …Renderers for 'Dreams PS4'*, SIGGRAPH 2015 — [slides (PDF)](https://advances.realtimerendering.com/s2015/AlexEvans_SIGGRAPH-2015-sml.pdf) · [Umbra Ignite video announcement](https://www.mediamolecule.com/blog/article/alex_at_umbra_ignite_2015_learning_from_failure_video) · [detailed notes](https://cheneyshen.com/siggraph-15-learning-from-failure-a-survey-of-promising-unconventional-and-mostly-abandoned-renderers-for-dreams-ps4-a-geometrically-dense-painterly-ugc-game/)
- Sebastian Aaltonen — *GPU-based Clay Simulation and Ray-Tracing Tech in Claybook*, GDC 2018 — [slides (PDF)](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf) · [talk video](https://www.youtube.com/watch?v=Xpf7Ua3UqOA)
- Inigo Quilez — [*Simple Bounding Volumes for raymarching*](https://iquilezles.org/articles/sdfbounding/) (the proxy-substitution technique, from the field's primary practitioner)
- [*Synchronized Tracing of Primitive-based Implicit Volumes*](https://dl.acm.org/doi/full/10.1145/3702227), ACM TOG 2025 — bottom-up CSG tree pruning + tight bounding volumes for exactly this primitive-list problem
- Keinert et al. — [*Enhanced Sphere Tracing*](https://www.researchgate.net/publication/329152815_Accelerating_Sphere_Tracing) (over-relaxation); Galin et al. — *Segment Tracing Using Local Lipschitz Bounds*, Eurographics 2020
- [*Ray Tracing of Signed Distance Function Grids*](https://jcgt.org/published/0011/03/06/paper-lowres.pdf), JCGT 2022 — marching baked SDF volumes (the P9 hybrid)
