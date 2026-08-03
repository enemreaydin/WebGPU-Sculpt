# Performance Vulnerability Report — SDF Editor

> Symptom: FPS collapses past ~5 objects; ~10 objects makes the app unusable — even when
> objects are off-screen. This document ranks every performance vulnerability found in the
> engine, **most important first**, explains *why* each one costs what it costs (with the
> math), and lays out concrete solution options for each. No code was changed.

---

## 0. TL;DR — ranked list

> 🔴 **Read this before picking an item — the ranking below was rebuilt after P7.**
>
> The shader is **memory-latency-bound and occupancy-limited, not ALU-bound.** Proven by
> elimination (`maxSteps` ×4 changed nothing; δ→0.01 corrupted shapes without changing
> timing; P1a removed a lot of arithmetic for 12%), then confirmed when a one-integer change
> that removes *no* work gave **2.1× on the whole suite**.
>
> So the old ranking — which sorted by "how much arithmetic does this remove" — was sorting
> on the wrong axis. The list below is re-sorted by **which bottleneck the item attacks**:
>
> ```
>  cost  =  Pixels × StepsPerRay × Objects × CostPerEval
>                                                          ← the visible product
>        ÷  Concurrency (resident warps)
>                                                          ← the invisible divisor
> ```
>
> Occupancy is a *fifth factor that appears nowhere in the source* and multiplies everything.
> Two consequences that reorder the whole document:
> 1. **Pure-ALU wins are discounted.** P5 and P6 remove arithmetic on a shader that is not
>    short of arithmetic.
> 2. **Anything that adds per-thread state now has a cost on the other side of its ledger.**
>    P1b's `vec4<u32>` mask is 4 more registers carried through four evaluators — spending the
>    exact resource proven to be binding.
>
> Measured run-to-run noise on the suite is **±6–10%**. Anything claiming under ~15% needs
> repeated runs before it is believed — retroactively, that is why P1a's 12% read as ambiguous.

### Tier 1 — attack the latency directly (start here)

These target the *proven* bottleneck: the serialized memory chain in the hot loop, and the
per-thread state that stops the GPU from hiding it.

> 🔴 **Read this before P14 or P13 — the valley is a trap, and it has now sprung twice.**
>
> P7 established that cost is non-monotonic in the stacks' footprint: below the notch the
> compiler **scalarizes** (registers eaten, occupancy collapses), above it **spills to scratch**
> (bigger allocation out of a fixed per-SM pool). Depth 8 is where neither binds.
>
> **P15 was implemented and reverted because it made things much worse** (§P15). It removed
> ~22 scalars from the peak — exactly what it claimed — and the most likely reason that hurt is
> that shrinking the arrays by ~40% flipped the compiler's spill-vs-scalarize decision, putting
> depth 8 **packed** where depth 4 **unpacked** used to sit.
>
> The transferable warning: **the depth-8 constant is not independent of everything else in the
> shader. It is the optimum for the footprint the shader had when it was measured.** Any change
> that alters per-thread state — P15's packing, **P14's removal of ~8 live registers from
> `distanceCalc`'s scope**, P13's wider command struct, P1b's `vec4<u32>` mask — moves the notch
> as a side effect. So for each of them, "did it help?" and "did it help *at depth 8*?" are
> different questions, and a bad result must be re-tested at a neighbouring bucket before it is
> believed. A one-build check (set `kCsgStackDepthBuckets` to a single entry `{16}`, or `{4}`)
> distinguishes "this change is bad" from "this change moved the optimum."

| # | Vulnerability | Bottleneck attacked | Expected win | Effort |
|---|---|---|---|---|
| **P13** | 🔴 **The dependent load.** `scene_objects[cmd.operand]` cannot issue until `csg_program[i]` returns — a two-deep pointer chase per object per march step, unhideable *within* a thread | Latency (the root cause) | Fusing geometry into the command makes it one sequential prefetchable stream; removes the chain outright | **Medium** |
| **P14** | The 80-byte object struct is **45% dead weight in `distanceCalc`** (`color`, `selected`, ids, pad = 36 B never read there) and holds 20 live registers across the whole SDF switch | Latency + occupancy | Hot/cold split to a 48-byte geometry buffer: fewer bytes, 8 fewer live registers. Renderer-side only — no data-model or `.sdfp` change | **Medium** |

### Tier 2 — reduce the number of loop iterations (the structural ceiling)

Tier 1 makes each iteration cheaper. These reduce how many there are — the only thing that
changes the complexity class, and the only thing that survives a 5,000-object target.

| # | Vulnerability | Bottleneck attacked | Expected win | Effort |
|---|---|---|---|---|
| **P1d** | The RPN program is **flat**, so `distanceCalc` walks every command on every step even for subtrees the ray is nowhere near. At 5k objects this alone is ~10⁴ iterations × ~10² steps × 1.4M pixels — the walk dominates before a single SDF runs | Objects (iteration count) | `SKIP_IF_FAR` + bottom-up node bounds makes the walk sublinear. **The only item that scales to thousands** | Medium |
| P1b | No per-ray culling: every object evaluated on every ray | Objects | Real, but now discounted — the `vec4<u32>` mask spends registers, and it does not reduce the *walk* | Medium |
| P11 | No per-pixel ray interval — `tEnter` comes from one coarse whole-scene box | StepsPerRay | Tighter start than P4 on scattered scenes; the rasterized version scales far better than P1b's per-ray loop at high N | Medium |
| P3 | Shadow / AO / normal each re-run the full O(N) evaluator — on a hit pixel, usually **more** work than the primary march | Objects × Steps | 2–3× during interaction; P1d and P14 both reach it for free | Low–Medium |

### Tier 3 — fewer pixels, fewer steps, cheaper steps

| # | Vulnerability | Bottleneck attacked | Expected win | Effort |
|---|---|---|---|---|
| P8 | Interaction-time quality not auto-scaled (resolution/shadows stay at full while orbiting); SSAA multiplies everything ×N² | Pixels | Smooth camera at any scene size. **Best UX-per-hour in the document** | Low |
| P5 | Naive marcher: fixed `hitEpsilon`, no over-relaxation | StepsPerRay | 20–50% fewer steps — but adds per-step branching and state, which now costs occupancy | Low–Medium |
| P6 | Per-evaluation constant math: quaternion rotate always runs, `sin/cos` per cone eval | CostPerEval (ALU) | 🔻 **Discounted.** Pure ALU on a latency-bound shader; expect single digits | Low |
| P10 | Minor: object-id target written every frame, un-cleared accum layers, generous `maxDistance` | — | small | Trivial |

