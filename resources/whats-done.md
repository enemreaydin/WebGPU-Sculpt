1) SSAA'ya ek olarak TAAA implemente edildi
2) TAAA kamera hareketi sırasında deaktive edildi
3) Testbenchler oluşturuldu, darboğazlar analiz edildi
4) Render ve export ayrıldı
5) Export scissor bands ile yapılmaya başlandı, çünkü os 2 saniye sınırı ve progress bar
grow at most 2× per band, but shrink without limit. 0.25 saniye limitin altında kalıyor
nice, can you explain the scissor logic more verbally, so that I can explain it to my team confidently
Happy to — here's the version I'd actually stand up and deliver.

The one-liner
"We render the export in horizontal strips. Each strip is the same draw call as before, but we set a scissor rectangle so the GPU only produces pixels for that strip's rows. Same shader, same output, but now the work arrives in bounded chunks we can count and time."

Where the scissor lives, and why it's free
The mental model that makes this click: the GPU pipeline has a stage called the rasterizer sitting between the vertex shader and the fragment shader. Its job is to look at a triangle and decide which pixels it covers, then hand out one unit of work per covered pixel to the fragment shader.

Think of the rasterizer as a supervisor writing work tickets — one ticket per pixel — for a huge pool of workers. The scissor rectangle isn't an instruction to the workers; it's an instruction to the supervisor: "don't write tickets outside this rectangle." No ticket means no worker, means no cost.

That distinction is the part worth emphasizing to your team, because there's a similar-looking feature that behaves completely differently: discard (or an alpha test) in the fragment shader says "do the work, then throw the result away" — you pay full price for nothing. Scissor prevents the work from ever being scheduled. In our case the fragment shader is the raymarcher, so preventing a fragment from existing is precisely how we avoid marching that pixel's ray. Cost drops in exact proportion to the rows we scissor away.

The vertex shader still runs for all 6 quad vertices every band. Nobody cares: it's six vertices of trivial math against millions of rays.

The classic confusion: scissor vs. viewport
Someone will ask "why not just set the viewport to that strip?" It's a good question and the answer is a nice teaching moment.

The viewport defines the coordinate transform — it maps normalized device coordinates onto a region of the target. If you set the viewport to rows 240–360, the shader's full-screen quad would be squashed into those 120 rows: you'd get the entire image compressed into a strip. Wrong picture.

The scissor changes no coordinates at all. Geometry lands exactly where it always would; the rectangle just clips what gets rasterized. That's why the strips seamlessly reassemble into one correct image — every band draws the full-size picture and we only keep a different horizontal slice of it each time.

Short version for the team: viewport moves and scales, scissor only crops.

Why the output is bit-identical to the old single draw
This is the claim a careful reviewer should push on, so here's the rigorous argument.

Our fragment shader is a pure function of the pixel's own coordinate plus the uniforms and buffers. Each pixel reconstructs its camera ray from its screen position, marches it, shades it, writes it. No pixel reads any neighbor. No pixel depends on how many other pixels have been shaded, or in what order, or in how many passes.

Given that, splitting the work is mathematically invisible. Shading rows 0–15 in one submission and rows 16–47 in another produces exactly the bytes one big draw would have produced. This is also why the change required zero shader edits — a strong signal we did it at the right layer. If the shader had needed modification to support banding, that would have been evidence we were doing something semantically riskier.

Contrast: this decomposition would not be safe for a post-process blur, a screen-space effect, or anything that samples neighboring pixels, because a band boundary would cut across a pixel's data dependencies. Worth stating explicitly, because it tells your team when they can and can't reuse this trick.

The one bit of bookkeeping: clear vs. load
Every render pass declares what happens to the target when it begins. The first band uses loadOp: Clear — start from the clear color, same as before. Every later band uses loadOp: Load, meaning "keep what's already in this texture."

That single flag is the whole reason the strips accumulate into one image instead of erasing each other. If every band cleared, you'd finish with only the last strip painted and the rest flat background. It's the most likely place for someone to introduce a bug later, so it's worth calling out by name.

