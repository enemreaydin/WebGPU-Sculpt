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

The one non-obvious implementation note
Scale drags undo through a different mechanism than translate and rotate. The other two write only the GPU-facing struct, which the per-object transform snapshot covers. Scale's authoritative value lives in the editor store, and the GPU size is derived from it — so restoring size without restoring scale would leave them disagreeing, and the next parameter edit would recompute size from the stale scale and make the object jump. Scale therefore uses the scene-edit snapshot, which does cover the editor store. Same reason the drag writes scale and re-derives size instead of writing size directly: a direct size write wouldn't survive a save/reload.