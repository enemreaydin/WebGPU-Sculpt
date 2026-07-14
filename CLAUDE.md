# CLAUDE.md

Signed-distance-field (SDF) editor. C++ engine → **WASM** (Emscripten) renders SDFs on the GPU via **WebGPU** straight to a `<canvas>`; a **React/TS** app is the UI and drives the engine through a thin C bindings layer. A native build (SDL + wgpu-native, engine viewport only) exists for quick engine iteration.

## Architecture — the one mental model
The **engine owns all state** (scene, CSG tree, selection, per-object editor attributes). The **web layer is IO/UI/connection only** — it holds no authoritative domain state and re-reads engine state each mutation.

```
App.tsx (React) → engine.ts (typed facade) → Module.ccall → WebBindings.cpp (sdf_* C funcs)
   → Application (g_App singleton, main.cpp) → SceneManager / CsgTree / EditorObjectStore / SdfRenderer
```
- UI mirrors the engine via `engine.getScene()` → `GetSceneJson()` (JSON). `App.tsx` state `meta` is a **read-through mirror** rebuilt from that JSON in `refresh()`; setters call `sdf_set_*` then `refresh()`.
- Rendering: one full-screen quad raymarches the scene in `assets/shaders/main.wgsl` + `sdf_core.wgsl`. `CsgTree::CompileToRPN` turns the tree into an RPN `CsgCommand` program uploaded to a GPU buffer by `SceneSerializer::RefreshSceneBuffer()` (call it after any scene mutation). Camera pos/target go through `globals`; shader `focalLength 2.414` == 45° fovy == `Camera::GetProjectionMatrix`.
- Object picking: a second R32Uint render target stores each pixel's object **vector index** (`cmd.operand`), read back via `mapAsync`. `0xffffffff` = nothing. Pick coords are **render (device) pixels** — `App.tsx` must use `canvasPoint(e)`, not raw CSS coords.
- **Everything the shader needs travels in one uniform, `GlobalsData` → WGSL `Globals`**: camera, selection highlight, gizmo geometry, and all render settings. `Application::MainLoop` rebuilds `globals` every frame (`UpdateCamera`, `UpdateGizmoGlobals`, `SyncRenderGlobals`) — `globals` is a **per-frame mirror, never authoritative storage**.