Why this shape of fix, rather than the alternatives
We could have built the strip into the geometry — emit a quad covering only those rows instead of the full screen. That works too, but it means the vertex data and the UV interpolation have to change in lockstep with the band, and any mistake shows up as subtly wrong rays rather than an obvious error. Scissor achieves the same restriction with one call, no vertex changes, no shader changes, and no new opportunity to get the ray math wrong.

We could also have rendered each strip into its own small texture and stitched them afterward. That's more allocations, more copies, and the stitching becomes our problem. Scissoring into one full-size texture means the image is already assembled when the last band finishes.

The honest caveat to mention
One thing I'd surface proactively rather than let someone discover it: the export's bind group points at the live scene and CSG buffers — only the globals (camera, quality, resolution) are snapshotted privately per export. With a single draw, the whole image was guaranteed to reflect one consistent scene state. With bands, if the scene were mutated halfway through, the strips above and below the edit would disagree — a torn image.

In practice this can't happen today because the progress overlay covers the viewport and blocks interaction for the duration. But it's a real constraint the design now carries, and if anyone later adds background scene mutation (an animation system, a network sync, an auto-save that rebuilds buffers), the export would need its own snapshot of the scene buffers too. Better your team hears that from you now than finds it in a bug report.

Quick answers for likely questions
"Doesn't one pass per band add overhead?" A little — a command encoder, a pass begin/end, and a submit per band. Microseconds against bands sized to take a quarter-second of shading. Irrelevant.

"Why bother at all?" Two reasons. Windows resets the graphics driver if any single submission runs longer than about two seconds, which killed our exports outright and left the app hung. And a progress bar is impossible without countable units — one draw call is atomic, so there's literally nothing to measure until it's done. Bands give us both, from the same change.

"How do you pick the band height?" We don't guess. The first band is a small 16-row probe, we time it, and each subsequent band is sized from measured throughput toward a quarter-second target — growing at most 2× at a time but shrinking instantly, because overshooting risks the driver reset while undershooting only costs an extra submit.

6) Render-on-demand: motor artık sadece görüntü değiştiğinde çiziyor (perf raporundaki P2)

The one-liner
"The editor used to re-raymarch the entire screen 60 times a second even when absolutely nothing had changed. Now the frame loop only draws when something actually invalidated the image. An idle editor costs essentially zero GPU."

The idea, and why it's obvious in hindsight
The mental model I'd open with: a game and an editor have fundamentally different relationships with time.

A game's world evolves on its own — physics ticks, characters animate, AI moves things. Every frame genuinely differs from the last, so re-rendering unconditionally is correct.

An editor's image is a pure function of state that only the user changes. Written out: image = f(scene, camera, selection, gizmo, render settings, canvas size). If none of f's inputs changed since the last frame, the next frame is provably, bit-for-bit identical to the one already on screen. We were spending 1.4 billion SDF evaluations per frame to reproduce a picture we already had.

This is not a clever trick — it's the standard architecture for every editor-shaped application. Blender does it, the Unity and Godot editors do it, and your browser's compositor does it for the page you're reading. We had simply inherited the game-loop shape from the SDL/Emscripten template.

How it works: three rules
The whole discipline fits in three sentences.

One: every input to f, when it changes, must request a redraw. We call that invalidation.

Two: the frame loop draws only if there's an outstanding request.

