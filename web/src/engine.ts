// Loads the compiled C++/WASM SDF engine and wraps its C bindings
// (src/bindings/WebBindings.cpp) in a typed TypeScript API.

export enum PrimitiveType {
  Sphere = 0,
  Box = 1,
  Cylinder = 2,
  Torus = 3,
}

export interface SdfObject {
  index: number;
  id: number;
  type: number;
  typeName: string;
  position: [number, number, number];
  size: [number, number, number];
  color: [number, number, number];
}

export interface SceneState {
  selectedIndex: number;
  showGrid: boolean;
  objects: SdfObject[];
}

// Minimal shape of the Emscripten module we rely on.
interface EmscriptenModule {
  canvas: HTMLCanvasElement;
  // Resolves runtime asset URLs (.wasm, and the preloaded .data package).
  locateFile: (path: string, prefix: string) => string;
  ccall: (
    name: string,
    returnType: string | null,
    argTypes: string[],
    args: unknown[]
  ) => unknown;
}

type ModuleFactory = (
  overrides: Partial<EmscriptenModule>
) => Promise<EmscriptenModule>;

declare global {
  interface Window {
    createSdfModule?: ModuleFactory;
  }
}

const ENGINE_SCRIPT_URL = "/engine/sdf-engine.js";

function loadScript(url: string): Promise<void> {
  return new Promise((resolve, reject) => {
    if (window.createSdfModule) {
      resolve();
      return;
    }
    const script = document.createElement("script");
    script.src = url;
    script.async = true;
    script.onload = () => resolve();
    script.onerror = () =>
      reject(new Error(`Failed to load engine script: ${url}`));
    document.head.appendChild(script);
  });
}

/**
 * Typed facade over the WASM engine. Construct via Engine.create(canvas).
 */
export class Engine {
  private module: EmscriptenModule;

  private constructor(module: EmscriptenModule) {
    this.module = module;
  }

  static async create(canvas: HTMLCanvasElement): Promise<Engine> {
    await loadScript(ENGINE_SCRIPT_URL);
    if (!window.createSdfModule) {
      throw new Error("createSdfModule was not defined by the engine script.");
    }
    // The engine files (.wasm, .data) are served from /engine/. Emscripten
    // otherwise resolves the preloaded .data package relative to the page URL
    // (which 404s), leaving the virtual filesystem — and the shaders — empty.
    const module = await window.createSdfModule({
      canvas,
      locateFile: (path: string) => `/engine/${path}`,
    });
    return new Engine(module);
  }

  private call(
    name: string,
    returnType: string | null,
    argTypes: string[],
    args: unknown[]
  ): unknown {
    return this.module.ccall(name, returnType, argTypes, args);
  }

  // ---- Scene editing ------------------------------------------------------

  addPrimitive(type: PrimitiveType): number {
    return this.call("sdf_add_primitive", "number", ["number"], [type]) as number;
  }

  removeObject(index: number): void {
    this.call("sdf_remove_object", null, ["number"], [index]);
  }

  selectObject(index: number): void {
    this.call("sdf_select_object", null, ["number"], [index]);
  }

  setObjectPosition(index: number, x: number, y: number, z: number): void {
    this.call(
      "sdf_set_object_position",
      null,
      ["number", "number", "number", "number"],
      [index, x, y, z]
    );
  }

  setObjectSize(index: number, x: number, y: number, z: number): void {
    this.call(
      "sdf_set_object_size",
      null,
      ["number", "number", "number", "number"],
      [index, x, y, z]
    );
  }

  setObjectColor(index: number, r: number, g: number, b: number): void {
    this.call(
      "sdf_set_object_color",
      null,
      ["number", "number", "number", "number"],
      [index, r, g, b]
    );
  }

  getScene(): SceneState {
    const json = this.call("sdf_get_scene_json", "string", [], []) as string;
    return JSON.parse(json) as SceneState;
  }

  // ---- File ---------------------------------------------------------------

  newScene(): void {
    this.call("sdf_new_scene", null, [], []);
  }

  openScene(): void {
    this.call("sdf_open_scene", null, [], []);
  }

  saveScene(): void {
    this.call("sdf_save_scene", null, [], []);
  }

  // ---- View / camera ------------------------------------------------------

  setShowGrid(show: boolean): void {
    this.call("sdf_set_show_grid", null, ["number"], [show ? 1 : 0]);
  }

  resize(width: number, height: number): void {
    this.call("sdf_resize", null, ["number", "number"], [width, height]);
  }

  cameraOrbit(dx: number, dy: number): void {
    this.call("sdf_camera_orbit", null, ["number", "number"], [dx, dy]);
  }

  cameraPan(dx: number, dy: number): void {
    this.call("sdf_camera_pan", null, ["number", "number"], [dx, dy]);
  }

  cameraZoom(delta: number): void {
    this.call("sdf_camera_zoom", null, ["number"], [delta]);
  }
}
