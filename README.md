# SDF Editor

> Baykar internship &mdash; Imaging Systems Development

A small real-time editor for building scenes out of signed distance fields (SDFs).
Objects are raymarched on the GPU with WebGPU, and the interface is built with Dear ImGui.

## Features

- Add primitives: sphere, box, cylinder, torus
- Move objects with an ImGuizmo translate gizmo
- Edit position, size, color and boolean operation (union / subtract / intersect)
- Orbit / zoom / pan camera
- Save and load scenes as binary `.sdfo` files

## Project layout

```
assets/
  fonts/          UI font
  shaders/        WGSL shaders (sdf_core + main)
deps/             Third-party libraries (glfw, glm, imgui, imguizmo, webgpu)
src/
  editor/         Application + main loop, GUI panels (menu/settings/workspace), settings
  platform/       Window, GraphicsContext, WebGPU wrapper implementation
  renderer/       SdfRenderer (pipeline, buffers, render target)
  scene/          SceneManager, SdfObjectFactory
    data/         Plain data types (SdfObjectData, GlobalsData, enums)
  camera/         Camera (math) + CameraController (mouse input)
  interaction/    GizmoController, SelectionState
  serialization/  ExtensionLoader (.sdfo binary), SceneSerializer (new/open/save)
  ui/             Reusable widgets (Theme, AddObjectPanel)
  utils/          FileHelper
  tools/          create_test_sdfo (generates a sample scene file)
  main.cpp
```

## Building

Requires CMake 3.20+ and a C++17 compiler. Dependencies are fetched automatically.

```
cmake -S . -B build
cmake --build build
```

Two targets are produced:

- `main` &mdash; the editor
- `create_test_sdfo` &mdash; writes a sample `assets/test.sdfo`

## .sdfo file format

Binary layout: a 4-byte `SDFO` magic tag, an `int32` object count, then that many
64-byte `SdfObjectData` records.