Three — and this is the one that matters operationally: a missed invalidation is a frozen viewport. The mutation really happened, the data panel updates, the JSON mirror is correct — but the pixels never change. That's the entire risk surface of this feature, which is why most of the work wasn't the skip logic (that's about five lines) but systematically enumerating what f's inputs actually are.

Why it's a counter, not a flag — the part people get wrong
This is the detail I'd spend real time on, because it's the non-obvious coupling.

We use temporal anti-aliasing: we keep four accumulation layers, each holding the frame rendered with a slightly different sub-pixel offset, and the final image is their average. That's what gives us free 4× supersampling on a still camera.

The consequence: one rendered frame refreshes exactly one of those four layers. So if a change scheduled a single redraw, the screen would show one quarter new content and three quarters stale — a ghost that never clears, because nothing schedules the rest.

So "redraw" doesn't mean "draw a frame." It means "restart a convergence sequence" — schedule a full cycle of four. And the request is saturating, not additive: twenty invalidations in one tick still mean "the image is stale, converge again," not eighty frames of work. When temporal AA is switched off the resolve shows only the current layer, so one frame is already the finished image and we schedule one.

The generalizable point for your team: any renderer that accumulates over time couples invalidation to a convergence length. Path tracers have exactly the same rule with sample counts.

Two invalidations that aren't events
Most invalidation sources are ordinary — a setter runs, it requests a redraw. Two are not, and they're the interesting ones because nobody calls a function when they happen.

The first is the camera coming to rest. While you're orbiting we disable temporal AA, because averaging across a moving camera smears the image. That means every frame drawn during the orbit lands in the accumulation layers un-jittered, so when you let go, all four layers hold the same lower-quality picture. Under the old continuous loop the engine quietly drew four more frames and reconverged for free. Under render-on-demand nobody schedules them — so the viewport would stay permanently aliased after every camera move. We detect the moving-to-idle transition in the loop and schedule the reconvergence explicitly.

The second is an image export finishing. We skip the viewport entirely while an export runs, so the canvas still holds a pre-export frame when the progress overlay comes down.

Worth stating as a principle: state-machine transitions are invalidation sources too, and they're easy to miss precisely because there's no setter to hang the request on.

What stays correct for free — and why that's not luck
A reviewer should push on two things here, and both have real answers rather than "we tested it."

Click-to-select still works. We render object IDs into a second target during the same pass that produces the visible image. Because we skip both attachments together or neither, the ID buffer is always in exact correspondence with the pixels the user is looking at when they click. Skipping frames can't desynchronize them — it's a structural guarantee, not a timing accident.

The canvas doesn't go black. Not drawing is not the same as drawing nothing. The clear operation only happens if the pass runs; skip the pass and nothing touches those pixels. WebGPU canvas presentation is retained — the compositor keeps showing the last image you handed it. Think painted wall, not projector. Content is genuinely lost only on canvas resize and device loss, and both of those invalidate.

The honest caveat
Two, actually, and I'd raise them before anyone finds them.

Hovering the mouse over the viewport keeps the engine rendering, because gizmo hover-highlighting is a genuine visual state change and we request a redraw per pointer move. That's correct but conservative — a tighter version would only invalidate when the hovered axis actually changes. We chose correct-and-obvious first.

And this optimization does nothing for framerate while you're actively orbiting a heavy scene. It removes the cost of being idle, which is most of a working day, but the cost of a single frame is unchanged. That's the next item on the list (P1: the raymarcher evaluates every object's exact distance function at every step of every ray, whether or not that object is anywhere near the ray).

Quick answers for likely questions
"Isn't the loop still being called 60 times a second?" Yes — we return immediately from it. The cost we removed isn't the function call or the command encoding, both of which are microseconds; it's the submission that asks the GPU to march over a million rays. CPU-side per-frame cost was already essentially a single 256-byte upload.

"What if you miss an invalidation?" You get a viewport that doesn't update — visible immediately, and we built a force-continuous debug switch so you can confirm the diagnosis in one toggle. We also audited the engine's entire public setter surface mechanically rather than by eye, which is how we caught one selection path that changes the highlight without going through the usual route.

"Why not just diff the state and redraw when it differs?" That's a real alternative — content-based invalidation instead of event-based. Most of our per-frame state lives in one 256-byte uniform block, so a memcmp against the last submitted copy would catch camera, gizmo, selection and every render setting automatically, with no call-site auditing. It's much harder to get wrong and slightly less precise. We went event-based because it forced us to actually learn the state graph, and kept the diffing idea in our pocket.

7) FPS sayacı motorun gerçek karelerini ve GPU kare süresini ölçüyor

The one-liner
"The old FPS counter counted its own callbacks, so it would have reported a confident 60 on a completely idle engine. It now samples the engine's real submitted-frame count, and — more importantly — reports GPU frame time, which is the only number that can measure an improvement past 60fps."

Why the old meter had to change
This is a nice illustration of a general hazard: an optimization you can't observe isn't finished, and a meter that measures the wrong layer is worse than no meter.

The old counter incremented once per browser animation-frame callback. Those callbacks keep firing at the display refresh rate whether or not our engine draws anything. So the moment we shipped render-on-demand, the meter would have read a healthy 60 FPS on an engine that was drawing precisely zero frames. It measured the browser, not us.

The fix is straightforward: the engine keeps a monotonic count of frames it has actually submitted, and the meter samples that counter over an interval instead of tallying its own ticks. Now zero is a true reading — and it's the good case, so we display it as "Idle" in neutral grey rather than an alarming red. Training yourself to read success as failure is a real cost.

The more interesting problem: you cannot measure past vsync by counting frames
This is the part I'd make sure lands, because it corrects an intuition almost everyone has.

Our frame loop is driven by the browser's animation-frame callback, which fires once per display refresh. That's a hard ceiling: one frame per refresh, full stop. So no frame-counting metric — no matter whose frames you count — can ever read above 60 on a 60Hz display. A scene that could run at 300fps and a scene that runs at exactly 60 report the identical number. The metric is blind in exactly the region where you want to prove an optimization worked.

Frame rate is capped. Frame time is not. So we measure duration instead: we take a timestamp when we submit the frame's work, and the GPU tells us when it finished. That delta keeps falling below 16.6 milliseconds long after the frame rate has flatlined at the refresh rate.

The readout now shows both, and they answer different questions. Frames per second answers "is the app smooth right now." Milliseconds per frame answers "did my change make rendering cheaper" — that's the one we A/B optimizations with, and we show the uncapped rate it implies so the improvement is legible at a glance.

How we get the timing, and its limits
Browser WebGPU deliberately exposes no GPU timestamp queries — they're a fingerprinting and timing-attack vector, so the API withholds them. What we do have is a completion signal: we can ask the queue to notify us when everything submitted so far has finished executing. Timestamp at submit, timestamp in the callback, subtract.

I'd be upfront that this is a good relative measure, not a profiler. It includes queueing latency and excludes presentation. It's exactly right for "version A was 24ms, version B is 9ms," and it is not the number to quote as an absolute GPU cost. For that we'd use RenderDoc or Nsight against the native build, which is precisely why the native build exists.

One implementation detail worth mentioning, because it's a trap
We only keep one measurement in flight at a time. The completion callback is a heap object the graphics driver holds a raw pointer to; arming a second one while the first is still pending would replace and free a closure the driver is about to call — a use-after-free that surfaces as an inscrutable WebAssembly crash rather than anything resembling its cause. Same rule the banded export path follows. Sampling every few frames is plenty for a readout that updates twice a second anyway.

Quick answers for likely questions
"Why not just show milliseconds and drop FPS?" They're genuinely different questions, and the FPS line doubles as the render-on-demand indicator — "Idle" is how you confirm at a glance that the scheduler is working.

"Doesn't polling the engine twice a second cost something?" Two function calls across the WASM boundary per second. It is not measurable.

8) Camera movement a animasyon eklendi

