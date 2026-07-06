# Running the SDF Editor (build, compile, run)

This is the complete, repeatable guide to build the C++ engine to WebAssembly
and run the React web app. Follow it top to bottom the first time; after that
you only need the short **Daily loop** at the end.

> Everything below is written for **Windows + PowerShell**. Paths match this
> machine's setup (Visual Studio 2022 Community + the bundled `deps/emsdk`).

---

## 0. What you are building

- **The engine** → C++ compiled by Emscripten into
  `web/public/engine/sdf-engine.js` + `.wasm` + `.data`.
- **The UI** → a React app in `web/` served by Vite at `http://localhost:5173`.

You need TWO things running/produced: (1) the compiled engine files, and
(2) the Vite dev server. The dev server serves the engine files from
`web/public/engine/`.

---

## 1. One-time prerequisites

You already have these on this machine, listed so a fresh setup is reproducible:

- **Node.js 18+** and **npm** (check: `node --version`).
- **CMake** and **Ninja** — bundled with Visual Studio 2022 at:
  - CMake: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`
  - Ninja: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja`
- **Emscripten** — bundled in `deps/emsdk`.

### 1a. Activate the correct Emscripten version (IMPORTANT: 3.1.61)

The code targets the WebGPU API from the Emscripten **3.1.61** era. The `latest`
Emscripten (6.x) will NOT compile it. Install + activate 3.1.61 once:

```powershell
& "$PWD\deps\emsdk\emsdk.bat" install 3.1.61
& "$PWD\deps\emsdk\emsdk.bat" activate 3.1.61
```

Verify:

```powershell
& "$PWD\deps\emsdk\emsdk_env.ps1"
emcc --version    # must print 3.1.61
```

---

## 2. Build the engine (C++ → WebAssembly)

Run these from the **project root** (`SDFEditor/`) in PowerShell.

```powershell
# Put the VS CMake + Ninja on PATH, and activate Emscripten, for THIS shell:
$vs = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:Path = "$vs\CMake\bin;$vs\Ninja;$env:Path"
& "$PWD\deps\emsdk\emsdk_env.ps1"

# Configure once (creates the build-web/ folder). Downloads WebGPU + builds SDL:
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile the engine (repeat this line after any C++ change):
cmake --build build-web --target main
```

Success looks like:
```
[N/N] Linking CXX executable .../web/public/engine/sdf-engine.js
```
and these three files now exist:
```
web/public/engine/sdf-engine.js
web/public/engine/sdf-engine.wasm
web/public/engine/sdf-engine.data
```

> Tip: the env setup (`$env:Path` + `emsdk_env.ps1`) only lasts for the current
> PowerShell window. Open a new window → run those two setup lines again before
> `cmake --build`.

---

## 3. Run the React web app

In a **separate** terminal, from the project root:

```powershell
cd web
npm install        # first time only (downloads React, Vite, etc.)
npm run dev        # starts the dev server
```

Open the URL it prints — usually **http://localhost:5173** — in a
**WebGPU-capable browser** (latest Chrome or Edge). You should see the grid,
and **Add → Sphere/Box/Cylinder/Torus** should draw shapes you can select and
edit in the right-hand panel. Drag on the canvas to orbit; wheel to zoom;
Ctrl/Shift-drag (or middle-drag) to pan.

If the canvas is blank, open DevTools (F12) → Console and check for errors
(see Troubleshooting).

---

## 4. Daily loop (after the one-time setup)

**When you change C++ code** (anything in `src/`, shaders in `assets/`):
```powershell
# in a shell that has the PATH + emsdk set up (see step 2):
cmake --build build-web --target main
# then hard-refresh the browser tab (Ctrl+Shift+R)
```

**When you change only web code** (`web/src/**`): nothing to rebuild — Vite
hot-reloads automatically. Just look at the browser.

**If `npm run dev` isn't running:** `cd web; npm run dev`.

---

## 5. Production build of the web app (optional)

To produce a static, deployable version of the UI:
```powershell
cd web
npm run build      # type-checks and bundles into web/dist/
npm run preview    # serve the built version locally to test it
```
Make sure the engine files in `web/public/engine/` are up to date first
(step 2), because they get copied into the build.

---

## 6. Native desktop build (optional, no browser)

A native Windows build still exists for quick engine iteration. It shows only
the 3D viewport (no UI — the UI is the web app). It is **not** required to run
the web app.

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build-native -G "Visual Studio 17 2022" -A x64
& $cmake --build build-native --config Debug --target main
# produces build-native/Debug/main.exe
```

> If you use the VS Code "CMake Tools" extension and its `build/` folder was
> configured before the WebGPU backend was pinned, it may still fail with
> WebGPU API errors. Run **"CMake: Delete Cache and Reconfigure"** from the
> command palette (or delete the `build/` folder) so it re-fetches the correct
> (older) wgpu-native backend.

---

## 7. Troubleshooting (symptoms we already hit)

- **`webgpu/webgpu.h file not found` during the web build**
  → Wrong Emscripten version. Activate 3.1.61 (step 1a), delete `build-web/`,
  reconfigure + rebuild.

- **Browser: `Aborted(... descriptor == nullptr ... wgpuCreateInstance)`**
  → Already fixed in code. If it reappears, ensure you rebuilt the engine after
  pulling changes.

- **Browser: `[KRITIK]: Shader dosyalari okunamadi!` (shaders not read)**
  → The engine `.data` file isn't being found. Confirm all three
  `web/public/engine/sdf-engine.*` files exist and were regenerated by the last
  build, and hard-refresh (Ctrl+Shift+R). The loader is told to fetch them from
  `/engine/` via `locateFile` in `web/src/engine.ts`.

- **Browser: `depthSlice ... non-3D attachment` or `TextureView ... cannot be
  used with Device`**
  → Already fixed (depthSlice set for web; React StrictMode removed so the
  engine is created only once). If it returns, make sure `web/src/main.tsx`
  does NOT wrap `<App/>` in `<React.StrictMode>` and that you rebuilt the engine.

- **Blank canvas, no errors, or "requestAdapter" failures**
  → Your browser/GPU may not support WebGPU. Use the latest Chrome/Edge and
  check `chrome://gpu` (WebGPU should be "Enabled").

- **`emcc` / `cmake` / `ninja` "not recognized"**
  → You opened a fresh shell and didn't run the setup lines. Re-run the
  `$env:Path = ...` line and `& "$PWD\deps\emsdk\emsdk_env.ps1"` (step 2).

---

## 8. Quick reference — folder map

```
src/                     C++ engine (compiles to WASM and native)
  bindings/WebBindings.cpp   C functions React calls
  editor/Application.cpp     engine app + render loop + control API
assets/shaders/*.wgsl    the SDF/CSG shaders (preloaded into the web build)
web/                     React + Vite UI
  src/App.tsx                the editor UI
  src/engine.ts              loads WASM + wraps the C functions
  public/engine/             <- compiled sdf-engine.js/.wasm/.data land here
build-web/               Emscripten build output (generated)
build-native/            native build output (generated, optional)
```
