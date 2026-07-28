// Loads the compiled C++/WASM SDF engine and wraps its C bindings
// (src/bindings/WebBindings.cpp) in a typed TypeScript API.
export enum PrimitiveType {
  Sphere = 0,
  Box = 1,
  Cylinder = 2,
  Torus = 3,
  Link = 4,
  Cone = 5,
  Capsule = 6,
  SolidAngle = 7,
  Pyramid = 8,
}

// Shape parameters interpreted per primitive type (mirrors PrimitiveParams).
export interface SdfParams {
  radius: number;
  height: number;
  thickness: number;
  roundness: number;
  length: number;
  angle: number;
}

// Identifies one shape parameter for setObjectParam (mirrors PrimitiveParam).
export enum ShapeParam {
  Radius = 0,
  Height = 1,
  Thickness = 2,
  Roundness = 3,
  Length = 4,
  Angle = 5,
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
// (0 = Primitive, 1 = Union, 2 = Subtract, 3 = Intersection, 4 = Group,
// 5 = Modifier). `objectId` is
// valid only for primitive nodes (-1 otherwise); `children` lists child nodeIds
// in evaluation order (for Subtract, children[0] is the base that the rest are
// subtracted from).
export interface CsgNodeJson {
  nodeId: number;
  type: number;
  objectId: number;
  // Custom name for operation/group nodes (engine-owned, persisted). "" when the
  // node has no custom name; the UI then shows a generated default label.
  name?: string;
  children: number[];
  blendMode: number;
  blendRadius: number;
  modifierType: number;
  modifierId: number;
  modifierAmount: number;
  modifierEnabled: boolean;
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
  Group = 4, // pure container: groups the selection with no boolean effect (compiles as Union)
}

// What the gizmo edits, and which frame its handles are aligned to. Both mirror
// the enums in src/interaction/GizmoState.h — keep them in sync.
export enum GizmoMode {
  Translate = 0,
  Rotate = 1,
  Scale = 2,
}

export enum GizmoSpace {
  World = 0,
  Local = 1,
}

// Render "look" setting ids for Engine.setRenderSetting — mirror RenderSetting in
// src/editor/Settings.h. Vector components are individual ids. These are shared
// by the viewport and the export; the cost knobs live in QualitySetting below.
export enum RenderSetting {
  LightDirX = 0, LightDirY = 1, LightDirZ = 2,
  LightColorR = 3, LightColorG = 4, LightColorB = 5,
  LightIntensity = 6,
  AmbientStrength = 7,
  ShadowSharpness = 8,
  SpecularGloss = 9,
  SpecularStrength = 10,
  AoStrength = 11,
}

// Which of the engine's two quality budgets a setter addresses — mirrors
// QualityContext in src/editor/QualitySettings.h. The viewport budget drives the
// live canvas, the export budget drives the offscreen image render, and they are
// fully independent (work at 50% / no shadows, still export clean).
export enum QualityContext {
  Viewport = 0,
  Export = 1,
}

// Quality-budget ids for Engine.setQualitySetting — mirror QualitySetting in
// src/editor/QualitySettings.h.
export enum QualitySetting {
  MaxSteps = 0,
  HitEpsilon = 1,
  MaxDistance = 2,
  ShadowSteps = 3,
  ShadowDistance = 4,
  AoSamples = 5,
  AaSamples = 6,
  Taa = 7,
  ShadowsEnabled = 8,
  AoEnabled = 9,
}

// Current "look" settings, mirrored from the engine (Settings). Keys match the
// "render" object in the scene JSON.
export interface RenderSettings {
  lightDirX: number; lightDirY: number; lightDirZ: number;
  lightColR: number; lightColG: number; lightColB: number;
  lightIntensity: number;
  ambientStrength: number;
  shadowSharpness: number;
  specularGloss: number;
  specularStrength: number;
  aoStrength: number;
}

// One quality budget, mirrored from the engine (QualitySettings). Keys match the
// "viewportQuality" / "exportQuality" objects in the scene JSON.
export interface QualitySettings {
  maxSteps: number;
  hitEpsilon: number;
  maxDistance: number;
  shadowSteps: number;
  shadowDistance: number;
  aoSamples: number;
  aaSamples: number;
  taaEnabled: boolean;
  shadowsEnabled: boolean;
  aoEnabled: boolean;
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
  // Current render "look" settings (light/material). Optional so the UI still
  // works against an engine build that predates them.
  render?: RenderSettings;
  // The two independent quality budgets. Optional for the same reason.
  viewportQuality?: QualitySettings;
  exportQuality?: QualitySettings;
}

// Minimal shape of the Emscripten module we rely on.
interface EmscriptenModule {
  canvas: HTMLCanvasElement;
  HEAPU8: Uint8Array;
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

// Binary <-> base64 for the .sdfp project bytes that cross the ccall boundary.
// btoa/atob operate on binary strings (one char per byte), so we bridge through
// those. Chunked to avoid blowing the argument stack on large String.fromCharCode.
function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
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

