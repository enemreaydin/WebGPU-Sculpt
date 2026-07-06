# SDF Editor

> Baykar internship &mdash; Imaging Systems Development

A small real-time editor for building scenes out of signed distance fields (SDFs).
Objects are raymarched on the GPU with **WebGPU**. The C++ engine is compiled to
**WebAssembly** (Emscripten) and renders straight to an HTML `<canvas>`; the
editor UI is a **React** app that drives the engine through a small C bindings
layer.

```
┌─────────────────────────────┐        ┌──────────────────────────┐
│  React front-end (web/)      │  ccall │  C++ engine → WASM        │
│  menus, panels, inspector    ├───────►│  SDF raymarch + CSG       │
│                              │        │  renders to #canvas       │
└─────────────────────────────┘        └──────────────────────────┘
```

Dear ImGui / ImGuizmo have been removed — all UI now lives in the web layer.

## Features

- Add primitives: sphere, box, cylinder, torus
- Object list, select, edit position / size / color, delete
- Orbit / zoom / pan camera (mouse on the canvas)
- Toggle grid
- Save and load scenes as binary `.sdfo` files

## Project layout

```
assets/
  shaders/        WGSL shaders (sdf_core + main)  — preloaded into the WASM FS
deps/             Third-party libraries (SDL, glm, webgpu, emsdk)
src/
  core/           Vec2, ViewportInput (toolkit-agnostic input/math types)
  editor/         Application (owns window/context/scene, render loop, control API), Settings
  platform/       Window, GraphicsContext (HWND on native, #canvas on web), WebGPU impl
  renderer/       SdfRenderer (pipeline, buffers)
  scene/          SceneManager, SdfObjectFactory
    data/         Plain data types (SdfObjectData, GlobalsData, enums)
    csg/          CSG tree + RPN compiler
  camera/         Camera (math) + CameraController (input-driven)
  interaction/    SelectionState
  serialization/  ExtensionLoader (.sdfo binary), SceneSerializer (new/open/save)
  utils/          FileHelper
  bindings/       WebBindings.cpp — C API exposed to JS (Emscripten only)
  tools/          create_test_sdfo (generates a sample scene file)
  main.cpp
web/              React + Vite + TypeScript front-end
  src/engine.ts   Loads the WASM module and wraps the C bindings
  src/App.tsx     Editor UI
  public/engine/  Emscripten build output lands here (sdf-engine.js/.wasm/.data)
```

## Building & running (web target)

Requires CMake 3.20+, the Emscripten SDK (bundled in `deps/emsdk`), and Node 18+.

**1. Compile the engine to WASM** (activate emsdk first):

```
deps/emsdk/emsdk activate latest      # or run deps/setup_em.ps1
emcmake cmake -S . -B build-web
cmake --build build-web --target main
```

This emits `web/public/engine/sdf-engine.{js,wasm,data}`.

**2. Run the React front-end:**

```
cd web
npm install
npm run dev          # opens http://localhost:5173
```

The dev server serves the engine from `public/engine`. Rebuild step 1 whenever
the C++ changes; the page picks up the new `sdf-engine.*` on reload.

## Building (native target, optional)

A native build (wgpu-native + an SDL window) is still supported for quick engine
iteration without a browser. It renders the SDF viewport only — there is no
native UI, since the UI is the web app.

```
cmake -S . -B build
cmake --build build
```

Targets: `main` (engine/viewport) and `create_test_sdfo` (writes a sample
`assets/test.sdfo`).

## C bindings (JS ⇄ engine)

`src/bindings/WebBindings.cpp` exposes `sdf_*` functions (add/remove/select
objects, set transform/color, get scene JSON, camera orbit/pan/zoom, grid,
resize, new/open/save). `web/src/engine.ts` wraps them in a typed `Engine`
class.

## .sdfo file format

Binary layout: a 4-byte `SDFO` magic tag, an `int32` object count, then that many
`SdfObjectData` records.
