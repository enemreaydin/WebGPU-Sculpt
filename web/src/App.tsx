import { useCallback, useEffect, useRef, useState } from "react";
import { Engine, PrimitiveType, type SceneState, type SdfObject } from "./engine";
import "./App.css";
import { useShortcuts } from "./shortcuts.ts";
const EMPTY_SCENE: SceneState = { selectedIndex: -1, showGrid: true, objects: [] };

export default function App() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const engineRef = useRef<Engine | null>(null);
  const viewportRef = useRef<HTMLDivElement>(null);

  const [status, setStatus] = useState("Loading engine…");
  const [ready, setReady] = useState(false);
  const [scene, setScene] = useState<SceneState>(EMPTY_SCENE);

  const refresh = useCallback(() => {
    const engine = engineRef.current;
    if (engine) setScene(engine.getScene());
  }, []);

  // Boot the WASM engine once.
  useEffect(() => {
    let cancelled = false;
    const canvas = canvasRef.current;
    if (!canvas) return;

    Engine.create(canvas)
      .then((engine) => {
        if (cancelled) return;
        engineRef.current = engine;
        setReady(true);
        setStatus("Ready");
        // Match the render surface to the canvas' displayed size.
        const rect = canvas.getBoundingClientRect();
        canvas.width = Math.floor(rect.width);
        canvas.height = Math.floor(rect.height);
        engine.resize(canvas.width, canvas.height);
        setScene(engine.getScene());
      })
      .catch((err: unknown) => {
        if (!cancelled) setStatus(`Engine failed to load: ${String(err)}`);
      });

    return () => {
      cancelled = true;
    };
  }, []);

  // Keep the render surface in sync with the viewport size.
  useEffect(() => {
    const viewport = viewportRef.current;
    const canvas = canvasRef.current;
    if (!viewport || !canvas) return;

    const observer = new ResizeObserver(() => {
      const engine = engineRef.current;
      if (!engine) return;
      const rect = viewport.getBoundingClientRect();
      canvas.width = Math.max(1, Math.floor(rect.width));
      canvas.height = Math.max(1, Math.floor(rect.height));
      engine.resize(canvas.width, canvas.height);
    });
    observer.observe(viewport);
    return () => observer.disconnect();
  }, [ready]);

  // ---- Menu actions --------------------------------------------------------

  const addPrimitive = (type: PrimitiveType) => {
    engineRef.current?.addPrimitive(type);
    refresh();
  };

  const onNew = () => {
    engineRef.current?.newScene();
    refresh();
  };
  const onOpen = () => {
    engineRef.current?.openScene();
    refresh();
  };
  const onSave = () => {
    engineRef.current?.saveScene();
  };

  const toggleGrid = () => {
    const next = !scene.showGrid;
    engineRef.current?.setShowGrid(next);
    setScene((s) => ({ ...s, showGrid: next }));
  };

  const selectObject = (index: number) => {
    engineRef.current?.selectObject(index);
    refresh();
  };

  const removeSelected = () => {
    if (scene.selectedIndex < 0) return;
    engineRef.current?.removeObject(scene.selectedIndex);
    refresh();
  };

  // ---- Canvas camera interaction -------------------------------------------
  useShortcuts({
    ready,
    onNew,
    onOpen,
    onSave,
    toggleGrid,
    removeSelected,
    addPrimitive
  });
  const dragging = useRef(false);
  const panning = useRef(false);

  const onPointerDown = (e: React.PointerEvent) => {
    dragging.current = true;
    panning.current = e.button === 1 || e.ctrlKey || e.shiftKey;
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onPointerUp = (e: React.PointerEvent) => {
    dragging.current = false;
    panning.current = false;
    (e.target as HTMLElement).releasePointerCapture(e.pointerId);
  };
  const onPointerMove = (e: React.PointerEvent) => {
    if (!dragging.current) return;
    const engine = engineRef.current;
    if (!engine) return;
    if (panning.current) engine.cameraPan(e.movementX, e.movementY);
    else engine.cameraOrbit(e.movementX, e.movementY);
  };
  const onWheel = (e: React.WheelEvent) => {
    engineRef.current?.cameraZoom(-e.deltaY * 0.01);
  };


  const selected: SdfObject | undefined =
    scene.selectedIndex >= 0 ? scene.objects[scene.selectedIndex] : undefined;

  return (
    <div className="app">
      <header className="menubar">
        <span className="brand">SDF Editor</span>
        <MenuGroup label="File">
          <button onClick={onNew} disabled={!ready}>New</button>
          <button onClick={onOpen} disabled={!ready}>Open</button>
          <button onClick={onSave} disabled={!ready}>Save</button>
        </MenuGroup>
        <MenuGroup label="Add">
          <button onClick={() => addPrimitive(PrimitiveType.Sphere)} disabled={!ready}>Sphere</button>
          <button onClick={() => addPrimitive(PrimitiveType.Box)} disabled={!ready}>Box</button>
          <button onClick={() => addPrimitive(PrimitiveType.Cylinder)} disabled={!ready}>Cylinder</button>
          <button onClick={() => addPrimitive(PrimitiveType.Torus)} disabled={!ready}>Torus</button>
        </MenuGroup>
        <MenuGroup label="View">
          <label className="check">
            <input type="checkbox" checked={!!scene.showGrid} onChange={toggleGrid} disabled={!ready} />
            Grid
          </label>
        </MenuGroup>
        <span className="status">{status}</span>
      </header>

      <div className="body">
        <div className="viewport" ref={viewportRef}>
          <canvas
            id="canvas"
            ref={canvasRef}
            className="scene-canvas"
            onPointerDown={onPointerDown}
            onPointerUp={onPointerUp}
            onPointerMove={onPointerMove}
            onWheel={onWheel}
            onContextMenu={(e) => e.preventDefault()}
          />
        </div>

        <aside className="panel">
          <h2>Objects</h2>
          <ul className="object-list">
            {scene.objects.length === 0 && <li className="empty">No objects</li>}
            {scene.objects.map((o) => (
              <li
                key={o.id}
                className={o.index === scene.selectedIndex ? "selected" : ""}
                onClick={() => selectObject(o.index)}
              >
                {o.typeName} {o.index}
              </li>
            ))}
          </ul>

          {selected ? (
            <ObjectInspector
              key={selected.id}
              object={selected}
              onPosition={(x, y, z) => {
                engineRef.current?.setObjectPosition(selected.index, x, y, z);
                refresh();
              }}
              onSize={(x, y, z) => {
                engineRef.current?.setObjectSize(selected.index, x, y, z);
                refresh();
              }}
              onColor={(r, g, b) => {
                engineRef.current?.setObjectColor(selected.index, r, g, b);
                refresh();
              }}
              onDelete={removeSelected}
            />
          ) : (
            <p className="hint">Select an object to edit it.</p>
          )}
        </aside>
      </div>
    </div>
  );
}