### Tier 4 — architecture

| # | Vulnerability | Bottleneck attacked | Expected win | Effort |
|---|---|---|---|---|
| P9 | Re-evaluating the CSG per pixel per frame **at all** — vs. Dreams/Claybook "evaluate once per *edit*, march a cached field per frame" | All of them | O(1) in object count per frame. The real answer at thousands of objects | High |

### Landed

| # | What | Result |
|---|---|---|
| ~~P2~~ | Full raymarch re-rendered every vsync even when nothing changed | Idle GPU ≈ 0 (`FrameScheduler`, §6a of CLAUDE.md) |
| ~~P4~~ | No whole-scene bounds — background & grazing rays paid full march cost | `E_background_10` 300 → 13 ms; dense scenes flat, as predicted |
| ~~P7~~ | Register pressure from the RPN stacks | **Suite 12.2 s → 5.7 s (2.1×)**, every scene 23–60%. Optimum depth **8**, found by measurement; cost is *not* monotonic in size |
| ~~P1a~~ | Per-object proxy substitution — built, measured at ~12%, **removed** | Its CPU half is retained and staged for P1b. Proved the shader is not ALU-bound |
| ~~P15~~ | Pack `evaluateScene`'s colour stack (`array<vec3f,N>` → `array<u32,N>`) — built, **much worse**, reverted | Removed ~22 scalars and lost performance. Best explanation: it moved the P7 notch. **The valley is now a known hazard for every remaining footprint change** |
| ~~P12~~ | *(Rejected, logged)* Per-pixel hit history reused to seed the next frame | Collides with P2; superseded by P11 |

**Recommended attack order:**
**P14 → P13 → P1d → P8 → P11/P1b → P5 → P9.**

P14 before P13 because it is renderer-side only — no change to `CsgCommand`, the
GPU contract, or the id spaces — so it is the cheaper of the two structural memory fixes and
its result tells you how much of the win is bytes versus dependency before you commit to P13.
**P15 previously led this list and no longer does** — see the box above; it was Low effort and
a direct continuation of P7's mechanism, and it still lost.

> **The organising idea for P1/P11 (established while designing them — read this before either).**
> The two attack *different factors of the same product*, and are not substitutes:
> ```
> cost = Pixels × StepsPerRay × Objects × CostPerEval
>                     ↑            ↑
>               P11 (tEnter)    P1b (mask)
>              needs a SCALAR   needs a SET
> ```
> **You need the set of nearby objects if and only if you intend to skip objects inside
> `distanceCalc`.** For shortening the ray, one scalar per pixel is enough — and it is safe
> unconditionally, because the worst case of a too-early `tEnter` is wasted steps, never
> missing geometry. That asymmetry is why P11 can be built from rasterization (which reports
> *the nearest* proxy) while P1b cannot (it needs *all* of them: the step size at any point on
> the walk is set by whichever object is nearest **there**, which changes as the ray travels,
> and CSG needs operands the ray never hits).


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

> ### ⚠️ P1a was implemented, measured, and **removed**. Read this before re-attempting.
>
> **What shipped:** the CPU half is done and still in the tree — `SceneBounds::EnclosingRadius`
> per primitive, `WriteEnclosingRadii` publishing the radius into `position.w`, and δ
> (`SceneAABB::proxyMargin`) published into `globals.sceneBoundsMin.w`. **All of it is still
> uploaded every refresh and is marked `!! STAGED, NOT YET CONSUMED` in the source.** P1b needs
> exactly this data — do not delete it as dead code.
>
> **What was removed:** the shader-side branch in `distanceCalc` that substituted
> `length(p − c) − r` for the exact SDF when `dBound > δ`.
>
> **Why (measured, post-P4 baseline → with P1a):**
>
> | scene | before | after | Δ |
> |---|---|---|---|
> | `B` | 1100 ms | 850 ms | −23% |
> | `G_correctness_17` | 720 ms | 630 ms | −12% |
> | `C` | 280 ms | 245 ms | −12% |
> | `D`, `E`, `H` | — | ~flat | — |
>
> Pixel-identical throughout (the safety math holds). But ~12–23% is a poor return for a
> permanent branch in the hottest loop, and — more importantly — the *shape* of the result was
> diagnostic: a uniform win across scenes with very different object distributions is not what a
> culling optimisation looks like.
>
> **What the follow-up experiments proved (worth more than the optimisation itself):**
> - `maxSteps 100 → 400` changed nothing ⇒ rays are **not** riding the step cap; step count
>   didn't grow, so P5-class work is not the bottleneck either.
> - `δ → 0.01` changed performance **not at all** but visibly **corrupted shapes** ⇒ the δ
>   safety argument is real and load-bearing, *and* the exact-SDF path was not where the time
>   was going.
> - ⇒ **The shader is not ALU-bound.** Replacing expensive arithmetic with cheap arithmetic
>   barely moved the needle. The remaining suspects are (1) the **dependent 80-byte object
>   load** `scene_objects[cmd.operand]` — an address that depends on a value loaded one
>   instruction earlier, so it cannot be prefetched — and (2) **P7**, occupancy loss from the
>   dynamically-indexed RPN stacks.
>
> **Consequence for the ranking: P7 is promoted by evidence.** It was ranked on speculation;
> it is now the best remaining explanation for where the time actually goes. The
> `CSG_STACK_DEPTH 16 → 8` experiment (P7 solution 1) is the cheapest test in this entire
> document and is still un-run.
>
> **Consequence for P1b:** P1a could never skip the object load — it needed `position.xyz` and
> `position.w` to compute the proxy. **P1b's mask needs nothing from the object at all**, so
> the load moves inside the branch and masked lanes never issue it. If the dependent load is
> the bottleneck, P1b is the only design that removes it. That is now the primary hypothesis
> P1b tests, not just an optimisation it delivers.

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

**b) Per-ray pruning — 🔴 THE LIVE ITEM. Full design below.**

Once per ray, test it against every object's bubble **inflated by δ**; objects it misses are
*fully skipped* for that ray. The decision is **constant for the whole ray** (a ray is a fixed
line, the bubbles are fixed), so it is answered **once** and consumed by the 60–200
`distanceCalc` calls that follow. That is the structural difference from (a):