9) Camera movement TAAA bug resolved

10) Scale gizmo

The one-liner
"Translate and rotate already existed, so the third gizmo was mostly plumbing — except for one design question the other two never had to answer: an SDF primitive cannot be scaled in every direction, so the gizmo only shows the handles the shape can actually use."

Why the handles differ per primitive
Our GPU `size` vector is derived from the editor's scale, and that derivation doesn't consult all three components. A sphere has one radius. A cone's width is an angle, and scaling an angle is meaningless. A torus' tube thickness is fixed. Every axial primitive is built around Y with XZ radial, so a cylinder's X and Z handles are literally the same knob — either one fattens it, Y makes it taller. Only the box is three-way independent.

So we derive the handles from a single table that is the inverse of the size formula: which scale component each local axis drives, or none. Handles that map to nothing are neither drawn nor grabbable. The argument for spending effort here: a handle that moves nothing is worse than no handle — the user drags it, nothing happens, and the reasonable conclusion is that the editor is broken. It also makes the gizmo teach the shape.

Why scale ignores the World/Local toggle
Scaling a rotated object along a world axis is a shear. Our primitives are position + rotation + size, which cannot represent one, so honouring the toggle would silently produce the wrong shape. The scale gizmo is always local and the toggle greys out to say so. Unity does the same thing for the same reason.

