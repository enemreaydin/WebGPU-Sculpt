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
- Object picking: a second R32Uint render target stores each pixel's object **vector index** (`cmd.operand`), read back via `mapAsync`. `0xffffffff` = nothing.

## Build & run (Windows; adapt paths elsewhere)
- **`cmake` is not on PATH** here → `"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`. Emsdk is bundled at `deps/emsdk` (activate `deps/emsdk/emsdk_env.ps1` / `.sh` first).
- **WASM engine:** `cmake --build build-web --target main` → emits `web/public/engine/sdf-engine.{js,wasm,data}`. (First time: `emcmake cmake -S . -B build-web`.) Rebuild whenever C++ changes.
- **Web UI:** `cd web && npm run dev` (Vite, :5173). Typecheck: `cd web && npx tsc --noEmit`.
- **Compile-check one TU without a full link** (e.g. when the link is red pending another dev's method): `em++ -std=c++17 -Isrc -Ideps/glm -Ideps/SDL/include -Ibuild-web/_deps/webgpu-backend-emscripten-src/include -Ibuild-web/deps/SDL/include-config-release/build_config -Ibuild-web/deps/SDL/include-revision -c <file.cpp> -o /tmp/x.o`.
- To verify a change works, build the WASM + `npm run dev` + drive it in the browser; there is no test suite.

## Load-bearing invariants (most bugs come from breaking these)
- **Three id spaces — never mix them.** `objectId` (permanent, on `SdfObjectData`), CSG `nodeId` (permanent, on `CsgNode`), and **vector indices** into `m_Objects`/`m_Nodes`. `CsgTree` public methods take **indices**; selection + all JSON speak **nodeId**. Convert with `CsgTree::IndexForNodeId` (must skip `!alive` nodes).
- **Node vector never shrinks** — `RemoveNode` tombstones (`alive=false`) in place, so node indices are stable. **Object vector erases** → object indices shift; always key on `objectId`.
- **Pick returns a vector index, not an objectId.** `selectByPick` maps index→object→objectId→nodeId.
- **Selection is engine-owned** (`SelectionState`: `selectedNodeIds`, `primaryNodeId`, plus `selectedObjectIndex` for the shader highlight). `primaryNodeId` = the Subtract base. UI selection keys: `"scene"` | `String(objectId)` (primitive) | `"g"+nodeId` (CSG group).
- **The root Union node is structural — never deletable.** `SceneManager::RemoveNodeSubtree` guards it; `EnsureRootUnion` self-heals a dead/missing root. Deleting it desyncs `CsgTree::m_Root` from `SceneManager::m_RootNodeIndex` and blanks everything.
- **Editor attributes** (name/visibility/lock/rotation/scale/shape-params) live in `EditorObjectStore` keyed by objectId; the SDF `size` is **derived** from params+scale via `PrimitiveGeometry::ComputeSize`. Not persisted to `.sdfo` yet (regenerated on load). Color: engine owns RGB; HSV↔RGB stays UI-side (color-picker geometry only).

## Emscripten gotchas
- **`webgpu.hpp` async callbacks** (`mapAsync`, `requestDevice`, …) return a `std::unique_ptr<...Callback>` that owns the heap callback the GPU will invoke. **Keep it alive** (store in a member) until it fires, or WASM throws `RuntimeError: table index is out of bounds`.
- Bindings rely on `EMSCRIPTEN_KEEPALIVE` (auto-exported; there is **no** `EXPORTED_FUNCTIONS` list). `-sASYNCIFY` is on (`pickObject` is async via `ccall {async:true}`).
- Adding a `sdf_*` binding that calls an undeclared `Application` method fails to compile → the whole link stays red until the method is defined.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` must be defined **before any glm include** (it's in `Camera.h`) — WebGPU wants [0,1] clip depth. Defining it after the include is a silent no-op.

## File map
- `src/main.cpp` — sets `g_App`. `src/editor/Application.{h,cpp}` — owns everything, the control API.
- `src/bindings/WebBindings.cpp` — `sdf_*` C functions (`__EMSCRIPTEN__` only).
- `src/scene/SceneManager.{h,cpp}` — objects vector + `CsgTree`; add/remove/group.
- `src/scene/csg/{CsgTree.cpp,CsgNode.h,CsgCommand.h}` — n-ary CSG tree + RPN compile.
- `src/scene/PrimitiveGeometry.*` — size derivation. `src/editor/EditorObjectStore.*` — editor attrs + naming.
- `src/scene/data/{SdfObjectData,EditorObject,SdfEnums,GlobalsData}.h`.
- `src/interaction/{SelectionState,GizmoState,GizmoMath}.*` — selection + translate gizmo (translate only).
- `src/renderer/SdfRenderer.*` — pipeline, buffers, object-id pick target. `src/serialization/SceneSerializer.*` — `RefreshSceneBuffer`, `.sdfo` I/O. `src/camera/Camera.*`.
- `assets/shaders/{main,sdf_core}.wgsl` — raymarch + CSG RPN eval + object-id + highlight.
- `web/src/engine.ts` — typed facade over `ccall`. `web/src/App.tsx` — the entire UI in one ~1800-line class component.

### App.tsx conventions
One giant class component. Persistent UI state in `this.state`; transient drag scratch in instance fields (`_camDrag`, `_gizmoDrag`, …). `refresh()` re-reads the scene. `renderVals()` builds a big view-model `V` that `render()` consumes. Helpers: `Hv` (hover-style wrapper), `Ico`/`Raw` (`dangerouslySetInnerHTML` SVG). Some comments are Turkish (`YENİ` = "new").

## Conventions
- **Never** add an AI/Claude `Co-Authored-By` trailer to commits. Prefer to let the user run git; they are particular about history.
- Don't commit build artifacts — `web/public/engine/*` is untracked (only `.gitkeep`).
- Reference code as `path:line`.

## Status
CSG multi-select + boolean operations (Union/Subtract/Intersect) and the editor-attribute model are **done and on main**. The **translate** gizmo is done; rotate/scale gizmos and `.sdfo` persistence of editor attributes are **not** built yet.