| | (a) proxy substitution | (b) per-ray culling |
|---|---|---|
| decision made | per step, per object | **once per ray**, per object |
| cost of deciding | O(steps × N) | O(N) |
| 80-byte object load | **still paid** (proxy needs `position`) | **skipped** |
| result for a far object | a cheap approximate distance | **no work at all** |

*(a) discounted; (b) skips.* Given the P1a diagnosis above, skipping the load is the point.

**The general principle.** Amortise an O(N) test over an O(N × M) loop, then carry the answer
in a register. Structurally identical to **tiled/clustered light culling (Forward+)** — test
once, build a short list, let the hot loop walk only that list. That framing is what makes (c)
the obvious destination rather than a random idea.

**Why a bitmask.** The mask is not an algorithm, it is a *container*: the cheapest way to carry
N booleans from the top of `shadeRay` into the innermost loop. `array<bool,N>` with a runtime
index usually spills to per-lane scratch memory (and that is P7's problem, i.e. paying for the
fix in the same currency); recomputing per step is (a). A `vec4<u32>` is **4 registers holding
128 answers**, tested in ~5 ALU with no memory access at all. That cheapness is load-bearing:
the test sits in the same O(N × steps) slot (a)'s proxy occupied, so if reading the answer cost
anything meaningful, (b) would have rebuilt (a)'s overhead and won nothing.

**Why pushing `BIG` is safe — the whole correctness argument.** A culled object must still
**push**, or `sp` desyncs and every downstream operator reads the wrong operands. It pushes
`100000.0`, the file's existing "nothing here" sentinel — which is the most extreme
*over*estimate possible, normally the cardinal sin. It is legal here because **the bubble
encloses the object, the ray misses the bubble, therefore the ray never touches the object**:
there is no surface on this ray to tunnel through. Per operator, with `dA = BIG`:

| op | result | why it's right |
|---|---|---|
| `opUnion(BIG, dB)` = `min` | `dB` | true value away from A |
| `opSubtract` = `max(−BIG, dB)` | `dB` | the cavity lies inside A's bubble; this ray never enters it |
| `opIntersection(BIG, dB)` = `max` | `BIG` | `A∩B ⊆ A`, and the ray never enters A — the subtree genuinely contributes nothing |
| `opSmoothUnion(BIG, dB, k)` | `h→0` ⇒ `dB` | requires `BIG ≫ k` |
| `opSmoothIntersection(BIG, dB, k)` | `h→1` ⇒ `BIG` | as hard intersect |

Note row 3: `BIG` propagating upward is the *payoff*, not a bug — a whole culled subtree
collapses to "nothing here" and the marcher takes one giant step.

**The one hole, and δ plugs it.** The argument above needs "misses the bubble ⇒ misses the
object", but a smooth union bulges **outward** past both operands by up to `k/4`, so the
compound surface can poke outside A's bubble and a grazing ray could hit that bulge. Hence the
rule is not "misses the bubble" but:

> **Cull A for this ray iff the ray's closest approach to A's bubble exceeds δ**,
> with δ = `maxBlendRadius + slack` — *exactly the `proxyMargin` already computed and
> published to `globals.sceneBoundsMin.w`.* The δ derived for P1a is the correct δ here.

**Staging (each with a verification gate, so a failure is traceable):**

1. **Plumbing only.** Thread `mask : vec4<u32>` through `distanceCalc` → and its callers
   `calcNormal` / `calcAO` / `calcSoftShadow`; every call site passes all-ones. Gate: *pixel
   identical AND GPU ms unchanged.* This isolates the floor cost of an O(N × steps) test
   before any win is layered on top — otherwise "P1b gave 30%" is really "35% minus 5% of
   self-inflicted overhead" and you can never separate them.
2. **Build + use the mask** on the primary march. Cull test:
   `sqrt(max(dot(oc,oc) − dot(oc,rd)², 0)) − obj.position.w > δ`. Start with the *unclamped
   perpendicular* form — it under-reports the miss distance for spheres behind the camera, so
   it culls less than it could, i.e. it errs safe. Gate: `G_correctness_17` pixel-identical.
3. **Tighten `tStart`/`tLimit`** from the same loop (see P11 — `min(tEnter)` falls out of it
   nearly free) and skip the march entirely when no bubble is hit.
4. **A second mask for the shadow ray.** Best cost ratio in the feature: one O(N) build feeding
   an O(N × shadowMaxSteps) march.

**Gotchas, front-loaded (these are the hours-costing ones):**

- 🔴 **`calcSoftShadow` is a different ray** (different origin *and* direction). Reusing the
  primary mask makes objects stop casting shadows. Either pass all-ones or build a second mask
  — never silently reuse.
- 🔴 **Shift width is undefined behaviour.** `1u << k` for `k ≥ 32` is undefined in WGSL, and
  `H_ultra_48` reaches operand 47. Write `1u << (idx & 31u)` with explicit word selection
  (`select` chains, **not** `mask[w]` with a runtime `w` — dynamic vector indexing often lowers
  to scratch memory, defeating the whole point). `vec4<u32>` covers 128 objects; beyond that
  **fail open** to all-ones. The asymmetry is absolute: culling too little is slow, culling too
  much deletes geometry.
- ⚪ **`calcAO` (≤0.13 along the normal) and `calcNormal` (0.0005) probe off-ray.** Reusing the
  primary mask is safe *only because* δ ≥ 0.5 ≫ those radii. That is a consequence of δ's
  value, not of geometry — if δ is ever tuned below ~0.15, AO breaks first and it will look
  like a shading bug. Note it next to `proxyMargin`.
- ⚪ **Do not cull in `evaluateScene`.** It runs once per pixel (nothing to win) and is the only
  evaluator carrying color and object-id up the stack — a `BIG`-pushing object still writes a
  color that `opSubtract`'s unconditional `c = cA` would read. All risk, no reward.
- ⚪ **The mask must live inside `shadeRay`, not `fs_main`.** SSAA calls `shadeRay` n² times
  with different `uv` ⇒ different `rd` ⇒ genuinely different rays. Hoisting it "to avoid
  recomputation" would let a sub-sample cull an object it can see.
- ⚪ **Divergence caps the win.** The mask is per-ray, so lanes in a warp disagree; if *any* lane
  needs object A the whole warp executes A. **The real win tracks warp coherence, not average
  mask density** — expect background/empty regions to win big (whole tiles agree) and
  silhouettes to win little, i.e. the same scene-dependence P4 showed, for the same reason.
  This is the main argument for (c), which removes divergence by construction.