The drag feel
The multiplier is the mouse travel projected onto the handle, divided by the handle's own on-screen length: pull one handle length out and you double the object. Since the handle is a constant pixel size regardless of distance, the same gesture always means the same factor — measuring in raw pixels would make distant objects feel glacial. Like the other two drags it is absolute-from-start rather than incremental, so long drags don't drift and Esc lands exactly on the original value. The factor is floored just above zero, because zero isn't a small object, it's a degenerate distance field.

The one non-obvious implementation note (scale gizmo)
Scale drags undo through a different mechanism than translate and rotate. The other two write only the GPU-facing struct, which the per-object transform snapshot covers. Scale's authoritative value lives in the editor store, and the GPU size is derived from it — so restoring size without restoring scale would leave them disagreeing, and the next parameter edit would recompute size from the stale scale and make the object jump. Scale therefore uses the scene-edit snapshot, which does cover the editor store. Same reason the drag writes scale and re-derives size instead of writing size directly: a direct size write wouldn't survive a save/reload.

11) Sahne sınırları ile ışın kırpma — boş uzay atlanıyor (perf raporundaki P4)

The one-liner
"We wrap the whole scene in a bounding box and intersect each ray with it analytically before marching. Rays that miss the box do zero work. Rays that hit it start at the box instead of at the camera, and stop at the box instead of at the far clip. One benchmark scene went from 300 milliseconds a frame to 13."

The idea, and the one sentence that makes it click
Sphere tracing is a search. The marcher has no idea where the geometry is — it finds out by repeatedly asking "how far to the nearest surface?" and stepping that far. That's elegant, and it means every ray pays to *discover* empty space, one full scene evaluation per step.

But we know something the marcher doesn't: where the scene is. If every object lives inside a box, then a ray that never enters that box cannot possibly hit anything, and the space before it enters is provably empty. A box has a closed-form ray intersection — about 25 arithmetic operations, no loop. So we replace an iterative search over empty space with an analytic bound on it.

The generalizable principle: whenever a loop is searching a region, ask whether a cheap conservative bound on that region can answer the question without iterating. This is the same idea as bounding volume hierarchies in a ray tracer, frustum culling, and tile binning in deferred lighting — they differ only in granularity. What we built is literally a one-node BVH.

The counter-intuitive part: misses are more expensive than hits
This is the insight I'd spend the most time on, because it explains why the win was so much bigger than we first estimated, and it surprises people.

A ray that hits something terminates early — the moment the distance drops below the hit threshold, it breaks out. A typical hit converges in twenty or thirty steps.

A ray that misses has no early exit. It pays the full step budget, every time, and contributes nothing to the image.

Worse: the step size is the distance to the nearest surface, so a ray threading between small scattered objects stays close to *something* the entire way and crawls in tiny steps until it exhausts its budget. A scene of ten small objects with lots of space between them is close to the worst possible case — lots of near-surface volume, almost no geometry to terminate rays early.

So the pixels that look like they cost nothing were the most expensive pixels in the frame. That's exactly the work this removes.

What the numbers mean, and why they differ so much per scene
The honest formula for the ceiling: the speedup is bounded by one divided by the share of frame work done by pixels that actually hit something. That single expression predicts all our results.

