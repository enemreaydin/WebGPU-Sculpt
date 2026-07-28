# CLAUDE.md

Signed-distance-field (SDF) editor. A C++ engine compiled to **WASM** (Emscripten) renders SDFs on the GPU via **WebGPU** straight to a `<canvas>`; a **React/TS** app is the UI and drives the engine through a thin C bindings layer. A native build (SDL + wgpu-native, engine viewport only) exists for quick engine iteration.

> **This file is both the agent instructions and the project's teaching guide.** It is deliberately long and explanatory: read the section relevant to your task in full before editing. The **load-bearing invariants** and **design preferences** sections are hard rules — breaking them causes silent corruption or violates how the owner wants the codebase shaped.

---

## 1. The one mental model (read this first)

**The engine owns all state. The web layer is IO/UI/connection only.**

Everything that *is* the document — the scene (primitives), the CSG tree (groups/boolean ops), the selection, per-object editor attributes (names, visibility, rotation…), and the render settings — lives in C++ and is authoritative there. The React app holds **no authoritative domain state**. It is a view: after every mutation it re-reads a JSON snapshot of the engine and rebuilds its local mirror.

```
App.tsx (React UI)
  → engine.ts (typed facade over ccall)
    → Module.ccall("sdf_*", …)
      → WebBindings.cpp (sdf_* C functions, one per Application method)
        → Application (the g_App singleton; main.cpp constructs it)
          → SceneManager · CsgTree · EditorObjectStore · SelectionState
          → SdfRenderer (WebGPU pipeline + buffers) · Camera · GizmoController
          → FrameScheduler (should we draw?) · FrameStats (what did it cost?)
```

The single most important consequence: **new engine capability crosses the boundary only through a `sdf_*` binding + the `engine.ts` facade.** Never make the web side compute or store domain truth, and never reach around the bindings. If you find yourself adding domain logic to `App.tsx`, stop — it belongs in the engine.

Two data paths cross the boundary:
- **Control (JS → C++):** `App.tsx` calls `engine.setX(...)` → `sdf_set_x(...)` → `Application::SetX(...)` → mutates a subsystem → calls `OnSceneChanged()` (which recompiles + re-uploads GPU buffers).
- **Mirror (C++ → JS):** `engine.getScene()` → `sdf_get_scene_json()` → `Application::GetSceneJson()` returns the whole scene as JSON. `App.tsx refresh()` parses it and rebuilds `this.state` (`objects`, `tree`, `render`, selection…). **The UI never owns values; it reflects them.**

Rendering is separate from both: the engine's `MainLoop` runs on its own animation-frame loop (driven by SDL/Emscripten), independent of React. React does **not** re-render per frame.

🔴 **The loop is invalidation-driven, not continuous (§6a).** `MainLoop` is still *called* every vsync, but it only draws when something requested a redraw. So a third obligation joins the two data paths above: **any control call that changes what the image looks like must also invalidate.** In practice that means calling `Application::RequestRedraw()` (or routing through `OnSceneChanged`/`OnCsgChanged`/`UpdateSelectionHighlight`, which already do). A setter that mutates render-visible state without invalidating produces a *frozen viewport* — the mutation is real and the JSON mirror shows it, but the pixels never update.

---

## 2. End-to-end data flow (three worked traces)

### 2a. "User adds a cube"
1. `App.tsx` → `engine.addPrimitive(Box)` → `sdf_add_primitive(1)` → `Application::AddPrimitive(Box)`.
2. `AddPrimitive` builds an `SdfObjectData` via `SdfObjectFactory`, appends it to `SceneManager::m_Objects`, and asks `EditorObjectStore` to create the editor record (default name, params derived from the factory size).
3. It attaches a new **Primitive** `CsgNode` under the root Union (`SceneManager::AddObject` → `EnsureRootUnion` + `CsgTree::AddPrimitiveNode`), updates selection, then calls `OnSceneChanged()`.
4. `OnSceneChanged()` → `SceneSerializer::RefreshSceneBuffer()`: builds the objectId→index map, calls `CsgTree::CompileToRPN`, and uploads **both** the objects array and the RPN program to the GPU (`SdfRenderer::UpdateSceneBuffer`).
5. Back in JS, `App.tsx` calls `refresh()` → new JSON → `state.objects`/`state.tree` rebuilt → outliner + panels update.
6. `OnSceneChanged()` also called `RequestRedraw()`, so the `MainLoop` — which would otherwise be idling — draws the next frames from the freshly-uploaded buffers (§6a).

### 2b. "User drags a slider / moves an object"
`App.tsx` → `engine.setObjectPosition(index, x, y, z)` → `sdf_set_object_position` → `Application::SetObjectPosition` mutates `m_Objects[index].position`, calls `OnSceneChanged()` (re-upload), records an undo entry. Then `refresh()`. The change is visible next frame: there is still no explicit "render" call, but `OnSceneChanged` invalidated, so the loop wakes and draws the current buffers (§6a).

### 2c. "User clicks in the viewport to select"
1. The scene is drawn to **two** color targets: the visible image, and an **R32Uint object-id target** where each pixel stores the winning object's **vector index**.
2. On click, `App.tsx` converts the mouse event to **render/device pixels** with `canvasPoint(e)` (never raw CSS coords), calls `engine.pickObject(x,y)` → `sdf_pick_object` → `PickObjectAt` copies that one pixel to a readback buffer and `mapAsync`-reads it. `0xffffffff` → empty space.
3. The returned **vector index** is fed to `SelectByPick(index, mode)`, which maps index → object → objectId → the **top-level** CSG nodeId, and updates `SelectionState`. `refresh()` mirrors the new selection.

---

## 3. Build & run (Windows; adapt paths elsewhere)

- **`cmake` is not on PATH** here → use the full path:
  `"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`.
  Emsdk is bundled at `deps/emsdk` (activate `deps/emsdk/emsdk_env.ps1` / `.sh` first).
- **WASM engine build:** `cmake --build build-web --target main` → emits `web/public/engine/sdf-engine.{js,wasm,data}`. First time only: `emcmake cmake -S . -B build-web`. **Rebuild whenever any C++ *or* shader changes.**
- 🔴 **Shaders are baked into `sdf-engine.data` at link time** (`--preload-file`); the engine reads them from the WASM virtual FS at `/assets`, **not from disk**. Editing a `.wgsl` does nothing until you rebuild the `main` target — `npm run dev` alone keeps running the old shader. Symptom: a WGSL compile error whose reported line doesn't match the file on disk. Verify with `grep -c <newSymbol> web/public/engine/sdf-engine.data` and compare the `.data` mtime against the `.wgsl` mtime.
- If a build silently produces no new `.data`, it probably **aborted during CMake reconfigure** (any `CMakeLists.txt` edit forces one, and reconfigure needs Python on PATH via the emsdk env). A failed reconfigure means nothing relinks.
- **Web UI:** `cd web && npm run dev` (Vite, :5173). Typecheck: `cd web && npx tsc --noEmit`.
- **Compile-check one C++ TU without a full link** (fast red/green while the link is pending, e.g. another method not yet defined):
  `em++ -std=c++17 -Isrc -Ideps/glm -Ideps/SDL/include -Ibuild-web/_deps/webgpu-backend-emscripten-src/include -Ibuild-web/deps/SDL/include-config-release/build_config -Ibuild-web/deps/SDL/include-revision -c <file.cpp> -o /dev/null`.
  If `em++`/Python aren't on PATH in a plain shell, drive `emcc.py` with the bundled interpreter directly:
  `EM_CONFIG=deps/emsdk/.emscripten EMSDK_PYTHON=deps/emsdk/python/3.13.3_64bit/python.exe deps/emsdk/python/3.13.3_64bit/python.exe deps/emsdk/upstream/emscripten/emcc.py -std=c++17 <same -I flags> -c <file.cpp> -o /dev/null`.
- **There is no test suite.** To verify a change: build the WASM, `npm run dev`, and drive it in the browser (Chrome/Edge — WebGPU + File System Access are Chromium-only).

---

## 4. The three id spaces — the #1 source of bugs

Three different integers can all look like "the id of a thing." **Never mix them.**

| Space | Lives on | Property | Who speaks it |
|---|---|---|---|
| **`objectId`** | `SdfObjectData.objectId` | Permanent, from an ever-increasing counter. Never reused, never == array index. | Primitives; CSG Primitive nodes reference it. |
| **`nodeId`** | `CsgNode.nodeId` | Permanent, from `CsgTree::m_NextNodeId`. Never reused, never == vector index. | Selection, **all JSON**, the outliner. |
| **vector index** | position in `m_Objects` / `m_Nodes` | **Transient.** | `CsgTree` *public methods*; the object-id pick buffer (`cmd.operand`). |