- ⚪ **Neutralize inputs, don't gate the loop.** `PER_RAY_CULL_ENABLED = false` must set the mask
  to all-ones and run identical code — copy the `SCENE_BOUNDS_OPT_ENABLED` pattern exactly, or
  the A/B isn't one.

**c) Screen-tile culling prepass** (the "engine-grade" version of b — **the endgame for P1+P11
together**):
a tiny compute pass bins objects into 8×8 or 16×16 pixel tiles (frustum-vs-sphere per
tile), writing one bitmask per tile to a storage buffer; `fs_main` reads its tile's mask.
Amortizes the per-pixel sphere tests and also lets you *sort* per tile. Same idea as tiled
deferred lighting.

Two things make this strictly better than (b), not merely cheaper:

1. **~256× less culling work.** At 1080p that's ~8,100 tiles instead of 2M pixels.
2. 🔴 **It kills the divergence ceiling.** A per-ray mask differs across a warp, so the warp
   executes the union of all its lanes' objects; a **per-tile mask is identical for every lane
   in the warp by construction**. The measured win moves much closer to the theoretical one.

It also produces **both** halves at once — the mask *and* the tile's `min(tEnter)`/`max(tExit)`
— which is what makes it the common destination of P1b and P11.

**Sequencing note:** (b) is not a detour. `distanceCalc(p, mask)` does not care whether the mask
came from a per-ray loop or a tile buffer, so **(b) stage 1's plumbing is the consumer side of
(c), unchanged.** Do (b) first anyway, because it is ~40 lines of shader and answers the
question (c) costs a day to ask: *does masking objects out of `distanceCalc` actually make this
shader faster?* Given the P1a diagnosis, that is still genuinely unknown — and if the answer is
no, the correct next move is P7, not a compute prepass.

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

## P4 — No whole-scene bound: background and grazing rays pay full price  ✅ LANDED

> **Shipped** as `src/scene/SceneBounds.{h,cpp}` + `intersectSceneBounds` in `main.wgsl`, via
> solution 1 below (scene AABB ray clip, slab test). See CLAUDE.md §7b for the implementation and
> its invariants. `E_background_10` 300 ms → 13 ms; `D_cluster_12` and `H_ultra_48` flat.
> Two notes the estimate below got wrong, worth keeping:
> **(a)** the win is much larger than "~30–60% of pixels" suggests, because a *miss* has no early
> exit and pays the full `maxSteps` budget while a *hit* breaks out on convergence — near-misses
> are the most expensive rays in the frame, so removing them removes disproportionate work. The
> ceiling is `1 / (share of frame work done by hit pixels)`.
> **(b)** solution 2 (tighter `maxDistance`) is now genuinely moot — the `tExit` clamp subsumes it.
> Solution 3 still stands as a P1 follow-on. Clamping the *shadow* march to `tExit` is the obvious
> next increment and is not yet done.

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

## P7 — Register pressure from the RPN stacks  ✅ LANDED — the largest win in this document

> **Shipped** as a shader-source-injected `CSG_STACK_DEPTH`, an exact required-depth
> computed by `CsgTree::CompileToRPN`, and a per-depth pipeline cache in `SdfRenderer`.
> See CLAUDE.md §6b.

**Where:** `distanceCalc` held `array<f32, 16>`; `evaluateScene` holds **three** parallel
stacks (`f32` + `vec3f` + `u32` = 5 scalars per slot, so 80 at depth 16)
([sdf_core.wgsl](assets/shaders/sdf_core.wgsl)). The **peak** register demand of the whole
fragment shader sets its occupancy, and `evaluateScene` sets the peak even though it runs
once per pixel.

### The result

Whole suite, `maxSteps`/scene/camera unchanged, only the declared array size varying:

| scene | depth 16 (before) | depth 8 (after) | Δ |
|---|---|---|---|
| A | 130 ms | 80 ms | −38% |
| B | 1100 ms | 700 ms | −36% |
| C | 280 ms | 210 ms | −25% |
| D | 300 ms | 190 ms | −37% |
| E `background_10` | 13 ms | 10 ms | −23% |
| F | 1180 ms | 680 ms | −42% |
| G `correctness_17` | 720 ms | 450 ms | −38% |
| H `ultra_48` | ~8500 ms | 3400 ms | −60% |
| **total** | **12.2 s** | **5.7 s** | **2.1×** |

Pixel-identical throughout. **Zero arithmetic was removed.** The programs, the step counts
and the SDF evaluations are byte-for-byte what they were; only how many warps fit on an SM
changed.

### 🔴 Cost is NOT monotonic in stack size — this is the finding

The obvious next move was "size the stack to what each scene actually needs." It was
implemented (see below) and it made things **worse**, which is how the real shape came out:

| scene | depth 4 | **depth 8** | depth 16 |
|---|---|---|---|
| D | 330 | **190** | 300 |
| G | 450 | **440** | 720 |
| H | ~8500 | **3200** | ~8500 |

A valley, not a slope. The best-fitting explanation (inferred from the curve, **not**
verified against a driver disassembly):

- **GPU registers are not addressable.** A register operand is encoded into the instruction
  word, so there is no `read r[sp]`. A dynamically-indexed local array therefore cannot
  simply live in registers.
- The compiler picks one of two lowerings: **scalarize** (keep all N in registers and turn
  every access into a `select` chain — a *write* must conditionally rewrite all N), or
  **spill to scratch** (per-thread memory, backed by VRAM, in practice L1-resident).
- **At 4 it scalarizes** → ~20 scalars charged to every invocation of the whole shader →
  fewer resident warps → the storage-buffer latency is exposed.
- **At 8 it spills** → register cost collapses to ~0, scratch footprint still small → most
  warps resident → latency hidden. The ~25-cycle L1 access is repaid many times over,
  because the extra warps also hide the pre-existing 80-byte `scene_objects` loads.
- **At 16 it spills too**, but the bigger scratch allocation comes out of a fixed per-SM
  pool and caps occupancy from the other side.

Two resources bind occupancy — registers below the notch, scratch above it — and 8 is where
neither does. **The optimum is a property of this machine and this driver.** Re-measure
before changing it; it is a measured constant, not a derived one.

### What was actually built