Our sparse-background scene: 300ms to 13ms. Almost all the work was background rays riding the step cap.
Our dense-cluster scene: essentially unchanged. Geometry fills the frame, so there's no empty space to skip.
Our 48-object stress scene: also flat, because it contains a floor slab and off-screen objects, so the box is enormous and nearly every ray enters it.

I'd present that variance as evidence the implementation is correct rather than as an inconsistency. An optimization that claimed a uniform speedup across all three would be suspicious.

Getting the bound right — where all the real risk was
A bounding volume optimization has a brutal asymmetry: a box that's too big costs a little performance, and a box that's too small silently deletes geometry. Our own codebase had a previous attempt at a related optimization that was reverted for exactly that reason. So essentially all the engineering care went into "never too small."

Three decisions came out of that.

We bound each primitive with a sphere centred on its pivot, not a box. A sphere is rotation-invariant, so a rotated object needs no recomputation and can't be got wrong. The cost is looseness — a unit cube needs a sphere of radius 1.73 — and we accept it to buy exactness under a transform users apply constantly. The principle: pick the proxy that's invariant under the transforms you support.

We bound the *whole scene* with a box rather than a sphere, for the opposite reason: the world axes never rotate, and our scenes tend to be wide and flat (objects spread on a ground plane), where a sphere would be several times too loose. Same question, different answer, because the symmetry of the thing being bounded is different.

And we inflate the box by a margin, because smooth blending pushes surfaces *outward* past the objects that produced them. Interestingly, the hit threshold does *not* need a margin — stopping short of a surface stops inside the box, never outside. Reasoning about which direction each term errs is what keeps a margin honest rather than superstitious.

The subtle correctness point: bound what renders, not what's stored
The bound is computed by walking the CSG tree, not the object array. That sounds like a detail and isn't.

The tree is what gets compiled into the program the GPU actually executes. The object array is storage. They're normally identical, but they can diverge — a corrupt or hand-crafted project file can contain objects that no tree node references, and those objects are uploaded to the GPU yet drawn by nothing. Bounding them would inflate the box around geometry that isn't on screen.

More importantly, deriving the bound from the same source of truth as the render, at the same moment the render program is built, makes a stale bound structurally impossible rather than merely unlikely. You can't upload a new program without recomputing the box, because they're the same three lines of code.

Verification: make the invisible thing visible before you trust it
The process point I'd want a team to take away. We built this in three stages, and deliberately did not let stage one affect a single pixel.

First we computed the box on the CPU and printed it, then checked it against radii we'd worked out by hand for every primitive type. Several bugs surfaced in minutes as obviously wrong numbers.

Second we uploaded it to the GPU and used it for nothing but tinting the background green where a ray entered the box. You cannot see a bounding box, so we made it visible — and then confirmed it hugged the geometry, tracked edits, grew when we raised a blend radius, and crucially did not swim when the camera orbited (which would have meant a world-versus-view space mix-up).

Only third did we let it change the march. And that change shipped behind a compile-time switch that restores the old behaviour exactly, so "is this optimization responsible?" is one rebuild away from an answer — and we verified the image is pixel-identical against a benchmark scene built specifically to contain every clipping trap: rotated exotic primitives, subtract nodes, large smooth blends, deep nesting.

Effort went down each stage while blast radius went up. That's precisely backwards from comfortable, which is why the verification order mattered.

The honest caveat
This does nothing for the secondary rays. Every pixel that hits a surface still pays for soft shadows, ambient occlusion, normals and colour evaluation completely unclipped — and on a hit pixel that's usually *more* work than the primary march. So the win is concentrated entirely in empty space, which is why the dense scenes don't move.

It also doesn't help grazing rays that pass just outside a silhouette: those do enter the box, so they still crawl. Those need a different fix (a distance-proportional hit threshold).

And the box is only as tight as the scene is compact. One object far from the rest inflates it for everything. The fix for that is per-object and per-CSG-node bounds — the next item on the list, and the reason we wrote the per-primitive radius function as a standalone, hand-verified piece: it's exactly what that work needs.