Rules that fall out of this:
- **`CsgTree` public methods take vector indices**, but selection and every JSON field speak **nodeId**. Convert with `CsgTree::IndexForNodeId(nodeId)` — which **must skip `!alive` nodes** (a dead nodeId must resolve to `-1`, or callers act on a tombstone).
- **The node vector never shrinks.** `RemoveNode` tombstones (`alive = false`) in place, so node indices are stable and every `parent`/`child`/`sibling` link stays valid.
- **The object vector *does* erase** (`RemoveObjectAt`) → object indices shift. **Always key on `objectId`**, never a stored object index.
- **Pick returns a vector index, not an objectId.** `SelectByPick` does index → `m_Objects[index].objectId` → `TopLevelNodeIdForObject` → nodeId.

**Worked example.** Say you have Sphere (objectId 7, node 3), Box (objectId 8, node 4), unioned under root (node 0). `m_Objects = [Sphere@0, Box@1]`. Delete the Sphere: the object vector erases → Box shifts to index 0, but `Box.objectId` is still 8. Its CSG node 4 still says `objectId = 8` — unaffected. The RPN recompile rebuilds the objectId→index map fresh (`Box: 8→0`), so `cmd.operand` for the box is now 0. Everything keyed on the permanent ids survives; anything that had cached "the box is at index 1" is now wrong.

---

## 5. Core data structures (all GPU-facing structs are size-locked)

### `SdfObjectData` — one primitive (80 bytes, `static_assert`-locked)
```
int32 type          // SdfObjectType 0..8 (Sphere,Box,Cylinder,Torus,Link,Cone,Capsule,SolidAngle,Pyramid)
int32 objectId      // permanent identity
int32 parentId      // reserved (future transform hierarchy; NOT the CSG tree)
int32 selected      // 1 = highlight in shader; set by UpdateSelectionHighlight for every
                    //     primitive under any selected node (whole group lights up)
float[4] position   // xyz used, w=0
float[4] rotation   // quaternion (x,y,z,w), identity (0,0,0,1)
float[4] size       // xyz interpreted per primitive type (see the shader switch); w reserved
float[4] color      // rgb; a reserved
```
It's a POD blitted straight into the GPU storage buffer and straight into `.sdfp`. `size` is **derived** from editor params+scale (see §7 / EditorObjectStore) — it is not independently authored.

