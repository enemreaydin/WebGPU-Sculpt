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