1. **Exact required depth, computed during emission.** `CsgTree::CompileToRPN` returns
   `CompiledProgram { commands, requiredStackDepth }`. `CompileNode` now returns
   `{peak, yield}` and tracks the running stack height, so the bound comes from the real
   emission rather than a second traversal that could drift from it. Depth grows with CSG
   *nesting*, not object count — **a flat union of any size peaks at 2**.
2. **Sibling reordering (Sethi–Ullman).** For *hard* Union/Group/Intersection only — `min`
   and `max` are associative and commutative, so any permutation gives the identical field.
   Subtract is not commutative and the smooth ops are not even associative, so they are
   excluded. Emitting the deepest child first collapses accidental nesting chains to depth 2.
3. **Per-depth pipeline variants.** WGSL array sizes must be const-expressions, so the depth
   is injected into the shader source (`SdfRenderer::BuildShaderSource`) and one pipeline is
   compiled and cached per bucket.
4. **A latent bug fixed on the way.** The old loop counted an *alive* child even when it
   emitted nothing (an empty group), producing an op command with one value beneath it —
   harmless at runtime thanks to the `sp >= 2` guard, but a dead instruction in a program the
   shader walks linearly at every march step.

### The honest accounting

**The 2.1× came from a one-line constant (16 → 8), which was always available.** The
machinery above adds **no speed**. What it adds is:

- **Deep scenes render correctly.** A program needing more than the fixed depth silently lost
  pushes to the `sp < CSG_STACK_DEPTH` guard — missing geometry, no error. It now gets a
  deeper variant, and anything past `kMaxCsgStackDepth` gets a loud warning. That matters
  because `.sdfp` is untrusted input (§11).
- **The depth is a measured number**, printed per scene, instead of a hope.

The bucket ladder starts at **8, not at the smallest depth a scene could use** — sizing down
is actively harmful, which is the whole point of the valley above.

### Still on the table (untested)

**Pack `evaluateScene`'s colour stack**: `vec3f` → one `u32` via `pack4x8unorm`, dropping 5
scalars per slot to 3. Its *runtime* is irrelevant (once per pixel) but its *footprint* is
charged to the whole shader. Given the valley, this may move the notch rather than simply
help — measure the same three depths again after.

**Verify the mechanism**: RenderDoc/Nsight on the native build reports registers-per-thread,
scratch bytes and achieved occupancy. Ten minutes, and it would turn the inference above into
a fact.

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

## P11 — No per-pixel ray interval: `tEnter` is one coarse box for the whole screen

**Where:** P4 clips the march to the **whole-scene AABB** ([main.wgsl](assets/shaders/main.wgsl),
`intersectSceneBounds`). That box is a single O(1) answer shared by every ray, so on a scene of
scattered objects it is enormously loose: a ray entering the box may still travel most of its
length through empty space before reaching anything, paying full O(N) steps the entire way.
`E_background_10` is the canonical case — P4 already made *missing* rays free, but rays that
*hit the box* still crawl through the emptiness inside it.

**The fix, in one sentence:** derive `tEnter` from the **objects' own bubbles** instead of from
the box around all of them.

**Why this is safe unconditionally — and why it needs no mask.** The worst case of a `tEnter`
that is too small is *wasted steps*; it can never skip geometry, because the full object set is
still evaluated from that point onward. Contrast P1b, where a wrong answer deletes geometry.
**This is why P11 needs only a scalar per pixel while P1b needs a set** (see the box in §0):
rasterization reports *the nearest* proxy, which is exactly `tEnter` and is useless as a mask.

### Solution options (a → b, in increasing cost and payoff)

**a) Per-ray `min(tEnter)` over the bubbles — ~20 lines, no new plumbing.**
While looping over objects, solve the ray-sphere entry and track the minimum; then
`tStart = max(tStart, minEnter − δ)`, and if no bubble was hit at all, **skip the march
entirely** (the P4 miss early-out, now exact instead of box-shaped). Two notes: this needs the
real `t ≥ 0` entry solve, which — unlike P1b's closest-approach test — is **not** conservative
in its naive form, so it is a separate increment; and `tExit` can be tightened the same way with
`max(tExit) + δ`.

If P1b's mask loop already exists this is nearly free (it is P1b stage 3). Standalone it is
still the cheapest experiment available, and it is the **correct prototype for (b)**: same
number, 20 lines instead of a pipeline. If (a) gives nothing, (b) gives nothing either.

**b) Rasterized bounding-proxy depth prepass — better asymptote, the real version.**
Draw one screen-space quad per object covering its projected bubble; the fragment shader solves
the ray-sphere entry and writes it as **depth**, with `depthCompare: "less"` so the fixed-function
depth test performs the `min` across overlapping proxies **for free**. `fs_main` then reads that
texture for its `tStart`.

*This is how GPU volume raycasting has worked for twenty years* (rasterize bounding-box front
faces for ray entry, back faces for exit, march only the interval). Cost scales with **summed
projected area**, not with pixels × objects: at 128 objects, (a) does 128 sphere solves on each
of 1.4M rays, while (b) does roughly one per covered pixel — plausibly 50–100× less. Since P1
exists to fix the many-object cliff, that is the regime that matters.

Implementation notes:
- **Do not** use an `R32Float` target with min-blending — float32 blending is not in core
  WebGPU. The `depth32float` + `less` route is core and free.