## Build & run (Windows; adapt paths elsewhere)
- **`cmake` is not on PATH** here → `"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`. Emsdk is bundled at `deps/emsdk` (activate `deps/emsdk/emsdk_env.ps1` / `.sh` first).
- **WASM engine:** `cmake --build build-web --target main` → emits `web/public/engine/sdf-engine.{js,wasm,data}`. (First time: `emcmake cmake -S . -B build-web`.) Rebuild whenever C++ changes.
- 🔴 **Shaders are baked into `sdf-engine.data` at link time** (`--preload-file`); the engine reads them from the WASM virtual FS at `/assets`, **not from disk**. Editing a `.wgsl` does nothing until you rebuild the `main` target — `npm run dev` alone will keep running the old shader. Symptom: a WGSL compile error whose reported line doesn't match the file on disk. Verify with `grep -c <newSymbol> web/public/engine/sdf-engine.data` and compare `.data` mtime against the `.wgsl` mtime.
- If a build silently produces no new `.data`, it probably **aborted during CMake reconfigure** (any `CMakeLists.txt` edit forces one, and reconfigure needs Python on PATH via the emsdk env). A failed reconfigure means nothing relinks.
- **Web UI:** `cd web && npm run dev` (Vite, :5173). Typecheck: `cd web && npx tsc --noEmit`.
- **Compile-check one TU without a full link** (e.g. when the link is red pending another dev's method): `em++ -std=c++17 -Isrc -Ideps/glm -Ideps/SDL/include -Ibuild-web/_deps/webgpu-backend-emscripten-src/include -Ibuild-web/deps/SDL/include-config-release/build_config -Ibuild-web/deps/SDL/include-revision -c <file.cpp> -o /tmp/x.o`.
- To verify a change works, build the WASM + `npm run dev` + drive it in the browser; there is no test suite.

## Load-bearing invariants (most bugs come from breaking these)
- **Three id spaces — never mix them.** `objectId` (permanent, on `SdfObjectData`), CSG `nodeId` (permanent, on `CsgNode`), and **vector indices** into `m_Objects`/`m_Nodes`. `CsgTree` public methods take **indices**; selection + all JSON speak **nodeId**. Convert with `CsgTree::IndexForNodeId` (must skip `!alive` nodes).
- **Node vector never shrinks** — `RemoveNode` tombstones (`alive=false`) in place, so node indices are stable. **Object vector erases** → object indices shift; always key on `objectId`.
- **Pick returns a vector index, not an objectId.** `selectByPick` maps index→object→objectId→nodeId.
- **Selection is engine-owned** (`SelectionState`: `selectedNodeIds`, `primaryNodeId`, plus `selectedObjectIndex` for the shader highlight). `primaryNodeId` = the **active / last-clicked** node (drives the 3D highlight); it must stay default-initialized to `-1`. UI selection keys: `"scene"` | `String(objectId)` (primitive) | `"g"+nodeId` (CSG group).
- **The Subtract base is the FIRST-selected node**, *not* the primary. `ApplyOperation` builds children straight from `selectedNodeIds` (click order), so "click A, then B, Subtract" = `A − B`. `selectedNodeIds[0]` becomes `firstChild`, which `CompileNode` folds as the RPN base.
- **`GlobalsData` (C++) and the WGSL `Globals` struct must match byte-for-byte** (currently 224 bytes, 16-aligned). Store every vec3 as a `vec4` (only `.xyz` used) to dodge the std140 vec3-padding trap; group trailing scalars into 16-byte blocks. A mismatch is silent — it just corrupts every field after the drift. The globals buffer and bind-group entry size come from `sizeof(GlobalsData)`, so the struct can grow without touching `SdfRenderer`.
- **The root Union node is structural — never deletable.** `SceneManager::RemoveNodeSubtree` guards it; `EnsureRootUnion` self-heals a dead/missing root. Deleting it desyncs `CsgTree::m_Root` from `SceneManager::m_RootNodeIndex` and blanks everything.
- **Editor attributes** (name/visibility/lock/rotation/scale/shape-params) live in `EditorObjectStore` keyed by objectId; the SDF `size` is **derived** from params+scale via `PrimitiveGeometry::ComputeSize`. **Now persisted in `.sdfp`** (see File operations). Color: engine owns RGB; HSV↔RGB stays UI-side (color-picker geometry only).
- **Group/operation node names are engine-owned too** — stored in `EditorObjectStore::m_NodeNames` keyed by **CSG nodeId** (groups have no objectId). Emitted as `"name"` on each tree node in `GetSceneJson`; set via `sdf_set_node_name(nodeId, name)`. `App.tsx commitRename` routes group renames there (primitives → `sdf_set_object_name`); `refresh()` prefers the engine name and falls back to a generated default only when it's empty. Everything the UI can rename is engine-owned and persisted — no UI-only names.

## Shading pipeline (`sdf_core.wgsl` + `main.wgsl`)
Two SDF evaluators, deliberately split — **never merge them**:
- `distanceCalc(p) -> f32` — distance-only RPN eval. Used by the march step, `calcNormal`, `calcSoftShadow`, `calcAO` (called dozens of times per pixel, so it must not do color work).
- `evaluateScene(p) -> SceneResult` — distance + color + objectId. Called **once**, at the primary hit only.

Shading order in `fs_main`: march → (on hit) `hitPos`, `calcNormal`, `evaluateScene` → Blinn-Phong (`diff`, half-vector `spec`) × `calcSoftShadow` + ambient × `calcAO` → `toneMapAndEncode` (ACES filmic, then `pow(1/2.2)` gamma) → `overlayGizmo`.
- `calcNormal` = 4-tap tetrahedron gradient of `distanceCalc`. `calcSoftShadow` = IQ penumbra march (`min(res, k*h/t)`). `calcAO` = 5-tap probe along the normal.
- **Tone map before gamma**, and the swapchain is **non-sRGB** (`getPreferredFormat`), so the shader *must* gamma-encode itself. Gizmo is composited **after** the output transform (its colors are authored in display space).
- The **translate gizmo is drawn by the fragment shader**, not a React overlay — that's what makes it frame-exact with the scene (an HTML/SVG overlay always trailed the WebGPU canvas by a frame, worst during camera orbit). `Application::UpdateGizmoGlobals` writes world-space origin + axis endpoints into `globals`; `overlayGizmo` projects them with the *same* camera basis the rays use. It writes color only, never `object_id`, so the gizmo is never pickable.
- Known gaps: the grid floor is drawn analytically in `gridBackground` (not SDF geometry), so **objects cast no shadow on it**. There is **no antialiasing** on SDF silhouettes (one ray/pixel, binary hit; MSAA can't help a full-screen quad), and the canvas renders at **1× DPR**.
- `.wgsl` files are kept **comment-free** by user preference.

## Render settings chain
Adding a setting touches five layers, in this order:
`Settings.h` (field + `RenderSetting` enum id) → `Application::SetRenderSetting(id, value)` (writes `appSettings`) → `Application::SyncRenderGlobals()` (per-frame copy into `globals`) → `sdf_set_render_setting` (WebBindings) → `engine.ts` `setRenderSetting` + `RenderSetting` enum → `App.tsx` render panel.
- One **parameterized setter** keyed by id, not one binding per setting. The `RenderSetting` enum is duplicated in `Settings.h` and `engine.ts` — **keep them in sync**.
- `GetSceneJson()` emits a `"render"` object so the UI mirrors engine state (`getScene().render`); the panel never owns the values.
- `App.tsx`: `RENDER_SETTINGS` (look: light dir/color/intensity, ambient, shadow sharpness, gloss, highlight, AO) and `RENDER_ADVANCED` (quality/perf: ray steps, surface epsilon, view distance, shadow steps/distance, AO samples, behind a checkbox). `rsRow`/`rsSet` render + clamp each row; `rsSet` optimistically mirrors into state (no full `refresh()`).

## File operations & persistence (`.sdfp`)
- **`.sdfp` = the editable PROJECT format** (magic `"SDFP"`): objects (raw `SdfObjectData`) + CSG tree (groups/operations) + an **editor-attributes section**. `.sdfo` (magic `"SDFO"`) is the **legacy/native** format and is reserved for a future *composite-object import*; the web app never touches it. `.sdfp` is what "Save"/"Open" use.
- **Serialization is stream-based** so one code path serves file (native) and in-memory (web): `ExtensionLoader::WriteSDFP(std::ostream&, objects, tree)` / `ReadSDFP(std::istream&, sceneManager)`. `Application::SerializeProject()` writes to an `ostringstream` (tree section, then `EditorObjectStore::WriteToStream`) and returns the bytes; `DeserializeProject(bytes)` reads them back, then `RefreshSceneBuffer()` → `RebuildEditorRecords()` (defaults from geometry) → **`EditorObjectStore::ReadFromStream` overlays** the saved attrs on top → clears selection/undo.
- **`.sdfp` byte layout is `[magic+count][objects][CSG tree][editor-attrs]`, and the editor-attrs block is the LAST section.** New persisted data MUST be **appended at the end of the editor-attrs block**, and every reader MUST be **EOF-tolerant** (an absent trailing sub-block = older file → keep defaults). *Never* insert a section in the middle: older files have the *following* section's bytes there, so a mid-file read desyncs everything after it. The editor-attrs block itself is: object records `[objectId, visible, locked, rotationEuler(3), scale(3), params(6), name(len-prefixed)]`, then naming `typeCounters`, then group **node names** `[nodeId, name]` — each sub-block length-prefixed and EOF-tolerant.
- **`EditorObject` holds a `std::string name`, so it can't be blitted like the 80-byte POD `SdfObjectData`** — that's why the editor-attrs (de)serializer exists at all (`SdfObjectData`/`CsgNode` are still raw-blitted; `CsgNode` stays POD, so node names live in `EditorObjectStore`, not on the node).
- **Web ↔ engine bridge is base64 over `ccall`** (no `FS`/`EXPORTED_FUNCTIONS`/CMake change): `sdf_save_project()` returns the project base64-encoded (helpers in `WebBindings.cpp`), `sdf_load_project(b64)` takes it. `engine.ts saveProject()/loadProject()` convert `Uint8Array`↔base64.
- **The browser owns the real file via the File System Access API** (Chrome/Edge only — same constraint as WebGPU). `App.tsx` keeps the `FileSystemFileHandle` in `_fileHandle`: first **Save** calls `showSaveFilePicker` (fixed `.sdfp`, `excludeAcceptAllOption:true` and a **custom MIME** `application/x-sdf-project` so Windows doesn't smuggle `.exe`/`.com`/`.bin` into the dialog), later Saves overwrite that handle **silently** via `createWritable()`. **Open** = `showOpenFilePicker` then adopt the handle (so Save overwrites the opened file). The native pick dialogs **cannot be skinned** (browser security); only our own modals can.
- **`App.tsx` file flow:** `newScene`/`openProject` go through `confirmDiscard(action, label)` which shows a **styled discard-confirm modal** (matching the Shortcuts overlay) when `!state.saved`; the modal button runs the action inside its click so Open's picker keeps its user gesture. `state.fileName` shows in the title bar (`untitled.sdfp` until saved/opened); `state.saved` is the dirty flag (mutations set it false, save/open/new set it true). There is **no "Save As"** (Save already overwrites; export is planned separately).
- Known gap: group rename is **not** on the undo stack yet (object rename is, via `FinishSceneEdit("Rename Object")`).

## Emscripten gotchas
- **`webgpu.hpp` async callbacks** (`mapAsync`, `requestDevice`, …) return a `std::unique_ptr<...Callback>` that owns the heap callback the GPU will invoke. **Keep it alive** (store in a member) until it fires, or WASM throws `RuntimeError: table index is out of bounds`.
- Bindings rely on `EMSCRIPTEN_KEEPALIVE` (auto-exported; there is **no** `EXPORTED_FUNCTIONS` list). `-sASYNCIFY` is on (`pickObject` is async via `ccall {async:true}`).
- Adding a `sdf_*` binding that calls an undeclared `Application` method fails to compile → the whole link stays red until the method is defined.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` must be defined **before any glm include** (it's in `Camera.h`) — WebGPU wants [0,1] clip depth. Defining it after the include is a silent no-op.
- 🔴 **SDL steals the keyboard.** SDL's emscripten backend attaches `keydown`/`keyup`/`keypress` to the **whole window** and `preventDefault()`s them, so **no HTML `<input>` ever receives typed characters** (rename, numeric params, color hex, search all silently reject keys). Fixed in `Window.cpp` with `SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#none")` **before** `SDL_CreateWindow` (`#ifdef __EMSCRIPTEN__`): `"#none"` makes SDL register no keyboard handlers at all. Safe because **React owns all keyboard** (`App.tsx _kd`, which itself early-returns on editable targets). Don't undo this hint.

## File map
- `src/main.cpp` — sets `g_App`. `src/editor/Application.{h,cpp}` — owns everything, the control API.
- `src/bindings/WebBindings.cpp` — `sdf_*` C functions (`__EMSCRIPTEN__` only).
- `src/scene/SceneManager.{h,cpp}` — objects vector + `CsgTree`; add/remove/group.
- `src/scene/csg/{CsgTree.cpp,CsgNode.h,CsgCommand.h}` — n-ary CSG tree + RPN compile.
- `src/scene/PrimitiveGeometry.*` — size derivation. `src/editor/EditorObjectStore.*` — editor attrs + object naming + **group node names** (`m_NodeNames`, keyed by nodeId) + the `.sdfp` editor-attrs (de)serializer (`WriteToStream`/`ReadFromStream`).
- `src/serialization/ExtensionLoader.*` — binary I/O: `WriteSDFP`/`ReadSDFP` (stream-based, the `.sdfp` project) and legacy `SaveToSDFO`/`LoadFromSDFO` (file-path, native `.sdfo`). `src/platform/Window.cpp` — SDL window + the keyboard-hint fix.
- `src/scene/data/{SdfObjectData,EditorObject,SdfEnums,GlobalsData}.h`. `GlobalsData.h` = the GPU uniform (camera + gizmo + render settings); mirror of the WGSL `Globals` struct.
- `src/editor/Settings.h` — `Settings` (camera sensitivities + authoritative render settings) and the `RenderSetting` id enum.
- `src/interaction/` — `SelectionState.h`; `GizmoMath.*` (pure projection/hit-test math); `GizmoState.h`; **`GizmoController.h`** (header-only: the hover/drag state machine, decoupled from `Application` — takes mouse/origin/view/proj per call, holds no engine refs). Translate only.
- `src/renderer/SdfRenderer.*` — pipeline, buffers, object-id pick target. `src/serialization/SceneSerializer.*` — `RefreshSceneBuffer` + native new/open/save. `src/camera/Camera.*`.
- `assets/shaders/sdf_core.wgsl` — primitives, CSG ops, `distanceCalc`, `evaluateScene`, `calcNormal`, `calcSoftShadow`, `calcAO`. `assets/shaders/main.wgsl` — `Globals`, raymarch, Blinn-Phong shading, ACES tone map + gamma, gizmo overlay, grid. (`sdf_core` is concatenated **before** `main`; WGSL module scope is order-independent, so `sdf_core` functions may read `globals`.)
- `web/src/engine.ts` — typed facade over `ccall`. `web/src/App.tsx` — the entire UI in one ~1900-line class component.

### App.tsx conventions
One giant class component. Persistent UI state in `this.state`; transient drag scratch in instance fields (`_camDrag`, `_gizmoDrag`, …). `refresh()` re-reads the scene. `renderVals()` builds a big view-model `V` that `render()` consumes. Helpers: `Hv` (hover-style wrapper), `Ico`/`Raw` (`dangerouslySetInnerHTML` SVG). Some comments are Turkish (`YENİ` = "new").
- There is **no gizmo overlay / rAF sync loop** any more (the engine draws the gizmo). `camDown/camMove/camUp` only forward pointer coords via `gizmoHover/BeginDrag/Drag/EndDrag`; `componentDidUpdate` calls `engine.setGizmoEnabled(tool === "move")` so the engine knows when to draw it.
- The properties panel's Render tab is the render-settings UI; its content wrapper already has `overflow:auto`.
- **File ops** live in `newScene`/`doNewScene`, `openProject`/`doOpenProject`, `saveProject`, `confirmDiscard`/`cancelDiscard`/`proceedDiscard`; FSA typings are in `web/src/fsaccess.d.ts`. Rename flow: `commitRename` + inline outliner `<input>` (double-click `onRenameStart`), plus F2 and the context-menu "Rename" (both handle primitive **and** group via `state.sel`).

## Conventions
- **Never** add an AI/Claude `Co-Authored-By` trailer to commits. Prefer to let the user run git; they are particular about history.
- Don't commit build artifacts — `web/public/engine/*` is untracked (only `.gitkeep`).
- Reference code as `path:line`.

### Design preferences (uphold these; the user cares about them)
- **Keep the C++ engine and the TS/React UI strictly separated.** The engine is the authoritative core (all domain state + logic); the web layer is IO/UI/connection only. Never leak domain logic into `App.tsx`, and never make the web side hold authoritative state — it re-reads engine JSON after every mutation. New engine capability crosses the boundary **only** through the `sdf_*` bindings + the `engine.ts` facade, never by reaching around them.
- **Favor a modular, single-responsibility structure.** Prefer a new focused file/class (like `SceneManager`, `CsgTree`, `EditorObjectStore`, `GizmoController`, `ExtensionLoader`) over piling logic into `Application` or `App.tsx`. When a concern grows, extract it rather than letting the god-objects bloat further.
- **Extend along the established chains, don't invent parallel paths.** Follow the existing layering (`App.tsx → engine.ts → WebBindings → Application → subsystem`), the render-settings 5-layer chain, the one-parameterized-setter pattern, and the append-at-end/EOF-tolerant serialization rule. Match surrounding style; keep `.wgsl` comment-free.

## Status
**Done:** CSG multi-select + boolean ops (Union/Subtract/Intersect); the editor-attribute model; the **translate** gizmo (now in `GizmoController` and rendered in-shader); the full lighting stack (normals → Blinn-Phong diffuse/specular → ACES tone map + gamma → soft shadows → AO); the render-settings panel (general + advanced) wired `Settings.h → globals → GPU`; and **`.sdfp` project save/open** (File System Access API, base64 bridge) that **persists objects + CSG tree + editor attributes + group node names**.

**Not built:** rotate/scale gizmos; **render-settings persistence in `.sdfp`** (objects/tree/editor-attrs already persist; render settings don't yet — extend `WriteSDFP`/`ReadSDFP` + `Settings`); group-rename undo; per-object materials (gloss/specular are still global); antialiasing (candidates: DPR fix, then SSAA as an `aaSamples` setting); shadows on the grid floor (needs a real SDF ground plane); a `map`-sharing refactor between `distanceCalc` and `evaluateScene` (they duplicate the primitive switch + ops — adding a 5th primitive means editing both). A blending/smooth-min feature is being built **by another dev** — coordinate before touching the CSG ops in `sdf_core.wgsl` or the `CsgCommand` struct.

**Working style for this codebase:** the user is learning the graphics side, so for rendering topics explain the concept + math first and let them implement, unless they explicitly say "you do it." They will run builds and git themselves.
