# Codebase Comprehension Order

Reading path for understanding the whole engine before optimizing.
Principle: read the contracts before the code that speaks them, and read one
full top-to-bottom slice before going deep on any subsystem.
⭐ = most relevant to the eventual many-object-scaling optimization.

## Phase 0 — The data contracts (read first)
The nouns of the whole system. ~250 lines total.
- [ ] src/scene/data/SdfEnums.h — primitive/node/command type enums
- [ ] src/scene/data/SdfObjectData.h — 80-byte POD primitive (GPU + disk)
- [ ] src/scene/csg/CsgNode.h — one tree node (first-child / next-sibling)
- [ ] src/scene/csg/CsgCommand.h — one RPN instruction (GPU-matched)
- [ ] src/scene/data/GlobalsData.h — 240-byte uniform; read beside WGSL `Globals` (main.wgsl:3-36)
- [ ] src/scene/data/EditorObject.h — non-POD per-object attrs (name, params, scale)
- [ ] **Lock in the three id spaces (CLAUDE.md §4): objectId vs nodeId vs vector index**

## Phase 1 — One full slice through the boundary
Trace "user adds a cube" (CLAUDE.md §2a) across every layer.
- [ ] src/main.cpp — how g_App is built and MainLoop is driven
- [ ] src/bindings/WebBindings.cpp — the sdf_* C functions (skim whole once)
- [ ] web/src/engine.ts — typed facade over ccall + mirrored enums/interfaces
- [ ] web/src/App.tsx — read ONLY refresh(), renderVals(), one setter caller (UI holds no truth)

## Phase 2 — The big object
- [ ] src/editor/Application.h — read whole header as the control-surface index
- [ ] src/editor/Application.cpp (read by function, not top-to-bottom):
      MainLoop() → OnSceneChanged()/OnCsgChanged() → GetSceneJson()
      → SyncRenderGlobals()/UpdateSelectionHighlight() → BeginSceneEdit/CommitSceneEdit

## Phase 3 — Scene & CSG core (many-object-scaling turf)
- [ ] src/scene/SceneManager.h then .cpp — object vector + tree, id maps, EnsureRootUnion, RebuildAfterLoad
- [ ] ⭐ src/scene/csg/CsgTree.h then .cpp — structural ops + CompileToRPN (defines per-step shader cost)
- [ ] src/scene/PrimitiveGeometry.cpp + src/scene/SdfObjectFactory.cpp — size derivation + constructors

## Phase 4 — Editor state, selection, settings
- [ ] src/editor/EditorObjectStore.h + .cpp — names/attrs + editor-attrs serializer
- [ ] src/interaction/SelectionState.h — engine-owned selection
- [ ] src/editor/Settings.h — render settings + RenderSetting enum (§9 source of truth)

## Phase 5 — Rendering (the hot path)
- [ ] src/renderer/SdfRenderer.h then .cpp — pipeline, bind group, buffer reuse, two color targets, pick readback
- [ ] ⭐ assets/shaders/sdf_core.wgsl — primitives, CSG ops, distanceCalc (line 157) + evaluateScene (line 289)
- [ ] ⭐ assets/shaders/main.wgsl — fs_main → shadeRay → march loop → Blinn-Phong → tone map → gizmo/grid (AA loop line 176)

## Phase 6 — Camera & interaction
- [ ] src/camera/Camera.h + .cpp — orbit camera, focalLength 2.414 must match shader
- [ ] src/camera/CameraController.cpp + src/core/ViewportInput.h — input deltas → camera moves
- [ ] src/interaction/GizmoMath.h + .cpp — projection / hit-test math
- [ ] src/interaction/GizmoController.h + src/interaction/GizmoState.h — hover/drag state machine

## Phase 7 — Serialization & persistence
- [ ] src/serialization/ExtensionLoader.cpp — WriteSDFP/ReadSDFP, append-at-end / EOF-tolerant rule (§12)
- [ ] src/serialization/SceneSerializer.cpp — RefreshSceneBuffer/RefreshCsgBuffer + native file ops
- [ ] WebBindings.cpp base64 helpers + engine.ts Uint8Array↔base64 bridge

## Phase 8 — Undo & command layer (read last, off the perf path)
- [ ] src/editor/command/ — UndoStack, SceneEditCommand, TransformObjectsCommand, IEditorCommand

## Suggested pacing
- Sessions 1-2: Phases 0-2 (follow any feature end-to-end)
- Session 3: Phases 3-4
- Session 4: Phase 5 (rendering + shaders — the optimization payoff)
- Session 5: Phases 6-8