  // Viewport pick -> selection. `pickIndex` is the object vector index the pick
  // buffer returned (-1 = empty space); `mode` 0 = replace, 1 = toggle. The engine
  // selects the whole top-level group containing the picked primitive (Blender-
  // style); individual nodes stay selectable from the outliner.
  selectByPick(pickIndex: number, mode: number): void {
    this.call("sdf_select_by_pick", null, ["number", "number"], [pickIndex, mode]);
  }

  // Groups the engine's current selection under a new operation node and returns
  // the new node's id (-1 on failure, e.g. fewer than two nodes selected). The
  // primary node is the base for Subtract.
  applyOperation(op: CsgOp): number {
    return this.call("sdf_apply_operation", "number", ["number"], [op]) as number;
  }

  setBlendMode(nodeId: number, mode: number): void { this.call("sdf_set_blend_mode", null, ["number", "number"], [nodeId, mode]); }
  setBlendRadius(nodeId: number, value: number): void { this.call("sdf_set_blend_radius", null, ["number", "number"], [nodeId, value]); }
  addRoundnessModifier(nodeId: number): number { return this.call("sdf_add_roundness_modifier", "number", ["number"], [nodeId]) as number; }
  setRoundnessEnabled(nodeId: number, enabled: boolean): void { this.call("sdf_set_roundness_enabled", null, ["number", "number"], [nodeId, enabled ? 1 : 0]); }
  setRoundnessAmount(nodeId: number, value: number): void { this.call("sdf_set_roundness_amount", null, ["number", "number"], [nodeId, value]); }
  removeRoundnessModifier(nodeId: number): void { this.call("sdf_remove_roundness_modifier", null, ["number"], [nodeId]); }

  // Group (CSG subtree) batch edits: fan a color/uniform-scale over every
  // primitive under the node. Engine-side single scene-edit → one undo step.
  setGroupColor(nodeId: number, r: number, g: number, b: number): void { this.call("sdf_set_group_color", null, ["number", "number", "number", "number"], [nodeId, r, g, b]); }
  scaleGroup(nodeId: number, factor: number): void { this.call("sdf_scale_group", null, ["number", "number"], [nodeId, factor]); }

  // Removes every currently-selected node — primitives (and their objects) and
  // operation nodes (and their subtrees).
  deleteSelection(): void {
    this.call("sdf_delete_selection", null, [], []);
  }

  selectObject(index: number): void {
    this.call("sdf_select_object", null, ["number"], [index]);
  }

  // Monotonic count of frames the engine has actually submitted. Sample it over
  // an interval to get the real render rate: with render-on-demand the engine
  // draws nothing while the scene is static, so counting requestAnimationFrame
  // callbacks instead would report the display refresh rate on an idle engine.
  getFrameCount(): number {
    return this.call("sdf_get_frame_count", "number", [], []) as number;
  }