Quick answers for likely questions
"Is this culling?" No, and the distinction matters. Culling removes things from consideration — frustum culling drops objects outside the view. Inside the box, we still evaluate every object at every step. What we changed is the interval of the *ray* we walk. The next optimization on our list is the one that's actually culling.

"Why a box and not a sphere?" At the scene level the box is tighter for the layouts this editor produces, and cheaper to compute correctly — a good bounding sphere of many spheres requires the box first anyway. At the per-object level we do use spheres, for rotation-invariance.

"What happens when the scene is empty?" The box is stored inverted — minimum greater than maximum. That's the identity element of the union we build it with, so it needs no special case, and the ray test rejects every ray against it automatically.

"What if the camera is inside the box?" The entry distance comes out negative, which is true and useless, so we clamp the march start to zero. That case happens constantly — any time you zoom in — and it's the single most likely place to introduce a bug here.

12) Register baskısı — bir sabit sayı, iki kat hız (perf raporundaki P7)

The one-liner
"We made the renderer twice as fast by changing one integer from 16 to 8. It removes no work — the shader computes exactly the same thing, in exactly the same number of instructions. What changed is how many copies of it the GPU can run at once."

The result
Eight benchmark scenes, camera and settings fixed, images pixel-identical: 12.2 seconds of frame time across the suite became 5.7. Every scene improved, between 23% and 60%. The heaviest scene — 48 objects — went from about 8.5 seconds a frame to 3.4.

For comparison, every algorithmic optimization we had attempted on the same shader, combined, was worth less.

The idea, and the sentence that makes it click
A GPU doesn't make a single thread fast. It makes thousands of threads and hides the fact that each one spends most of its life waiting for memory. When one group of threads stalls on a read, the scheduler instantly switches to another group that's ready. As long as there's always another group, the waiting is invisible and the machine runs at full speed.

The number of groups that can be resident at once is fixed by a budget: each thread's variables occupy registers, and a chip has a finite register file shared by everyone. So every extra variable a shader needs reduces how many threads fit — and therefore how much waiting can be hidden.

That is the whole thing: **registers are the currency you buy latency-hiding with.**

Our shader kept a CSG evaluation stack — actually four of them, sixteen slots deep. That's roughly ninety-six values held live per thread, for a data structure that was almost always nearly empty. We were spending most of our latency-hiding budget on empty stack slots.

How we knew where to look — the part I'd actually present
We didn't guess this. We arrived at it by elimination, and the eliminations are more interesting than the fix.

We first built a proper algorithmic optimization: skip an object's expensive exact distance function when a cheap bound proves it's far away. Textbook, correct, pixel-identical. It bought 12%.

That was disappointing enough to be informative, so we ran two diagnostics. We quadrupled the marching step budget — no change, so rays weren't running out of steps. We shrank a safety margin until shapes visibly corrupted — the timing didn't move, so the expensive distance functions weren't where the time went either.

Both results said the same thing: **the shader is not limited by arithmetic.** It's limited by waiting for memory. And once you believe that, "remove some math" is obviously the wrong lever, and "let more threads run so the waiting overlaps" is obviously the right one.

The lesson I'd draw out: a negative result is a result, if you designed it to discriminate between hypotheses. A 12% win that we couldn't explain was worth more than a 12% win we could have shrugged at.

The genuinely surprising part: smaller is not better
Having found that a smaller stack was faster, the obvious next step was to size it per scene. The required depth is knowable exactly — it's a property of how deeply the CSG tree nests, not how many objects there are, and a flat union of a thousand objects only ever needs two slots. So we computed it during compilation and compiled a shader variant to match.

It made things slower. Measurably, reproducibly.

The curve turned out to be a valley, not a slope: depth 4 was bad, 8 was best, 16 was bad again. That non-monotonicity is the interesting result, and the explanation is that we were never tuning a *quantity* — we were tuning which side of a compiler decision we landed on.

GPU registers can't be indexed at runtime. The register number is baked into the instruction, so there is no "read register number N" where N is a variable. That means an array you index dynamically cannot simply live in registers. The compiler has to choose: either keep every element in its own register and replace each access with a chain of conditional moves, or put the array in per-thread memory and use real loads and stores.