### `CsgNode` — one node of the CSG tree
Array-based n-ary tree using **first-child / next-sibling** links (so an op node can have any number of children without variable-size fields). Fields: `type` (`CsgNodeType`), `nodeId`, `objectId` (valid only for Primitive), `parent`, `previousSibling`, `nextSibling`, `firstChild`, `lastChild`, `blendMode` (Hard/Smooth), `blendRadius`, `modifierType`/`modifierAmount`/`modifierEnabled` (roundness), and `alive` (tombstone). `CsgNodeType` = `Primitive, Union, Subtract, Intersection, Group, Modifier`. **`Group` is kept last** so the 1/2/3 boolean values never shift (they're used in JSON, `ApplyOperation`, and the shader command mapping). `Group` compiles exactly like Union but is a distinct type so the UI can label it. `Modifier` is a legacy on-disk value only — modern modifiers are node metadata (`modifier*` fields), normalized on load by `NormalizeLegacyModifierNodes`.

### `CsgCommand` — one RPN instruction (16 bytes, `static_assert`-locked, GPU-matched)
```
int32 cmdType   // CsgCmdType: 0 PUSH_OBJECT, 1 UNION, 2 SUBTRACT, 3 INTERSECTION,
                //             4 SMOOTH_UNION, 5 SMOOTH_SUBTRACT, 6 SMOOTH_INTERSECTION
int32 operand   // PUSH_OBJECT: real array index into scene_objects (NOT objectId)
int32 nodeId    // OP_*: the op node's permanent id (for future naming of intermediate results)
float parameter // PUSH_OBJECT: roundness modifier amount; OP smooth: blend radius k
```
This struct is mirrored in WGSL (`struct CsgCommand`). It's part of the GPU contract — changing it means editing the shader too.

### `GlobalsData` — the single per-frame uniform (256 bytes, 16-aligned) — see §8.

---

## 6. Subsystem deep-dives

### `Application` (`src/editor/Application.{h,cpp}`) — the control API + god object
Owns every subsystem by value and exposes the entire control surface the bindings call. Key responsibilities:

- **`MainLoop()`** (per vsync): bail if the pipeline/targets aren't ready → `StepFinalRender` (advance an in-flight export) → export/redraw **skip guards** (§6a) → `UpdateCamera()` → camera-idle edge check → compute `view`/`proj` from the camera → `UpdateGizmoGlobals(view,proj)` → `SyncRenderGlobals()` → advance `m_JitterIndex` → write the 256-byte `globals` uniform → acquire the swapchain texture → encode the scene pass (6-vertex full-screen quad → **two** color attachments: accumulation layer + object-id) and the resolve pass → submit → `FrameStats::OnFrameSubmitted` → `FrameScheduler::Consume()`. `globals` is a **per-frame mirror, never authoritative storage** — it's rebuilt from scratch every frame from the camera, gizmo, and `appSettings`.
  🔴 **Guard placement is load-bearing.** `StepFinalRender` must run *above* the skip guards (an export is a state machine that advances once per iteration; skipping it hangs the export forever). The redraw guard must sit *above* `UpdateCamera` — everything below it either acquires a resource released at the bottom of the function (the command encoder, the swapchain texture view) or advances per-frame state (`m_JitterIndex`), so returning from further down leaks a WebGPU object per idle frame and desyncs the accumulation layers. `Consume()` runs only after a real `submit`, never on an early-out.
- **Scene-edit transactions (undo grouping).** `BeginSceneEdit(name)` / `CommitSceneEdit()` bracket a logical edit; nested calls use a depth counter (`m_SceneEditDepth`) so only the outermost commit records. A single click-setter (no bracket) records its own one-step undo via `ShouldRecordSceneEdit()` + `FinishSceneEdit()`. **Caveat:** render-setting changes inside a Begin/Commit bracket don't set the "changed" flag (only `OnSceneChanged` does), so a *bracketed* batch of pure render-setting writes currently records **no** undo entry — this is why slider drags and profile-applies aren't individually undoable. Un-bracketed single render-setting clicks *are*.
- **`OnSceneChanged()`** vs **`OnCsgChanged()`**: the first re-uploads objects **and** the recompiled RPN program (`RefreshSceneBuffer`); the second re-uploads only the program (`RefreshCsgBuffer`). Call one after any mutation.
- **`GetSceneJson()`**: hand-rolled JSON emitting `selectedIndex`, `showGrid`, the `objects[]` (merged with their EditorObject records), engine-owned `selectedNodeIds`/`primaryNodeId`, the CSG `tree` (nodeIds + children), and the `render` settings object. This is the entire C++→JS mirror.
- **Selection highlight** (`UpdateSelectionHighlight`): recomputes `selectedObjectIndex` (the primary primitive's vector index, for the single-object gizmo/props path) and sets the `selected` flag on every primitive under any selected node, then re-uploads. Selection is user-driven, not per-frame, so re-upload here is cheap.
- **Gizmo** methods (`GizmoHover/BeginDrag/Drag/EndDrag/Cancel`, `UpdateGizmoGlobals`, `ComputeGizmoTarget`, `SyncGizmoScaleAxes`): translate + rotate + scale. See §6 GizmoController.

### 6a. `FrameScheduler` + `FrameStats` — render-on-demand and its measurement

**The idea.** A game re-renders every frame because the world evolves on its own. An **editor's image is a pure function of state you only change deliberately** — `image = f(scene, camera, selection, gizmo, settings, canvasSize)` — so if no input to `f` changed, the next frame is provably identical to the last. Re-marching 1.44M rays to reproduce it is 100% waste. `FrameScheduler` (`src/editor/FrameScheduler.{h,cpp}`) turns the loop from continuous into **invalidation-driven**, the same model Blender / the Unity & Godot editors / the browser compositor use.

**The API is three calls:** `Request(n)` (saturating — `n = max(n, pending)`, so twenty invalidations in one tick don't queue eighty frames), `ShouldRender()`, `Consume()` (floored at 0). Plus `ForceContinuous(bool)`, a debug escape hatch that pins the loop back to per-vsync drawing — if a bug disappears with it on, the cause is a missing invalidation, not the thing being investigated. (It has no `sdf_*` binding yet.)

🔴 **Why it's a counter and not a boolean.** `fs_resolve` averages up to `SdfRenderer::kAccumLayers` (4) accumulation layers, and one rendered frame fills exactly **one** layer (§7a). A single redraw after a change would therefore leave the image at 1 sample and never improve, because nothing schedules the rest. **Invalidation restarts a convergence sequence**, so every request must cover a full cycle. That is what `Application::RequestRedraw()` centralizes: `frameScheduler.Request(viewportQuality.taaEnabled ? kAccumLayers : 1)` — with TAA off one sample already *is* the final image and asking for 4 would burn 3 redundant raymarches per edit. `RequestRedraw()` **also sets `m_AccumReset`**, which is what makes the sequence restart at layer 0 instead of averaging in frames from the previous state (§7a). **Never call `frameScheduler.Request` directly; call `RequestRedraw()`** — bypassing it schedules frames without invalidating the accumulation, which is exactly the ghosting bug.

**Where invalidation comes from.** Most mutations funnel through `OnSceneChanged` / `OnCsgChanged` / `UpdateSelectionHighlight`, which all request — so Undo/Redo, project load, CSG edits and selection are covered for free. The rest request individually: the camera setters (`CameraOrbit/Pan/Zoom/Snap`, `FrameSelected/All`), every gizmo entry point, `SetRenderSetting` / `SetQualitySetting`, `OnResize`, and the small inline setters in `Application.h` (`SetShowGrid`, `SetSelectionVisuals`, `SetGizmoEnabled/Mode/Space/PixelScale`) — those are easy to miss precisely because they don't appear in the `.cpp`.

🔴 **Invalidate on content, not on the event — a redraw request is not free any more.** `GizmoHover` runs on *every* pointer move and `SetGizmoEnabled/Mode/Space` + `SetShowGrid`/`SetSelectionVisuals` are re-pushed wholesale by `syncGizmo`/`syncPreviewVisibility` whenever any one of them changes, so most calls write the value they already held. They therefore compare first and only `RequestRedraw()` on an actual change (`GizmoHover`: only when the *hovered axis* differs). Since `RequestRedraw` also resets the temporal accumulation (§7a), an event-based invalidation here doesn't just waste frames — it pins the viewport at one sample for as long as the events keep arriving, so merely moving the cursor over the viewport looks identical to TAA being switched off. `SelectObject` also requests: unlike the other selection paths it never reaches `UpdateSelectionHighlight`, yet it writes `selectedObjectIndex` → `globals.selectedIndex` → the in-shader highlight.

**Two invalidations are *transitions*, not events** — nobody calls a setter when they happen, so `MainLoop` detects the edges itself (`m_CameraWasMoving`, `m_LastExportState`):
- **Camera moving → idle.** Frames drawn during a camera move force `taaEnabled = 0` and zero the jitter, so the 4 layers end up holding 4 identical un-jittered images. Continuous rendering used to silently reconverge afterwards; on-demand must schedule it, or the viewport stays permanently aliased after every orbit.
- **Export state change.** The viewport passes are skipped for an entire export, so the surface still holds a pre-export frame when the overlay comes down. It redraws on *every* edge rather than guessing which one uncovers the canvas.

**What stays correct for free, and why:** picking reads the object-id target, which is written by the *same pass* as the visible image — skip both or neither, so the id buffer is always consistent with the pixels the user clicked on. And a skipped frame doesn't blank the canvas: WebGPU canvas presentation is retained, and the `LoadOp::Clear` only happens if the pass runs. Content is genuinely lost only on canvas resize and device loss.

**`FrameStats` (`src/editor/FrameStats.{h,cpp}`) measures what the scheduler makes unmeasurable.** Two numbers, two questions:
- **`FrameCount()`** — frames actually submitted, sampled by the web FPS meter. This replaced the old self-counting rAF meter, which would report 60 on a completely idle engine (rAF keeps firing whether or not the engine draws). Sampling the engine counter makes **0 a true reading, displayed as "Idle"** — the *good* case, so it's coloured neutral, not red.
- **`GpuFrameMs()`** — GPU cost of a frame, timed `submit` → `queue.onSubmittedWorkDone`, lightly smoothed. 🔴 **Frame *rate* can never exceed the display refresh** (the loop is rAF-driven), so it saturates at 60 and cannot show headroom; frame *time* is not vsync-bound and keeps falling below 16.6 ms. **This is the number to A/B an optimisation with.** It is approximate — it includes queue latency, excludes presentation, and browser WebGPU exposes no real GPU timestamp queries; for absolute numbers use RenderDoc/Nsight on the native build.

Only one measurement is in flight at a time: arming a second `onSubmittedWorkDone` while the first is pending would replace the `unique_ptr` and free a closure the driver is about to call (§13). The measurement state is a `shared_ptr` the lambda captures **by value**, so it outlives the owner, and a still-pending callback is parked in a file-static vector rather than destroyed.

### `SceneManager` (`src/scene/SceneManager.{h,cpp}`)
Owns `m_Objects` (the primitive vector) and the `CsgTree`. Provides add/remove/group, the objectId↔node maps, `EnsureRootUnion` (self-heals a dead/missing root), `RebuildAfterLoad` (after deserialization: restores the id counter, validates tree↔object consistency, and falls back to a flat default Union if the loaded tree is missing or inconsistent — see §11), and `BuildObjectIdMap` (fresh objectId→index map used by RPN compile). `CollectSubtreeObjectIds` gathers all descendant primitive objectIds of a node (used by group color/scale, gizmo pivots, highlight).

### `CsgTree` (`src/scene/csg/CsgTree.cpp`) — the tree + RPN compiler
The heart of the "what shape is this" logic. Nodes live in a flat `m_Nodes` vector; the root is `m_Root` (a vector index). Structural ops (`AttachChild`, `Detach`, `Reparent`, `RemoveNode`) maintain the first-child/next-sibling links; `RemoveNode` recursively tombstones a whole subtree.

**`CompileToRPN(objectIdToIndex)` → `std::vector<CsgCommand>`** is the bridge to the GPU. It postorder-walks the tree:
- A **Primitive** node emits one `PUSH_OBJECT` whose `operand` is `objectIdToIndex.at(objectId)` (permanent id → current array index; `.at()` throws on a stale id so bugs fail fast rather than silently corrupt — external files are pre-validated in `RebuildAfterLoad` so this can't throw on load).
- An **operation** node emits its children, then **one binary op command after every child past the first** — n children → n−1 ops (n-ary folded to binary). Hard vs Smooth is chosen from `blendMode`; the op's `parameter` carries the blend radius.

**Worked example.** Union{ Sphere, Box, Cylinder } compiles to:
`PUSH sphere, PUSH box, UNION, PUSH cylinder, UNION`.
The GPU executes it as a stack machine: push sphere-dist, push box-dist, UNION pops two and pushes `min`, push cylinder-dist, UNION again → one distance on the stack. Because ops are emitted *as it goes*, a flat union keeps the stack depth at **2** no matter how many children — **stack depth grows with tree *nesting*, not object count** (relevant to the fixed GPU stack, §7/§11).

### `EditorObjectStore` (`src/editor/EditorObjectStore.*`)
Holds everything the POD `SdfObjectData` can't: per-object `EditorObject` (name, visibility, lock, rotationEuler, non-uniform scale, shape params) keyed by **objectId**, plus **group/operation node names** in `m_NodeNames` keyed by **nodeId** (groups have no objectId). It also owns the type counters for default naming and the `.sdfp` editor-attrs (de)serializer (`WriteToStream`/`ReadFromStream`). The SDF `size` on `SdfObjectData` is **derived** from `params × scale` via `PrimitiveGeometry::ComputeSize` — so a param or scale edit recomputes size, and it survives save/reload (a raw size write would not).

### `SelectionState` (`src/interaction/SelectionState.h`) — engine-owned selection
`selectedNodeIds` (in click order), `primaryNodeId` (the active/last-clicked node; drives the 3D highlight; **must stay default `-1`**), and `selectedObjectIndex` (the primary primitive's vector index, for the shader highlight). **The Subtract base is the FIRST-selected node, not the primary** — `ApplyOperation` builds children straight from `selectedNodeIds`, so "click A, then B, Subtract" = `A − B` (A becomes `firstChild`, the RPN base). UI selection keys: `"scene"` (root), `String(objectId)` (primitive), `"g"+nodeId` (group/op).

### `Camera` (`src/camera/Camera.*`) + `CameraController`
Orbit camera. `GetViewMatrix`/`GetProjectionMatrix(aspect)`; the projection's `focalLength 2.414` corresponds to a 45° vertical FOV and **must match** the shader's hardcoded `focalLength = 2.414`. `GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined in `Camera.h` **before** any glm include (WebGPU wants [0,1] clip depth). `CameraController` turns per-frame `ViewportInput` (orbit/pan/zoom deltas, fed by `sdf_camera_*`) into camera moves using the sensitivities in `Settings`.

### `GizmoController` (`src/interaction/GizmoController.h`) — header-only interaction state machine
The hover/drag logic for the **translate**, **rotate** and **scale** gizmos, deliberately decoupled from `Application`: it takes mouse/origin/view/proj per call and holds no engine refs. `GizmoMath.*` is the pure projection/hit-test math. The gizmo is **drawn by the fragment shader** (not an HTML overlay) — `UpdateGizmoGlobals` writes world-space origin + axis endpoints into `globals`, and `overlayGizmo` in the shader projects them with the same camera basis the rays use, so it's frame-exact even during orbit. It writes color only, never `object_id`, so it's never pickable.

**The three modes share one skeleton and differ in three places.** Translate and scale draw the *same* three straight handles (differing only in the end cap — dot vs box) and therefore share `HitTestTranslateAxes`; rotate uses `HitTestRotateRings`. Each mode then has its own drag solver: `DragTranslate` (mouse projected onto the screen-space axis → a world offset), `DragRotate` (angle swept between the grabbed spoke and the current one), `DragScale` (mouse travel along the handle, **measured in handle lengths** → a multiplier, so one handle length out = 2×, floored at `MIN_SCALE_FACTOR` because a zero/negative size is a degenerate SDF, not a small object). All three are **absolute-from-drag-start**, never incremental, so there is no accumulated drift and Esc lands exactly back on the start pose.

🔴 **Two rules are specific to scale, and both come from what the data model can represent.**
1. **Scale is always LOCAL** (`GizmoController::EffectiveSpace()` overrides the user's World/Local toggle, and the web toggle greys out in scale mode). `EditorObject::scale` is defined per local axis; scaling a *rotated* object along a world axis is a shear, and `SdfObjectData` (position + quaternion + size) cannot express one. Honouring the toggle would silently produce the wrong shape.
2. **Only the handles a shape can actually use are drawn or grabbable.** `PrimitiveGeometry::ScaleAxisMap(type)` is the inverse view of `ComputeSize`: which `scale` component each local axis drives, `-1` = none. Every axial primitive is Y-axis with XZ radial, so a cylinder maps X and Z to the *same* radius component (either handle fattens it) and Y to height; a sphere maps all three to its single radius (always uniform); a cone's X/Z are `-1` because its flare is an unscaled angle; only the box is three-way independent. `Application::SyncGizmoScaleAxes` pushes the mask into the controller from every gizmo entry point, `HitTestTranslateAxes` skips masked axes, and the mask reaches the shader as the **`w` component of `gizmoAxis*`** (1 = draw, 0 = skip) — reusing the std140 padding those `vec4`s already carried, so it cost no uniform space (§8 is full). A handle that moves nothing is worse than no handle: the user drags it and concludes the editor is broken.

**Scale drags undo through the scene-edit bracket, not `TransformTransaction`** (`GizmoDragState::usesSceneEdit`, chosen at `BeginDrag` and read back at `End/CancelDrag`). `TransformTransaction` snapshots `SdfObjectData` only; scale's authoritative value lives in `EditorObjectStore`, so restoring `size` without `scale` would leave them desynced and the next param edit would recompute `size` from the stale scale and jump. `SceneEditSnapshot` covers the editor store, so it is the correct mechanism. For the same reason the drag writes `eo->scale` and re-derives `size` via `ComputeSize` — never `size` directly, which would not survive a save/reload.

### `SdfRenderer` (`src/renderer/SdfRenderer.*`) — WebGPU pipeline + buffers
`Initialize` builds the shader module (concatenated `sdf_core.wgsl` + `main.wgsl`), a 3-entry bind group layout (binding 0 = scene objects storage, 1 = `globals` uniform, 2 = CSG RPN program storage), the globals buffer (`sizeof(GlobalsData)`), the pick readback buffer, and the **scene** render pipeline (`fs_main`, two color targets: swapchain format + `R32Uint` object-id). It **also** builds a second, separate **resolve pipeline** (`fs_resolve`, one swapchain-format target) with its own 2-entry bind group layout (binding 1 = `globals` uniform, binding 3 = the accumulation texture array) — the temporal-AA resolve pass (§7a). **Buffers are reused when sizes match**: `UpdateSceneBuffer` only recreates the storage buffers + bind group when the object/program *byte size* changes; otherwise it just `writeBuffer`s new contents. So steady-state per-frame CPU cost is a single 256-byte uniform write. `CreateRenderTarget` (re)allocates the image + id targets **and the 4-layer accumulation array** (`accumTexture`, one 2D-array texture; 4 per-layer 2D views for rendering into, one `_2DArray` view for sampling) on resize, and rebuilds `resolveBindGroup` (which points at the array view). `ReadObjectIdAt` copies one pixel to the readback buffer and `mapAsync`-reads it (the returned callback `unique_ptr` is stored in a member — see §12).

### `SceneSerializer` (`src/serialization/SceneSerializer.*`)
`RefreshSceneBuffer` (recompile RPN + upload objects+program) and `RefreshCsgBuffer` (upload program only) are the two "push state to GPU" entry points. Also holds the native new/open/save-to-`.sdfo` paths (native build only).

---

## 7. The shading pipeline (`sdf_core.wgsl` + `main.wgsl`)

`sdf_core.wgsl` is concatenated **before** `main.wgsl`. WGSL module scope is order-independent, so `sdf_core` functions may read the `globals` uniform even though it's declared in `main`.

### The ray and the march (`fs_main` in `main.wgsl`)
Each of the 6 quad vertices covers the screen; the fragment shader runs per pixel. It reconstructs a camera basis (`forward/right/up`) from `globals.cameraPos`/`cameraTarget`, builds a ray direction with `focalLength 2.414`, then **sphere-traces**: step along the ray by the current SDF distance until the distance drops below `hitEpsilon` (hit) or exceeds `maxDistance`/`maxSteps` (miss). Sphere tracing relies on the SDF returning a *true or conservatively-underestimated* distance — that's why every optimization below must never *over*-estimate.

### Two SDF evaluators — deliberately split, never merge
- **`distanceCalc(p) -> f32`** — distance-only RPN evaluation. Called **dozens of times per pixel** (the march, plus `calcNormal`, `calcSoftShadow`, `calcAO`), so it must do **no color work**.
- **`evaluateScene(p) -> SceneResult`** — distance + color + objectId. Called **once**, at the primary hit, to shade and to write the pick id.

Both interpret the RPN program with a **fixed stack** (`array<…, 16>`): `PUSH_OBJECT` evaluates one primitive's SDF and pushes it; an op pops two and pushes the combined result. Both guard the stack (`if (sp < 16)` on push, `if (sp >= 2)` on op) so a malformed program or over-deep nesting degrades safely instead of corrupting the index (§11). Adding a new primitive means editing the `switch(obj_type)` in **both** — a known duplication (a `map`-sharing refactor is a candidate but not done).

> **No object culling.** A bounding-sphere "skip the far object's exact SDF" optimization was tried and **reverted** — under-covering bounds clipped real geometry (corrupted the render). `distanceCalc` evaluates every object's exact SDF every step. If revisited, the many-object scaling fix must be provably non-clipping (and verified visually across all primitives, rotations, and scales) before landing.

### Lighting (Blinn-Phong) and its guards
On hit: `hitPos` → `calcNormal` (4-tap tetrahedron gradient of `distanceCalc`) → `evaluateScene`. Then:
`col = albedo·ambient·ao + shadow·(diffuse + specular)`, where `diffuse = albedo·lightColor·intensity·max(dot(n,L),0)`, `specular` uses the half-vector, `shadow = calcSoftShadow` (IQ penumbra march, `min(res, k·h/t)`), `ao = calcAO` (5-tap probe along the normal). Then `toneMapAndEncode` (ACES filmic, then `pow(1/2.2)` gamma). **`fs_main` no longer draws the gizmo** — the gizmo overlay moved to `fs_resolve` (§7a), so it's composited once, un-jittered, over the averaged image (one gizmo per displayed frame, not one per accumulated sample).
- **Feature guards (performance):** the shadow march runs only `if (shadowsEnabled && intensity > 0)` — so **light off (intensity 0) auto-skips shadows**, since a shadow multiplied by zero direct light is wasted work. AO runs only `if (aoEnabled)`. "Off" fallbacks are physically neutral (`shadow = 1`, `ao = 1`). These are *coherent* branches on uniforms (every pixel takes the same side), so "off" genuinely skips the loop.
- **Tone map before gamma**, and the swapchain is **non-sRGB** (`getPreferredFormat`), so the shader *must* gamma-encode itself. The gizmo is composited **after** the output transform (its colors are authored in display space).
- Known gaps: the grid floor is analytic in `gridSample` (not SDF geometry) so **objects cast no shadow on it**; the canvas backing store is 1× DPR (scaled by Render Resolution, §10). (Silhouette AA is handled by temporal accumulation — §7a — plus the optional `aaSamples` SSAA grid.)
- **`.wgsl` files are kept comment-free by owner preference.**

### 7a. Temporal accumulation anti-aliasing (two-pass)
The viewport renders in **two passes per frame** (both encoded in one command buffer in `Application::MainLoop`, so WebGPU auto-inserts the write→read barrier):
- **Pass A (scene, `fs_main`):** raymarch + light + tone-map, writing color into **one layer of a 4-layer accumulation texture array** (plus the object-id target as before). The ray is jittered by a sub-pixel offset (`globals.jitterOffset`, applied to `uv` in `fs_main`).
- **Pass B (resolve, `fs_resolve`):** samples all 4 layers with `textureLoad`, **averages them** (box filter, `×0.25`), overlays the gizmo (un-jittered), and writes the swapchain.

**One counter drives both roles.** `Application::m_JitterIndex` cycles `0→1→2→3` each frame; it selects *both* the jitter offset (a 4-entry 2×2 sub-pixel grid, `UpdateJitterGlobal`) *and* the layer rendered into (`GetAccumLayerView(m_JitterIndex)`). So layer *i* always holds "the frame jittered by offset *i*." **No shifting/ring-copy** — overwriting layer *i* in place keeps the 4 layers = the 4 sub-pixel positions.

Consequences: with a **static** camera the 4 layers are 4 fixed sub-pixel samples → a true **4× supersample, free** (the raymarch still runs one ray/pixel). Averaging is done in **display space** (layers are already tone-mapped/gamma-encoded) — a deliberate approximation.

🔴 **The layers are only ever averaged with each other while they all sample the *same* image — that is enforced by `globals.accumCount`, not by trusting the layers.** An invalidation sets `m_AccumReset` (in `RequestRedraw`, the one place every invalidation funnels through); the next drawn frame restarts the sequence at **layer 0** with `accumCount = 1`, and the count then climbs 1→2→3→4 as later frames fill layers 1..3. `fs_resolve` averages exactly `[0, accumCount)` and divides by that count, so **a frame drawn at an older camera position or scene state can never blend into the current image.** Before this the resolve unconditionally averaged all 4 layers, which meant every invalidation displayed `¼ new + ¾ stale` until the convergence run finished: invisible at 60 fps (66 ms), but on a heavy scene at ~10 fps it read as *previous frames showing through* for ~½ second at the start and end of every orbit/zoom — the classic "TAA ghost". Progressive counting also removes the old un-cleared-layer flash after load/resize (nothing is read until it has been written) and the ghosting on scene edits (gizmo drags, slider changes) for free, since those invalidate too. The **Temporal AA toggle** (Properties → Render → Effects; `QualitySetting::Taa` on the **viewport** context, §9) pins `accumCount = 1` and layer 0 with zero jitter — one pin-sharp sample, still two-pass so the gizmo path is unchanged. This is still *not* reprojected TAA: previous frames are **discarded** on any change rather than re-projected, so a change costs AA quality for a few frames instead of smearing. Pick reads the object-id at the jittered ray (≤0.25px off, negligible). The offscreen export path (`StartFinalRender`) is unaffected — it uses the scene pipeline directly (no accumulation, no gizmo), renders at the **export** quality budget (§9), and force-disables TAA + jitter because a single pass has nothing to accumulate; `aaSamples` SSAA is its only anti-aliasing. The export is **band-tiled**: instead of one monolithic draw, `StartFinalRender` submits the same full-screen quad once per horizontal **scissor band**, `SdfRenderer::StepFinalRender` (called each `MainLoop` frame) submits the next band only after the previous band's `onSubmittedWorkDone` fired (callback kept alive per §13; it only flips flags and measures — the next submit never happens inside the callback), and the readback runs after the last band. 🔴 **Band height is adaptive and that is a safety requirement, not a tuning nicety:** a submission that outruns the OS GPU watchdog (**Windows TDR = 2s by default**) gets the display driver reset under it, the device is lost, and the completion callback is *never delivered*. So the first band is a small 16-row probe, each band is timed, and `NextBandRows` scales the next one toward `kBandTargetSeconds` (0.5s) — growing ≤2× per band, shrinking instantly, clamped to 4..256 rows. Progress is therefore measured in **completed scanlines**, not bands (`finalRowsDone / finalHeight`). A **stall watchdog** in `StepFinalRender` fails the export (state 4) if no callback arrives within `kStallSeconds` (15s), covering both the band and readback phases — without it a TDR leaves the UI frozen at a fixed percentage forever. Pending callbacks are **parked, never freed** (`RetireFinalCallbacks` + a `shared_ptr<bool>` token): destroying one leaves the driver holding a raw pointer into released memory. `MainLoop` **skips the viewport passes entirely while an export is in flight** (state 1 or 2) — the overlay hides the canvas anyway, and a heavy viewport frame would both steal GPU time and spend the same watchdog budget. `sdf_final_render_progress` → `engine.ts renderImage(w, h, onProgress?)` feeds the UI progress bar + elapsed timer.

---

## 8. `GlobalsData` ↔ WGSL `Globals` — the byte-for-byte contract

**`GlobalsData` (C++, `src/scene/data/GlobalsData.h`) and the WGSL `Globals` struct (`main.wgsl`) must match byte-for-byte. Currently 256 bytes, 16-aligned.** A mismatch is **silent** — it corrupts every field after the drift. Rules:
- Store every logical `vec3` as a `vec4` (only `.xyz` used) to dodge the std140 vec3-padding trap.
- Group trailing scalars into 16-byte blocks; **append new fields at the end**, reusing pad slots where possible.
- The globals buffer size and bind-group entry size both come from `sizeof(GlobalsData)`, so the struct can grow **without touching `SdfRenderer`** — just keep the two structs in lockstep.

Layout (offsets in bytes):
```
0    resolution[2]         (vec2f)
8    showGrid  (i32)   12  selectedIndex (i32)
16   cameraPos[4]          32  cameraTarget[4]
48   gizmoOrigin[4]  64 gizmoAxisX[4]  80 gizmoAxisY[4]  96 gizmoAxisZ[4]
112  gizmoVisible/Hovered/Active/Mode (4× i32)
128  lightDirection[4] 144 lightColor[4] 160 ambientColor[4]
176  lightIntensity  180 specularShininess  184 specularStrength  188 shadowSoftness
192  aoStrength  196 maxSteps(i32)  200 hitEpsilon  204 maxDistance
208  shadowMaxSteps(i32)  212 shadowMaxDistance  216 aoSamples(i32)  220 aaSamples(i32)
224  shadowsEnabled(i32)  228 aoEnabled(i32)  232 qualityPad1(i32)  236 gizmoPixelScale(f32)
240  jitterOffset[2] (vec2f)  248 taaEnabled(i32)  252 accumCount(i32)
=> 256 bytes
```
The 224-byte block (0..223) is the original layout; the 224..239 block is the feature-toggle block added for the performance work (all three former pad slots used: `qualityPad0`→`aaSamples` supersampling grid N, `qualityPad1` = the selection-visuals flag [1 = suppress the object highlight during the object-id pick pass; see `SetSelectionVisuals`], `qualityPad2`→`gizmoPixelScale` render-buffer px per display px = the web Render Resolution scale; the shader multiplies the gizmo line/dot thickness by it, and the engine sizes the gizmo handles by it, so the gizmo stays a constant on-screen size at any Render Resolution — §10). The 240..255 block (16 bytes) was appended for the **temporal-AA** work (§7a): `jitterOffset` = the current frame's sub-pixel ray offset in pixel units (set per frame in `MainLoop` from `m_JitterIndex`, applied in `fs_main`); `taaEnabled` = the viewport TAA toggle; `accumCount` = how many accumulation layers currently sample the live image, i.e. how many `fs_resolve` averages (§7a). **The struct is now full — there are no free pad slots left. The next field must append a new 16-byte block (256 → 272), keeping `GlobalsData` and the WGSL `Globals` in lockstep; the buffer and bind-group sizes come from `sizeof(GlobalsData)`, so nothing in `SdfRenderer` needs touching.**

---

## 9. Render settings: **look** (shared) vs **quality** (per-context)

There are **two** setting chains, and the split is the important idea:

| | Lives in | Setter | Scope |
|---|---|---|---|
| **Look** — light dir/color/intensity, ambient, shadow sharpness, gloss, highlight, AO strength | `Settings` (flat fields) | `SetRenderSetting(id, value)`, ids = `RenderSetting` **0..11** | **Shared.** The exported image must look like what was on screen. |
| **Quality** — maxSteps, hitEpsilon, maxDistance, shadowSteps, shadowDistance, aoSamples, aaSamples, taa, shadowsEnabled, aoEnabled | `QualitySettings` (`src/editor/QualitySettings.h`), **two instances**: `Settings::viewportQuality` / `Settings::exportQuality` | `SetQualitySetting(context, id, value)`, ids = `QualitySetting` **0..9**, context = `QualityContext` (0 viewport / 1 export) | **Per-context.** The viewport is tuned for framerate, the export for image quality; neither affects the other. |

**Why the split exists:** before it, working at 50% / shadows off / low step budget *also* degraded every exported PNG, so you had to re-tune the viewport before every export and undo it afterwards. Now the two budgets are independent engine state.

**The look chain (5 layers, unchanged):**
`Settings.h` (field + `RenderSetting` id) → `Application::SetRenderSetting` → `SyncRenderGlobals()` → `sdf_set_render_setting` → `engine.ts setRenderSetting` → `App.tsx` Render tab.

**The quality chain (same shape, plus a context):**
`QualitySettings.h` (field + `QualitySetting` id) → `Application::SetQualitySetting(context, …)` (writes `QualityFor(context)`, with clamps) → `Application::ApplyQuality(q, globals)` → `sdf_set_quality_setting(context, id, value)` → `engine.ts setQualitySetting` → `App.tsx` Render **or** Export tab.

- **One parameterized setter per chain**, not one binding per setting. `sdf_set_render_setting(id, value)` / `sdf_set_quality_setting(context, id, value)` never change — only the enums grow.
- **`ApplyQuality` is the single place a `QualitySettings` maps onto `GlobalsData`.** `SyncRenderGlobals()` calls it with `viewportQuality` every frame; `StartFinalRender` calls it with `exportQuality` on its private `finalGlobals` copy. That is the whole mechanism — the two contexts can never drift in *how* they're applied, only in their values.
- **Both enums are duplicated** in the C++ headers and `engine.ts` — **keep them in sync.** `RenderSetting` = `0..11` look only. `QualitySetting` = `0 MaxSteps, 1 HitEpsilon, 2 MaxDistance, 3 ShadowSteps, 4 ShadowDistance, 5 AoSamples, 6 AaSamples, 7 Taa, 8 ShadowsEnabled, 9 AoEnabled` (booleans passed as 1/0 floats).
- `GetSceneJson()` emits **three** mirrors: `"render"` (look) plus `"viewportQuality"` and `"exportQuality"`; `App.tsx` keeps them in `state.render` / `state.viewportQuality` / `state.exportQuality`. The panels never own values.
- `App.tsx`: `RENDER_SETTINGS` is the look config array (`rsRow`/`rsSet`); `QUALITY_SETTINGS` is the **one** quality config array used by **both** tabs — `qualityRows(ctx)` applies that context's slider bounds (each row carries an optional `exportMax`, since an export may go far past what a live viewport tolerates), and `qRow`/`qSet`/`qBool` bind a row to a context. Help-open flags are namespaced per context so the same row can be expanded independently in each tab.
- **Setter clamps (robustness):** `SetQualitySetting` hard-clamps loop counts / epsilon to safe caps (maxSteps ≤ 2048, shadowSteps ≤ 1024, aoSamples ≤ 128, aaSamples ≤ 16, hitEpsilon ∈ [1e-6, 1], distances positive) — well above the UI slider maxes, so they never touch legitimate values but stop a rogue call from hanging the GPU.
- **Undo** works because `SceneEditSnapshot` captures the whole `Settings` (both budgets included). The §6 caveat still applies: a *bracketed* batch of pure setting writes (slider drag, preset apply) records no undo entry.

---

## 10. Performance systems (Properties → **Render** tab) and the **Export** tab

The properties panel has five tabs: Object · Material · Modifiers · **Render** · **Export**. Render owns everything about the *live viewport*; Export owns everything about the *saved image*. They are separate tabs precisely because they own separate engine-side quality budgets (§9) — one long scrolling "Render" tab hid that split and made "work fast, export clean" impossible to discover. Shared **Lighting & Look** lives in the Render tab (labeled as shared, since it also drives exports).

These are the speed controls. Understand which are engine-owned (go through §9) and which are pure web display prefs.

- **Render Resolution (50/75/100%)** — the biggest single dial. `App.tsx state.renderScale` scales the canvas **backing store** (`syncCanvasSize` sets `canvas.width/height = cssSize × scale`) while CSS keeps the canvas full-size, so the browser upscales a smaller buffer. Raymarch cost scales with pixel count, so 50% ≈ 25% of the pixels. **This is a viewport *display* preference (like DPR), so it lives web-side, NOT in the engine** — it modulates what `syncCanvasSize` tells the engine to render at; it is not scene state and is not persisted. `canvasPoint()` normalizes pointer coords by `canvas.width/rect.width`, so picking/gizmo stay pixel-correct at any scale. **The gizmo is sized in render-buffer px, so it would balloon on a down-scaled (upscaled) buffer; `syncCanvasSize` therefore pushes `renderScale` to the engine via `engine.setGizmoPixelScale(scale)` → `sdf_set_gizmo_pixel_scale` → `Application::SetGizmoPixelScale` → `GizmoController::SetPixelScale`, which multiplies the handle length + hit tolerance (and `globals.gizmoPixelScale` scales the shader's line/dot thickness), keeping the gizmo a constant *display* size at any Render Resolution. This is display info like DPR, not scene state.**
- **Viewport Quality presets (Fast / Balanced / High / Ultra)** — `App.tsx PERF_PROFILES` batch-apply render scale + the viewport toggles + the advanced budgets in one click, wrapped in one `beginSceneEdit/commitSceneEdit`. Hand-tweaking any *viewport* control flips that preset to **"Custom"** (`state.perfProfile`) and leaves the export preset alone.
- **Export Quality presets (Draft / Standard / High / Ultra)** — `App.tsx EXPORT_PROFILES`, same mechanism into `QualityContext.Export` (`state.exportProfile`; "High" mirrors `QualitySettings::ExportDefaults()`). They carry **no** render scale (that's a viewport display pref) and **no** TAA. Both preset tables go through `applyQualityProfile(ctx, …)`.
- 🔴 **The two preset families make opposite AA trades, on purpose.** Viewport presets keep `aaSamples = 1` and `taa = 1`: temporal accumulation smooths edges essentially for free in a continuously redrawn viewport (§7a), while SSAA would multiply *every frame's* cost. Export presets scale `aaSamples` 1→4 and never touch TAA: the export is a **single offscreen pass** through the scene pipeline (no accumulation array, no resolve), so there are no previous frames to average and TAA does literally nothing — SSAA is the only anti-aliasing an export has. `StartFinalRender` therefore forces `taaEnabled = 0` and zeroes `jitterOffset` on its `finalGlobals`.
- **Feature toggles: Shadows / Ambient Occlusion (per context) + Light (shared).**
  - *Shadows* (`ShadowsEnabled`) and *AO* (`AoEnabled`) are **per-context** quality settings (§9) that gate the two biggest optional per-pixel marches. Turning shadows off to model fast no longer costs you shadows in the export.
  - *Light* is **web-side sugar** over `lightIntensity` (no new engine state): off remembers the current intensity and sets it to 0 (which the shader treats as "skip shadows"), on restores it. It is a **look** setting, so it sits in Render → Lighting & Look and affects exports too. The Light Intensity slider stays the fine control and mirrors the same value.
- **Framing Preview** (Export tab; `state.cleanPreview`, `enterCleanPreview`/`exitCleanPreview`, formerly "Clean Preview") — a temporary mode for **composing an export shot**. It hides every piece of editor furniture (grid, gizmos, selection highlight, overlays), forces rendered shading, and makes the viewport **camera-only** (clicking no longer picks, the outliner is dimmed and non-interactive) so you cannot nudge the scene while framing it. `_cleanSnapshot` stores the preferences it overrode — including the active props tab — so exiting restores them exactly. It previews the **framing, not the quality**: the canvas still draws at the viewport budget while the exported PNG uses the export one.
- **Frame readout** (**bottom-right** of the viewport) — a `requestAnimationFrame` loop in `App.tsx` (`startFpsMeter`) **samples the engine's counters** ~2×/sec (it does *not* count its own callbacks — see §6a) and publishes `state.fps` + `state.frameMs`. Two lines: engine frames/sec (`"Idle"` in neutral grey at 0 — the correct reading on a static scene under render-on-demand; green ≥50 / amber / red otherwise), and GPU frame time with the uncapped rate it implies (`8.3 ms · 120 fps`), coloured by the 16.6/33.3 ms thresholds. **Use the ms line for optimisation A/Bs** — the FPS line is vsync-capped and can only show how far *below* smooth you are, never how much headroom is left. Fed by `sdf_get_frame_count` / `sdf_get_gpu_frame_ms` → `engine.getFrameCount()` / `getGpuFrameMs()`. Cleaned up in `componentWillUnmount`.

---

## 11. Robustness / untrusted-data handling

`.sdfp` files are untrusted input (a corrupt/hostile file must never crash the WASM or hang the GPU). The guards:
- **CSG link validation on load** (`CsgTree::LinksAreValid`): after `ReadFromStream` blits the raw node array, every `m_Root`/`parent`/`firstChild`/`lastChild`/`nextSibling`/`previousSibling` is bounds-checked. An out-of-range link (which would later be dereferenced with `operator[]` in `CompileNode`/`GetSceneJson` → **C++ heap OOB read**) makes `ReadFromStream` `Clear()` the tree; `RebuildAfterLoad` then reconstructs a flat default Union, so the scene still loads (ungrouped) instead of crashing.
- **objectId consistency** (`SceneManager::RebuildAfterLoad`): a link-valid tree whose Primitive references a missing objectId would throw in `CompileToRPN`'s `.at()`. Detected → same flat-rebuild fallback.
- **GPU stack guards** (§7): push/op are guarded so overflow (deep nesting) or underflow (malformed program) degrade safely. The array is intentionally kept at **16** — enlarging it costs GPU occupancy, and depth grows only with nesting depth, not object count.
- **Setting clamps** (§9): hard safety caps on loop counts / epsilon.

---

## 12. File operations & persistence (`.sdfp`)

- **`.sdfp` = the editable PROJECT format** (magic `"SDFP"`): objects (raw `SdfObjectData`) + CSG tree (groups/ops) + an **editor-attributes section**. `.sdfo` (magic `"SDFO"`) is the legacy/native format, reserved for a future composite-object import; the web app never touches it.
- **Serialization is stream-based** so one code path serves file (native) and in-memory (web): `ExtensionLoader::WriteSDFP(std::ostream&, objects, tree)` / `ReadSDFP(std::istream&, sceneManager)`. `Application::SerializeProject()` writes to an `ostringstream` (objects+tree via `WriteSDFP`, then `EditorObjectStore::WriteToStream`) and returns the bytes; `DeserializeProject(bytes)` reads them → `RefreshSceneBuffer()` → `RebuildEditorRecords()` (defaults from geometry) → **`EditorObjectStore::ReadFromStream` overlays** saved attrs on top → clears selection/undo.
- 🔴 **Byte layout is `[magic+count][objects][CSG tree][editor-attrs]`, and editor-attrs is the LAST section.** New persisted data MUST be **appended at the end of the editor-attrs block**, and every reader MUST be **EOF-tolerant** (absent trailing sub-block = older file → keep defaults). **Never insert a section in the middle** — older files have the *following* section's bytes there, so a mid-file read desyncs everything after it. The editor-attrs block is: object records `[objectId, visible, locked, rotationEuler(3), scale(3), params(6), name(len-prefixed)]`, then naming `typeCounters`, then group **node names** `[nodeId, name]` — each sub-block length-prefixed and EOF-tolerant.
- **`EditorObject` holds a `std::string name`, so it can't be blitted** like the 80-byte POD `SdfObjectData` — that's why the editor-attrs (de)serializer exists (`SdfObjectData`/`CsgNode` are still raw-blitted; `CsgNode` stays POD, so node names live in `EditorObjectStore`).
- **Web ↔ engine bridge is base64 over `ccall`** (no `FS`/`EXPORTED_FUNCTIONS`/CMake change): `sdf_save_project()` returns the project base64-encoded, `sdf_load_project(b64)` takes it. `engine.ts` converts `Uint8Array`↔base64.
- **The browser owns the real file via the File System Access API** (Chrome/Edge only). `App.tsx` keeps the `FileSystemFileHandle` in `_fileHandle`: first **Save** calls `showSaveFilePicker` (fixed `.sdfp`, `excludeAcceptAllOption:true` + custom MIME `application/x-sdf-project`), later Saves overwrite silently via `createWritable()`. **Open** = `showOpenFilePicker` then adopt the handle. Native pick dialogs can't be skinned; only our own modals can.
- **`App.tsx` file flow:** `newScene`/`openProject` go through `confirmDiscard(action, label)` (styled discard-confirm modal when `!state.saved`; the action runs inside the modal button's click so Open's picker keeps its user gesture). `state.fileName` shows in the title bar (`untitled.sdfp` until saved/opened); `state.saved` is the dirty flag. There is **no "Save As"** (Save overwrites; export is planned separately).
- Known gaps: **render settings do NOT persist in `.sdfp`** yet (objects/tree/editor-attrs do; the shadow/AO toggles, budgets, render scale, and profile don't — extend `WriteSDFP`/`ReadSDFP` + `Settings` to add them, appending at the end + EOF-tolerant). Group rename is **not** on the undo stack (object rename is).

---

## 13. Emscripten gotchas

- **`webgpu.hpp` async callbacks** (`mapAsync`, `requestDevice`, …) return a `std::unique_ptr<…Callback>` that owns the heap callback the GPU will invoke. **Keep it alive** (store in a member) until it fires, or WASM throws `RuntimeError: table index is out of bounds`.
- Bindings rely on `EMSCRIPTEN_KEEPALIVE` (auto-exported; there is **no** `EXPORTED_FUNCTIONS` list). `-sASYNCIFY` is on (`pickObject` is async via `ccall {async:true}`).
- Adding a `sdf_*` binding that calls an undeclared `Application` method fails to compile → the whole link stays red until the method is defined.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` must be defined **before any glm include** (it's in `Camera.h`) — WebGPU wants [0,1] clip depth. Defining it after is a silent no-op.
- 🔴 **SDL steals the keyboard.** SDL's emscripten backend attaches `keydown`/`keyup`/`keypress` to the whole window and `preventDefault()`s them, so **no HTML `<input>` ever receives typed characters**. Fixed in `Window.cpp` with `SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#none")` **before** `SDL_CreateWindow` (`#ifdef __EMSCRIPTEN__`). Safe because React owns all keyboard (`App.tsx _kd`, which early-returns on editable targets). **Don't undo this hint.**

---

## 14. File map

- `src/main.cpp` — SDL callbacks; sets `g_App`, drives `Application::MainLoop`.
- `src/editor/Application.{h,cpp}` — owns everything; the control API, undo, JSON emit, gizmo glue, render-setting setter/sync.
- `src/bindings/WebBindings.cpp` — `sdf_*` C functions (`__EMSCRIPTEN__` only) + the base64 helpers.
- `src/scene/SceneManager.{h,cpp}` — objects vector + `CsgTree`; add/remove/group, id maps, `RebuildAfterLoad`, `EnsureRootUnion`.
- `src/scene/csg/{CsgTree.cpp,CsgNode.h,CsgCommand.h}` — n-ary CSG tree, RPN compile, link validation.
- `src/scene/PrimitiveGeometry.*` — size derivation from params+scale. `src/scene/SdfObjectFactory.*` — primitive constructors.
- `src/editor/EditorObjectStore.*` — editor attrs (per objectId) + group node names (per nodeId) + the `.sdfp` editor-attrs (de)serializer.
- `src/editor/FrameScheduler.{h,cpp}` — the render-on-demand frame counter (§6a). `src/editor/FrameStats.{h,cpp}` — submitted-frame count + GPU frame time via `onSubmittedWorkDone`.
- `src/editor/Settings.h` — camera sensitivities + the authoritative **look** settings + the `RenderSetting` id enum + the two `QualitySettings` instances. `src/editor/QualitySettings.h` — the per-context quality budget struct + the `QualitySetting` / `QualityContext` id enums (§9).
- `src/serialization/ExtensionLoader.*` — `WriteSDFP`/`ReadSDFP` (stream, the `.sdfp` project) + legacy `SaveToSDFO`/`LoadFromSDFO`. `src/serialization/SceneSerializer.*` — `RefreshSceneBuffer`/`RefreshCsgBuffer` + native file ops.
- `src/scene/data/{SdfObjectData,EditorObject,SdfEnums,GlobalsData}.h` — the POD structs + `GlobalsData` (the GPU uniform; mirror of the WGSL `Globals`).
- `src/interaction/` — `SelectionState.h`; `GizmoMath.*` (projection/hit-test math); `GizmoState.h`; `GizmoController.h` (header-only hover/drag state machine; translate + rotate + scale).
- `src/renderer/SdfRenderer.*` — scene + resolve pipelines, buffers, the 4-layer accumulation array (§7a), object-id pick target, readback. `src/camera/Camera.*`. `src/platform/Window.cpp` — SDL window + the keyboard-hint fix.
- `assets/shaders/sdf_core.wgsl` — primitives, CSG ops, `distanceCalc`, `evaluateScene`, `calcNormal`, `calcSoftShadow`, `calcAO`. `assets/shaders/main.wgsl` — `Globals`, raymarch (`fs_main`, jittered), Blinn-Phong, ACES tone map + gamma, `fs_resolve` (averages the accumulation layers + gizmo overlay — §7a), grid. (`sdf_core` concatenated **before** `main`.)
- `web/src/engine.ts` — typed facade over `ccall` (+ `RenderSetting`/`QualitySetting`/`QualityContext` enums, `RenderSettings`/`QualitySettings`/`SceneState` interfaces). `web/src/App.tsx` — the entire UI in one giant class component. `web/src/fsaccess.d.ts` — File System Access typings.

### App.tsx conventions
One giant class component. Persistent UI state in `this.state`; transient drag scratch in instance fields (`_camDrag`, `_gizmoDrag`, `_fpsFrames`, …). `refresh()` re-reads the scene JSON. `renderVals()` builds a big view-model `V` that `render()` consumes. Helpers: `Hv` (hover-style wrapper), `Ico`/`Raw` (`dangerouslySetInnerHTML` SVG), `css()` (string→style). Some comments are Turkish (`YENİ` = "new").
- **No gizmo overlay / rAF sync loop** for the gizmo — the engine draws it. `camDown/Move/Up` forward pointer coords via `gizmoHover/BeginDrag/Drag/EndDrag`; `componentDidUpdate` calls `engine.setGizmoEnabled(tool === "move" || "rotate")`.
- The **only** rAF loop in `App.tsx` is the frame-stat meter (`startFpsMeter`), and it *samples* the engine rather than counting its own ticks (§6a).
- The properties tab strip is **config-driven** (`V.propTabs`); the five tabs flex to share the panel width. The **Render** tab is the viewport quality + Performance + shared Lighting UI, the **Export** tab is the export quality + output size + Render Image UI (§10). Both wrappers have `overflow:auto`.
- **File ops:** `newScene`/`doNewScene`, `openProject`/`doOpenProject`, `saveProject`, `confirmDiscard`/`cancelDiscard`/`proceedDiscard`. Rename: `commitRename` + inline outliner `<input>` (double-click `onRenameStart`), F2, and context-menu "Rename" (all handle primitive **and** group via `state.sel`).

---

## 15. Conventions

- **Never** add an AI/Claude `Co-Authored-By` trailer to commits. Prefer to let the owner run git; they are particular about history.
- Don't commit build artifacts — `web/public/engine/*` is untracked (only `.gitkeep`).
- Reference code as `path:line`.

### Design preferences (uphold these; the owner cares)
- **Keep the C++ engine and the TS/React UI strictly separated.** Engine = authoritative core (all domain state + logic); web = IO/UI/connection only, re-reads engine JSON after every mutation. New capability crosses the boundary **only** through `sdf_*` bindings + the `engine.ts` facade.
- **Favor a modular, single-responsibility structure.** Prefer a new focused file/class (like `SceneManager`, `CsgTree`, `EditorObjectStore`, `GizmoController`, `ExtensionLoader`) over piling logic into `Application` or `App.tsx`. Extract a concern when it grows.
- **Extend along the established chains, don't invent parallel paths.** Follow the layering (`App.tsx → engine.ts → WebBindings → Application → subsystem`), the render-settings 5-layer chain, the one-parameterized-setter pattern, and the append-at-end / EOF-tolerant serialization rule. Match surrounding style; keep `.wgsl` comment-free.

---

## 16. Status

**Done:** CSG multi-select + boolean ops (Union/Subtract/Intersect) + pure Group containers; **smooth booleans** (`BlendMode::Smooth`, per-node blend radius, `opSmooth*` in the shader) and the **roundness modifier** (box); the editor-attribute model; **translate + rotate + scale** gizmos (in `GizmoController`, rendered in-shader; scale is local-space only and offers just the handles a given primitive can use — §6); the full lighting stack (normals → Blinn-Phong → ACES tone map + gamma → soft shadows → AO); the render-settings panel (look + advanced) wired `Settings.h → globals → GPU`; **`.sdfp` project save/open** (File System Access API, base64 bridge) persisting objects + CSG tree + editor attributes + group node names; the **performance system** (Render Resolution, Quality Presets, Light/Shadows/AO toggles, FPS meter); **temporal accumulation anti-aliasing** (two-pass, 4-layer jittered accumulation + resolve — §7a); the **viewport/export quality split** (two independent `QualitySettings` budgets, separate Render and Export tabs, opposite AA strategies — §9/§10); **untrusted-file hardening** (CSG link validation, objectId consistency, GPU stack guards, setting clamps); and **render-on-demand** (`FrameScheduler` — the loop only draws when something invalidates the image, so an idle editor costs ~0 GPU; plus `FrameStats` giving a real submitted-frame count and a vsync-independent GPU frame time — §6a).

**Performance work in flight.** `resources/perf_vuln.md` is the ranked vulnerability report (P1…P10) driving the optimisation pass; `resources/whats-done.md` holds the presentation-ready write-ups. **P2 (render-on-demand) is done.** The headline remaining item is **P1** — `distanceCalc` evaluates every object's exact SDF at every march step of every ray, so cost tracks total scene size rather than what the ray can see; that is the 5→10 object cliff. Note P1's bounding-sphere approach was tried once before and reverted for clipping geometry (§7); the report documents the enclosure math that makes a retry safe (proxy substitution only beyond a δ margin, δ ≥ max smooth-blend radius).

**Not built:** a uniform (centre-handle) scale grip — the scale gizmo has three axis handles, and uniform scaling is reached either by grabbing any handle on a shape whose axes all map to one radius (sphere/solid-angle) or by scaling a group; adding a real centre grip needs a 4th `GizmoAxis` value, which the shader's hover/active ids would have to learn. **Render-settings persistence in `.sdfp`** (objects/tree/editor-attrs persist; the look settings and *both* quality budgets don't yet — extend `WriteSDFP`/`ReadSDFP` + `Settings`, appending at the end. The export budget is the one users will most expect to survive a reload); group-rename undo; render-setting undo for *bracketed* (slider-drag / profile) edits; per-object materials (gloss/specular are still global); true **reprojected TAA** (accumulated samples are *discarded* on any change rather than re-projected, so a moving camera renders at 1 sample instead of reusing previous frames — correct, but it leaves free AA on the table, §7a); shadows on the grid floor (needs a real SDF ground plane); a `map`-sharing refactor between `distanceCalc` and `evaluateScene` (they duplicate the primitive switch + ops — a 5th primitive means editing both).

**Working style for this codebase:** the owner is learning the graphics side, so for rendering topics **explain the concept + math first and let them implement** — unless they explicitly say "you do it" / "you can write" (as in the performance pass, where the agent implemented and explained after). The owner runs builds and git themselves. This file is the reference the agent uses to explain the project back to the owner in later sessions — keep it accurate when subsystems change (e.g. update the §8 byte count and the §9 enum ids whenever `GlobalsData`/`RenderSetting`/`QualitySetting` grow).

### The teaching loop that works here (use it for the remaining perf items)

This is the process that produced §6a, and the owner asked for it to be the default for the rest of the optimisation pass. It is a **collaboration protocol**, not a lecture format — the owner writes the first version, the agent reviews and explains.

1. **Roadmap before code.** When the owner picks an item (e.g. "starting P2"), don't implement and don't dump a diff. Produce a staged roadmap: the *concept* first (what architecture is being replaced, and the one sentence that makes it click — "a game's world evolves on its own, an editor's image is a pure function of state you change"), then numbered stages, each with a **verification gate** so a failure is always traceable to the stage that caused it. Read the actual code first so the roadmap cites real call sites, not plausible-sounding ones.
2. **Name the general principle, not just the fix.** "This is *invalidation-driven rendering*, what Blender and the browser compositor do", "this is a *saturating counter*", "*content-based* vs *event-based* invalidation", "scissor crops, viewport moves-and-scales". The transferable idea is the deliverable; the code is a side effect. Say when a technique would *not* transfer (band-splitting is safe here only because no pixel reads a neighbour).
3. **Front-load the gotcha list.** Before they write anything, enumerate what will actually bite — guard placement, acquire/release pairs, the TAA convergence coupling. These are the things that cost hours and teach nothing when discovered the hard way.
4. **Review their implementation by severity, and explain the *consequence*, not the correction.** "`ShouldRender` is inverted" is worth one line; "the guard sits below `GetNextSurfaceTextureView`, so you acquire a swapchain image and a command encoder and then return without releasing either — a leaked WebGPU object per idle frame" is the part they keep. Separate 🔴 blocking from non-blocking so they can triage.
5. **Audit, don't assume, when declaring something done.** Before saying "P2 is complete", mechanically cross-check the whole `sdf_*` surface against coverage rather than eyeballing it — that is how the uncovered `SelectObject` path and the "`SetObjectVisible` doesn't actually need one, because visibility never reaches the GPU" finding both surfaced.
6. **Measurement is part of the feature, not a follow-up.** An optimisation you can't observe isn't finished; and check whether the *existing* meter can even see the thing being changed (here it could not — it would have read 60 on a fully idle engine).
7. **Correct the premise when it's wrong.** The owner asked for an uncapped FPS counter; frame *rate* is vsync-bound no matter what you count, so the honest answer was to explain that and add frame *time* alongside it. Don't silently build the thing that can't work.
8. **Write the presentation version.** The owner presents this work. After landing a feature, add a plain-language, defensible explanation to `resources/whats-done.md` — including the honest caveats and the "why not the alternatives" reasoning a reviewer would probe.