  // Last measured GPU frame time in ms (submit → onSubmittedWorkDone, lightly
  // smoothed). Frame *rate* is capped by vsync and saturates at the refresh
  // rate; frame *time* is not, so this is the number that still moves when a
  // change makes rendering cheaper than 16.6 ms. 0 = not measured yet.
  getGpuFrameMs(): number {
    return this.call("sdf_get_gpu_frame_ms", "number", [], []) as number;
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

  // Name a CSG operation/group node by its permanent nodeId. Engine-owned and
  // persisted with the project (read the value back via getScene().tree nodes).
  setNodeName(nodeId: number, name: string): void {
    this.call("sdf_set_node_name", null, ["number", "string"], [nodeId, name]);
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

  // ---- Undo / Redo --------------------------------------------------------
  // Undo/redo logic lives in the C++ engine. The web side only forwards calls.

  undo(): void {
      this.call("sdf_undo", null, [], []);
  }

  redo(): void {
      this.call("sdf_redo", null, [], []);
  }

  canUndo(): boolean {
      return (this.call("sdf_can_undo", "number", [], []) as number) !== 0;
  }

  canRedo(): boolean {
      return (this.call("sdf_can_redo", "number", [], []) as number) !== 0;
  }

  clearUndoStack(): void {
      this.call("sdf_clear_undo_stack", null, [], []);
  }

  beginSceneEdit(name: string): void {
      this.call("sdf_begin_scene_edit", null, ["string"], [name]);
  }

  commitSceneEdit(): void {
      this.call("sdf_commit_scene_edit", null, [], []);
  }

  cancelSceneEdit(): void {
      this.call("sdf_cancel_scene_edit", null, [], []);
  }



  getScene(): SceneState {
    const json = this.call("sdf_get_scene_json", "string", [], []) as string;
    return JSON.parse(json) as SceneState;
  }

  // ---- File ---------------------------------------------------------------

  newScene(): void {
    this.call("sdf_new_scene", null, [], []);
  }

  // ---- Project (.sdfp) bytes ----------------------------------------------
  // The engine produces/consumes the raw project bytes; the browser (App.tsx via
  // the File System Access API) owns the actual file. Bytes cross the ccall
  // boundary base64-encoded because ccall marshals strings, not buffers.

  // Serialize the current scene to .sdfp project bytes.
  saveProject(): Uint8Array {
    const b64 = this.call("sdf_save_project", "string", [], []) as string;
    return base64ToBytes(b64);
  }

  // Load a .sdfp project from bytes read out of a file.
  loadProject(bytes: Uint8Array): void {
    this.call("sdf_load_project", null, ["string"], [bytesToBase64(bytes)]);
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

  cameraSnap(x: number, y: number, z: number): void {
    this.call("sdf_camera_snap", null, ["number", "number", "number"], [x, y, z]);
  }

  cameraOrientation(): { yaw: number; pitch: number } {
    return {
      yaw: this.call("sdf_camera_yaw", "number", [], []) as number,
      pitch: this.call("sdf_camera_pitch", "number", [], []) as number,
    };
  }

  cameraFrameSelected(): boolean {
    return (this.call("sdf_camera_frame_selected", "number", [], []) as number) !== 0;
  }

  cameraFrameAll(): boolean {
    return (this.call("sdf_camera_frame_all", "number", [], []) as number) !== 0;
  }

  // ---- Render settings ----------------------------------------------------
  // Set one "look" setting by RenderSetting id. Read current values back from
  // getScene().render.
  setRenderSetting(id: RenderSetting, value: number): void {
    this.call("sdf_set_render_setting", null, ["number", "number"], [id, value]);
  }

  // Set one quality budget by (context, QualitySetting id). Booleans are passed
  // as 1/0. Read current values back from getScene().viewportQuality /
  // .exportQuality.
  setQualitySetting(context: QualityContext, id: QualitySetting, value: number): void {
    this.call("sdf_set_quality_setting", null, ["number", "number", "number"], [context, id, value]);
  }
  setSelectionVisuals(show: boolean): void {
    this.call("sdf_set_selection_visuals", null, ["number"], [show ? 1 : 0]);
  }

  // onProgress (optional) is called on every poll tick with the engine's band
  // completion percentage (0..100); the engine renders the export in scissor
  // bands, so this reflects GPU work actually finished, not just submitted.
  async renderImage(width: number, height: number,
                    onProgress?: (percent: number) => void): Promise<Uint8ClampedArray> {
    const started = this.call("sdf_start_final_render", "number", ["number", "number"], [width, height]) as number;
    if (!started) throw new Error("The image render could not be started.");
    try {
      for (;;) {
        const state = this.call("sdf_final_render_state", "number", [], []) as number;
        if (state === 3) break;
        // State 4 covers both a failed readback and the engine's stall
        // watchdog firing — the usual cause of the latter is the OS resetting
        // the GPU driver because a single submission ran too long.
        if (state === 4) throw new Error("the GPU stopped responding (the display driver may have been reset). Try a lower export quality — fewer AA samples is the biggest saving.");
        if (onProgress) {
          onProgress(this.call("sdf_final_render_progress", "number", [], []) as number);
        }
        await new Promise<void>((resolve) => window.setTimeout(resolve, 16));
      }
      const ptr = this.call("sdf_final_render_pixels", "number", [], []) as number;
      const size = this.call("sdf_final_render_byte_size", "number", [], []) as number;
      if (!ptr || size !== width * height * 4) throw new Error("The rendered pixel buffer is invalid.");
      return new Uint8ClampedArray(this.module.HEAPU8.slice(ptr, ptr + size));
    } finally {
      this.call("sdf_release_final_render", null, [], []);
    }
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

  gizmoCancelDrag(): void {
      this.call("sdf_gizmo_cancel_drag", null, [], []);
  }

  // Enable/disable drawing the gizmo (drive this from the active tool). The
  // gizmo is rendered by the engine's SDF shader — in the same frame as the
  // scene — so there is no overlay geometry to read back.
  setGizmoEnabled(enabled: boolean): void {
    this.call("sdf_set_gizmo_enabled", null, ["number"], [enabled ? 1 : 0]);
  }

  // Translate (arrows) or rotate (rings). Ignored by the engine mid-drag.
  setGizmoMode(mode: GizmoMode): void {
    this.call("sdf_set_gizmo_mode", null, ["number"], [mode]);
  }

  // Align the handles to the world axes, or to the selected object's own axes.
  setGizmoSpace(space: GizmoSpace): void {
    this.call("sdf_set_gizmo_space", null, ["number"], [space]);
  }

  // The Render Resolution scale (render-buffer px per display px). Keeps the
  // gizmo a constant on-screen size when the backing store is up/down-scaled.
  setGizmoPixelScale(scale: number): void {
    this.call("sdf_set_gizmo_pixel_scale", null, ["number"], [scale]);
  }
}