Small arrays get the first treatment, and it eats registers. Larger ones get the second, which costs almost no registers and lands in cache. So at depth 4 we were burning registers to avoid memory; at depth 8 the compiler gave up and spilled, which handed the registers back and let far more threads run. The spilled accesses cost maybe twenty-five cycles each, and we got that back many times over because the extra threads were also hiding the much larger stalls we already had. At depth 16 it spills too, but the spill space itself comes out of a limited pool and starts capping occupancy from the other direction.

Two different resources squeeze from opposite sides, and 8 is the gap between them.

The uncomfortable part I'd say out loud
Our per-scene sizing optimization *worked*. It correctly computed the minimum depth and correctly compiled a shader for it. And that is exactly why it hurt — it drove scenes down into the bad end of the valley.

"Did it do what I intended?" and "did it help?" are different questions, and we only found the difference by measuring. There is no version of reading the source code that would have revealed this.

So what did the machinery buy, if not speed?
Honest answer: the 2× came from the constant, which was always one line away. The per-scene infrastructure adds no performance.

What it does add is correctness. A fixed stack silently drops values when a program is deeper than it — the guard that prevents memory corruption also makes geometry disappear, with no error anywhere. Now the engine computes the exact requirement, picks a variant that fits, and prints a loud warning if a scene exceeds even the deepest one. Since project files are untrusted input, that matters.

It also made the number visible. Before, the safe stack depth was a hope; now it's printed for every scene you load.

The engineering details worth a slide
The required depth is computed *during* code emission rather than by a separate pass, so it can't disagree with the program it describes. A second traversal would be a second copy of the emission rules, and the day they drift is the day the stack is silently too small.

Sibling order matters, and reordering is free for some operators and forbidden for others. Emitting the deepest subtree first minimizes peak stack usage — a classic compiler result — and collapses accidental nesting to depth 2. But it's only valid where the operator is associative and commutative: plain union and intersection qualify, subtraction doesn't, and the smooth-blend variants aren't even associative. Reordering those would silently change the rendered shape.

The depth is injected into the shader source as text before compilation, because shader array sizes must be compile-time constants — you cannot pass this in a uniform. One compiled pipeline is cached per size.

We also found a real bug on the way: the compiler counted a child node that emitted no instructions, producing an operator with nothing to operate on. Harmless — a runtime guard caught it — but it was a dead instruction sitting in a loop the GPU walks at every single marching step.

The honest caveats
The optimum is 8 *on this machine, with this driver.* The compiler's threshold is undocumented and vendor-specific; on other hardware the valley could sit elsewhere, and the code has no way to discover that at runtime. We ship a measured constant with the measurement written next to it.

The mechanism is inference, not verification. The occupancy argument is textbook. The specific claim about where the compiler switches strategies is a hypothesis that fits a U-shaped curve — confirmable in ten minutes with a GPU profiler on the native build, which we haven't done.

And measurement noise on this suite is 6–10% run to run, which is worth stating whenever quoting any of these numbers. It's also why the 12% algorithmic result was genuinely ambiguous rather than merely small.

Quick answers for likely questions
"Why not just always use the biggest stack?" Because that's what we were doing, and it cost half the frame rate.

"Why can't the shader size the array at runtime?" Shading languages require compile-time array sizes, and the underlying reason is the same one that makes this whole problem exist: registers are allocated statically, per thread, before the shader ever runs.

"Isn't 8 arbitrary?" It's measured, not chosen. The comment above the constant carries the table it came from and an instruction to re-measure before changing it.

"Does this help other engines?" The specific number doesn't transfer. The method does: before optimizing what a shader computes, check what limits how many copies of it can run.

"Why didn't you just return early for rays that miss?" Because the grid floor is drawn analytically, not as SDF geometry, on the same code path. Returning early would have left the scene floating in a void with no horizon. We skip the loop and fall through instead.