- 🔴 **Conservative coverage is the gotcha that will bite.** A proxy quad only produces fragments
  for pixels whose **center** it covers, but the marched ray is jittered up to half a pixel by
  TAA ([main.wgsl:199](assets/shaders/main.wgsl#L199)) and sub-pixel-offset again by SSAA. A ray
  can therefore graze a bubble the rasterizer decided missed that pixel, whose `tEnter` then
  comes from a farther bubble — and the march **starts past real geometry.** It presents as a
  one-pixel sparkle on silhouettes that reads like an AA artifact, not a culling bug. WebGPU has
  no conservative-rasterization mode, so inflate the proxy yourself by one pixel's world-space
  footprint at that depth plus the jitter. Same shape as δ: a margin that makes the bound
  provably not-tight.
- A pixel covered by **no** proxy keeps the depth clear value — that is the free "this ray
  touches nothing" early-out, provided the clear value reads as a miss and not as `tEnter = 0`.

**Where it converges:** P1(c)'s tile prepass produces the tile's `min(tEnter)` alongside the
mask, so P11(b) and P1b(c) are the same pass. Build them separately, merge them there.

---

## P12 — *(Rejected, logged)* Reusing the object-id target as per-pixel hit history

**The idea:** the scene pass already writes an `R32Uint` object-id target every frame, and it
already stores the **array index** (CLAUDE.md §4: *"Pick returns a vector index, not an
objectId"*). So frame N tells every pixel which object it hit; frame N+1 could look that up,
re-derive that object's bubble entry analytically, and start its march there.

**What is right about it** (and worth keeping in mind elsewhere): it needs **no readback** — the
target is bound as a texture and read on the GPU, sidestepping the `mapAsync` stall that makes
`PickObjectAt` async. And **storing the id rather than the distance is the good variant**: a
stored `t` is stale garbage the moment an object moves, whereas a stored *id* re-derives the
entry against that object's **current** position, so the guess degrades to "which object" and
the geometry is always fresh.

**Why it was rejected, in order:**

1. 🔴 **It collides head-on with P2.** The stored id is only valid if the ray and the scene are
   unchanged — but under render-on-demand `FrameScheduler` *idles at zero cost* when nothing
   changed. The frames that get drawn are, by construction, the ones right after the camera or
   scene changed, i.e. exactly when the history is invalid. **Two optimisations can each be
   sound and still be mutually exclusive; the one that removes work beats the one that spreads
   work out.**
2. **P11 computes the same quantity, exactly, with no history**, and is correct on the first
   frame after an invalidation — where this is worthless.
3. It needs **ping-pong targets** after all (a pass cannot read and write the same texture), so
   the "no second buffer" advantage doesn't survive contact.
4. When the history *is* wrong (camera moved, or an object moved into the path), the march
   starts past a real surface: the disappearing-geometry failure class again.

**The one place it survives:** the 4 TAA accumulation frames after an invalidation share a
camera and a scene, differing only by ≤0.25 px of jitter, so the history is valid *by
construction* — no reprojection, no disocclusion, no unbounded object motion. Two caveats if it
is ever revisited: it buys a **starting `t`, not a cheaper evaluation** (knowing the pixel hits
object 5 does not let you evaluate only object 5 — the surface there may be a subtraction or a
smooth blend, so the full RPN program still runs), and it does not skip the mask build, so it
stacks on top of P1b/P11 rather than replacing either. Capped at 3 of every 4 rendered frames.

**General rule this produced:** never move per-pixel work toward the CPU to make the GPU faster.
CPU precomputation pays when the thing is *scene-sized* (tens of items — `SceneBounds::Compute`);
it is catastrophic when the thing is *pixel-sized*.

---

## P13 — The dependent load: a two-deep pointer chase in the hottest loop  🔴 the root cause

**Where:** [sdf_core.wgsl:168-176](assets/shaders/sdf_core.wgsl#L168-L176), inside `distanceCalc`:

```wgsl
for (var i = 0u; i < count; i++) {
    let cmd = csg_program[i];                    // load 1: 16 B, sequential, uniform
    if (cmd.cmd_type == 0) {
        let obj = scene_objects[cmd.operand];    // load 2: 80 B, DEPENDENT on load 1
```

The two loads are not equal:

- **Load 1 is nearly free.** `i` is the loop counter — identical across every lane, sequential
  across iterations. The hardware broadcasts one address to the warp, and the program (a few KB)
  stays resident in L1.
- **Load 2 is the bottleneck.** Its *address* is `cmd.operand`, a value that was itself just
  loaded. The memory unit cannot begin issuing it until load 1 returns. **Two serialized round
  trips per object per march step, with zero instruction-level parallelism available to fill
  them** — the only way to hide it is to run a different warp, which is exactly why P7's
  occupancy fix was worth 2.1×.

**Latency, not bandwidth — and the distinction decides the fix.** At current scene sizes the
whole dataset is ~4 KB and lives in L1 permanently; you are not moving much data. You are paying
~25 cycles, twice, in a chain. So the fix that matters is the one that **breaks the dependency**,
not the one that moves fewer bytes (that is P14).

**Solution — fuse the geometry into the command.** Widen `CsgCommand` to carry the primitive's
geometry inline instead of an index into a second buffer:

```
before:  csg_program[i] → operand → scene_objects[operand]     2 loads, serialized
after:   csg_program[i]                                        1 load, streaming
```

One sequential, prefetchable stream and **no pointer chase at all**. At 5k objects it is also
better for cache: ~320 KB streamed linearly beats random access into a 400 KB array.

Costs and cautions:
- The program buffer grows (16 B → ~64 B per PUSH). An object referenced by two nodes is
  duplicated — rare, and correctness is unaffected.
- The program must be rebuilt when an object *moves*, not just when the tree changes. Today
  `SetObjectPosition` → `OnSceneChanged` → `RefreshSceneBuffer` already recompiles, so this is
  free — but `OnCsgChanged`'s cheaper path would need auditing.
- `evaluateScene` still needs `color`/`selected`, and the pick target still writes
  `cmd.operand` as the array index (§4). Both keep the indexed path; both run once per pixel.
- ⚠️ Trades a small dependent load for a much larger sequential one. Could surprise in either
  direction — this is a measure-it item, not a certainty.

---

## P14 — The object struct is 45% dead weight in the hot path

**Where:** [SdfObjectData.h](src/scene/data/SdfObjectData.h) / the `SdfObject` struct in
[sdf_core.wgsl:1-13](assets/shaders/sdf_core.wgsl#L1-L13). `distanceCalc` reads all 80 bytes
and uses 44:

| field | bytes | used by `distanceCalc`? |
|---|---|---|
| `obj_type` | 4 | ✅ |
| `position.xyz` | 12 | ✅ |
| `rotation` | 16 | ✅ |
| `size.xyz` | 12 | ✅ |
| `color` | 16 | ❌ — `evaluateScene` only |
| `selected` | 4 | ❌ — `evaluateScene` only |
| `object_id`, `parent_id` | 8 | ❌ — never read by either |
| `position.w`, `size.w` | 8 | ❌ — staged (§P1) / reserved |

Two separate costs, both now known to matter:

1. **36 wasted bytes** fetched per object per march step.
2. 🔴 **`let obj = scene_objects[...]` holds all 80 bytes — 20 scalars — live across the entire
   SDF switch.** P7 proved this shader is acutely sensitive to live-register count. That is
   ~8 registers of pure waste in the hottest scope in the program.

**Solution — hot/cold split (AoS → SoA).** Geometry (`type`, `position`, `rotation`, `size`) into
one packed 48-byte buffer that `distanceCalc` reads; shading (`color`, `selected`, ids) into a
second buffer only `evaluateScene` touches.

The property that makes this cheap: **it is a renderer-side transformation only.**
`SdfObjectData` stays the CPU and `.sdfp` struct; `SdfRenderer::UpdateSceneBuffer` builds two
packed arrays from it. No change to the data model, the serialization format (§12), or the three
id spaces (§4). Cost is one extra binding in the scene bind-group layout and a little CPU work
per refresh — once per edit, irrelevant at any object count.

**Do this before P13.** Same class of fix, strictly less blast radius, and its result tells you
how much of the win is *bytes* versus *dependency* before you commit to changing the GPU
command contract.

---

## P15 — `evaluateScene`'s register footprint is charged to every thread  ⛔ TRIED, REVERTED

> ### ⚠️ P15 was implemented exactly as specified below and **made performance much worse.**
> ### The code was reverted. Read this before re-attempting — and before attempting P14 or P13.
>
> **What was built:** the ten-line change described below. `array<vec3f, CSG_STACK_DEPTH>` →
> `array<u32, CSG_STACK_DEPTH>`, packed with `pack4x8unorm` at the push site, unpacked only
> inside the smooth-blend branch (the sole place colour arithmetic happens — hard union /
> subtract / intersection just *select* an operand, so they never need the unpacked value) and
> once at the exit. `cA`/`cB`/`c` stayed packed as locals too, so the peak dropped by ~22
> scalars rather than the 16 estimated below.
>
> **Result: a large regression across the suite**, at `kCsgStackDepthBuckets[0] = 8`.
> It was not close, and it was not inside the ±6–10% noise floor.
>
> **The best explanation — and the reason this is worth more than the optimisation was:**
> the change did precisely what it set out to do, and that is *why* it lost. P7 showed the
> compiler **spills the stacks to scratch at depth 8**, which is the good regime: scratch costs
> almost no registers, occupancy stays high, and the L1 latency is repaid many times over by the
> extra resident warps. Depth 4 was slower because the arrays were small enough that the compiler
> **scalarized** instead — all N slots in registers, every access a `select` chain, every write
> conditionally rewriting all N. Shrinking the arrays ~40% is a large input to exactly that
> spill-vs-scalarize decision. **The most likely story is that depth 8 packed now sits where
> depth 4 unpacked sat: we moved along the valley rather than down it.**
>
> **The experiment that would settle it, and is UNRUN (one build).** Re-apply the packing and set
> `kCsgStackDepthBuckets` to the single entry `{16}`.
> - packed+16 **fast** ⇒ the notch moved; packing is fine, it was paired with the wrong depth,
>   and P15 is recoverable at a different bucket.
> - packed+16 **also slow** ⇒ packing itself is the problem regardless of depth, and P15 is
>   closed for good.
>
> It was skipped as a deliberate scheduling call, not because the question is uninteresting —
> P14 was judged the better use of the next build. **If P14 reads worse than expected, this is
> the first thing to suspect**, because P14 removes ~8 live registers from `distanceCalc` and
> therefore perturbs the same decision.
>
> **The reusable finding:** the depth-8 constant is **not independent of the rest of the shader.**
> It is the optimum for the footprint the shader had when it was measured. Treat it as a
> *coupled* parameter from here on: after any change to per-thread state, "did it help?" and
> "did it help at depth 8?" are different questions, and only the second one was asked here.
>
> Everything below is the original (still correct) reasoning about *why* the footprint matters.
> The mechanism is real — `evaluateScene` genuinely does charge every thread for state that only
> hit pixels use. What is now known is that shrinking that state is not automatically a win.

**Where:** [sdf_core.wgsl:301-303](assets/shaders/sdf_core.wgsl#L301-L303):

```wgsl
var stack      : array<f32,   CSG_STACK_DEPTH>;   // 1 scalar/slot
var stackColor : array<vec3f, CSG_STACK_DEPTH>;   // 3 scalars/slot  ← the target
var stackId    : array<u32,   CSG_STACK_DEPTH>;   // 1 scalar/slot
```

**Why this is the leftover half of P7.** Three of the four RPN stacks live in `evaluateScene`,
and they are **80 of the shader's ~96 stack scalars** — five of every six. Yet `evaluateScene`
is called exactly **once per hit pixel** ([main.wgsl:160](assets/shaders/main.wgsl#L160)).

Its *runtime* is irrelevant. Its *footprint* is not, because **register allocation is static,
per-shader, and decided before the shader runs.** `fs_main` is compiled as one entry point;
everything reachable from it is allocated out of a single budget sized to the worst case in the
call graph, handed to each thread at launch, and never reallocated on function entry or exit.
So a background pixel that never calls `evaluateScene` still reserved 80 scalars for it — and
that is 30–60% of the frame.

**Solution — pack the colour stack.** `array<vec3f, N>` → `array<u32, N>` via
`pack4x8unorm` / `unpack4x8unorm`. Per slot: 5 scalars → 3. At depth 8 that removes 16 scalars
from the shader's peak, on top of P7's 48.

The colour precision loss is 8 bits per channel, which is exactly what the swapchain stores
anyway. The pack/unpack cost lands entirely in the once-per-pixel function.

**Effort: Low** — roughly ten lines, all inside `evaluateScene`.

⚠️ **Re-measure the depth valley afterwards** (§P7). Changing the footprint may move the notch;
`4 / 8 / 16` on D, G and H is a three-build check, and `kCsgStackDepthBuckets` carries the table.

> 🔴 **This last warning was written, published, and then not acted on — and it is exactly what
> killed the item.** The change was measured at depth 8 only, the result was bad, and the item was
> reverted before the notch was re-checked. The warning was correct; the process failed to honour
> it. If any future footprint change is measured at a single depth, the result is not
> interpretable — it confounds "the change is bad" with "the change moved the optimum."

---

## Suggested roadmap

| Phase | Items | Outcome |
|---|---|---|
| ~~1~~ ✅ | ~~P2 (render-on-demand)~~, ~~P4 (scene AABB clip)~~ — both landed. P8.1 / P3.2 / P3.3 still open | Idle cost ≈ 0; background rays free (`E` 300 → 13 ms) |
| ~~1.5~~ ⛔ | ~~P1a (proxy substitution)~~ — built, measured at ~12–23%, **removed**. CPU half retained and staged for P1b | Proved the shader is **not ALU-bound**; promoted P7 by evidence |
| ~~1.75~~ ✅ | ~~**P7 (RPN stack depth)**~~ — one constant, 16 → 8, plus exact-depth computation, Sethi–Ullman sibling ordering and per-depth pipeline variants | **Suite 2.1×.** Confirmed the shader is occupancy-limited, and that cost is non-monotonic in stack size |
| ~~2.0~~ ⛔ | ~~**P15** (pack the colour stack)~~ — built, **much worse**, reverted. The packed+depth-16 re-test is unrun | Established that the depth-8 optimum is **coupled** to the shader's footprint, so every remaining Tier-1 item must be measured at ≥2 depths |
| **2 — finish the latency work** | **P14** (hot/cold split the object buffer — Medium, renderer-side only), then **P13** (fuse geometry into the command — Medium, changes the GPU command contract) | Attacks the *proven* bottleneck: the dependent 80-byte load that survived P1a. 🔴 Both perturb the P7 valley — measure each at depth 8 **and** one neighbouring bucket |
| 3 — reduce the iteration count | **P1d** (`SKIP_IF_FAR` + bottom-up node bounds) — the only item that changes the complexity class and the only one that survives a 5,000-object target. Then P11a/P11b and P1b, converging on P1c's tile prepass | Cost tracks *visible* geometry rather than total scene size |
| 4 — UX and polish | P8 (dynamic resolution + shadows/AO off during interaction — **best UX-per-hour in this document**), P3.2/3.3, then P5. **P6 is discounted**: pure ALU on a shader proven not to be ALU-bound | Smooth camera at any scene size |
| 5 — endgame | P9.1 (global distance volume hybrid) — the real answer if 5,000 objects is a firm target, since even P1d only makes the walk sublinear rather than removing it | Thousands of objects, Dreams/Claybook-direction architecture |

A note on process: land **one phase at a time and measure** (fixed benchmark scene,
frame-time in **ms**, not FPS — the loop is rAF-driven so frame *rate* saturates at 60 and can
never show headroom). Every optimisation ships behind a shader-side `const` A/B that
**neutralises its inputs rather than gating its code path**, so "off" is provably the previous
behaviour. Re-check `G_correctness_17` after *every* bounds change — it is the clipping trap,
and rotated/scaled primitives, subtract nodes and large smooth-blend radii are exactly the cases
that sank the first attempt. Note that `FrameScheduler::ForceContinuous(true)` (set in
`Application::Initialize`, **must not ship**) is the harness that makes steady-state readings
possible at all under P2.

Two process lessons this pass produced, worth more than either optimisation:

- **A negative result is a result.** P1a's ~12% and the flat `maxSteps 100 → 400` test together
  proved the shader is not ALU-bound and pointed at the dependent object load and occupancy —
  which re-ranked the remaining work. Design the *experiment* that distinguishes hypotheses, not
  just the optimisation.
- **Check which factor of the cost model you are attacking.** `Pixels × Steps × Objects ×
  CostPerEval` — P8 hits the first, P4/P5/P11 the second, P1 the third, P6 the fourth. Two ideas
  that attack the same factor compete; two that attack different factors multiply.
- **The cost model has a fifth factor that isn't in the product: occupancy.** It multiplies
  everything and appears nowhere in the source. P7 was ranked on speculation, promoted by
  evidence, and then beat every algorithmic item combined — by changing one integer. Before
  optimising *what the shader computes*, check what limits *how many copies of it can run*.
- **Establish the noise floor before trusting a result.** ±6–10% here. P1a's 12% was inside it.
- **Change one thing.** The stack-depth work shipped three changes at once (depth, stray-op
  removal, sibling reordering) and produced an unreadable result; two one-line isolation runs
  untangled it. The rule is cheap to follow and expensive to skip.
- **An optimisation can succeed at its stated goal and still be a pessimisation.** Sibling
  reordering worked perfectly — it drove scenes from depth 6 to depth 3 — and that is precisely
  why they got slower, because 3 rounds into a worse bucket than 6. "Did it do what I intended?"
  and "did it help?" are different questions. **P15 is the second instance of this exact
  shape**: it removed ~22 scalars as designed, and losing them is the best available explanation
  for why it lost. Twice now, on this shader, *reducing per-thread state made things worse.*
- **A tuned constant is a coupled parameter, not a fixed one.** `CSG_STACK_DEPTH = 8` is the
  optimum *for the footprint the shader had when it was measured*. Any change to per-thread state
  moves the optimum with it. So a footprint change measured at one depth is uninterpretable — it
  confounds "the change is bad" with "the change moved the optimum." Budget two builds, not one,
  for every remaining Tier-1 item.

## References

- Alex Evans — *Learning from Failure: …Renderers for 'Dreams PS4'*, SIGGRAPH 2015 — [slides (PDF)](https://advances.realtimerendering.com/s2015/AlexEvans_SIGGRAPH-2015-sml.pdf) · [Umbra Ignite video announcement](https://www.mediamolecule.com/blog/article/alex_at_umbra_ignite_2015_learning_from_failure_video) · [detailed notes](https://cheneyshen.com/siggraph-15-learning-from-failure-a-survey-of-promising-unconventional-and-mostly-abandoned-renderers-for-dreams-ps4-a-geometrically-dense-painterly-ugc-game/)
- Sebastian Aaltonen — *GPU-based Clay Simulation and Ray-Tracing Tech in Claybook*, GDC 2018 — [slides (PDF)](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf) · [talk video](https://www.youtube.com/watch?v=Xpf7Ua3UqOA)
- Inigo Quilez — [*Simple Bounding Volumes for raymarching*](https://iquilezles.org/articles/sdfbounding/) (the proxy-substitution technique, from the field's primary practitioner)
- [*Synchronized Tracing of Primitive-based Implicit Volumes*](https://dl.acm.org/doi/full/10.1145/3702227), ACM TOG 2025 — bottom-up CSG tree pruning + tight bounding volumes for exactly this primitive-list problem
- Keinert et al. — [*Enhanced Sphere Tracing*](https://www.researchgate.net/publication/329152815_Accelerating_Sphere_Tracing) (over-relaxation); Galin et al. — *Segment Tracing Using Local Lipschitz Bounds*, Eurographics 2020
- [*Ray Tracing of Signed Distance Function Grids*](https://jcgt.org/published/0011/03/06/paper-lowres.pdf), JCGT 2022 — marching baked SDF volumes (the P9 hybrid)
