// Loads the compiled C++/WASM SDF engine and wraps its C bindings
// (src/bindings/WebBindings.cpp) in a typed TypeScript API.
export enum PrimitiveType {
  Sphere = 0,
  Box = 1,
  Cylinder = 2,
  Torus = 3,
}

// Shape parameters interpreted per primitive type (mirrors PrimitiveParams).
export interface SdfParams {
  radius: number;
  height: number;
  thickness: number;
  roundness: number;
}

// Identifies one shape parameter for setObjectParam (mirrors PrimitiveParam).
export enum ShapeParam {
  Radius = 0,
  Height = 1,
  Thickness = 2,
  Roundness = 3,
}

export interface SdfObject {
  index: number;
  id: number;
  type: number;
  typeName: string;
  position: [number, number, number];
  size: [number, number, number];
  color: [number, number, number];
  // Editor attributes, owned by the engine (EditorObjectStore).
  name: string;
  visible: boolean;
  locked: boolean;
  rotation: [number, number, number];
  scale: [number, number, number];
  params: SdfParams;
}

// One node of the engine's CSG tree. `type` is CsgNodeType
// (0 = Primitive, 1 = Union, 2 = Subtract, 3 = Intersection). `objectId` is
// valid only for primitive nodes (-1 otherwise); `children` lists child nodeIds
// in evaluation order (for Subtract, children[0] is the base that the rest are
// subtracted from).
export interface CsgNodeJson {
  nodeId: number;
  type: number;
  objectId: number;
  children: number[];
}

export interface CsgTreeJson {
  root: number;
  nodes: CsgNodeJson[];
}

// Boolean operation codes accepted by Engine.applyOperation — these mirror
// CsgNodeType on the C++ side.
export enum CsgOp {
  Union = 1,
  Subtract = 2,
  Intersect = 3,
}

export interface SceneState {
  selectedIndex: number;
  showGrid: boolean;
  objects: SdfObject[];
  // The engine owns the selection. These mirror SelectionState on the C++ side:
  // every currently-selected CSG nodeId, and the active/primary one (the base
  // for Subtract). Optional so the UI still works against an older engine build.
  selectedNodeIds?: number[];
  primaryNodeId?: number;
  // Present once the engine serializes its CSG hierarchy. Optional so the UI
  // still works against an engine build that predates the tree section.
  tree?: CsgTreeJson;
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
    args: unknown[],
    opts?: { async?: boolean }
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

  // ---- Selection (engine-owned) -------------------------------------------
  // Selection state lives in the C++ SelectionState; these just issue commands.
  // Read the result back via getScene().selectedNodeIds / primaryNodeId.

  // mode 0 = replace selection with this node, 1 = toggle it in the set. The
  // engine tracks the active/primary node (the base for Subtract).
  selectNode(nodeId: number, mode: number): void {
    this.call("sdf_select_node", null, ["number", "number"], [nodeId, mode]);
  }

  clearSelection(): void {
    this.call("sdf_clear_selection", null, [], []);
  }

  // Groups the engine's current selection under a new operation node and returns
  // the new node's id (-1 on failure, e.g. fewer than two nodes selected). The
  // primary node is the base for Subtract.
  applyOperation(op: CsgOp): number {
    return this.call("sdf_apply_operation", "number", ["number"], [op]) as number;
  }

  // Removes every currently-selected node — primitives (and their objects) and
  // operation nodes (and their subtrees).
  deleteSelection(): void {
    this.call("sdf_delete_selection", null, [], []);
  }

  selectObject(index: number): void {
    this.call("sdf_select_object", null, ["number"], [index]);
  }

  async pickObject(x: number, y: number): Promise<number> {
  const promise = this.module.ccall(
    "sdf_pick_object",
    "number",
    ["number", "number"],
    [x, y],
    { async: true }
  ) as Promise<number>;
  return await promise;
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

  // ---- Editor attributes (engine-owned) -----------------------------------

  setObjectName(index: number, name: string): void {
    this.call("sdf_set_object_name", null, ["number", "string"], [index, name]);
  }

  setObjectVisible(index: number, visible: boolean): void {
    this.call("sdf_set_object_visible", null, ["number", "number"], [index, visible ? 1 : 0]);
  }

  setObjectLocked(index: number, locked: boolean): void {
    this.call("sdf_set_object_locked", null, ["number", "number"], [index, locked ? 1 : 0]);
  }

  setObjectRotation(index: number, x: number, y: number, z: number): void {
    this.call(
      "sdf_set_object_rotation",
      null,
      ["number", "number", "number", "number"],
      [index, x, y, z]
    );
  }

  // Scale and shape params recompute the object's SDF size in the engine.
  setObjectScale(index: number, x: number, y: number, z: number): void {
    this.call(
      "sdf_set_object_scale",
      null,
      ["number", "number", "number", "number"],
      [index, x, y, z]
    );
  }

  setObjectParam(index: number, param: ShapeParam, value: number): void {
    this.call(
      "sdf_set_object_param",
      null,
      ["number", "number", "number"],
      [index, param, value]
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

  // ---- Translate gizmo ----------------------------------------------------
  // All coords are in render (device) pixels, top-left origin. gizmoHover
  // returns the hit axis (0=none, 1=X, 2=Y, 3=Z).

  gizmoHover(x: number, y: number): number {
    return this.call("sdf_gizmo_hover", "number", ["number", "number"], [x, y]) as number;
  }

  gizmoBeginDrag(x: number, y: number): void {
    this.call("sdf_gizmo_begin_drag", null, ["number", "number"], [x, y]);
  }

  gizmoDrag(x: number, y: number): void {
    this.call("sdf_gizmo_drag", null, ["number", "number"], [x, y]);
  }

  gizmoEndDrag(): void {
    this.call("sdf_gizmo_end_drag", null, [], []);
  }

  // Enable/disable drawing the translate gizmo (drive this from the active
  // tool). The gizmo is rendered by the engine's SDF shader — in the same frame
  // as the scene — so there is no overlay geometry to read back.
  setGizmoEnabled(enabled: boolean): void {
    this.call("sdf_set_gizmo_enabled", null, ["number"], [enabled ? 1 : 0]);
  }
}
