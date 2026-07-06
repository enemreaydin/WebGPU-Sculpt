---
name: web-build-toolchain
description: How to build the Emscripten/WASM engine for the SDF Editor web app
metadata:
  type: project
---

Building the WASM engine (`web/public/engine/sdf-engine.{js,wasm,data}`) requires a very specific toolchain setup, none of which is on PATH by default:

- **Emscripten must be 3.1.61**, NOT the `latest` (6.0.2). The pinned WebGPU distribution in `deps/webgpu/webgpu.cmake` (emscripten-v3.1.61 tag) and all engine code use the legacy `-sUSE_WEBGPU` `webgpu/webgpu.h` API, which emscripten 6.x removed (it switched to the `emdawnwebgpu` port with a different API). Symptom of wrong version: `fatal error: 'webgpu/webgpu.h' file not found`.
  - Install/activate: `deps/emsdk/emsdk.bat install 3.1.61` then `activate 3.1.61`.
- **No ninja/cmake on the bash/PATH** — use the ones bundled with VS 2022: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`.
- emcc lives at `deps/emsdk/upstream/emscripten/emcc.exe`; activate the env with `deps/emsdk/emsdk_env.ps1` (or `deps/setup_em.ps1`, which also adds anaconda python).

Full build (PowerShell), after adding the VS CMake+Ninja dirs to `$env:Path` and sourcing `emsdk_env.ps1`:
```
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target main
```
Then hard-refresh the Vite dev server (`cd web && npm run dev`, port 5173) — it serves `public/engine/*` live, no restart needed.

## Gotcha: locateFile for the preloaded .data

The engine files are served from `/engine/`, but Emscripten resolves the preloaded `--preload-file` `.data` package relative to the PAGE url (`/sdf-engine.data`), not the script dir — so it 404s, the virtual FS stays empty, and shader reads fail with `[KRITIK]: Shader dosyalari okunamadi!`. Fix (in `web/src/engine.ts`): pass `locateFile: (path) => \`/engine/${path}\`` to `createSdfModule({...})` so both `.wasm` and `.data` resolve under `/engine/`. The `.wasm` alone loads fine without it (different lookup), which is misleading.

## Native build (optional desktop viewport)

The engine code targets the OLD WebGPU API (const char* labels, `ShaderModuleWGSLDescriptor`, `SurfaceGetCurrentTextureStatus::Success`, `Device::poll(bool)`, `getPreferredFormat`, `deviceLostCallback`). The default `WEBGPU_BACKEND=WGPU` fetches wgpu-native **v24**, whose API was rewritten (WGPUStringView labels, renamed descriptors/enums) → won't compile. CMakeLists now forces `WEBGPU_BACKEND=WGPU_STATIC` (wgpu v0.19.x) for non-Emscripten builds, which matches the code and the pinned Emscripten distribution.

Static wgpu-native (a Rust lib) also needs explicit Windows system libs linked: `ws2_32 userenv bcrypt ntdll opengl32 d3dcompiler propsys runtimeobject oleaut32` (added in CMakeLists). Without them: LNK2019 on `ioctlsocket`, `NtCreateFile`, `BCryptGenRandom`, `wglCreateContext`, etc.

If the VS/CMake-Tools `build/` dir was configured before this pin, it caches `WEBGPU_BACKEND=WGPU` + v24 deps — run "CMake: Delete Cache and Reconfigure" (or delete `build/`) so it picks up WGPU_STATIC.

See [[web-ui-architecture]] for how React drives the engine.