function MenuGroup({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="menu-group">
      <span className="menu-label">{label}</span>
      <div className="menu-buttons">{children}</div>
    </div>
  );
}

function Vec3Input({
  label,
  value,
  onChange,
}: {
  label: string;
  value: [number, number, number];
  onChange: (x: number, y: number, z: number) => void;
}) {
  const set = (i: number, v: number) => {
    const next: [number, number, number] = [value[0], value[1], value[2]];
    next[i] = v;
    onChange(next[0], next[1], next[2]);
  };
  return (
    <div className="field">
      <label>{label}</label>
      <div className="vec3">
        {[0, 1, 2].map((i) => (
          <input
            key={i}
            type="number"
            step="0.1"
            value={Number.isFinite(value[i]) ? value[i] : 0}
            onChange={(e) => set(i, parseFloat(e.target.value) || 0)}
          />
        ))}
      </div>
    </div>
  );
}

function ObjectInspector({
  object,
  onPosition,
  onSize,
  onColor,
  onDelete,
}: {
  object: SdfObject;
  onPosition: (x: number, y: number, z: number) => void;
  onSize: (x: number, y: number, z: number) => void;
  onColor: (r: number, g: number, b: number) => void;
  onDelete: () => void;
}) {
  const toHex = (c: [number, number, number]) =>
    "#" +
    c
      .map((v) => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, "0"))
      .join("");

  return (
    <div className="inspector">
      <h3>
        {object.typeName} {object.index}
      </h3>
      <Vec3Input label="Position" value={object.position} onChange={onPosition} />
      <Vec3Input label="Size" value={object.size} onChange={onSize} />
      <div className="field">
        <label>Color</label>
        <input
          type="color"
          value={toHex(object.color)}
          onChange={(e) => {
            const hex = e.target.value;
            const r = parseInt(hex.slice(1, 3), 16) / 255;
            const g = parseInt(hex.slice(3, 5), 16) / 255;
            const b = parseInt(hex.slice(5, 7), 16) / 255;
            onColor(r, g, b);
          }}
        />
      </div>
      <button className="danger" onClick={onDelete}>
        Delete Object
      </button>
    </div>
  );
}
