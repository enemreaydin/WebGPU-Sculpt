import React from "react";
import { Engine, PrimitiveType, CsgOp, ShapeParam, RenderSetting, GizmoMode, GizmoSpace } from "./engine";

// UI type-name <-> engine PrimitiveType enum.
const TYPE_ENUM: Record<string, PrimitiveType> = {
  sphere: PrimitiveType.Sphere,
  box: PrimitiveType.Box,
  cylinder: PrimitiveType.Cylinder,
  torus: PrimitiveType.Torus,
  link: PrimitiveType.Link,
  cone: PrimitiveType.Cone,
  capsule: PrimitiveType.Capsule,
  solidAngle: PrimitiveType.SolidAngle,
  pyramid: PrimitiveType.Pyramid,
};
const TYPE_NAME: Record<number, string> = {
  0: "sphere", 1: "box", 2: "cylinder", 3: "torus",
  4: "link", 5: "cone", 6: "capsule", 7: "solidAngle", 8: "pyramid",
};

/* ============================================================================
 * SDF Editor UI — faithful port of the root SDFEditorUI.html design mockup.
 *
 * The mockup shipped as a self-contained React-like class component driving a
 * Blender/Godot-style editor chrome. The component logic (state, tree model,
 * drag-fields, color picker, menus, etc.) is preserved verbatim here; only the
 * template DSL was translated to JSX and the dead viewport was replaced with
 * the real WebGPU/WASM engine canvas so the scene actually renders.
 *
 * The buttons still drive the mockup's own in-memory scene model — wiring them
 * to the C++/WASM engine bindings is deliberately left for a later pass.
 * ========================================================================== */

// Parse an inline CSS declaration string into a React style object. Lets us
// keep the mockup's exact `style="..."` strings instead of hand-translating
// every rule to camelCase.
function css(s: string): React.CSSProperties {
  const out: Record<string, string> = {};
  if (!s) return out;
  for (const decl of s.split(";")) {
    const i = decl.indexOf(":");
    if (i < 0) continue;
    const prop = decl.slice(0, i).trim();
    const val = decl.slice(i + 1).trim();
    if (!prop) continue;
    const camel = prop.replace(/-([a-z])/g, (_m, c: string) => c.toUpperCase());
    out[camel] = val;
  }
  return out as React.CSSProperties;
}

// A div/button/span that swaps in a hover style block (mirrors the mockup's
// `style-hover` attribute) while forwarding every other prop/handler.
function Hv({ tag = "div", s, h, onPointerEnter, onPointerLeave, ...rest }: any) {
  const [hv, setHv] = React.useState(false);
  const Tag: any = tag;
  return (
    <Tag
      {...rest}
      style={{ ...css(s), ...(hv && h ? css(h) : {}) }}
      onPointerEnter={(e: any) => {
        if (h) setHv(true);
        onPointerEnter && onPointerEnter(e);
      }}
      onPointerLeave={(e: any) => {
        if (h) setHv(false);
        onPointerLeave && onPointerLeave(e);
      }}
    />
  );
}

// Inline SVG / static markup injected verbatim (keeps hyphenated SVG attrs).
const Ico = ({ d }: { d: string }) => (
  <span style={{ display: "inline-flex" }} dangerouslySetInnerHTML={{ __html: d }} />
);
// Static block that stays transparent to flex layout via display:contents.
const Raw = ({ html }: { html: string }) => (
  <div style={{ display: "contents" }} dangerouslySetInnerHTML={{ __html: html }} />
);

// ---- Static markup lifted straight from the mockup ------------------------

const GIZMO_HTML = `<div style="position:absolute;top:14px;right:14px;pointer-events:none;opacity:0.92"><svg width="72" height="72" viewBox="0 0 72 72"><line x1="36" y1="36" x2="61" y2="36" stroke="#d8504a" stroke-width="2"></line><line x1="36" y1="36" x2="53" y2="19" stroke="#7fae3a" stroke-width="2"></line><line x1="36" y1="36" x2="36" y2="11" stroke="#4b84d6" stroke-width="2"></line><circle cx="61" cy="36" r="7.5" fill="#d8504a"></circle><circle cx="53" cy="19" r="7.5" fill="#7fae3a"></circle><circle cx="36" cy="11" r="7.5" fill="#4b84d6"></circle><circle cx="11" cy="36" r="5" fill="#101010" stroke="#d8504a" stroke-width="1.5"></circle><circle cx="19" cy="53" r="5" fill="#101010" stroke="#7fae3a" stroke-width="1.5"></circle><circle cx="36" cy="61" r="5" fill="#101010" stroke="#4b84d6" stroke-width="1.5"></circle><text x="61" y="39.5" text-anchor="middle" font-size="9" font-weight="600" fill="#101010" font-family="IBM Plex Sans">X</text><text x="53" y="22.5" text-anchor="middle" font-size="9" font-weight="600" fill="#101010" font-family="IBM Plex Sans">Y</text><text x="36" y="14.5" text-anchor="middle" font-size="9" font-weight="600" fill="#101010" font-family="IBM Plex Sans">Z</text></svg></div>`;

const KIT_SURFACES_HTML = `<div style="flex:0 0 auto;display:flex;flex-direction:column;gap:4px;width:180px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600;margin-bottom:2px">Surfaces</span><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#101010;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Viewport</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#101010</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#1f1f1f;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Panel</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#1F1F1F</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#262626;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Raised</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#262626</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#151515;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Inset</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#151515</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#2e2e2e;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Hover</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#2E2E2E</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#0c0c0c;border:1px solid #333"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Seam</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#0C0C0C</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#363636;border:1px solid #444"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Border</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#363636</span></div></div>`;

const KIT_ACCENT_HTML = `<div style="flex:0 0 auto;display:flex;flex-direction:column;gap:4px;width:190px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600;margin-bottom:2px">Accent &amp; Semantic</span><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#e8912d"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Accent</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#E8912D</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#f4a340"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Accent hover</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#F4A340</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#dedede"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Text</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#DEDEDE</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#a0a0a0"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Text secondary</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#A0A0A0</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#616161"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Disabled</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#616161</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#d8504a"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Axis X</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#D8504A</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#7fae3a"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Axis Y</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#7FAE3A</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#4b84d6"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Axis Z</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#4B84D6</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#c4433b"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Danger</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#C4433B</span></div><div style="display:flex;align-items:center;gap:8px"><span style="width:14px;height:14px;background:#d9a93a"></span><span style="flex:1;font-size:11px;color:#c9c9c9">Warning</span><span style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">#D9A93A</span></div></div>`;

const KIT_TYPO_HTML = `<div style="flex:0 0 auto;display:flex;flex-direction:column;gap:8px;width:210px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600">Typography</span><div><div style="font-size:12px;font-weight:600">Panel title</div><div style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">Plex Sans 600 · 12px</div></div><div><div style="font-size:12px">Menu / tree row</div><div style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">Plex Sans 400 · 12px</div></div><div><div style="font-size:11px;color:#a0a0a0">Control label</div><div style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">Plex Sans 400 · 11px</div></div><div><div style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#a0a0a0">Section header</div><div style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">Plex Sans 600 · 10px caps</div></div><div><div style="font-family:'IBM Plex Mono',monospace;font-size:11px">0.250 · 1920 × 1080</div><div style="font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a">Plex Mono 400 · 11px</div></div></div>`;

const KIT_BUTTONS_HTML = `<div style="flex:0 0 auto;display:flex;flex-direction:column;gap:6px;width:170px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600">Buttons</span><div style="display:flex;align-items:center;gap:8px"><button style="height:22px;padding:0 12px;background:#2a2a2a;color:#dedede;border:1px solid #363636;border-radius:2px;font-size:11px;font-family:inherit;cursor:pointer">Button</button><span style="font-size:10px;color:#7a7a7a">default</span></div><div style="display:flex;align-items:center;gap:8px"><button style="height:22px;padding:0 12px;background:#333333;color:#dedede;border:1px solid #404040;border-radius:2px;font-size:11px;font-family:inherit;cursor:pointer">Button</button><span style="font-size:10px;color:#7a7a7a">hover</span></div><div style="display:flex;align-items:center;gap:8px"><button style="height:22px;padding:0 12px;background:#e8912d;color:#151515;border:1px solid #e8912d;border-radius:2px;font-size:11px;font-weight:500;font-family:inherit;cursor:pointer">Button</button><span style="font-size:10px;color:#7a7a7a">active / primary</span></div><div style="display:flex;align-items:center;gap:8px"><button style="height:22px;padding:0 12px;background:#2a2a2a;color:#616161;border:1px solid #2e2e2e;border-radius:2px;font-size:11px;font-family:inherit;cursor:default">Button</button><span style="font-size:10px;color:#7a7a7a">disabled</span></div><div style="display:flex;margin-top:2px"><span style="width:26px;height:22px;display:flex;align-items:center;justify-content:center;background:#2a2a2a;border:1px solid #363636;border-right:none;color:#a8a8a8"><svg width="13" height="13" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg></span><span style="width:26px;height:22px;display:flex;align-items:center;justify-content:center;background:#e8912d;border:1px solid #e8912d;color:#151515"><svg width="13" height="13" viewBox="0 0 16 16"><path d="M8 1.75 14 4.9v6.2L8 14.25 2 11.1V4.9L8 1.75z M2 4.9l6 3.1 6-3.1 M8 8v6.25" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg></span><span style="width:26px;height:22px;display:flex;align-items:center;justify-content:center;background:#2a2a2a;border:1px solid #363636;border-left:none;color:#a8a8a8"><svg width="13" height="13" viewBox="0 0 16 16"><path d="M8 4.25c3.7 0 6.75 1.7 6.75 3.75S11.7 11.75 8 11.75 1.25 10.05 1.25 8 4.3 4.25 8 4.25z M5.75 8c0-.7 1-1.25 2.25-1.25S10.25 7.3 10.25 8 9.25 9.25 8 9.25 5.75 8.7 5.75 8z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg></span></div></div>`;

const KIT_TREE_HTML = `<div style="flex:0 0 auto;display:flex;flex-direction:column;gap:1px;width:200px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600;margin-bottom:5px">Tree rows</span><div style="display:flex;align-items:center;gap:6px;height:24px;padding:0 8px"><svg width="14" height="14" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="#8f8f8f" stroke-width="1.2"></path></svg><span style="flex:1;font-size:12px;color:#c9c9c9">Default</span></div><div style="display:flex;align-items:center;gap:6px;height:24px;padding:0 8px;background:#2a2a2a"><svg width="14" height="14" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="#8f8f8f" stroke-width="1.2"></path></svg><span style="flex:1;font-size:12px;color:#dedede">Hover</span></div><div style="display:flex;align-items:center;gap:6px;height:24px;padding:0 8px;background:#333333"><svg width="14" height="14" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="#f0a03c" stroke-width="1.2"></path></svg><span style="flex:1;font-size:12px;color:#f0a03c">Selected</span></div><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600;margin:10px 0 5px">Context menu</span><div style="background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:4px 0"><div style="display:flex;padding:4px 12px;font-size:12px;color:#d6d6d6"><span>Duplicate</span><span style="margin-left:auto;color:#616161;font-family:'IBM Plex Mono',monospace;font-size:10px">⇧D</span></div><div style="display:flex;padding:4px 12px;font-size:12px;color:#dedede;background:#2e2e2e"><span>Rename</span><span style="margin-left:auto;color:#616161;font-family:'IBM Plex Mono',monospace;font-size:10px">F2</span></div><div style="height:1px;background:#333;margin:4px 8px"></div><div style="display:flex;padding:4px 12px;font-size:12px;color:#d6d6d6"><span>Delete</span><span style="margin-left:auto;color:#616161;font-family:'IBM Plex Mono',monospace;font-size:10px">X</span></div></div></div>`;

const SHORTCUTS_GRID_HTML = `<div style="display:grid;grid-template-columns:repeat(3,1fr);gap:26px"><div style="display:flex;flex-direction:column;gap:7px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#e8912d;margin-bottom:2px">General</span><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">F1</span><span style="font-size:11px;color:#a0a0a0">Shortcut overlay</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Ctrl N</span><span style="font-size:11px;color:#a0a0a0">New scene</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Ctrl O</span><span style="font-size:11px;color:#a0a0a0">Open scene</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Ctrl S</span><span style="font-size:11px;color:#a0a0a0">Save scene</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Ctrl Z</span><span style="font-size:11px;color:#a0a0a0">Undo</span></div></div><div style="display:flex;flex-direction:column;gap:7px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#e8912d;margin-bottom:2px">Camera</span><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">MMB</span><span style="font-size:11px;color:#a0a0a0">Orbit</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">⇧ MMB</span><span style="font-size:11px;color:#a0a0a0">Pan</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Wheel</span><span style="font-size:11px;color:#a0a0a0">Zoom</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">.</span><span style="font-size:11px;color:#a0a0a0">Frame selected</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Home</span><span style="font-size:11px;color:#a0a0a0">Frame all</span></div></div><div style="display:flex;flex-direction:column;gap:7px"><span style="font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#e8912d;margin-bottom:2px">Tools &amp; Edit</span><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">Q</span><span style="font-size:11px;color:#a0a0a0">Select</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">G / R / S</span><span style="font-size:11px;color:#a0a0a0">Move / Rotate / Scale</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">⇧ D</span><span style="font-size:11px;color:#a0a0a0">Duplicate</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">X</span><span style="font-size:11px;color:#a0a0a0">Delete</span></div><div style="display:flex;align-items:center;gap:8px"><span style="display:inline-flex;min-width:22px;justify-content:center;padding:1px 6px;background:#2a2a2a;border:1px solid #3d3d3d;border-bottom-color:#0c0c0c;border-radius:3px;font-family:'IBM Plex Mono',monospace;font-size:10px;color:#c9c9c9">F2</span><span style="font-size:11px;color:#a0a0a0">Rename</span></div></div></div>`;

// Small helper: render an editable/drag numeric field control (transform axis,
// shape param, demo field) from a mockup `field()` descriptor.
function FieldCtl({ f, wrapS, editS, showS, prefix }: any) {
  const dis = !!f.disabled;
  // When disabled, drop the resize cursor + interactivity and dim the control.
  const wrap = dis ? wrapS.replace("cursor:ew-resize", "cursor:default") + ";opacity:0.4" : wrapS;
  const show = dis ? showS.replace(/color:#[0-9a-fA-F]+/, "color:#8a8a8a") : showS;
  return (
    <div
      onPointerDown={dis ? undefined : f.down}
      onPointerMove={dis ? undefined : f.move}
      onPointerUp={dis ? undefined : f.up}
      title={dis ? "Not applicable for this shape" : undefined}
      style={css(wrap)}
    >
      {prefix}
      {f.editing && (
        <input
          value={f.editText}
          onChange={f.onInput}
          onKeyDown={f.onKey}
          onBlur={f.onBlur}
          autoFocus
          style={css(editS)}
        />
      )}
      {f.show && <span style={css(show)}>{f.text}</span>}
    </div>
  );
}

function NumericSliderRow({ cfg, value, open, caretPath, onToggleHelp, onBegin, onCommit, onChange, disabled }: any) {
  const disp = cfg.int ? String(Math.round(value)) : String(Math.round(value * 10000) / 10000);
  return (
    <div style={css("display:flex;flex-direction:column;gap:3px")}>
      <div style={css("display:flex;align-items:center;gap:6px")}>
        <Hv onClick={onToggleHelp} s="flex:0 0 116px;display:flex;align-items:center;gap:4px;font-size:11px;color:#a0a0a0;cursor:default;overflow:hidden" h="color:#dedede">
          <Ico d={`<svg width="9" height="9" viewBox="0 0 16 16"><path d="${caretPath}" fill="none" stroke="#6f6f6f" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
          <span style={css("white-space:nowrap;overflow:hidden;text-overflow:ellipsis")}>{cfg.label}</span>
        </Hv>
        <input type="range" min={cfg.min} max={cfg.max} step={cfg.step} value={value} disabled={disabled}
          onPointerDown={disabled ? undefined : onBegin} onPointerUp={disabled ? undefined : onCommit} onPointerCancel={disabled ? undefined : onCommit}
          onChange={disabled ? undefined : (e: any) => onChange(Number(e.target.value))}
          style={css(`flex:1;min-width:0;height:4px;accent-color:#e8912d;cursor:${disabled ? "default" : "pointer"}`)} />
        <input type="number" min={cfg.min} max={cfg.max} step={cfg.step} key={cfg.key + ":" + value} defaultValue={disp}
          disabled={disabled} onBlur={disabled ? undefined : (e: any) => { onBegin(); onChange(Number(e.target.value)); onCommit(); }}
          onKeyDown={(e: any) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
          style={css("flex:0 0 56px;width:56px;height:22px;background:#151515;border:1px solid #303030;border-radius:2px;color:#dedede;font-family:'IBM Plex Mono',monospace;font-size:11px;padding:0 5px;box-sizing:border-box")} />
      </div>
      {open && <span style={css("font-size:10px;line-height:1.5;color:#8a8a8a;padding:0 6px 3px 15px")}>{cfg.help}</span>}
    </div>
  );
}

// ---- The editor component (logic ported verbatim from the mockup) ---------

export default class App extends React.Component<any, any> {
  // Icon path table.
  // Default blend radius applied when creating a Smooth Union/Subtract/Intersect
  // from the toolbar (the modifier panel can fine-tune it afterward).
  SMOOTH_DEFAULT_RADIUS = 0.3;
  IC: any = {
    collection: "M1.75 3.25h4.5l1.5 1.9h6.5v7.6H1.75V3.25z",
    sphere: "M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5",
    box: "M8 1.75 14 4.9v6.2L8 14.25 2 11.1V4.9L8 1.75z M2 4.9l6 3.1 6-3.1 M8 8v6.25",
    cylinder: "M3.75 4.2C3.75 3 5.65 2 8 2s4.25 1 4.25 2.2v7.6C12.25 13 10.35 14 8 14s-4.25-1-4.25-2.2V4.2z M3.75 4.2C3.75 5.4 5.65 6.4 8 6.4s4.25-1 4.25-2.2",
    torus: "M8 4.25c3.7 0 6.75 1.7 6.75 3.75S11.7 11.75 8 11.75 1.25 10.05 1.25 8 4.3 4.25 8 4.25z M5.75 8c0-.7 1-1.25 2.25-1.25S10.25 7.3 10.25 8 9.25 9.25 8 9.25 5.75 8.7 5.75 8z",
    link: "M6 5a2 2 0 1 0 0 4 2 2 0 0 0 0-4z M10 7a2 2 0 1 0 0 4 2 2 0 0 0 0-4z",
    cone: "M8 2 14 13H2L8 2z M2 13c0 .8 2.7 1.5 6 1.5s6-.7 6-1.5",
    capsule: "M8 2.25c-1.8 0-3.25 1.45-3.25 3.25v5c0 1.8 1.45 3.25 3.25 3.25s3.25-1.45 3.25-3.25v-5C11.25 3.7 9.8 2.25 8 2.25z",
    solidAngle: "M8 8 8 2A6 6 0 0 1 13.2 5z M8 8 2.8 5A6 6 0 0 1 8 2",
    pyramid: "M8 2 14.5 12.5H1.5L8 2z M1.5 12.5 8 9.5l6.5 3 M8 2v7.5",
    eye: "M1.5 8s2.4-4.25 6.5-4.25S14.5 8 14.5 8s-2.4 4.25-6.5 4.25S1.5 8 1.5 8z M8 9.9a1.9 1.9 0 1 0 0-3.8 1.9 1.9 0 0 0 0 3.8z",
    eyeOff: "M2.7 8.9C1.95 8.4 1.5 8 1.5 8s2.4-4.25 6.5-4.25c.9 0 1.75.2 2.5.55 M13.3 7.1c.75.5 1.2.9 1.2.9s-2.4 4.25-6.5 4.25c-.9 0-1.75-.2-2.5-.55 M3 13.5 13 2.5",
    lock: "M5 7.25V5a3 3 0 0 1 6 0v2.25 M3.75 7.25h8.5v6h-8.5v-6z",
    unlock: "M5 7.25V5a3 3 0 0 1 5.8-1.1 M3.75 7.25h8.5v6h-8.5v-6z",
    caretR: "M6.25 4.5 9.75 8l-3.5 3.5",
    caretD: "M4.5 6.25 8 9.75l3.5-3.5",
    play: "M5.25 3.25 12.5 8l-7.25 4.75z",
    pause: "M5.5 3.5v9 M10.5 3.5v9",
  };
  PRM_DEFAULTS: any = {
    sphere: { radius: 1 },
    box: { round: 0.02 },
    cylinder: { radius: 0.4, height: 2 },
    torus: { radius: 0.8, thickness: 0.25 },
    link: { radius: 0.4, length: 0.5, thickness: 0.15 },
    cone: { height: 1.5, angle: 0.5 },
    capsule: { radius: 0.4, height: 0.7 },
    solidAngle: { radius: 1.2, angle: 0.8 },
    pyramid: { radius: 0.6, height: 1.2 },
  };
  PRM_ROWS: any = {
    sphere: [["Radius", "radius", 0.01]],
    box: [["Round", "round", 0.005]],
    cylinder: [["Radius", "radius", 0.01], ["Height", "height", 0.01]],
    torus: [["Radius", "radius", 0.01], ["Thickness", "thickness", 0.005]],
    link: [["Radius", "radius", 0.01], ["Length", "length", 0.01], ["Thickness", "thickness", 0.005]],
    cone: [["Height", "height", 0.01], ["Angle", "angle", 0.01]],
    capsule: [["Radius", "radius", 0.01], ["Height", "height", 0.01]],
    solidAngle: [["Radius", "radius", 0.01], ["Angle", "angle", 0.01]],
    pyramid: [["Base", "radius", 0.01], ["Height", "height", 0.01]],
  };

  // Which scale axes (X, Y, Z) actually affect each primitive's SDF `size`.
  // Inactive axes are greyed out in the properties panel (see PrimitiveGeometry).
  SCALE_AXES: Record<string, boolean[]> = {
    sphere:     [true, false, false],
    box:        [true, true, true],
    cylinder:   [true, true, false],
    torus:      [true, false, false],
    link:       [true, true, false],
    cone:       [false, true, false],
    capsule:    [true, true, false],
    solidAngle: [true, false, false],
    pyramid:    [true, true, false],
  };

  // Property-row param name -> engine ShapeParam id (see engine.ts).
  PARAM_ID: Record<string, ShapeParam> = {
    radius: ShapeParam.Radius,
    height: ShapeParam.Height,
    thickness: ShapeParam.Thickness,
    round: ShapeParam.Roundness,
    length: ShapeParam.Length,
    angle: ShapeParam.Angle,
  };

  // Transient (non-render) scratch handles used by drag helpers.
  _dg: any = null;
  _sl: any = null;
  _sp: any = null;
  _rl = false;
  _sv = false;
  _hu = false;
  _groupScaleLast = 1;   // slider value at the start of a group-scale drag
  _kd: any = null;
  _pt: any = null;

  // Engine / canvas plumbing.
  canvasRef = React.createRef<HTMLCanvasElement>();
  viewportRef = React.createRef<HTMLDivElement>();
  engine: Engine | null = null;
  _ro: ResizeObserver | null = null;
  // FPS meter: a rAF loop counts frames and updates state.fps ~2×/sec. The engine
  // renders on its own emscripten rAF; under GPU load the browser throttles rAF
  // (it won't call back faster than the canvas can present), so this tracks the
  // real displayed framerate — the number the user actually perceives.
  _rafId: number | null = null;
  _fpsFrames = 0;
  _fpsLast = 0;
  _camDrag = false;
  _camPan = false;
  _downPos: { x: number; y: number } | null = null;
  _didDrag = false;
  _gizmoDrag = false;                // dragging a gizmo axis
  // The .sdfp project file the user is editing (File System Access API handle).
  // null until the first Save names one; kept so later Saves overwrite it silently.
  _fileHandle: FileSystemFileHandle | null = null;
  // Action awaiting the discard-changes confirmation (New / Open on a dirty scene).
  _pendingAction: (() => void) | null = null;

  state: any = {
    openMenu: null,
    fileName: null,        // current .sdfp file name, shown in the title bar (null = unsaved)
    confirm: null,         // { title, body, okLabel } when the discard-changes modal is open
    tool: "select",
    gizmoSpace: "world",   // "world" | "local" — which axes the gizmo handles follow
    shading: "solid",
    grid: true,
    snap: false,
    // Canvas render dimensions (kept in state so a resize re-renders panels).
    vpW: 1,
    vpH: 1,
    fps: 0,               // displayed framerate, sampled by the rAF meter (~2×/sec)
    // Internal render resolution as a fraction of the viewport's CSS size. The
    // raymarch cost scales with pixel count, so 0.75/0.5 are big, cheap FPS wins;
    // the browser upscales the smaller backing store to fill the canvas. This is
    // a viewport DISPLAY preference (like DPR), not scene state — it lives here,
    // not in the engine, and modulates what syncCanvasSize tells the engine to render.
    renderScale: 1,
    rightW: 300,
    dockH: 200,
    outlH: 300,
    dockTab: null,
    propsTab: null,
    filter: "",
    expanded: { scene: true },
    // Primary selection key: "scene" (root collection), a stringified engine
    // object id (primitive), or "g<nodeId>" (a CSG operation/group node). Drives
    // the property panel and the 3D highlight.
    sel: "scene",
    // Every currently-selected key (always includes `sel`). Multi-selection for
    // applying boolean operations; single-select keeps this at [sel].
    selSet: ["scene"] as string[],
    // Engine's CSG hierarchy from the last getScene(); null before first read or
    // against an engine build that predates the tree section (flat fallback).
    tree: null as any,
    // Engine render settings mirror (light/material/quality), from getScene().render.
    render: null as any,
    // Whether the advanced (quality/performance) render settings are shown.
    advancedRender: false,
    // Which render-setting rows have their explanation expanded (keyed by setting key).
    renderHelp: {} as Record<string, boolean>,
    modifierHelp: {} as Record<string, boolean>,
    modifierMenu: false,
    // Toolbar "Smooth Boolean" flyout (Smooth Union/Subtract/Intersect).
    smoothMenu: false,
    // Transient group-panel controls (groups store no color/scale of their own).
    // groupHsv = last colour picked for the group; groupScaleUI = the scale slider's
    // session-relative factor (1 = the size the group had when selected). Both reset
    // when the selection changes (componentDidUpdate).
    groupHsv: null as any,
    groupScaleUI: 1,
    blendOpen: true,
    roundnessOpen: true,
    renaming: null,
    renameText: "",
    // Engine-owned object list (rebuilt from engine.getScene() after each mutation).
    objects: [] as any[],
    // UI-only per-object attributes, keyed by stringified engine object id. These
    // hold what the engine has no storage for: display name, visibility/lock flags,
    // rotation, non-uniform scale, shape params, and the (planned) blend op/smooth.
    meta: {} as Record<string, any>,
    counts: { sphere: 0, box: 0, cylinder: 0, torus: 0, collection: 0 },
    secT: true,
    secS: true,
    secC: true,
    ctx: null,
    picker: null,
    dd: false,
    opDd: false,
    hexEdit: null,
    editing: null,
    editText: "",
    overlay: false,
    frame: 1,
    playing: false,
    saved: true,
    statusMsg: "",
    demoVal: 0.75,
    demoSlider: 0.4,
    demoCheck: true,
    logs: [] as any[],
  };

  componentDidMount() {
    this._kd = (e: KeyboardEvent) => {
            const target = e.target as HTMLElement | null;
            const isEditable =
                !!target &&
                (
                    target.tagName === "INPUT" ||
                    target.tagName === "TEXTAREA" ||
                    target.tagName === "SELECT" ||
                    target.isContentEditable ||
                    !!target.closest("[contenteditable='true']")
                );

            // F1 overlay shortcut. Do not steal keys while the user is typing.
            if (!isEditable && e.key === "F1") {
        e.preventDefault();
        this.setState((s: any) => ({ overlay: !s.overlay }));
        return;
      }

            // Text fields keep their native browser/editor undo behavior.
            if (isEditable) return;

            const k = e.key.toLowerCase();
            const isCtrlOrCmd = e.ctrlKey || e.metaKey;

            // Undo / Redo must be handled before single-key tools.
            if (isCtrlOrCmd) {
                if (k === "z") {
                    e.preventDefault();
                    if (e.shiftKey) this.redo();
                    else this.undo();
        return;
      }

                if (k === "y") {
                    e.preventDefault();
                    this.redo();
                    return;
                }

                if (k === "o") {
                    e.preventDefault();
                    this.openProject();
                    return;
                }

                if (k === "s") {
                    e.preventDefault();
                    this.saveProject();
                    return;
                }

                if (k === "n") {
                    e.preventDefault();
                    this.newScene();
                    return;
                }
            }

            if (e.key === "Escape") {
                e.preventDefault();

                // If a gizmo drag is active, Esc cancels the engine-side transform
                // transaction and restores the begin-drag snapshot.
                if (this._gizmoDrag) {
                    this.cancelGizmoDrag();
                }

                this.setState({
                    overlay: false,
                    openMenu: null,
                    ctx: null,
                    picker: null,
                    dd: false,
                    opDd: false,
                    editing: null,
                    renaming: null,
                });

                return;
            }

      if (k === "q") this.setState({ tool: "select" });
      else if (k === "g") this.setState({ tool: "move" });
      else if (k === "r") this.setState({ tool: "rotate" });
      else if (k === "s") this.setState({ tool: "scale" });
      else if (k === "f2") {
        // Rename the current selection — primitive or group/operation node.
        const key = this.state.sel;
        if (key && key !== "scene") {
          const nm = this.state.meta[key]?.name || this.objById(key)?.typeName || "Group";
          this.setState({ renaming: key, renameText: nm });
        }
      } else if (k === "x" || e.key === "Delete") this.deleteSel();
      else if (k === "d" && e.shiftKey) this.duplicateSel();
    };
    window.addEventListener("keydown", this._kd);

    this.bootEngine();
  }

  componentWillUnmount() {
    window.removeEventListener("keydown", this._kd);
    if (this._pt) clearInterval(this._pt);
    if (this._ro) this._ro.disconnect();
    if (this._rafId !== null) cancelAnimationFrame(this._rafId);
  }

  // Keep the engine's gizmo in step with the active tool and space (the gizmo is
  // drawn by the engine shader; it only shows for the move and rotate tools).
  componentDidUpdate(_prevProps: any, prevState: any) {
    if (!this.engine) return;
    if (prevState.tool !== this.state.tool || prevState.gizmoSpace !== this.state.gizmoSpace) {
      this.syncGizmo();
    }
    // Reset the transient group controls when the selection changes.
    if (prevState.sel !== this.state.sel && (this.state.groupHsv !== null || this.state.groupScaleUI !== 1)) {
      this.setState({ groupHsv: null, groupScaleUI: 1 });
    }
  }

  // Push the whole gizmo configuration down to the engine at once. Mode is sent
  // even when the gizmo is hidden, so it is already correct the moment it shows.
  syncGizmo() {
    if (!this.engine) return;
    const tool = this.state.tool;
    this.engine.setGizmoEnabled(tool === "move" || tool === "rotate");
    this.engine.setGizmoMode(tool === "rotate" ? GizmoMode.Rotate : GizmoMode.Translate);
    this.engine.setGizmoSpace(this.state.gizmoSpace === "local" ? GizmoSpace.Local : GizmoSpace.World);
  }

  // ---- Engine / viewport ---------------------------------------------------

  bootEngine() {
    const canvas = this.canvasRef.current;
    const viewport = this.viewportRef.current;
    if (!canvas || !viewport) return;

    Engine.create(canvas)
      .then((engine) => {
        this.engine = engine;
        this.syncCanvasSize();
        this._ro = new ResizeObserver(() => this.syncCanvasSize());
        this._ro.observe(viewport);
        this.log("engine", "WebGPU device acquired — viewport ready");
        engine.setShowGrid(this.state.grid);
        this.syncGizmo();
        this.seedDemo();
        this.startFpsMeter();
      })
      .catch((err: unknown) => {
        this.log("error", "engine failed: " + String(err));
        this.status("Engine failed to load — check console");
      });
  }

  // Count frames on a rAF loop and publish FPS to state ~2×/sec (a coarse cadence
  // so the counter itself costs almost nothing and doesn't spam re-renders).
  startFpsMeter() {
    const tick = (now: number) => {
      this._fpsFrames++;
      if (this._fpsLast === 0) this._fpsLast = now;
      const dt = now - this._fpsLast;
      if (dt >= 500) {
        const fps = Math.round((this._fpsFrames * 1000) / dt);
        this._fpsFrames = 0;
        this._fpsLast = now;
        if (fps !== this.state.fps) this.setState({ fps });
      }
      this._rafId = requestAnimationFrame(tick);
    };
    this._rafId = requestAnimationFrame(tick);
  }

  // Populate a small starting scene through the real bindings so the viewport
  // shows something on load and the outliner mirrors what is actually rendered.
  seedDemo() {
    const e = this.engine;
    if (!e) return;
    const specs: Array<[string, [number, number, number]]> = [
      ["box", [0, -0.25, 0]],
      ["sphere", [-2.2, 1, 0]],
      ["cylinder", [2.2, 1, 0]],
      ["torus", [0, 2.4, 0]],
    ];
    for (const [t, pos] of specs) {
      const i = e.addPrimitive(TYPE_ENUM[t]);
      e.setObjectPosition(i, pos[0], pos[1], pos[2]);
    }
    e.clearSelection();
        e.clearUndoStack();
    this.refresh();
    this.setState({ saved: true });
    this.log("scene", "untitled.sdfo — " + specs.length + " objects");
  }

  syncCanvasSize() {
    const canvas = this.canvasRef.current;
    const viewport = this.viewportRef.current;
    if (!canvas || !viewport || !this.engine) return;
    const rect = viewport.getBoundingClientRect();
    // Backing-store size = CSS size × renderScale. CSS keeps the canvas full-size
    // (width:100%/height:100%), so the browser upscales a smaller buffer for free.
    const scale = this.state.renderScale || 1;
    const w = Math.max(1, Math.floor(rect.width * scale));
    const h = Math.max(1, Math.floor(rect.height * scale));
    canvas.width = w;
    canvas.height = h;
    this.engine.resize(w, h);
    this.setState({ vpW: w, vpH: h });
  }

  // Change the internal render resolution and immediately re-size the backing
  // store. canvasPoint() normalizes pointer coords by canvas.width/rect.width, so
  // picking + the gizmo stay pixel-correct at any scale.
  setRenderScale(scale: number) {
    this.setState({ renderScale: scale }, () => this.syncCanvasSize());
  }

  // Mouse position in render (device) pixels — the space the engine picks and
  // the gizmo hit-tests in. canvas.width may differ from its CSS size.
  canvasPoint(e: React.PointerEvent): { x: number; y: number } {
    const canvas = this.canvasRef.current;
    if (!canvas) return { x: 0, y: 0 };
    const rect = canvas.getBoundingClientRect();
    const sx = rect.width > 0 ? canvas.width / rect.width : 1;
    const sy = rect.height > 0 ? canvas.height / rect.height : 1;
    return { x: (e.clientX - rect.left) * sx, y: (e.clientY - rect.top) * sy };
  }

  // True when the gizmo is applicable: move/rotate tool + something the engine can
  // transform — either a selected primitive or a selected group/operation node.
  // (The engine draws the gizmo and computes its target; this only gates whether a
  // viewport press grabs an axis instead of orbiting.)
  gizmoActive(): boolean {
    const tool = this.state.tool;
    if (!(tool === "move" || tool === "rotate") || !this.engine) return false;
    return !!this.selObj() || this.isGroupKey(this.state.sel);
  }

  camDown = (e: React.PointerEvent) => {
    if (e.button === 2) return; // right-click → context menu

    // Grabbing a gizmo axis takes priority over camera/pick. If no axis is hit
    // we fall through, so clicking elsewhere still orbits / selects as before.
    if (e.button === 0 && this.gizmoActive()) {
      const p = this.canvasPoint(e);
      const axis = this.engine!.gizmoHover(p.x, p.y);
      if (axis !== 0) {
        this._gizmoDrag = true;
        this.engine!.gizmoBeginDrag(p.x, p.y);
        (e.target as HTMLElement).setPointerCapture(e.pointerId);
        this.setState({ saved: false });
        return;
      }
    }

    this._camDrag = true;
    this._camPan = e.button === 1 || e.ctrlKey || e.shiftKey;
    this._downPos = { x: e.clientX, y: e.clientY };
    this._didDrag = false;
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  };
  _pickInFlight = false;
  camUp = (e: React.PointerEvent) => {
    if (this._gizmoDrag) {
      this._gizmoDrag = false;
      this.engine?.gizmoEndDrag();
      try { (e.target as HTMLElement).releasePointerCapture(e.pointerId); } catch (_) {}
      this.refresh(); // pull the object's new position/rotation into the property panel
      return;
    }

    const wasClick = this._camDrag && !this._didDrag && e.button === 0;

    this._camDrag = false;
    this._camPan = false;
    this._downPos = null;
    try {
      (e.target as HTMLElement).releasePointerCapture(e.pointerId);
    } catch (_) {}

    if (wasClick && this.engine && !this._pickInFlight) {
      // Pick in render (device) pixels — the same space the engine's object-id
      // target lives in. Using raw CSS coords (clientX - rect.left) mis-picks
      // whenever the canvas render size differs from its displayed CSS size.
      const p = this.canvasPoint(e);
      // Ctrl/Shift/Cmd click adds to the selection; captured now since the
      // event may be recycled by the time the pick promise resolves.
      const additive = e.shiftKey || e.ctrlKey || e.metaKey;
      this._pickInFlight = true;
      this.engine.pickObject(p.x, p.y)
        .then((objectId) => this.selectByPick(objectId, additive))
        .finally(() => { this._pickInFlight = false; });
    }
  };

  camMove = (e: React.PointerEvent) => {
    if (!this.engine) return;

    // Active gizmo drag owns the pointer. The gizmo itself is drawn by the
    // engine's shader, so moving the object is all we do here — it and the gizmo
    // are redrawn together in the engine's next frame.
    if (this._gizmoDrag) {
      const p = this.canvasPoint(e);
      this.engine.gizmoDrag(p.x, p.y);
      return;
    }

    // Hover-highlight the gizmo axis under the cursor (updates engine state that
    // the overlay loop reads) when not orbiting.
    if (!this._camDrag && this.gizmoActive()) {
      const p = this.canvasPoint(e);
      this.engine.gizmoHover(p.x, p.y);
    }

    if (!this._camDrag) return;
    if (this._downPos) {
      const dx = e.clientX - this._downPos.x;
      const dy = e.clientY - this._downPos.y;
      if (Math.abs(dx) > 4 || Math.abs(dy) > 4) this._didDrag = true;
    }
    if (this._camPan) this.engine.cameraPan(e.movementX, e.movementY);
    else this.engine.cameraOrbit(e.movementX, e.movementY);
  };
  camWheel = (e: React.WheelEvent) => {
    this.engine?.cameraZoom(-e.deltaY * 0.01);
  };

  // ---- Ported logic --------------------------------------------------------

  now() {
    return new Date().toTimeString().slice(0, 8);
  }
  log(tag: string, msg: string) {
    this.setState((s: any) => ({ logs: [...s.logs, { time: this.now(), tag, msg }] }));
  }
  status(m: string) {
    this.setState({ statusMsg: m });
  }
  cap(t: string) {
    return t.charAt(0).toUpperCase() + t.slice(1);
  }
  pad3(n: number) {
    return String(n).padStart(3, "0");
  }

  countObjects(): number {
    return this.state.objects.length;
  }

  // Number of primitive leaves under a CSG node (recursively), for the group
  // properties panel. Reads the engine-emitted tree; 0 if the tree isn't ready.
  groupPrimitiveCount(nodeId: number): number {
    const t = this.state.tree;
    if (!t) return 0;
    const byId: Record<number, any> = {};
    for (const n of t.nodes) byId[n.nodeId] = n;
    let count = 0;
    const walk = (id: number) => {
      const n = byId[id];
      if (!n) return;
      if (n.type === 0) count++; // primitive leaf
      for (const c of n.children || []) walk(c);
    };
    walk(nodeId);
    return count;
  }

  // Representative colour (as HSV) for a group: read straight from its objects.
  // If they all share a colour (the case after a group-colour edit) that colour is
  // returned; otherwise the first primitive's colour stands in. Lets the group
  // colour control show the real state instead of a placeholder. null = no objects.
  groupColorHsv(nodeId: number): any {
    const t = this.state.tree;
    if (!t) return null;
    const byId: Record<number, any> = {};
    for (const n of t.nodes) byId[n.nodeId] = n;
    const colors: number[][] = [];
    const walk = (id: number) => {
      const n = byId[id];
      if (!n) return;
      if (n.type === 0) {
        const o = this.objById(String(n.objectId));
        if (o && o.color) colors.push(o.color);
      }
      for (const c of n.children || []) walk(c);
    };
    walk(nodeId);
    if (colors.length === 0) return null;
    const [r, g, b] = colors[0];
    return this.rgb2hsv(Math.round(r * 255), Math.round(g * 255), Math.round(b * 255));
  }

  // Engine object for a selection id ("scene" and null resolve to no object).
  objById(id: any): any {
    if (id == null || id === "scene") return null;
    return this.state.objects.find((o: any) => String(o.id) === String(id)) || null;
  }
  selObj(): any {
    return this.objById(this.state.sel);
  }

  // Build the UI view-model (`meta`) for an engine object. Everything here is a
  // straight mirror of engine-owned data (name/visibility/lock/rotation/scale/
  // shape params/color) — no logic. `prev` carries the two UI-only, engine-less
  // fields (blend `op` and `smooth`, both planned) across refreshes.
  metaFromObject(o: any, prev: any): any {
    const typeName = TYPE_NAME[o.type] || "box";
    const p = o.params || {};
    let prm: any;
    if (typeName === "sphere") prm = { radius: p.radius };
    else if (typeName === "cylinder") prm = { radius: p.radius, height: p.height };
    else if (typeName === "torus") prm = { radius: p.radius, thickness: p.thickness };
    else if (typeName === "link") prm = { radius: p.radius, length: p.length, thickness: p.thickness };
    else if (typeName === "cone") prm = { height: p.height, angle: p.angle };
    else if (typeName === "capsule") prm = { radius: p.radius, height: p.height };
    else if (typeName === "solidAngle") prm = { radius: p.radius, angle: p.angle };
    else if (typeName === "pyramid") prm = { radius: p.radius, height: p.height };
    else prm = { round: p.roundness };
    const c = o.color.map((v: number) => Math.round(Math.max(0, Math.min(1, v)) * 255));
    return {
      name: o.name,
      hidden: !o.visible,
      locked: !!o.locked,
      rot: o.rotation ? [...o.rotation] : [0, 0, 0],
      scl: o.scale ? [...o.scale] : [1, 1, 1],
      prm,
      op: prev?.op ?? "Union",       // UI-only, planned CSG blend selector
      smooth: prev?.smooth ?? 0,     // UI-only, planned blend radius
      hsv: this.rgb2hsv(c[0], c[1], c[2]),
    };
  }

  // Re-read the engine's authoritative scene. `objects` and their `meta`
  // view-model are rebuilt from what the engine reports (it owns name, transform,
  // shape params, etc.); the UI keeps no object logic of its own. Selection is
  // likewise mirrored from the engine. The legacy `adoptSel` param is a no-op.
  refresh(_adoptSel = false) {
    const e = this.engine;
    if (!e) return;
    const snap = e.getScene();
    const tree = snap.tree || null;
    this.setState((s: any) => {
      const counts = { ...s.counts };
      // Object meta is a pure mirror of engine data (rebuilt every refresh), so
      // engine-side edits like rename/scale show up without UI bookkeeping.
      const meta: Record<string, any> = {};
      for (const o of snap.objects) {
        const id = String(o.id);
        meta[id] = this.metaFromObject(o, s.meta[id]);
      }
      // CSG operation (group) nodes have no engine-side name yet, so their labels
      // stay UI-only: carry the previous meta over, or make a default.
      if (tree) {
        for (const nd of tree.nodes) {
          if (nd.type === 0 || nd.nodeId === tree.root) continue;
          const gk = "g" + nd.nodeId;
          const base = s.meta[gk] || this.defaultGroupMeta(nd.type, counts);
          // The engine owns the custom name (persisted). Fall back to the previous
          // UI label / generated default only when the node has no engine name.
          meta[gk] = nd.name ? { ...base, name: nd.name } : base;
        }
      }
      // Mirror the engine's selection (nodeIds) into UI selection keys.
      const keyForNodeId = (nid: number | undefined): string | null => {
        if (nid == null || nid < 0 || !tree) return null;
        const nd = tree.nodes.find((n: any) => n.nodeId === nid);
        if (!nd) return null;
        return nd.type === 0 ? String(nd.objectId) : "g" + nid;
      };
      const selSet = (snap.selectedNodeIds || [])
        .map(keyForNodeId)
        .filter((k: string | null): k is string => !!k);
      const primary = keyForNodeId(snap.primaryNodeId);
      const next: any = { objects: snap.objects, tree, meta, grid: !!snap.showGrid, counts, render: snap.render || s.render || null };
      next.selSet = selSet.length ? selSet : ["scene"];
      next.sel = primary || next.selSet[0];
      return next;
    });
  }

  // ---- Selection & CSG-tree helpers ---------------------------------------
  // Selection state is owned by the engine (SelectionState). These helpers only
  // translate between UI selection keys and engine CSG nodeIds, issue selection
  // commands, and then refresh() re-reads the authoritative selection back.
  //
  // A selection key is "scene" (root / nothing selected), a stringified object
  // id (primitive), or "g<nodeId>" (a CSG operation/group node).

  isGroupKey(key: any): boolean {
    return typeof key === "string" && key.length > 1 && key[0] === "g";
  }
  // Engine CSG nodeId a selection key refers to, or -1 (e.g. "scene", or when
  // the engine has not emitted a tree yet).
  nodeIdForKey(key: any): number {
    if (this.isGroupKey(key)) return Number(key.slice(1));
    const o = this.objById(key);
    const t = this.state.tree;
    if (!o || !t) return -1;
    const n = t.nodes.find((nd: any) => nd.type === 0 && nd.objectId === o.id);
    return n ? n.nodeId : -1;
  }

  // Housekeeping after any selection command: close transient popups and pull
  // the new selection back from the engine.
  afterSelect() {
    this.setState({ openMenu: null, ctx: null, picker: null, dd: false, opDd: false });
    this.refresh();
  }

  // Click-select a key. Additive (ctrl/shift/meta) toggles it in the engine's
  // set; a plain click replaces it. "scene" clears the selection.
  selectKey(key: string, additive = false) {
    const e = this.engine;
    if (!e) return;
    try {
      if (key === "scene") {
        e.clearSelection();
      } else {
        const nid = this.nodeIdForKey(key);
        if (nid < 0) return;
        e.selectNode(nid, additive ? 1 : 0);
      }
    } catch (err) {
      this.status("Selection needs the engine bridge");
      this.log("error", "select: " + String(err));
      return;
    }
    this.afterSelect();
  }

  // Back-compat single-select entry point (menus, root row).
  select(id: any) {
    this.selectKey(id, false);
  }

  // Viewport pick result -> selection. The pick buffer stores the object's vector
  // index (-1 = empty space). The engine maps that to the whole top-level group
  // containing the picked primitive (Blender-style) and updates the engine-owned
  // selection; we just forward the raw index and re-read the result via refresh().
  selectByPick(pickIndex: number, additive: boolean) {
    const e = this.engine;
    if (!e) return;
    try {
      e.selectByPick(pickIndex, additive ? 1 : 0);
    } catch (err) {
      this.status("Selection needs the engine bridge");
      this.log("error", "select: " + String(err));
      return;
    }
    this.afterSelect();
  }

  // ---- Render settings panel ----------------------------------------------
  // Config for the render panel. `id` = engine RenderSetting, `key` matches the
  // scene JSON `render` object, `help` is the standard-user explanation, min/max
  // are the clamp bounds. General ("look") settings are cheap/free; the advanced
  // set trades speed for quality.
  RENDER_SETTINGS = [
        {
            id: RenderSetting.LightIntensity, key: "lightIntensity", label: "Light Intensity", min: 0, max: 3, step: 0.05,
            help: "Overall brightness of the main light. Higher = brighter direct light. No performance cost."
        },
        {
            id: RenderSetting.AmbientStrength, key: "ambientStrength", label: "Ambient Light", min: 0, max: 1, step: 0.01,
            help: "Base fill light present everywhere, even in shadow. Higher = flatter and brighter; lower = darker, moodier shadows. No performance cost."
        },
        {
            id: RenderSetting.ShadowSharpness, key: "shadowSharpness", label: "Shadow Sharpness", min: 1, max: 64, step: 1,
            help: "How crisp shadow edges are. Higher = hard, sharp shadows; lower = soft, blurry penumbra. No performance cost."
        },
        {
            id: RenderSetting.SpecularGloss, key: "specularGloss", label: "Glossiness", min: 1, max: 256, step: 1,
            help: "Size of the shiny highlight. Higher = small, tight, polished-looking; lower = broad, matte. No performance cost."
        },
        {
            id: RenderSetting.SpecularStrength, key: "specularStrength", label: "Highlight Strength", min: 0, max: 1, step: 0.02,
            help: "Brightness of the specular highlight. 0 = fully matte, no shine. No performance cost."
        },
        {
            id: RenderSetting.AoStrength, key: "aoStrength", label: "Ambient Occlusion", min: 0, max: 5, step: 0.1,
            help: "Soft darkening in creases and where objects meet, which adds depth. Higher = darker contact shadows. Tiny performance cost."
        },
    { id: RenderSetting.LightColorR, key: "lightColR", label: "Light Color R", min: 0, max: 1, step: 0.02, help: "Red amount of the main light's color." },
    { id: RenderSetting.LightColorG, key: "lightColG", label: "Light Color G", min: 0, max: 1, step: 0.02, help: "Green amount of the main light's color." },
    { id: RenderSetting.LightColorB, key: "lightColB", label: "Light Color B", min: 0, max: 1, step: 0.02, help: "Blue amount of the main light's color." },
    { id: RenderSetting.LightDirX, key: "lightDirX", label: "Light Dir X", min: -1, max: 1, step: 0.05, help: "Horizontal direction the light shines from (left / right)." },
    { id: RenderSetting.LightDirY, key: "lightDirY", label: "Light Dir Y", min: -1, max: 1, step: 0.05, help: "Vertical direction of the light. Higher = more from above (overhead)." },
    { id: RenderSetting.LightDirZ, key: "lightDirZ", label: "Light Dir Z", min: -1, max: 1, step: 0.05, help: "Front / back direction the light shines from." },
  ];

  RENDER_ADVANCED = [
        {
            id: RenderSetting.MaxSteps, key: "maxSteps", label: "Max Ray Steps", min: 16, max: 512, step: 1, int: true,
            help: "How many steps each view ray may take. This is the MAIN performance dial: higher renders distant and thin shapes correctly but costs speed; too low and object edges dissolve or vanish."
        },
        {
            id: RenderSetting.HitEpsilon, key: "hitEpsilon", label: "Surface Threshold", min: 0.0001, max: 0.01, step: 0.0001,
            help: "How close a ray must get to count as hitting a surface. Smaller = sharper silhouettes and finer detail, but needs more Max Ray Steps or surfaces develop holes. Lower value = higher quality, higher cost."
        },
        {
            id: RenderSetting.MaxDistance, key: "maxDistance", label: "View Distance", min: 10, max: 200, step: 1,
            help: "How far view rays travel before giving up. Higher lets you see distant objects; too high just wastes steps on empty space."
        },
        {
            id: RenderSetting.ShadowSteps, key: "shadowSteps", label: "Shadow Steps", min: 8, max: 128, step: 1, int: true,
            help: "Step budget for shadow rays. Higher = more accurate soft shadows, at a performance cost."
        },
        {
            id: RenderSetting.ShadowDistance, key: "shadowDistance", label: "Shadow Distance", min: 5, max: 100, step: 1,
            help: "How far shadow rays search for blockers. Higher catches shadows from distant objects; costs performance."
        },
        {
            id: RenderSetting.AoSamples, key: "aoSamples", label: "AO Samples", min: 2, max: 16, step: 1, int: true,
            help: "Number of samples used for ambient occlusion. Higher = smoother contact shadows, lower performance."
        },
  ];

  // Apply a render setting: clamp to the config bounds, push to the engine, and
  // optimistically mirror into state (no full refresh — the engine just stores it).
  rsSet(cfg: any, value: number) {
    if (isNaN(value)) return;
    let v = Math.max(cfg.min, Math.min(cfg.max, value));
    if (cfg.int) v = Math.round(v);
    this.engine?.setRenderSetting(cfg.id, v);
    this.setState((s: any) => ({ render: { ...s.render, [cfg.key]: v }, saved: false }));
  }

  // Toggle a boolean render setting (shadows / AO). The engine takes a float, so
  // send 1/0; mirror the bool into state like rsSet does for numbers.
  rsBool(id: number, key: string, value: boolean) {
    this.engine?.setRenderSetting(id, value ? 1 : 0);
    this.setState((s: any) => ({ render: { ...s.render, [key]: value }, saved: false }));
  }

  rsToggleHelp(key: string) {
    this.setState((s: any) => ({ renderHelp: { ...s.renderHelp, [key]: !s.renderHelp[key] } }));
  }

  // One render-setting row: an expandable label (click to show the explanation),
  // a slider, and a number field. The number field is uncontrolled (keyed on the
  // value) so free typing works; it commits on blur / Enter.
  rsRow(cfg: any) {
    const r = this.state.render || {};
    const val = typeof r[cfg.key] === "number" ? r[cfg.key] : cfg.min;
    const open = !!this.state.renderHelp[cfg.key];
    return (
      <NumericSliderRow key={cfg.key} cfg={cfg} value={val} open={open}
        caretPath={open ? this.IC.caretD : this.IC.caretR}
        onToggleHelp={() => this.rsToggleHelp(cfg.key)}
        onBegin={() => this.engine?.beginSceneEdit("Change Render Setting")}
        onCommit={() => this.engine?.commitSceneEdit()}
        onChange={(value: number) => this.rsSet(cfg, value)} />
    );
  }

  // Default UI meta for a CSG operation (group) node the UI hasn't named yet.
  defaultGroupMeta(nodeType: number, counts: any): any {
    const opName = nodeType === 2 ? "Subtract" : nodeType === 3 ? "Intersect" : nodeType === 4 ? "Group" : "Union";
    const ck = "group" + opName;
    const n = (counts[ck] || 0) + 1;
    counts[ck] = n;
    return { name: opName + "." + this.pad3(n), hidden: false, locked: false, group: true, op: opName };
  }

  hsv2rgb(h: number, s: number, v: number) {
    const f = (n: number) => {
      const k = (n + h / 60) % 6;
      return v - v * s * Math.max(Math.min(k, 4 - k, 1), 0);
    };
    return [f(5), f(3), f(1)].map((x) => Math.round(x * 255));
  }
  rgb2hsv(r: number, g: number, b: number) {
    r /= 255;
    g /= 255;
    b /= 255;
    const mx = Math.max(r, g, b),
      mn = Math.min(r, g, b),
      d = mx - mn;
    let h = 0;
    if (d) {
      if (mx === r) h = ((g - b) / d) % 6;
      else if (mx === g) h = (b - r) / d + 2;
      else h = (r - g) / d + 4;
      h *= 60;
      if (h < 0) h += 360;
    }
    return { h, s: mx ? d / mx : 0, v: mx };
  }
  hex(rgb: number[]) {
    return "#" + rgb.map((v) => v.toString(16).padStart(2, "0")).join("").toUpperCase();
  }

  gf(key: string): number {
    const S = this.state;
    if (key === "demo") return S.demoVal;
    if (key === "demoSlider") return S.demoSlider;
    const o = this.selObj();
    if (!o) return 0;
    const m = S.meta[String(o.id)];
    if (!m) return 0;
    if (key === "smooth") return m.smooth;
    const [a, b] = key.split(".");
    if (a === "pos") return o.position[Number(b)];
    if (a === "rot") return m.rot[Number(b)];
    if (a === "scl") return m.scl[Number(b)];
    if (a === "prm") return m.prm[b];
    return 0;
  }
  sf(key: string, v: number) {
    const S = this.state;
    if (key === "demo") {
      this.setState({ demoVal: v });
      return;
    }
    if (key === "demoSlider") {
      this.setState({ demoSlider: Math.max(0, Math.min(1, v)) });
      return;
    }
    const o = this.selObj();
    if (!o) return;
    const id = String(o.id);
    const m = S.meta[id];
    if (!m) return;
    const e = this.engine;
    const [a, b] = key.split(".");

    if (!e) return;

    if (key === "smooth") {
      // Planned blend param — the only per-object field with no engine storage.
      const val = Math.max(0, Math.min(1, v));
      this.setState((s: any) => ({ meta: { ...s.meta, [id]: { ...s.meta[id], smooth: val } }, saved: false }));
      return;
    }
    if (a === "pos") {
      const p = [...o.position];
      p[Number(b)] = v;
      e.setObjectPosition(o.index, p[0], p[1], p[2]);
      this.setState({ saved: false });
      this.refresh();
      return;
    }
    if (a === "rot") {
      const rot = [...m.rot];
      rot[Number(b)] = v;
      e.setObjectRotation(o.index, rot[0], rot[1], rot[2]);
      this.setState({ saved: false });
      this.refresh();
      return;
    }
    if (a === "scl") {
      const scl = m.scl.map((x: number, i: number) => (i === Number(b) ? Math.max(0.001, v) : x));
      e.setObjectScale(o.index, scl[0], scl[1], scl[2]);
      this.setState({ saved: false });
      this.refresh();
      return;
    }
    if (a === "prm") {
      const param = this.PARAM_ID[b];
      if (param === undefined) return;
      e.setObjectParam(o.index, param, Math.max(0, v));
      this.setState({ saved: false });
      this.refresh();
      return;
    }
  }
  commitEdit() {
    const k = this.state.editing;
    if (!k) return;
    const v = parseFloat(this.state.editText);
        if (!isNaN(v)) {
            const engineField = k !== "demo" && k !== "demoSlider" && k !== "smooth";
            if (engineField) this.engine?.beginSceneEdit("Edit Numeric Field");
            this.sf(k, v);
            if (engineField) this.engine?.commitSceneEdit();
        }
    this.setState({ editing: null });
  }
  field(key: string, step: number, disabled?: boolean): any {
    const v = this.gf(key);
    if (disabled) {
      // Non-interactive: still shows the value, but greyed and inert. Used for
      // scale axes a shape ignores and for params not yet wired to the SDF.
      return {
        disabled: true,
        editing: false,
        show: true,
        text: (Math.round(v * 1000) / 1000).toFixed(2),
        editText: "",
        down: () => {},
        move: () => {},
        up: () => {},
        onInput: () => {},
        onKey: () => {},
        onBlur: () => {},
      };
    }
    const editing = this.state.editing === key;
    return {
      editing,
      show: !editing,
      text: (Math.round(v * 1000) / 1000).toFixed(2),
      editText: this.state.editText,
      down: (e: any) => {
        if (e.button !== 0) return;
        e.preventDefault();
        try {
          e.currentTarget.setPointerCapture(e.pointerId);
        } catch (_) {}
                if (key !== "demo" && key !== "demoSlider" && key !== "smooth") {
                    this.engine?.beginSceneEdit("Drag Numeric Field");
                }
        this._dg = { key, step, sx: e.clientX, start: this.gf(key), moved: 0 };
      },
      move: (e: any) => {
        const d = this._dg;
        if (!d || d.key !== key) return;
        const dx = e.clientX - d.sx;
        d.moved = Math.max(d.moved, Math.abs(dx));
        if (d.moved >= 3) this.sf(key, Math.round((d.start + dx * step) * 1000) / 1000);
      },
      up: (e: any) => {
        const d = this._dg;
        if (!d || d.key !== key) return;
        try {
          e.currentTarget.releasePointerCapture(e.pointerId);
        } catch (_) {}
                if (d.moved < 3) {
                    if (key !== "demo" && key !== "demoSlider" && key !== "smooth") {
                        this.engine?.cancelSceneEdit();
                    }
                    this.setState({ editing: key, editText: this.gf(key).toFixed(2) });
                }
                else if (key !== "demo" && key !== "demoSlider" && key !== "smooth") {
                    this.engine?.commitSceneEdit();
                }
        this._dg = null;
      },
      onInput: (e: any) => this.setState({ editText: e.target.value }),
      onKey: (e: any) => {
        if (e.key === "Enter") this.commitEdit();
        else if (e.key === "Escape") this.setState({ editing: null });
      },
      onBlur: () => this.commitEdit(),
    };
  }
  sliderH(key: string): any {
    const setFrom = (e: any) => {
      const r = e.currentTarget.getBoundingClientRect();
      this.sf(key, Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)));
    };
    return {
      down: (e: any) => {
        e.preventDefault();
        try {
          e.currentTarget.setPointerCapture(e.pointerId);
        } catch (_) {}
        this._sl = key;
        setFrom(e);
      },
      move: (e: any) => {
        if (this._sl === key) setFrom(e);
      },
      up: (e: any) => {
        try {
          e.currentTarget.releasePointerCapture(e.pointerId);
        } catch (_) {}
        this._sl = null;
      },
    };
  }
  split(key: string, axis: string, sign: number, min: number, max: number): any {
    return {
      down: (e: any) => {
        e.preventDefault();
        try {
          e.currentTarget.setPointerCapture(e.pointerId);
        } catch (_) {}
        this._sp = { key, start: this.state[key], s: axis === "x" ? e.clientX : e.clientY };
      },
      move: (e: any) => {
        const p = this._sp;
        if (!p || p.key !== key) return;
        const c = axis === "x" ? e.clientX : e.clientY;
        this.setState({ [key]: Math.max(min, Math.min(max, p.start + sign * (c - p.s))) });
      },
      up: (e: any) => {
        try {
          e.currentTarget.releasePointerCapture(e.pointerId);
        } catch (_) {}
        this._sp = null;
      },
    };
  }

  addPrimitive(type: string) {
    const e = this.engine;
    if (!e) return;
    e.addPrimitive(TYPE_ENUM[type]); // engine appends + selects the new object
    this.refresh(true); // names/meta assigned lazily; selection adopted from engine
    this.setState((s: any) => ({ saved: false, ctx: null, openMenu: null, expanded: { ...s.expanded, scene: true } }));
    this.log("scene", this.cap(type) + " added");
    this.status(this.cap(type) + " added");
  }
  deleteSel() {
    const e = this.engine;
    if (!e) return;
    const keys = (this.state.selSet || []).filter((k: string) => k !== "scene");
    if (keys.length === 0) return;
    const names = keys.map((k: string) => {
      const o = this.objById(k);
      return this.state.meta[k]?.name || (o ? o.typeName : "Group");
    });
    try {
      // The engine deletes its current selection (primitives + op subtrees).
      e.deleteSelection();
    } catch (err) {
      this.status("Delete failed — engine bridge missing");
      this.log("error", "delete: " + String(err));
      return;
    }
    this.refresh();
    this.setState({ ctx: null, saved: false });
    this.log("scene", names.join(", ") + " deleted");
    this.status(names.length + " deleted");
  }
  duplicateSel() {
    const o = this.selObj();
    const e = this.engine;
    if (!o || !e) return;
    const typeName = TYPE_NAME[o.type] || "box";
    // Copy the source's engine-owned attributes onto a fresh primitive through
    // the same setters the UI uses — no size math on the web side.
        e.beginSceneEdit("Duplicate Object");
        try {
    const idx = e.addPrimitive(TYPE_ENUM[typeName]);
    const p = o.position;
    e.setObjectPosition(idx, p[0] + 0.5, p[1], p[2]);
    e.setObjectScale(idx, o.scale[0], o.scale[1], o.scale[2]);
    e.setObjectParam(idx, ShapeParam.Radius, o.params.radius);
    e.setObjectParam(idx, ShapeParam.Height, o.params.height);
    e.setObjectParam(idx, ShapeParam.Thickness, o.params.thickness);
    e.setObjectParam(idx, ShapeParam.Roundness, o.params.roundness);
    e.setObjectParam(idx, ShapeParam.Length, o.params.length);
    e.setObjectParam(idx, ShapeParam.Angle, o.params.angle);
    e.setObjectRotation(idx, o.rotation[0], o.rotation[1], o.rotation[2]);
    e.setObjectColor(idx, o.color[0], o.color[1], o.color[2]);
    e.setObjectName(idx, (o.name || typeName) + " copy");
            e.commitSceneEdit();
        } catch (err) {
            e.cancelSceneEdit();
            throw err;
        }
    this.refresh();
    this.setState({ saved: false, ctx: null });
    this.log("scene", (o.name || typeName) + " duplicated");
  }
  addCollection() {
    // Collections need an object hierarchy the engine does not have yet.
    this.setState({ ctx: null });
    this.log("scene", "New Collection — planned (engine has no hierarchy yet)");
    this.status("Collections — planned");
  }
  commitRename() {
    const S = this.state;
    if (!S.renaming || S.renaming === "scene") {
      this.setState({ renaming: null });
      return;
    }
    const id = S.renaming;
    const name = S.renameText.trim();
    if (!name || !S.meta[id]) {
      this.setState({ renaming: null });
      return;
    }
    const o = this.objById(id);
    if (o) {
      // Primitive names are engine-owned (EditorObjectStore, keyed by objectId).
      this.engine?.setObjectName(o.index, name);
      this.setState({ renaming: null, saved: false });
      this.refresh();
    } else if (this.isGroupKey(id)) {
      // Group/operation node names are engine-owned too (keyed by CSG nodeId), so
      // they persist with the project; refresh() reads the value back from the tree.
      this.engine?.setNodeName(this.nodeIdForKey(id), name);
      this.setState({ renaming: null, saved: false });
      this.refresh();
    } else {
      this.setState((s: any) => ({ meta: { ...s.meta, [id]: { ...s.meta[id], name } }, renaming: null, saved: false }));
    }
  }
  // Guard destructive actions (New / Open) behind a discard-changes confirmation.
  // If the scene is clean, the action runs immediately. Otherwise we stash it and
  // open the modal; the button handler runs it (still inside the user gesture, so
  // Open's file picker is allowed).
  confirmDiscard(action: () => void, okLabel: string) {
    this.setState({ openMenu: null });
    if (this.state.saved) { action(); return; }
    this._pendingAction = action;
    this.setState({
      confirm: {
        title: "Discard unsaved changes?",
        body: "Your project has unsaved changes that will be lost.",
        okLabel,
      },
    });
  }
  cancelDiscard() {
    this._pendingAction = null;
    this.setState({ confirm: null });
  }
  proceedDiscard() {
    const action = this._pendingAction;
    this._pendingAction = null;
    this.setState({ confirm: null });
    action?.();
  }

  newScene() {
    this.confirmDiscard(() => this.doNewScene(), "Discard & new");
  }
  doNewScene() {
    this.engine?.newScene();
    this._fileHandle = null;
    this.setState({
      meta: {},
      counts: { sphere: 0, box: 0, cylinder: 0, torus: 0, collection: 0 },
      sel: "scene",
      saved: true,
      fileName: null,
      openMenu: null,
    });
    this.refresh(true);
    this.log("scene", "New scene");
    this.status("New scene");
  }
  openProject() {
    this.confirmDiscard(() => this.doOpenProject(), "Discard & open");
  }
  async doOpenProject() {
    this.setState({ openMenu: null });
    const e = this.engine;
    if (!e) return;
    if (!window.showOpenFilePicker) {
      this.log("error", "Open needs the File System Access API — use Chrome or Edge.");
      this.status("Open unavailable");
      return;
    }
    try {
      const [handle] = await window.showOpenFilePicker({
        types: [{ description: "SDF Project", accept: { "application/x-sdf-project": [".sdfp"] } }],
        excludeAcceptAllOption: true,
      });
      const file = await handle.getFile();
      const bytes = new Uint8Array(await file.arrayBuffer());
      e.loadProject(bytes);
      // The opened file becomes the current project, so later Saves overwrite it.
      this._fileHandle = handle;
      this.setState({
        meta: {},
        counts: { sphere: 0, box: 0, cylinder: 0, torus: 0, collection: 0 },
        sel: "scene",
        fileName: handle.name,
        saved: true,
      });
      this.refresh(true);
      this.log("scene", "Opened " + handle.name + " (" + this.countObjects() + " objects)");
      this.status("Opened " + handle.name);
    } catch (err: any) {
      if (err && err.name === "AbortError") return;  // user cancelled the picker
      this.log("error", "Open failed: " + String(err));
      this.status("Open failed");
    }
  }
  async saveProject() {
    this.setState({ openMenu: null });
    const e = this.engine;
    if (!e) return;
    if (!window.showSaveFilePicker) {
      this.log("error", "Save needs the File System Access API — use Chrome or Edge.");
      this.status("Save unavailable");
      return;
    }
    try {
      // First save (no remembered file): the OS dialog is the only place the
      // browser lets us name the file, so the .sdfp name is chosen there. Later
      // saves reuse the handle and overwrite it silently — no dialog.
      if (!this._fileHandle) {
        this._fileHandle = await window.showSaveFilePicker({
          suggestedName: "untitled.sdfp",
          types: [{ description: "SDF Project", accept: { "application/x-sdf-project": [".sdfp"] } }],
          excludeAcceptAllOption: true,
        });
        this.setState({ fileName: this._fileHandle.name });
      }
      const handle = this._fileHandle;
      const bytes = e.saveProject();
      const w = await handle.createWritable();
      await w.write(bytes as BufferSource);
      await w.close();
      this.setState({ saved: true });
      this.log("scene", "Saved " + handle.name + " (" + this.countObjects() + " objects)");
      this.status("Saved");
    } catch (err: any) {
      if (err && err.name === "AbortError") return;  // user cancelled the picker
      this.log("error", "Save failed: " + String(err));
      this.status("Save failed");
    }
  }
  csg(op: string) {
    const e = this.engine;
    if (!e) return;
    const codeMap: Record<string, CsgOp> = {
      Union: CsgOp.Union,
      Subtract: CsgOp.Subtract,
      Intersect: CsgOp.Intersect,
      Group: CsgOp.Group,
    };
    const code = codeMap[op];
    if (!code) {
      // Smooth variants go through csgSmooth(), not here; this guards unknown ops.
      this.status(op + " — not implemented yet");
      return;
    }
    // The engine operates on its own current selection; we only sanity-check the
    // count here for a friendly message.
    const count = (this.state.selSet || []).filter((k: string) => k !== "scene").length;
    if (count < 2) {
      this.status(op + " needs 2 or more selected objects");
      return;
    }
    let newNodeId = -1;
    try {
      newNodeId = e.applyOperation(code);
    } catch (err) {
      this.status(op + " failed — engine bridge missing");
      this.log("error", op + ": " + String(err));
      return;
    }
    // The engine selects the new group; refresh() mirrors that. Just expand it.
    this.refresh();
    if (newNodeId >= 0) {
      this.setState((s: any) => ({ saved: false, expanded: { ...s.expanded, ["g" + newNodeId]: true } }));
    }
    this.log("scene", op + " applied");
    this.status(op);
  }
  // Smooth variants: apply the hard boolean, then switch the new op node to Smooth
  // blend with a default radius — all inside one scene-edit so it's a single undo
  // step. The engine already owns smooth union/subtract/intersect (blendMode +
  // blendRadius); this is just a one-click path to them.
  csgSmooth(op: "Union" | "Subtract" | "Intersect") {
    const e = this.engine;
    if (!e) return;
    const count = (this.state.selSet || []).filter((k: string) => k !== "scene").length;
    if (count < 2) {
      this.setState({ smoothMenu: false });
      this.status("Smooth " + op + " needs 2 or more selected objects");
      return;
    }
    const code = { Union: CsgOp.Union, Subtract: CsgOp.Subtract, Intersect: CsgOp.Intersect }[op];
    let newNodeId = -1;
    try {
      // finally-commit keeps the scene-edit depth balanced even if a call throws.
      e.beginSceneEdit("Smooth " + op);
      try {
        newNodeId = e.applyOperation(code);
        if (newNodeId >= 0) {
          e.setBlendMode(newNodeId, 1);            // 1 = Smooth
          e.setBlendRadius(newNodeId, this.SMOOTH_DEFAULT_RADIUS);
        }
      } finally {
        e.commitSceneEdit();
      }
    } catch (err) {
      this.setState({ smoothMenu: false });
      this.status("Smooth " + op + " failed — engine bridge missing");
      this.log("error", "Smooth " + op + ": " + String(err));
      return;
    }
    this.refresh();
    if (newNodeId < 0) {
      this.setState({ smoothMenu: false });
      this.status("Smooth " + op + " needs 2 or more selected objects");
      return;
    }
    this.setState((s: any) => ({
      saved: false,
      smoothMenu: false,
      expanded: { ...s.expanded, ["g" + newNodeId]: true },
    }));
    this.log("scene", "Smooth " + op + " applied");
    this.status("Smooth " + op);
  }
  toggleGrid() {
    const grid = !this.state.grid;
    this.engine?.setShowGrid(grid);
    this.setState({ grid });
  }

    undo() {
        const e = this.engine;
        if (!e) return;

        try {
            e.undo();
            this.refresh();
            this.setState({ saved: false });
            this.status("Undo");
        } catch (err) {
            this.status("Undo failed");
            this.log("error", "Undo: " + String(err));
        }
    }

    redo() {
        const e = this.engine;
        if (!e) return;

        try {
            e.redo();
            this.refresh();
            this.setState({ saved: false });
            this.status("Redo");
        } catch (err) {
            this.status("Redo failed");
            this.log("error", "Redo: " + String(err));
        }
    }

    cancelGizmoDrag() {
        if (!this.engine) return;

        try {
            this._gizmoDrag = false;
            this.engine.gizmoCancelDrag();
            this.refresh();
            this.status("Transform cancelled");
        } catch (err) {
            this.status("Cancel transform failed");
            this.log("error", "Cancel transform: " + String(err));
        }
    }

  mkItem(label: string, key: string, fn: () => void, disabled?: boolean): any {
    return {
      label,
      key: key || "",
      fg: disabled ? "#5c5c5c" : "#d6d6d6",
      onClick: disabled
        ? () => {}
        : () => {
            fn();
            this.setState({ openMenu: null, ctx: null });
          },
    };
  }
  menu(id: string): any {
    const S = this.state;
    return {
      open: S.openMenu === id,
      bg: S.openMenu === id ? "#2e2e2e" : "transparent",
      mt: (e: any) => {
        e.stopPropagation();
        this.setState((s: any) => ({ openMenu: s.openMenu === id ? null : id, ctx: null, picker: null, dd: false, opDd: false }));
      },
      mh: () => {
        if (this.state.openMenu && this.state.openMenu !== id) this.setState({ openMenu: id });
      },
    };
  }

  renderVals(): any {
    const S = this.state;
    const P = this.props || {};
    const sep = { sep: true };
    const mi = this.mkItem.bind(this);

    const mF = this.menu("file"),
      mE = this.menu("edit"),
      mO = this.menu("object"),
      mV = this.menu("view"),
      mR = this.menu("render"),
      mH = this.menu("help");

    const selObj = this.selObj();
    const isObj = !!selObj;
    const isColl = S.sel === "scene";
    const selId = selObj ? String(selObj.id) : null;
    const d = selObj ? S.meta[selId!] : null;
    const selTypeName = selObj ? TYPE_NAME[selObj.type] || "box" : "";

    // A CSG group (operation node) is selected: build a compact panel model so the
    // group reads like a first-class object (its op + how many primitives it holds).
    const isGroupSel = this.isGroupKey(S.sel);
    let groupSel: any = null;
    if (isGroupSel && S.tree) {
      const gnid = this.nodeIdForKey(S.sel);
      const gnode = gnid >= 0 ? S.tree.nodes.find((n: any) => n.nodeId === gnid) : null;
      if (gnode) {
        const opName = gnode.type === 2 ? "Subtract" : gnode.type === 3 ? "Intersect" : gnode.type === 4 ? "Group" : "Union";
        const gm = S.meta[S.sel];
        groupSel = { nodeId: gnid, name: (gm && gm.name) || opName, op: opName, count: this.groupPrimitiveCount(gnid) };
      }
    }

    // The colour picker serves both a selected primitive (its meta.hsv) and a
    // selected group (a transient groupHsv, since a group stores no colour of its
    // own). activeHsv unifies the two so the picker + swatch reuse one code path.
    // For a group, show the objects' actual colour (groupColorHsv); groupHsv only
    // overrides it live during a picker drag, and resets on reselect so the control
    // always reflects the real object colour when you come back to it.
    const isGroupColor = (isGroupSel && !!groupSel) as boolean;
    const activeHsv = d ? d.hsv : (isGroupColor ? (S.groupHsv || this.groupColorHsv(groupSel.nodeId) || { h: 0, s: 0, v: 0.85 }) : null);
    const rgb = activeHsv ? this.hsv2rgb(activeHsv.h, activeHsv.s, activeHsv.v) : [128, 128, 128];
    const colorCss = "rgb(" + rgb.join(",") + ")";
    const colorHex = this.hex(rgb);

    // tree rows — the engine's CSG hierarchy rendered under the "Scene
    // Collection" root (which stands in for the tree's root operation node).
    // When a filter is active, or the engine has not yet emitted a tree, fall
    // back to a flat list of primitives.
    const rows: any[] = [];
    const filt = S.filter.trim().toLowerCase();
    const nameOf = (o: any) => S.meta[String(o.id)]?.name || o.typeName;
    const visibleObjs = S.objects.filter((o: any) => !filt || nameOf(o).toLowerCase().includes(filt));
    const tree = S.tree;
    const isRowSel = (key: string) => (S.selSet || []).includes(key);
    const objByOid: Record<number, any> = {};
    for (const o of S.objects) objByOid[o.id] = o;

    // Modifier-aware click / context handlers shared by every row.
    const rowSelect = (key: string) => (e: any) =>
      this.selectKey(key, !!(e && (e.ctrlKey || e.shiftKey || e.metaKey)));
    const rowCtx = (key: string) => (e: any) => {
      e.preventDefault();
      e.stopPropagation();
      if (!isRowSel(key)) this.selectKey(key, false);
      this.setState({ ctx: { x: e.clientX, y: e.clientY, kind: "node" }, openMenu: null, picker: null });
    };

    const pushPrimitive = (o: any, depth: number) => {
      const id = String(o.id);
      const m = S.meta[id] || {};
      const tn = TYPE_NAME[o.type] || "box";
      const selr = isRowSel(id);
      const hid = !!m.hidden;
      const lck = !!m.locked;
      rows.push({
        id,
        name: m.name || o.typeName,
        pad: 6 + depth * 14 + "px",
        hasKids: false,
        caretPath: "",
        iconPath: this.IC[tn] || this.IC.box,
        iconColor: selr ? "#f0a03c" : "#8f8f8f",
        bg: selr ? "#333333" : "transparent",
        hoverBg: selr ? "#383838" : "#282828",
        fg: hid ? "#5a5a5a" : selr ? "#f0a03c" : "#c9c9c9",
        renaming: S.renaming === id,
        notRenaming: S.renaming !== id,
        editName: S.renameText,
        onNameInput: (e: any) => this.setState({ renameText: e.target.value }),
        onNameKey: (e: any) => {
          if (e.key === "Enter") this.commitRename();
          else if (e.key === "Escape") this.setState({ renaming: null });
        },
        onNameBlur: () => this.commitRename(),
        eyePath: hid ? this.IC.eyeOff : this.IC.eye,
        eyeColor: hid ? "#5a5a5a" : "#7f7f7f",
        onEye: (e: any) => {
          e.stopPropagation();
          this.engine?.setObjectVisible(o.index, hid); // hid=true -> make visible
          this.refresh();
        },
        lockPath: lck ? this.IC.lock : this.IC.unlock,
        lockColor: lck ? "#e8912d" : "#4d4d4d",
        onLock: (e: any) => {
          e.stopPropagation();
          this.engine?.setObjectLocked(o.index, !lck);
          this.refresh();
        },
        onCaret: () => {},
        onSelect: rowSelect(id),
        onCtx: rowCtx(id),
        onRenameStart: () => this.setState({ renaming: id, renameText: m.name || o.typeName }),
      });
    };

    // Recurse a CSG node: primitives become leaf rows, operation nodes become
    // expandable group rows that nest their children.
    const renderNode = (nodeId: number, depth: number) => {
      const nd = tree.nodes.find((n: any) => n.nodeId === nodeId);
      if (!nd) return;
      if (nd.type === 0) {
        const o = objByOid[nd.objectId];
        if (o) pushPrimitive(o, depth);
        return;
      }
      const key = "g" + nd.nodeId;
      const m = S.meta[key] || {};
      const selr = isRowSel(key);
      const gexp = S.expanded[key] !== false; // default expanded
      rows.push({
        id: key,
        name: m.name || "Group",
        pad: 6 + depth * 14 + "px",
        hasKids: nd.children.length > 0,
        caretPath: gexp ? this.IC.caretD : this.IC.caretR,
        iconPath: this.IC.collection,
        iconColor: selr ? "#f0a03c" : "#8f8f8f",
        bg: selr ? "#333333" : "transparent",
        hoverBg: selr ? "#383838" : "#282828",
        fg: selr ? "#f0a03c" : "#c9c9c9",
        renaming: S.renaming === key,
        notRenaming: S.renaming !== key,
        editName: S.renameText,
        onNameInput: (e: any) => this.setState({ renameText: e.target.value }),
        onNameKey: (e: any) => {
          if (e.key === "Enter") this.commitRename();
          else if (e.key === "Escape") this.setState({ renaming: null });
        },
        onNameBlur: () => this.commitRename(),
        eyePath: this.IC.eye,
        eyeColor: "#7f7f7f",
        onEye: (e: any) => e.stopPropagation(),
        lockPath: this.IC.unlock,
        lockColor: "#4d4d4d",
        onLock: (e: any) => e.stopPropagation(),
        onCaret: (e: any) => {
          e.stopPropagation();
          this.setState((s: any) => ({ expanded: { ...s.expanded, [key]: s.expanded[key] === false } }));
        },
        onSelect: rowSelect(key),
        onCtx: rowCtx(key),
        onRenameStart: () => this.setState({ renaming: key, renameText: m.name || "Group" }),
      });
      if (gexp) for (const cid of nd.children) renderNode(cid, depth + 1);
    };

    const rootExpanded = filt ? true : !!S.expanded.scene;
    const rootSel = isRowSel("scene");
    rows.push({
      id: "scene",
      name: "Scene Collection",
      pad: "6px",
      hasKids: S.objects.length > 0,
      caretPath: rootExpanded ? this.IC.caretD : this.IC.caretR,
      iconPath: this.IC.collection,
      iconColor: rootSel ? "#f0a03c" : "#8f8f8f",
      bg: rootSel ? "#333333" : "transparent",
      hoverBg: rootSel ? "#383838" : "#282828",
      fg: rootSel ? "#f0a03c" : "#c9c9c9",
      renaming: false,
      notRenaming: true,
      editName: "",
      onNameInput: () => {},
      onNameKey: () => {},
      onNameBlur: () => {},
      eyePath: this.IC.eye,
      eyeColor: "#7f7f7f",
      onEye: (e: any) => e.stopPropagation(),
      lockPath: this.IC.unlock,
      lockColor: "#4d4d4d",
      onLock: (e: any) => e.stopPropagation(),
      onCaret: (e: any) => {
        e.stopPropagation();
        this.setState((s: any) => ({ expanded: { ...s.expanded, scene: !s.expanded.scene } }));
      },
      onSelect: () => this.select("scene"),
      onCtx: rowCtx("scene"),
      onRenameStart: () => {},
    });
    if (rootExpanded) {
      if (tree && !filt) {
        const rootNode = tree.nodes.find((n: any) => n.nodeId === tree.root);
        const kids = rootNode ? rootNode.children : [];
        for (const cid of kids) renderNode(cid, 1);
      } else {
        for (const o of visibleObjs) pushPrimitive(o, 1);
      }
    }

    // properties
    const axisColors = ["#d8504a", "#7fae3a", "#4b84d6"];
    const axisNames = ["X", "Y", "Z"];
    const trRow = (label: string, base: string, step: number, mask?: boolean[]) => ({
      label,
      axes: [0, 1, 2].map((i) => ({ axis: axisNames[i], color: axisColors[i], ...this.field(base + "." + i, step, mask ? !mask[i] : false) })),
    });
    const scaleMask = this.SCALE_AXES[selTypeName] || [true, true, true];
    const transformRows = isObj ? [trRow("Position", "pos", 0.02), trRow("Rotation", "rot", 0.5), trRow("Scale", "scl", 0.01, scaleMask)] : [];
    // `round` (box corner rounding) is stored but not yet applied to the SDF — greyed out.
    const paramRows = isObj ? (this.PRM_ROWS[selTypeName] || []).map(([label, k, step]: any) => ({ label, disabled: k === "round", ...this.field("prm." + k, step, k === "round") })) : [];

    // Changing type is done via delete + re-create (no in-place type binding).
    // Carry over the engine-owned attributes that still make sense; shape params
    // reset to the new type's defaults.
    const typeSet = (t: string) => () => {
      const e = this.engine;
      const o = this.selObj();
      if (!e || !o) {
        this.setState({ dd: false });
        return;
      }
      const pos = o.position;
      const col = o.color;
      const rot = o.rotation;
      const name = o.name;
            e.beginSceneEdit("Change Primitive Type");
            try {
      e.removeObject(o.index);
      const idx = e.addPrimitive(TYPE_ENUM[t]);
      e.setObjectPosition(idx, pos[0], pos[1], pos[2]);
      e.setObjectColor(idx, col[0], col[1], col[2]);
      e.setObjectRotation(idx, rot[0], rot[1], rot[2]);
      e.setObjectName(idx, name);
                e.commitSceneEdit();
            } catch (err) {
                e.cancelSceneEdit();
                throw err;
            }
      this.refresh();
      this.setState({ dd: false, saved: false });
    };
    // CSG and modifier values below are engine-owned; this view model only mirrors them.
    const selectedKey = String(S.sel ?? "scene");
    const selectedNode = selectedKey.startsWith("g")
      ? S.tree?.nodes?.find((n: any) => n.nodeId === Number(selectedKey.slice(1))) || null
      : S.tree?.nodes?.find((n: any) => n.type === 0 && n.objectId === Number(selectedKey)) || null;
    const operationNode = selectedNode && selectedNode.type >= 1 && selectedNode.type <= 3 ? selectedNode : null;
    const blendNode = operationNode?.blendMode === 1 ? operationNode : null;
    const selectedPrimitive = selectedNode?.type === 0 ? S.objects.find((o: any) => o.id === selectedNode.objectId) : null;
    const roundnessNode = selectedNode?.type === 0 && selectedNode.modifierType === 1 ? selectedNode : null;
    const roundnessSupported = selectedPrimitive?.type === PrimitiveType.Box;
    const refreshEdit = (fn: () => void) => { fn(); this.setState({ saved: false }); this.refresh(); };
    const beginProperty = (name: string) => this.engine?.beginSceneEdit(name);
    const commitProperty = () => { this.engine?.commitSceneEdit(); this.refresh(); };
    const modifierOptions: any[] = [
      { label: "Blend", disabled: !operationNode || !!blendNode,
        reason: !operationNode ? "Select a CSG operation" : "Blend is already added",
        onClick: () => operationNode && !blendNode && refreshEdit(() => this.engine!.setBlendMode(operationNode.nodeId, 1)) },
      { label: "Roundness", disabled: !roundnessSupported || !!roundnessNode,
        reason: !roundnessSupported ? "Select a supported primitive" : "Roundness is already added",
        onClick: () => selectedNode && roundnessSupported && !roundnessNode && refreshEdit(() => this.engine!.addRoundnessModifier(selectedNode.nodeId)) },
    ];
    const roundnessSize = selectedPrimitive?.size || [];
    const roundnessMax = roundnessSupported ? Math.max(0, Math.min(Number(roundnessSize[0]) || 0, Number(roundnessSize[1]) || 0, Number(roundnessSize[2]) || 0) - 0.00001) : 1;
    const roundnessCfg = { key: "roundness", label: "Amount", min: 0, max: roundnessMax, step: roundnessMax > 0 ? Math.max(roundnessMax / 100, 0.0001) : 0.0001, help: "Rounds Box corners inward while preserving the original outer bounds." };

    // dock / tabs
    const dockTab = S.dockTab || P.dockTab || "timeline";
    const propsTab = S.propsTab || P.propsTab || "object";
    const tabSt = (on: boolean) => ({ bg: on ? "#1f1f1f" : "transparent", fg: on ? "#dedede" : "#8a8a8a", line: on ? "#e8912d" : "transparent" });
    const toolSt = (on: boolean) => ({ bg: on ? "#e8912d" : "transparent", fg: on ? "#151515" : "#a8a8a8" });
    const togSt = (on: boolean) => ({ bg: on ? "#383838" : "transparent", fg: on ? "#e8912d" : "#a8a8a8" });

    const tagColors: any = { engine: "#5b8dbe", shader: "#e8912d", scene: "#6fa85f", warn: "#d9a93a", error: "#c4433b" };

    const tickFrames = [1, 25, 50, 75, 100, 125, 150, 175, 200, 225, 250];
    const setFrameFrom = (e: any) => {
      const r = e.currentTarget.getBoundingClientRect();
      const fr = Math.round(((e.clientX - r.left) / r.width) * 249) + 1;
      this.setState({ frame: Math.max(1, Math.min(250, fr)) });
    };

    const hints: any = {
      select: "LMB Select · MMB Orbit · Shift+MMB Pan · Wheel Zoom",
      move: "Drag gizmo Move · X/Y/Z Constrain · Esc Cancel",
      rotate: "Drag Rotate · X/Y/Z Constrain · Esc Cancel",
      scale: "Drag Scale · X/Y/Z Constrain · Esc Cancel",
    };

    // Push an HSV value to the engine colour of whatever is selected: a primitive
    // (its meta.hsv + setObjectColor) or a group (transient groupHsv + setGroupColor
    // fanned over the subtree). Either way the control reflects it live via setState.
    const applyHsv = (hsv: any) => {
      const c = this.hsv2rgb(hsv.h, hsv.s, hsv.v);
      if (selObj && selId) {
        this.engine?.setObjectColor(selObj.index, c[0] / 255, c[1] / 255, c[2] / 255);
        this.setState((s: any) => ({ hexEdit: null, saved: false, meta: { ...s.meta, [selId!]: { ...s.meta[selId!], hsv } } }));
      } else if (isGroupColor) {
        this.engine?.setGroupColor(groupSel.nodeId, c[0] / 255, c[1] / 255, c[2] / 255);
        this.setState({ hexEdit: null, saved: false, groupHsv: hsv });
      }
    };
    const svSet = (e: any) => {
      if (!activeHsv) return;
      const r = e.currentTarget.getBoundingClientRect();
      const sN = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
      const vN = 1 - Math.max(0, Math.min(1, (e.clientY - r.top) / r.height));
      applyHsv({ ...activeHsv, s: sN, v: vN });
    };
    const hueSet = (e: any) => {
      if (!activeHsv) return;
      const r = e.currentTarget.getBoundingClientRect();
      const h = Math.max(0, Math.min(359.9, ((e.clientX - r.left) / r.width) * 360));
      applyHsv({ ...activeHsv, h });
    };
    const commitHex = () => {
      const t = (this.state.hexEdit || "").trim().replace(/^#/, "");
      if (/^[0-9a-f]{6}$/i.test(t) && activeHsv) {
                this.engine?.beginSceneEdit("Change Color");
        applyHsv(this.rgb2hsv(parseInt(t.slice(0, 2), 16), parseInt(t.slice(2, 4), 16), parseInt(t.slice(4, 6), 16)));
                this.engine?.commitSceneEdit();
        this.refresh();
      } else this.setState({ hexEdit: null });
    };

    const ctxItems = !S.ctx
      ? []
      : S.ctx.kind === "vp"
      ? [
          mi("Add Sphere", "", () => this.addPrimitive("sphere")),
          mi("Add Box", "", () => this.addPrimitive("box")),
          mi("Add Cylinder", "", () => this.addPrimitive("cylinder")),
          mi("Add Torus", "", () => this.addPrimitive("torus")),
          mi("Add Link", "", () => this.addPrimitive("link")),
          mi("Add Cone", "", () => this.addPrimitive("cone")),
          mi("Add Capsule", "", () => this.addPrimitive("capsule")),
          mi("Add Solid Angle", "", () => this.addPrimitive("solidAngle")),
          mi("Add Pyramid", "", () => this.addPrimitive("pyramid")),
          sep,
          mi("Frame Selected", ".", () => this.status("Frame Selected — camera op (engine)")),
          mi("Frame All", "Home", () => this.status("Frame All — camera op (engine)")),
          mi("Reset Camera", "", () => this.status("Camera reset")),
          sep,
          mi(S.grid ? "✓ Grid" : "Grid", "", () => this.toggleGrid()),
        ]
      : [
          mi("Rename", "F2", () => {
            // Works for a primitive or a group/operation node — both are renamable.
            const k = S.sel;
            if (k && k !== "scene") {
              const nm = S.meta[k]?.name || this.objById(k)?.typeName || "Group";
              this.setState({ renaming: k, renameText: nm });
            }
          }, !(S.sel && S.sel !== "scene")),
          mi("Duplicate", "⇧D", () => this.duplicateSel(), !isObj),
          sep,
          mi("New Collection", "", () => this.addCollection()),
          sep,
          mi("Delete", "X", () => this.deleteSel(), !isObj),
        ];

    return {
      // menubar
      mFile: { bg: mF.bg }, fileOpen: mF.open, mtFile: mF.mt, mhFile: mF.mh,
      fileItems: [
        mi("New", "Ctrl N", () => this.newScene()),
        mi("Open…", "Ctrl O", () => this.openProject()),
        mi("Save", "Ctrl S", () => this.saveProject()),
        sep,
        mi("Import .sdfo", "", () => {}, true),
        mi("Export", "", () => {}, true),
      ],
      mEdit: { bg: mE.bg }, editOpen: mE.open, mtEdit: mE.mt, mhEdit: mE.mh,
      editItems: [
                mi("Undo", "Ctrl Z", () => this.undo(), !this.engine),
                mi("Redo", "Ctrl ⇧ Z", () => this.redo(), !this.engine),
        sep,
        mi("Preferences…", "", () => {}, true),
      ],
      mObj: { bg: mO.bg }, objOpen: mO.open, mtObj: mO.mt, mhObj: mO.mh,
      objItems: [
        mi("Add Sphere", "", () => this.addPrimitive("sphere")),
        mi("Add Box", "", () => this.addPrimitive("box")),
        mi("Add Cylinder", "", () => this.addPrimitive("cylinder")),
        mi("Add Torus", "", () => this.addPrimitive("torus")),
        mi("Add Link", "", () => this.addPrimitive("link")),
        mi("Add Cone", "", () => this.addPrimitive("cone")),
        mi("Add Capsule", "", () => this.addPrimitive("capsule")),
        mi("Add Solid Angle", "", () => this.addPrimitive("solidAngle")),
        mi("Add Pyramid", "", () => this.addPrimitive("pyramid")),
        sep,
        mi("Duplicate", "⇧D", () => this.duplicateSel(), !isObj),
        mi("Delete", "X", () => this.deleteSel(), !isObj),
      ],
      mView: { bg: mV.bg }, viewOpen: mV.open, mtView: mV.mt, mhView: mV.mh,
      viewItems: [
        mi(S.grid ? "✓ Grid" : "Grid", "", () => this.toggleGrid()),
        mi("Frame Selected", ".", () => this.status("Frame Selected — camera op (engine)")),
        mi("Frame All", "Home", () => this.status("Frame All — camera op (engine)")),
        sep,
        mi("Quad View", "", () => {}, true),
        mi("Fullscreen Viewport", "", () => {}, true),
      ],
      mRender: { bg: mR.bg }, renderOpen: mR.open, mtRender: mR.mt, mhRender: mR.mh,
      renderItems: [
        mi("Render Image", "F12", () => {}, true),
        mi("Render Animation", "", () => {}, true),
        sep,
        mi("Render Settings…", "", () => this.setState({ propsTab: "render" })),
      ],
      mHelp: { bg: mH.bg }, helpOpen: mH.open, mtHelp: mH.mt, mhHelp: mH.mh,
      helpItems: [
        mi("Keyboard Shortcuts", "F1", () => this.setState({ overlay: true })),
        mi("Manual", "", () => {}, true),
        sep,
        mi("About SDF Editor", "", () => this.status("SDF Editor v0.1.0 — WebGPU raymarched CSG editor")),
      ],
      unsaved: !S.saved,
      fileName: S.fileName || "untitled.sdfp",

      // left toolbar
      addSphere: () => this.addPrimitive("sphere"),
      addBox: () => this.addPrimitive("box"),
      addCyl: () => this.addPrimitive("cylinder"),
      addTor: () => this.addPrimitive("torus"),
      addLink: () => this.addPrimitive("link"),
      addCone: () => this.addPrimitive("cone"),
      addCaps: () => this.addPrimitive("capsule"),
      addSolid: () => this.addPrimitive("solidAngle"),
      addPyr: () => this.addPrimitive("pyramid"),
      csgGroup: () => this.csg("Group"),
      csgUnion: () => this.csg("Union"),
      csgSub: () => this.csg("Subtract"),
      csgInt: () => this.csg("Intersect"),
      smoothMenu: S.smoothMenu,
      csgSmoothToggle: () => this.setState((s: any) => ({ smoothMenu: !s.smoothMenu, dd: false, opDd: false, modifierMenu: false, openMenu: null })),
      csgSmoothUnion: () => this.csgSmooth("Union"),
      csgSmoothSub: () => this.csgSmooth("Subtract"),
      csgSmoothInt: () => this.csgSmooth("Intersect"),

      // viewport
      vpCtx: (e: any) => {
        e.preventDefault();
        this.setState({ ctx: { x: e.clientX, y: e.clientY, kind: "vp" }, openMenu: null, picker: null });
      },
      tSel: toolSt(S.tool === "select"), setSel: () => this.setState({ tool: "select" }),
      tMove: toolSt(S.tool === "move"), setMove: () => this.setState({ tool: "move" }),
      tRot: toolSt(S.tool === "rotate"), setRot: () => this.setState({ tool: "rotate" }),
      tScl: toolSt(S.tool === "scale"), setScl: () => this.setState({ tool: "scale" }),

      // Gizmo space toggle: only meaningful while a gizmo tool is active.
      gzLabel: S.gizmoSpace === "local" ? "Local" : "World",
      gzSt: toolSt(S.gizmoSpace === "local"),
      gzEnabled: S.tool === "move" || S.tool === "rotate",
      gzToggle: () => this.setState({ gizmoSpace: S.gizmoSpace === "local" ? "world" : "local" }),
      shWire: togSt(S.shading === "wire"), setWire: () => this.setState({ shading: "wire" }),
      shSolid: togSt(S.shading === "solid"), setSolid: () => this.setState({ shading: "solid" }),
      shRend: togSt(S.shading === "rendered"), setRend: () => this.setState({ shading: "rendered" }),
      tgGrid: togSt(S.grid), onGrid: () => this.toggleGrid(),
      tgSnap: togSt(S.snap), onSnap: () => this.setState((s: any) => ({ snap: !s.snap })),
      showGizmo: P.showAxisGizmo !== false,

      // splitters + sizes
      spRight: this.split("rightW", "x", -1, 240, 480),
      spDock: this.split("dockH", "y", -1, 110, 420),
      spOutl: this.split("outlH", "y", 1, 120, 560),
      rightW: S.rightW + "px", dockH: S.dockH + "px", outlH: S.outlH + "px",

      // bottom dock
      dtT: tabSt(dockTab === "timeline"), onDockT: () => this.setState({ dockTab: "timeline" }),
      dtC: tabSt(dockTab === "console"), onDockC: () => this.setState({ dockTab: "console" }),
      dtK: tabSt(dockTab === "uikit"), onDockK: () => this.setState({ dockTab: "uikit" }),
      onDockPlus: () => this.status("Planned panels: Node Editor · Shader Graph · Render Queue"),
      isTimeline: dockTab === "timeline", isConsole: dockTab === "console", isKit: dockTab === "uikit",
      onSkipS: () => this.setState({ frame: 1 }),
      onSkipE: () => this.setState({ frame: 250 }),
      onPlay: () => {
        if (this.state.playing) {
          clearInterval(this._pt);
          this._pt = null;
          this.setState({ playing: false });
        } else {
          this._pt = setInterval(() => this.setState((s: any) => ({ frame: s.frame >= 250 ? 1 : s.frame + 1 })), 42);
          this.setState({ playing: true });
        }
      },
      playPath: S.playing ? this.IC.pause : this.IC.play,
      frameText: S.frame + " / 250",
      ticks: tickFrames.map((fr) => ({ left: ((fr - 1) / 249) * 100 + "%", label: String(fr) })),
      playheadLeft: ((S.frame - 1) / 249) * 100 + "%",
      ruler: {
        down: (e: any) => {
          e.preventDefault();
          try {
            e.currentTarget.setPointerCapture(e.pointerId);
          } catch (_) {}
          this._rl = true;
          setFrameFrom(e);
        },
        move: (e: any) => {
          if (this._rl) setFrameFrom(e);
        },
        up: (e: any) => {
          try {
            e.currentTarget.releasePointerCapture(e.pointerId);
          } catch (_) {}
          this._rl = false;
        },
      },
      logs: S.logs.map((l: any) => ({ ...l, tag: "[" + l.tag + "]", tagColor: tagColors[l.tag] || "#8a8a8a" })),
      demoField: this.field("demo", 0.01),
      demoSlider: { pct: S.demoSlider * 100 + "%", text: S.demoSlider.toFixed(2), ...this.sliderH("demoSlider") },
      demoCheckOn: S.demoCheck,
      demoCheckBg: S.demoCheck ? "#e8912d" : "#151515",
      demoCheckBorder: S.demoCheck ? "#e8912d" : "#363636",
      onDemoCheck: () => this.setState((s: any) => ({ demoCheck: !s.demoCheck })),

      // outliner
      filterVal: S.filter,
      onFilter: (e: any) => this.setState({ filter: e.target.value }),
      treeRows: rows,

      // properties
      ptObj: tabSt(propsTab === "object"), onPtObj: () => this.setState({ propsTab: "object" }),
      ptMat: tabSt(propsTab === "material"), onPtMat: () => this.setState({ propsTab: "material" }),
      ptMod: tabSt(propsTab === "modifiers"), onPtMod: () => this.setState({ propsTab: "modifiers" }),
      ptRen: tabSt(propsTab === "render"), onPtRen: () => this.setState({ propsTab: "render" }),
      isObjTab: propsTab === "object", isMatTab: propsTab === "material", isModTab: propsTab === "modifiers", isRenTab: propsTab === "render",
      hasObj: isObj, isCollSel: isColl, isGroupSel: isGroupSel && !!groupSel, groupSel,
      noSel: S.sel == null || (isGroupSel && !groupSel),
      // Group colour uses the shared picker (applyHsv routes to setGroupColor).
      // Group scale is a session-relative slider: the whole drag is one scene-edit,
      // and each tick applies the ratio to the previous value so the engine scales
      // incrementally about the group centre. The slider reflects the value live.
      groupScaleValue: S.groupScaleUI,
      groupScaleCfg: { key: "groupscale", label: "Scale", min: 0.1, max: 3, step: 0.01, help: "Uniformly resizes every object in the group about the group centre. Relative to the group's size when selected (1 = unchanged); reselecting resets to 1." },
      groupScaleBegin: () => { this._groupScaleLast = this.state.groupScaleUI; this.engine?.beginSceneEdit("Scale Group"); },
      groupScaleChange: (v: number) => {
        const nid = this.nodeIdForKey(S.sel);
        const last = this._groupScaleLast || 1;
        if (nid >= 0 && v > 0 && isFinite(v) && Math.abs(v - last) > 1e-6) {
          this.engine?.scaleGroup(nid, v / last);
          this._groupScaleLast = v;
        }
        this.setState({ groupScaleUI: v, saved: false });
      },
      groupScaleCommit: () => { this.engine?.commitSceneEdit(); this.refresh(); },
      objIconPath: isObj ? this.IC[selTypeName] : "",
      objName: isObj ? (d?.name || selObj.typeName) : "",
      objTypeChip: isObj ? "SDF " + this.cap(selTypeName) : "",
      secTCaret: S.secT ? this.IC.caretD : this.IC.caretR, secTOpen: S.secT, onSecT: () => this.setState((s: any) => ({ secT: !s.secT })),
      secSCaret: S.secS ? this.IC.caretD : this.IC.caretR, secSOpen: S.secS, onSecS: () => this.setState((s: any) => ({ secS: !s.secS })),
      secCCaret: S.secC ? this.IC.caretD : this.IC.caretR, secCOpen: S.secC, onSecC: () => this.setState((s: any) => ({ secC: !s.secC })),
      transformRows, paramRows,
      typeName: isObj ? this.cap(selTypeName) : "",
      ddToggle: (e: any) => {
        e.stopPropagation();
        this.setState((s: any) => ({ dd: !s.dd, opDd: false, openMenu: null }));
      },
      ddOpen: S.dd,
      typeOptions: ["sphere", "box", "cylinder", "torus", "link", "cone", "capsule", "solidAngle", "pyramid"].map((t) => ({ label: this.cap(t), fg: isObj && selTypeName === t ? "#e8912d" : "#d6d6d6", onClick: typeSet(t) })),
      operationNode,
      blendNode,
      roundnessNode,
      hasModifierTarget: !!selectedNode,
      modifierOptions,
      modifierMenu: S.modifierMenu,
       toggleModifierMenu: () => this.setState((s: any) => ({ modifierMenu: !s.modifierMenu })),
      closeModifierMenu: () => this.setState({ modifierMenu: false }),
      blendOpen: S.blendOpen,
      roundnessOpen: S.roundnessOpen,
      toggleBlendOpen: () => this.setState((s: any) => ({ blendOpen: !s.blendOpen })),
      toggleRoundnessOpen: () => this.setState((s: any) => ({ roundnessOpen: !s.roundnessOpen })),
      blendCfg: { key: "blend", label: "Amount", min: 0, max: 2, step: 0.01, help: "Smooths the selected CSG operation without changing its operation type or child order." },
       roundnessCfg,
      modifierHelp: S.modifierHelp,
      toggleModifierHelp: (key: string) => this.setState((s: any) => ({ modifierHelp: { ...s.modifierHelp, [key]: !s.modifierHelp[key] } })),
      beginProperty,
      commitProperty,
      setBlendAmount: (value: number) => blendNode && refreshEdit(() => this.engine!.setBlendRadius(blendNode.nodeId, Math.max(0, value))),
      removeBlend: () => blendNode && refreshEdit(() => this.engine!.setBlendMode(blendNode.nodeId, 0)),
      toggleRoundness: () => roundnessNode && refreshEdit(() => this.engine!.setRoundnessEnabled(roundnessNode.nodeId, !roundnessNode.modifierEnabled)),
      removeRoundness: () => roundnessNode && refreshEdit(() => this.engine!.removeRoundnessModifier(roundnessNode.nodeId)),
       setRoundnessAmount: (value: number) => roundnessNode && refreshEdit(() => this.engine!.setRoundnessAmount(roundnessNode.nodeId, Math.max(0, Math.min(roundnessCfg.max, value)))),
      colorCss, colorHex,
      onPickerOpen: (e: any) => {
        const r = e.currentTarget.getBoundingClientRect();
        this.setState({ picker: { x: Math.min(r.left, window.innerWidth - 220), y: Math.min(r.bottom + 6, window.innerHeight - 230) }, hexEdit: null, openMenu: null, ctx: null });
      },
      collName: isColl ? "Scene Collection" : "",
      collCount: isColl ? this.countObjects() + " objects" : "",

      // status bar
      showHints: P.statusHints !== false,
      hintText: hints[S.tool],
      statusMsg: S.statusMsg,
      objCountText: this.countObjects() + " objects",

      // popups
      anyPopup: !!(S.openMenu || S.ctx || S.picker || S.dd || S.opDd || S.modifierMenu || S.smoothMenu),
      closeAll: () => this.setState({ openMenu: null, ctx: null, picker: null, dd: false, opDd: false, modifierMenu: false, smoothMenu: false }),
      ctxOpen: !!S.ctx,
      ctxX: S.ctx ? Math.min(S.ctx.x, window.innerWidth - 200) + "px" : "0px",
      ctxY: S.ctx ? Math.min(S.ctx.y, window.innerHeight - 240) + "px" : "0px",
      ctxItems,
      pickerOpen: !!S.picker,
      pickerX: S.picker ? S.picker.x + "px" : "0px",
      pickerY: S.picker ? S.picker.y + "px" : "0px",
      svBg: activeHsv ? "linear-gradient(to top, #000000, rgba(0,0,0,0)), linear-gradient(to right, #ffffff, hsl(" + activeHsv.h + ", 100%, 50%))" : "#000",
      svX: activeHsv ? activeHsv.s * 100 + "%" : "0%",
      svY: activeHsv ? (1 - activeHsv.v) * 100 + "%" : "0%",
      sv: {
        down: (e: any) => {
          e.preventDefault();
          try {
            e.currentTarget.setPointerCapture(e.pointerId);
          } catch (_) {}
                    this.engine?.beginSceneEdit("Change Color");
          this._sv = true;
          svSet(e);
        },
        move: (e: any) => {
          if (this._sv) svSet(e);
        },
        up: (e: any) => {
          try {
            e.currentTarget.releasePointerCapture(e.pointerId);
          } catch (_) {}
          this._sv = false;
                    this.engine?.commitSceneEdit();
          if (isGroupColor) this.refresh();
        },
      },
      hueX: activeHsv ? (activeHsv.h / 360) * 100 + "%" : "0%",
      hue: {
        down: (e: any) => {
          e.preventDefault();
          try {
            e.currentTarget.setPointerCapture(e.pointerId);
          } catch (_) {}
                    this.engine?.beginSceneEdit("Change Color");
          this._hu = true;
          hueSet(e);
        },
        move: (e: any) => {
          if (this._hu) hueSet(e);
        },
        up: (e: any) => {
          try {
            e.currentTarget.releasePointerCapture(e.pointerId);
          } catch (_) {}
          this._hu = false;
                    this.engine?.commitSceneEdit();
          if (isGroupColor) this.refresh();
        },
      },
      hexShown: S.hexEdit != null ? S.hexEdit : colorHex,
      onHexInput: (e: any) => this.setState({ hexEdit: e.target.value }),
      onHexKey: (e: any) => {
        if (e.key === "Enter") commitHex();
        else if (e.key === "Escape") this.setState({ hexEdit: null });
      },
      onHexBlur: () => {
        if (this.state.hexEdit != null) commitHex();
      },

      overlayOpen: S.overlay,
      closeOverlay: () => this.setState({ overlay: false }),
    };
  }

  // ---- JSX render (translation of the mockup template) ---------------------

  menuDropdown(items: any[]) {
    return (
      <div style={css("position:absolute;top:100%;left:0;min-width:210px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:4px 0;z-index:400")}>
        {items.map((it, i) =>
          it.sep ? (
            <div key={i} style={css("height:1px;background:#333333;margin:4px 8px")} />
          ) : (
            <Hv
              key={i}
              onClick={it.onClick}
              s={`display:flex;align-items:center;gap:24px;padding:4px 12px;font-size:12px;color:${it.fg};cursor:default`}
              h="background:#2e2e2e"
            >
              <span>{it.label}</span>
              <span style={css("margin-left:auto;color:#616161;font-family:'IBM Plex Mono',monospace;font-size:10px")}>{it.key}</span>
            </Hv>
          )
        )}
      </div>
    );
  }

  topMenu(label: string, bg: string, open: boolean, mt: any, mh: any, items: any[]) {
    return (
      <div style={css("position:relative")}>
        <Hv onClick={mt} onPointerEnter={mh} s={`padding:3px 10px;border-radius:2px;cursor:default;background:${bg}`} h="background:#2e2e2e">
          {label}
        </Hv>
        {open && this.menuDropdown(items)}
      </div>
    );
  }

  // Text pill for the gizmo space toggle. Dimmed (and inert) unless a gizmo tool
  // is active, since the world/local distinction means nothing without handles.
  spaceButton(label: string, onClick: any, tint: any, enabled: boolean) {
    return (
      <Hv
        tag="button"
        title="Gizmo space: world axes vs the object's own axes"
        onClick={enabled ? onClick : undefined}
        s={`height:22px;padding:0 7px;display:flex;align-items:center;border:none;border-radius:2px;font-size:10px;letter-spacing:0.3px;padding-top:1px;cursor:${enabled ? "pointer" : "default"};opacity:${enabled ? 1 : 0.35};background:${tint.bg};color:${tint.fg}`}
        h={enabled ? "background:#2e2e2e" : ""}
      >
        {label}
      </Hv>
    );
  }

  toolButton(title: string, onClick: any, tint: any, svg: string) {
    return (
      <Hv
        tag="button"
        title={title}
        onClick={onClick}
        s={`width:24px;height:22px;display:flex;align-items:center;justify-content:center;border:none;border-radius:2px;cursor:pointer;padding:0;background:${tint.bg};color:${tint.fg}`}
        h="background:#2e2e2e"
      >
        <Ico d={svg} />
      </Hv>
    );
  }

  render() {
    const V = this.renderVals();
    return (
      <div
        data-screen-label="Editor"
        style={css("display:flex;flex-direction:column;height:100vh;background:#161616;color:#dedede;font-family:'IBM Plex Sans',system-ui,sans-serif;font-size:12px;user-select:none;overflow:hidden;color-scheme:dark")}
      >
        {/* ============ MENU BAR ============ */}
        <header style={css("height:30px;flex:0 0 auto;display:flex;align-items:center;background:#1c1c1c;border-bottom:1px solid #0c0c0c;padding:0 8px;gap:1px")}>
          <Ico d={`<svg width="16" height="16" viewBox="0 0 16 16" style="margin:0 6px 0 2px;flex:0 0 auto"><path d="M9.75 1.75 14.25 6.25 6.75 13.75H2.25V9.25l7.5-7.5z M6 7.5l2.5 2.5" fill="none" stroke="#e8912d" stroke-width="1.2" stroke-linejoin="round" stroke-linecap="round"></path></svg>`} />
          <span style={css("font-weight:600;font-size:12px;letter-spacing:0.01em;margin-right:10px")}>SDF Editor</span>

          {this.topMenu("File", V.mFile.bg, V.fileOpen, V.mtFile, V.mhFile, V.fileItems)}
          {this.topMenu("Edit", V.mEdit.bg, V.editOpen, V.mtEdit, V.mhEdit, V.editItems)}
          {this.topMenu("Object", V.mObj.bg, V.objOpen, V.mtObj, V.mhObj, V.objItems)}
          {this.topMenu("View", V.mView.bg, V.viewOpen, V.mtView, V.mhView, V.viewItems)}
          {this.topMenu("Render", V.mRender.bg, V.renderOpen, V.mtRender, V.mhRender, V.renderItems)}
          {this.topMenu("Help", V.mHelp.bg, V.helpOpen, V.mtHelp, V.mhHelp, V.helpItems)}

          <div style={css("flex:1")} />
          <div style={css("display:flex;align-items:center;gap:6px;padding-right:4px")}>
            {V.unsaved && <span style={css("width:6px;height:6px;border-radius:50%;background:#e8912d;display:block")} title="Unsaved changes" />}
            <span style={css("font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>{V.fileName}</span>
          </div>
        </header>

        {/* ============ MAIN ROW ============ */}
        <div style={css("flex:1 1 auto;display:flex;min-height:0")}>
          {/* Left toolbar */}
          <div style={css("flex:0 0 34px;background:#1f1f1f;border-right:1px solid #0c0c0c;display:flex;flex-direction:column;align-items:center;padding-top:6px;gap:2px")}>
            {[
              ["Add Sphere", V.addSphere, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Box", V.addBox, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 1.75 14 4.9v6.2L8 14.25 2 11.1V4.9L8 1.75z M2 4.9l6 3.1 6-3.1 M8 8v6.25" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Cylinder", V.addCyl, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M3.75 4.2C3.75 3 5.65 2 8 2s4.25 1 4.25 2.2v7.6C12.25 13 10.35 14 8 14s-4.25-1-4.25-2.2V4.2z M3.75 4.2C3.75 5.4 5.65 6.4 8 6.4s4.25-1 4.25-2.2" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Torus", V.addTor, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 4.25c3.7 0 6.75 1.7 6.75 3.75S11.7 11.75 8 11.75 1.25 10.05 1.25 8 4.3 4.25 8 4.25z M5.75 8c0-.7 1-1.25 2.25-1.25S10.25 7.3 10.25 8 9.25 9.25 8 9.25 5.75 8.7 5.75 8z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Link", V.addLink, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 5a2 2 0 1 0 0 4 2 2 0 0 0 0-4z M10 7a2 2 0 1 0 0 4 2 2 0 0 0 0-4z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Cone", V.addCone, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 2 14 13H2L8 2z M2 13c0 .8 2.7 1.5 6 1.5s6-.7 6-1.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Capsule", V.addCaps, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 2.25c-1.8 0-3.25 1.45-3.25 3.25v5c0 1.8 1.45 3.25 3.25 3.25s3.25-1.45 3.25-3.25v-5C11.25 3.7 9.8 2.25 8 2.25z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Solid Angle", V.addSolid, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 8 8 2A6 6 0 0 1 13.2 5z M8 8 2.8 5A6 6 0 0 1 8 2" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
              ["Add Pyramid", V.addPyr, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M8 2 14.5 12.5H1.5L8 2z M1.5 12.5 8 9.5l6.5 3 M8 2v7.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`],
            ].map(([title, onClick, svg]: any, i) => (
              <Hv
                key={i}
                tag="button"
                title={title}
                onClick={onClick}
                s="width:26px;height:26px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;border-radius:2px;color:#a8a8a8;cursor:pointer;padding:0"
                h="background:#2e2e2e;color:#dedede"
              >
                <Ico d={svg} />
              </Hv>
            ))}
            <div style={css("width:18px;height:1px;background:#333333;margin:4px 0")} />
            {[
              ["Group Selection", V.csgGroup, `<svg width="16" height="16" viewBox="0 0 16 16"><rect x="2" y="2.75" width="12" height="10.5" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.1" stroke-dasharray="2 1.6"></rect><circle cx="6.2" cy="8" r="1.9" fill="none" stroke="currentColor" stroke-width="1.1"></circle><circle cx="9.8" cy="8" r="1.9" fill="none" stroke="currentColor" stroke-width="1.1"></circle></svg>`],
              ["Boolean Union", V.csgUnion, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
              ["Boolean Subtract", V.csgSub, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path><path d="M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 2"></path></svg>`],
              ["Boolean Intersect", V.csgInt, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 2" opacity="0.55"></path><path d="M8 4.83A3.75 3.75 0 0 1 8 11.17A3.75 3.75 0 0 1 8 4.83z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
            ].map(([title, onClick, svg]: any, i) => (
              <Hv
                key={i}
                tag="button"
                title={title}
                onClick={onClick}
                s="width:26px;height:26px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;border-radius:2px;color:#a8a8a8;cursor:pointer;padding:0"
                h="background:#2e2e2e;color:#dedede"
              >
                <Ico d={svg} />
              </Hv>
            ))}
            {/* Smooth Boolean — opens a flyout of the smooth variants; the engine
                owns them via blendMode/blendRadius (see csgSmooth). */}
            <div style={css("position:relative")}>
              <Hv
                tag="button"
                title="Smooth Boolean"
                onClick={V.csgSmoothToggle}
                s={`width:26px;height:26px;display:flex;align-items:center;justify-content:center;background:${V.smoothMenu ? "#2e2e2e" : "transparent"};border:none;border-radius:2px;color:${V.smoothMenu ? "#e8912d" : "#a8a8a8"};cursor:pointer;padding:0`}
                h="background:#2e2e2e;color:#dedede"
              >
                <Ico d={`<svg width="16" height="16" viewBox="0 0 16 16"><path d="M5.5 4.5a3.5 3.5 0 0 0 0 7c.9 0 1.8-.3 2.5-.9.7.6 1.6.9 2.5.9a3.5 3.5 0 0 0 0-7c-.9 0-1.8.3-2.5.9C7.3 4.8 6.4 4.5 5.5 4.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"></path></svg>`} />
              </Hv>
              {V.smoothMenu && (
                <div style={css("position:absolute;left:calc(100% + 8px);top:0;min-width:152px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:3px 0;z-index:400")}>
                  {[
                    ["Smooth Union", V.csgSmoothUnion, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
                    ["Smooth Subtract", V.csgSmoothSub, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path><path d="M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 2"></path></svg>`],
                    ["Smooth Intersect", V.csgSmoothInt, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 2" opacity="0.55"></path><path d="M8 4.83A3.75 3.75 0 0 1 8 11.17A3.75 3.75 0 0 1 8 4.83z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
                  ].map(([label, onClick, svg]: any) => (
                    <Hv key={label} tag="button" onClick={onClick} s="width:100%;display:flex;align-items:center;gap:8px;padding:5px 10px;background:transparent;border:none;font-size:11px;color:#d6d6d6;cursor:pointer;text-align:left" h="background:#2e2e2e;color:#ffffff">
                      <span style={css("flex:0 0 16px;height:16px;display:flex;align-items:center;justify-content:center;color:#a8a8a8")}><Ico d={svg} /></span>
                      <span>{label}</span>
                    </Hv>
                  ))}
                </div>
              )}
            </div>
          </div>

          {/* Center: viewport + bottom dock */}
          <div style={css("flex:1 1 auto;min-width:0;display:flex;flex-direction:column")}>
            <div ref={this.viewportRef} style={css("flex:1 1 auto;position:relative;background:#101010;min-height:0")}>
              {/* Real engine render surface */}
              <canvas
                id="canvas"
                ref={this.canvasRef}
                onPointerDown={this.camDown}
                onPointerMove={this.camMove}
                onPointerUp={this.camUp}
                onWheel={this.camWheel}
                onContextMenu={(e) => {
                  e.preventDefault();
                  V.vpCtx(e);
                }}
                style={css("position:absolute;inset:0;width:100%;height:100%;display:block;touch-action:none;background:#101010")}
              />

              {/* The translate gizmo is drawn by the engine's SDF shader (same
                  frame as the scene), so there is no HTML/SVG overlay here. */}

              <span style={css("position:absolute;top:10px;left:12px;font-size:11px;color:#6b6b6b;pointer-events:none")}>User Perspective</span>

              {/* FPS readout (top-right). Green >=50, amber 30-49, red <30. */}
              <span style={css(`position:absolute;top:10px;right:12px;font-size:11px;font-variant-numeric:tabular-nums;pointer-events:none;color:${this.state.fps >= 50 ? "#5fbf5f" : this.state.fps >= 30 ? "#e8b64a" : "#d15b5b"}`)}>{this.state.fps} FPS</span>

              {/* Viewport overlay toolbar */}
              <div style={css("position:absolute;top:8px;left:50%;transform:translateX(-50%);display:flex;align-items:center;gap:1px;background:rgba(28,28,28,0.92);border:1px solid #333333;border-radius:3px;padding:2px")}>
                {this.toolButton("Select (Q)", V.setSel, V.tSel, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M5.25 2.25v10.25l2.65-2.4 1.7 3.65 1.7-.8-1.7-3.55 3.4-.3z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"></path></svg>`)}
                {this.toolButton("Move (G)", V.setMove, V.tMove, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M8 1.5v13 M1.5 8h13 M8 1.5 6.2 3.3 M8 1.5l1.8 1.8 M8 14.5l-1.8-1.8 M8 14.5l1.8-1.8 M1.5 8l1.8-1.8 M1.5 8l1.8 1.8 M14.5 8l-1.8-1.8 M14.5 8l-1.8 1.8" fill="none" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"></path></svg>`)}
                {this.toolButton("Rotate (R)", V.setRot, V.tRot, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M13.5 8a5.5 5.5 0 1 1-2.05-4.28 M11.6 1.7l.25 2.45 2.45-.35" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"></path></svg>`)}
                {this.toolButton("Scale (S)", V.setScl, V.tScl, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M2.25 9.5v4.25H6.5 M13.75 6.5V2.25H9.5 M13.75 2.25 2.25 13.75" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"></path></svg>`)}
                <div style={css("width:1px;height:14px;background:#333333;margin:0 4px")} />
                {this.spaceButton(V.gzLabel, V.gzToggle, V.gzSt, V.gzEnabled)}
                <div style={css("width:1px;height:14px;background:#333333;margin:0 4px")} />
                {this.toolButton("Wireframe", V.setWire, V.shWire, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M8 1.9a6.1 6.1 0 1 0 0 12.2A6.1 6.1 0 0 0 8 1.9z M8 1.9v12.2 M1.9 8h12.2 M8 1.9C5.6 3.3 5.6 12.7 8 14.1 M8 1.9c2.4 1.4 2.4 10.8 0 12.2" fill="none" stroke="currentColor" stroke-width="1"></path></svg>`)}
                {this.toolButton("Solid", V.setSolid, V.shSolid, `<svg width="15" height="15" viewBox="0 0 16 16"><circle cx="8" cy="8" r="5.5" fill="currentColor" stroke="none"></circle></svg>`)}
                {this.toolButton("Rendered", V.setRend, V.shRend, `<svg width="15" height="15" viewBox="0 0 16 16"><circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" stroke-width="1.2"></circle><circle cx="6" cy="6" r="1.7" fill="currentColor" stroke="none"></circle></svg>`)}
                <div style={css("width:1px;height:14px;background:#333333;margin:0 4px")} />
                {this.toolButton("Toggle Grid", V.onGrid, V.tgGrid, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M2.25 2.25h11.5v11.5H2.25z M2.25 6.08h11.5 M2.25 9.92h11.5 M6.08 2.25v11.5 M9.92 2.25v11.5" fill="none" stroke="currentColor" stroke-width="1"></path></svg>`)}
                {this.toolButton("Snap", V.onSnap, V.tgSnap, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M3 2.25h3.25V7.5a1.75 1.75 0 0 0 3.5 0V2.25H13V7.5a5 5 0 0 1-10 0V2.25z M3 5h3.25 M9.75 5H13" fill="none" stroke="currentColor" stroke-width="1.1" stroke-linejoin="round"></path></svg>`)}
              </div>

              {/* Axis gizmo */}
              {V.showGizmo && <Raw html={GIZMO_HTML} />}
            </div>

            {/* dock splitter */}
            <Hv onPointerDown={V.spDock.down} onPointerMove={V.spDock.move} onPointerUp={V.spDock.up} s="height:5px;flex:0 0 auto;cursor:row-resize;background:#0c0c0c" h="background:#e8912d" />

            {/* Bottom dock */}
            <div style={css(`flex:0 0 auto;height:${V.dockH};background:#1f1f1f;display:flex;flex-direction:column;min-height:0`)}>
              <div style={css("height:28px;flex:0 0 auto;display:flex;align-items:stretch;background:#1a1a1a;border-bottom:1px solid #0c0c0c")}>
                <Hv onClick={V.onDockT} s={`display:flex;align-items:center;padding:0 14px;font-size:11px;cursor:default;background:${V.dtT.bg};color:${V.dtT.fg};border-bottom:2px solid ${V.dtT.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Timeline</Hv>
                <Hv onClick={V.onDockC} s={`display:flex;align-items:center;padding:0 14px;font-size:11px;cursor:default;background:${V.dtC.bg};color:${V.dtC.fg};border-bottom:2px solid ${V.dtC.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Console</Hv>
                <Hv onClick={V.onDockK} s={`display:flex;align-items:center;padding:0 14px;font-size:11px;cursor:default;background:${V.dtK.bg};color:${V.dtK.fg};border-bottom:2px solid ${V.dtK.line};border-right:1px solid #0c0c0c`} h="color:#dedede">UI Kit</Hv>
                <Hv tag="button" title="Add panel — Node Editor, Shader Graph (planned)" onClick={V.onDockPlus} s="width:26px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;color:#6b6b6b;cursor:pointer;padding:0" h="color:#dedede">
                  <Ico d={`<svg width="13" height="13" viewBox="0 0 16 16"><path d="M8 3.25v9.5 M3.25 8h9.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"></path></svg>`} />
                </Hv>
              </div>

              {V.isTimeline && (
                <div style={css("flex:1 1 auto;display:flex;flex-direction:column;min-height:0")}>
                  <div style={css("height:30px;flex:0 0 auto;display:flex;align-items:center;gap:2px;padding:0 10px;border-bottom:1px solid #2a2a2a")}>
                    <Hv tag="button" title="Jump to start" onClick={V.onSkipS} s="width:24px;height:22px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;border-radius:2px;color:#a8a8a8;cursor:pointer;padding:0" h="background:#2e2e2e;color:#dedede">
                      <Ico d={`<svg width="14" height="14" viewBox="0 0 16 16"><path d="M3.75 3.5v9 M12.25 3.5 6.25 8l6 4.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round" stroke-linecap="round"></path></svg>`} />
                    </Hv>
                    <Hv tag="button" title="Play / Pause" onClick={V.onPlay} s="width:24px;height:22px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;border-radius:2px;color:#dedede;cursor:pointer;padding:0" h="background:#2e2e2e">
                      <Ico d={`<svg width="14" height="14" viewBox="0 0 16 16"><path d="${V.playPath}" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round" stroke-linecap="round"></path></svg>`} />
                    </Hv>
                    <Hv tag="button" title="Jump to end" onClick={V.onSkipE} s="width:24px;height:22px;display:flex;align-items:center;justify-content:center;background:transparent;border:none;border-radius:2px;color:#a8a8a8;cursor:pointer;padding:0" h="background:#2e2e2e;color:#dedede">
                      <Ico d={`<svg width="14" height="14" viewBox="0 0 16 16"><path d="M12.25 3.5v9 M3.75 3.5 9.75 8l-6 4.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round" stroke-linecap="round"></path></svg>`} />
                    </Hv>
                    <span style={css("margin-left:10px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede")}>{V.frameText}</span>
                    <div style={css("flex:1")} />
                    <span style={css("font-family:'IBM Plex Mono',monospace;font-size:10px;color:#616161")}>24 fps</span>
                  </div>
                  <div style={css("flex:1 1 auto;position:relative;min-height:0;overflow:hidden")}>
                    <div onPointerDown={V.ruler.down} onPointerMove={V.ruler.move} onPointerUp={V.ruler.up} style={css("height:20px;position:relative;border-bottom:1px solid #2a2a2a;cursor:ew-resize;background:#1a1a1a")}>
                      {V.ticks.map((t: any, i: number) => (
                        <div key={i} style={css(`position:absolute;left:${t.left};top:10px;bottom:0;border-left:1px solid #3a3a3a;padding-left:3px;font-size:9px;color:#6b6b6b;font-family:'IBM Plex Mono',monospace;line-height:10px`)}>{t.label}</div>
                      ))}
                    </div>
                    <div style={css("height:24px;border-bottom:1px solid #242424;background:#1c1c1c")} />
                    <div style={css("height:24px;border-bottom:1px solid #242424;background:#1f1f1f")} />
                    <span style={css("position:absolute;left:0;right:0;top:44px;text-align:center;font-size:11px;color:#616161;pointer-events:none")}>No animation channels</span>
                    <div style={css(`position:absolute;top:0;bottom:0;left:${V.playheadLeft};width:1px;background:#e8912d;pointer-events:none`)}>
                      <div style={css("position:absolute;top:0;left:-3.5px;width:8px;height:8px;background:#e8912d")} />
                    </div>
                  </div>
                </div>
              )}

              {V.isConsole && (
                <div style={css("flex:1 1 auto;overflow:auto;padding:6px 0;font-family:'IBM Plex Mono',monospace;font-size:11px")}>
                  {V.logs.map((l: any, i: number) => (
                    <div key={i} style={css("display:flex;gap:10px;padding:2px 12px;white-space:nowrap")}>
                      <span style={css("color:#616161")}>{l.time}</span>
                      <span style={css(`color:${l.tagColor};min-width:52px`)}>{l.tag}</span>
                      <span style={css("color:#c2c2c2")}>{l.msg}</span>
                    </div>
                  ))}
                </div>
              )}

              {V.isKit && (
                <div style={css("flex:1 1 auto;overflow:auto;display:flex;gap:28px;padding:12px 16px;align-items:flex-start")}>
                  <Raw html={KIT_SURFACES_HTML} />
                  <Raw html={KIT_ACCENT_HTML} />
                  <Raw html={KIT_TYPO_HTML} />
                  <Raw html={KIT_BUTTONS_HTML} />
                  {/* Inputs (live) — interactive demos */}
                  <div style={css("flex:0 0 auto;display:flex;flex-direction:column;gap:6px;width:190px")}>
                    <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:#7a7a7a;font-weight:600")}>Inputs (live)</span>
                    <div style={css("display:flex;align-items:center;gap:8px")}>
                      <FieldCtl
                        f={V.demoField}
                        wrapS="flex:1;height:22px;display:flex;align-items:center;background:#151515;border:1px solid #303030;border-radius:2px;cursor:ew-resize;overflow:hidden"
                        editS="width:100%;background:transparent;border:none;color:#dedede;font-family:'IBM Plex Mono',monospace;font-size:11px;padding:0 6px;user-select:text"
                        showS="flex:1;text-align:right;padding:0 8px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede"
                      />
                      <span style={css("font-size:10px;color:#7a7a7a")}>drag / click</span>
                    </div>
                    <div style={css("display:flex;align-items:center;gap:8px")}>
                      <div onPointerDown={V.demoSlider.down} onPointerMove={V.demoSlider.move} onPointerUp={V.demoSlider.up} style={css("flex:1;height:22px;position:relative;background:#151515;border:1px solid #303030;border-radius:2px;overflow:hidden;cursor:ew-resize")}>
                        <div style={css(`position:absolute;left:0;top:0;bottom:0;width:${V.demoSlider.pct};background:rgba(232,145,45,0.4)`)} />
                        <span style={css("position:absolute;left:8px;top:0;bottom:0;display:flex;align-items:center;font-size:11px;color:#c9c9c9")}>Slider</span>
                        <span style={css("position:absolute;right:8px;top:0;bottom:0;display:flex;align-items:center;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede")}>{V.demoSlider.text}</span>
                      </div>
                      <span style={css("font-size:10px;color:#7a7a7a")}>slider</span>
                    </div>
                    <div style={css("display:flex;align-items:center;gap:8px")}>
                      <div onClick={V.onDemoCheck} style={css(`width:13px;height:13px;border-radius:2px;cursor:pointer;display:flex;align-items:center;justify-content:center;background:${V.demoCheckBg};border:1px solid ${V.demoCheckBorder}`)}>
                        {V.demoCheckOn && <Ico d={`<svg width="9" height="9" viewBox="0 0 16 16"><path d="M3 8.5 6.5 12 13 4.5" fill="none" stroke="#151515" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />}
                      </div>
                      <span style={css("font-size:11px;color:#c9c9c9")}>Checkbox</span>
                    </div>
                    <input placeholder="Text input" style={css("height:22px;background:#151515;border:1px solid #303030;border-radius:2px;color:#dedede;font-family:inherit;font-size:11px;padding:0 8px;user-select:text")} />
                    <div style={css("height:22px;display:flex;align-items:center;justify-content:space-between;background:#2a2a2a;border:1px solid #363636;border-radius:2px;padding:0 8px;font-size:11px;color:#dedede;cursor:default")}>
                      <span>Dropdown</span>
                      <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="M4.5 6.25 8 9.75l3.5-3.5" fill="none" stroke="#a0a0a0" stroke-width="1.4"></path></svg>`} />
                    </div>
                  </div>
                  <Raw html={KIT_TREE_HTML} />
                </div>
              )}
            </div>
          </div>

          {/* right splitter */}
          <Hv onPointerDown={V.spRight.down} onPointerMove={V.spRight.move} onPointerUp={V.spRight.up} s="width:5px;flex:0 0 auto;cursor:col-resize;background:#0c0c0c" h="background:#e8912d" />

          {/* Right column */}
          <div style={css(`flex:0 0 auto;width:${V.rightW};display:flex;flex-direction:column;min-width:0`)}>
            {/* Outliner */}
            <div style={css(`flex:0 0 auto;height:${V.outlH};background:#1f1f1f;display:flex;flex-direction:column;min-height:0`)}>
              <div style={css("height:28px;flex:0 0 auto;display:flex;align-items:stretch;background:#1a1a1a;border-bottom:1px solid #0c0c0c")}>
                <div style={css("display:flex;align-items:center;padding:0 14px;font-size:11px;background:#1f1f1f;color:#dedede;border-bottom:2px solid #e8912d;border-right:1px solid #0c0c0c")}>Outliner</div>
                <div style={css("flex:1")} />
                <div style={css("display:flex;align-items:center;padding:0 6px")}>
                  <input placeholder="Filter" value={V.filterVal} onChange={V.onFilter} style={css("width:110px;height:20px;background:#151515;border:1px solid #303030;border-radius:2px;color:#dedede;font-family:inherit;font-size:11px;padding:0 7px;user-select:text")} />
                </div>
              </div>
              <div style={css("flex:1 1 auto;overflow:auto;padding:3px 0")}>
                {V.treeRows.map((n: any) => (
                  <Hv
                    key={n.id}
                    onClick={n.onSelect}
                    onDoubleClick={n.onRenameStart}
                    onContextMenu={n.onCtx}
                    s={`display:flex;align-items:center;gap:5px;height:24px;padding-left:${n.pad};padding-right:8px;background:${n.bg};cursor:default`}
                    h={`background:${n.hoverBg}`}
                  >
                    <span style={css("width:14px;height:14px;display:flex;align-items:center;justify-content:center;flex:0 0 auto")}>
                      {n.hasKids && (
                        <svg onClick={n.onCaret} width="12" height="12" viewBox="0 0 16 16" style={{ cursor: "pointer" }}>
                          <path d={n.caretPath} fill="none" stroke="#8f8f8f" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
                        </svg>
                      )}
                    </span>
                    <Ico d={`<svg width="14" height="14" viewBox="0 0 16 16" style="flex:0 0 auto"><path d="${n.iconPath}" fill="none" stroke="${n.iconColor}" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                    {n.renaming ? (
                      <input value={n.editName} onChange={n.onNameInput} onKeyDown={n.onNameKey} onBlur={n.onNameBlur} autoFocus style={css("flex:1;min-width:0;height:18px;background:#151515;border:1px solid #e8912d;border-radius:2px;color:#dedede;font-family:inherit;font-size:12px;padding:0 4px;user-select:text")} />
                    ) : (
                      <span style={css(`flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px;color:${n.fg}`)}>{n.name}</span>
                    )}
                    <svg onClick={n.onEye} width="13" height="13" viewBox="0 0 16 16" style={{ flex: "0 0 auto", cursor: "pointer" }}>
                      <path d={n.eyePath} fill="none" stroke={n.eyeColor} strokeWidth="1.1" strokeLinecap="round" />
                    </svg>
                    <svg onClick={n.onLock} width="12" height="12" viewBox="0 0 16 16" style={{ flex: "0 0 auto", cursor: "pointer" }}>
                      <path d={n.lockPath} fill="none" stroke={n.lockColor} strokeWidth="1.1" strokeLinecap="round" strokeLinejoin="round" />
                    </svg>
                  </Hv>
                ))}
              </div>
            </div>

            {/* outliner splitter */}
            <Hv onPointerDown={V.spOutl.down} onPointerMove={V.spOutl.move} onPointerUp={V.spOutl.up} s="height:5px;flex:0 0 auto;cursor:row-resize;background:#0c0c0c" h="background:#e8912d" />

            {/* Properties */}
            <div style={css("flex:1 1 auto;background:#1f1f1f;display:flex;flex-direction:column;min-height:0")}>
              <div style={css("height:28px;flex:0 0 auto;display:flex;align-items:stretch;background:#1a1a1a;border-bottom:1px solid #0c0c0c")}>
                <Hv onClick={V.onPtObj} s={`display:flex;align-items:center;padding:0 12px;font-size:11px;cursor:default;background:${V.ptObj.bg};color:${V.ptObj.fg};border-bottom:2px solid ${V.ptObj.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Object</Hv>
                <Hv onClick={V.onPtMat} s={`display:flex;align-items:center;padding:0 12px;font-size:11px;cursor:default;background:${V.ptMat.bg};color:${V.ptMat.fg};border-bottom:2px solid ${V.ptMat.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Material</Hv>
                <Hv onClick={V.onPtMod} s={`display:flex;align-items:center;padding:0 12px;font-size:11px;cursor:default;background:${V.ptMod.bg};color:${V.ptMod.fg};border-bottom:2px solid ${V.ptMod.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Modifiers</Hv>
                <Hv onClick={V.onPtRen} s={`display:flex;align-items:center;padding:0 12px;font-size:11px;cursor:default;background:${V.ptRen.bg};color:${V.ptRen.fg};border-bottom:2px solid ${V.ptRen.line};border-right:1px solid #0c0c0c`} h="color:#dedede">Render</Hv>
              </div>

              <div style={css("flex:1 1 auto;overflow:auto;min-height:0")}>
                {V.isObjTab && (
                  <div>
                    {V.hasObj && (
                      <div>
                        <div style={css("display:flex;align-items:center;gap:7px;padding:9px 10px 7px")}>
                          <Ico d={`<svg width="15" height="15" viewBox="0 0 16 16"><path d="${V.objIconPath}" fill="none" stroke="#e8912d" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:12px;font-weight:600")}>{V.objName}</span>
                          <span style={css("margin-left:auto;font-size:10px;color:#7a7a7a;font-family:'IBM Plex Mono',monospace")}>{V.objTypeChip}</span>
                        </div>

                        {/* Transform */}
                        <div onClick={V.onSecT} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                          <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.secTCaret}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Transform</span>
                        </div>
                        {V.secTOpen && (
                          <div style={css("display:flex;flex-direction:column;gap:7px;padding:8px 10px 10px")}>
                            {V.transformRows.map((row: any, ri: number) => (
                              <div key={ri} style={css("display:flex;flex-direction:column;gap:3px")}>
                                <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.06em;color:#7a7a7a")}>{row.label}</span>
                                <div style={css("display:flex;gap:4px")}>
                                  {row.axes.map((ax: any, ai: number) => (
                                    <FieldCtl
                                      key={ai}
                                      f={ax}
                                      wrapS="flex:1;min-width:0;height:22px;display:flex;align-items:stretch;background:#151515;border:1px solid #303030;border-radius:2px;cursor:ew-resize;overflow:hidden"
                                      editS="width:100%;min-width:0;background:transparent;border:none;color:#dedede;font-family:'IBM Plex Mono',monospace;font-size:11px;padding:0 5px;user-select:text"
                                      showS="flex:1;display:flex;align-items:center;justify-content:flex-end;padding:0 6px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede;min-width:0;overflow:hidden"
                                      prefix={<span style={css(`width:15px;display:flex;align-items:center;justify-content:center;font-size:9px;font-weight:600;color:${ax.color};background:#1d1d1d;border-right:1px solid #2a2a2a;flex:0 0 auto`)}>{ax.axis}</span>}
                                    />
                                  ))}
                                </div>
                              </div>
                            ))}
                          </div>
                        )}

                        {/* Shape */}
                        <div onClick={V.onSecS} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                          <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.secSCaret}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Shape</span>
                        </div>
                        {V.secSOpen && (
                          <div style={css("display:flex;flex-direction:column;gap:6px;padding:8px 10px 10px")}>
                            <div style={css("display:flex;align-items:center;gap:8px")}>
                              <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Type</span>
                              <div style={css("flex:1;position:relative")}>
                                <Hv onClick={V.ddToggle} s="height:22px;display:flex;align-items:center;justify-content:space-between;background:#2a2a2a;border:1px solid #363636;border-radius:2px;padding:0 8px;font-size:11px;color:#dedede;cursor:default" h="background:#333333">
                                  <span>{V.typeName}</span>
                                  <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="M4.5 6.25 8 9.75l3.5-3.5" fill="none" stroke="#a0a0a0" stroke-width="1.4"></path></svg>`} />
                                </Hv>
                                {V.ddOpen && (
                                  <div style={css("position:absolute;top:100%;left:0;right:0;margin-top:2px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:3px 0;z-index:400")}>
                                    {V.typeOptions.map((o: any, oi: number) => (
                                      <Hv key={oi} onClick={o.onClick} s={`padding:4px 10px;font-size:11px;color:${o.fg};cursor:default`} h="background:#2e2e2e">{o.label}</Hv>
                                    ))}
                                  </div>
                                )}
                              </div>
                            </div>
                            {V.paramRows.map((p: any, pi: number) => (
                              <div key={pi} style={css("display:flex;align-items:center;gap:8px")}>
                                <span style={css(`flex:0 0 64px;font-size:11px;color:${p.disabled ? "#5c5c5c" : "#a0a0a0"}`)} title={p.disabled ? "Not implemented yet" : undefined}>{p.label}</span>
                                <FieldCtl
                                  f={p}
                                  wrapS="flex:1;min-width:0;height:22px;display:flex;align-items:center;background:#151515;border:1px solid #303030;border-radius:2px;cursor:ew-resize;overflow:hidden"
                                  editS="width:100%;background:transparent;border:none;color:#dedede;font-family:'IBM Plex Mono',monospace;font-size:11px;padding:0 6px;user-select:text"
                                  showS="flex:1;text-align:right;padding:0 8px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede"
                                />
                              </div>
                            ))}
                          </div>
                        )}

                        {/* Color */}
                        <div onClick={V.onSecC} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                          <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.secCCaret}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Color</span>
                        </div>
                        {V.secCOpen && (
                          <div style={css("display:flex;align-items:center;gap:8px;padding:8px 10px 12px")}>
                            <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Base</span>
                            <div onClick={V.onPickerOpen} style={css(`width:46px;height:22px;border:1px solid #363636;border-radius:2px;cursor:pointer;background:${V.colorCss}`)} />
                            <span style={css("font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>{V.colorHex}</span>
                          </div>
                        )}
                      </div>
                    )}

                    {V.isCollSel && (
                      <div style={css("padding:12px 10px;display:flex;flex-direction:column;gap:4px")}>
                        <div style={css("display:flex;align-items:center;gap:7px")}>
                          <Ico d={`<svg width="15" height="15" viewBox="0 0 16 16"><path d="M1.75 3.25h4.5l1.5 1.9h6.5v7.6H1.75V3.25z" fill="none" stroke="#e8912d" stroke-width="1.2" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:12px;font-weight:600")}>{V.collName}</span>
                        </div>
                        <span style={css("font-size:11px;color:#7a7a7a")}>{V.collCount}</span>
                      </div>
                    )}

                    {V.isGroupSel && (
                      <div style={css("padding:12px 10px;display:flex;flex-direction:column;gap:8px")}>
                        <div style={css("display:flex;align-items:center;gap:7px")}>
                          <Ico d={`<svg width="15" height="15" viewBox="0 0 16 16"><path d="M1.75 3.25h4.5l1.5 1.9h6.5v7.6H1.75V3.25z" fill="none" stroke="#e8912d" stroke-width="1.2" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:12px;font-weight:600")}>{V.groupSel.name}</span>
                          <span style={css("margin-left:auto;font-size:10px;color:#7a7a7a;font-family:'IBM Plex Mono',monospace")}>{V.groupSel.op}</span>
                        </div>
                        <span style={css("font-size:11px;color:#7a7a7a")}>{V.groupSel.count} objects grouped</span>

                        {/* Color — one colour painted over every object in the group,
                            through the app's shared picker (applyHsv → setGroupColor). */}
                        <div style={css("display:flex;align-items:center;gap:8px;margin-top:2px")}>
                          <span style={css("flex:0 0 52px;font-size:11px;color:#a0a0a0")}>Color</span>
                          <div onClick={V.onPickerOpen} style={css(`width:46px;height:22px;border:1px solid #363636;border-radius:2px;cursor:pointer;background:${V.colorCss}`)} />
                          <span style={css("font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>{V.colorHex}</span>
                        </div>

                        {/* Scale — uniform, about the group centre; session-relative slider. */}
                        <NumericSliderRow cfg={V.groupScaleCfg} value={V.groupScaleValue}
                          open={!!V.modifierHelp.groupscale} caretPath={V.modifierHelp.groupscale ? this.IC.caretD : this.IC.caretR}
                          onToggleHelp={() => V.toggleModifierHelp("groupscale")}
                          onBegin={V.groupScaleBegin} onChange={V.groupScaleChange} onCommit={V.groupScaleCommit} />

                        <span style={css("font-size:10px;line-height:1.5;color:#616161")}>Colour and scale apply to every object in the group (overwriting each object's own colour), about their shared centre. Pick an individual object from the outliner above to edit it on its own.</span>
                      </div>
                    )}

                    {V.noSel && <div style={css("padding:24px 10px;text-align:center;font-size:11px;color:#616161")}>No selection</div>}
                  </div>
                )}

                {V.isMatTab && (
                  <div style={css("display:flex;flex-direction:column;align-items:center;gap:6px;padding:36px 16px;text-align:center")}>
                    <Ico d={`<svg width="22" height="22" viewBox="0 0 16 16"><path d="M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5" fill="none" stroke="#616161" stroke-width="1.2"></path></svg>`} />
                    <span style={css("font-size:12px;color:#a0a0a0")}>Material system</span>
                    <span style={css("font-size:11px;color:#616161")}>Planned — SDF surface shading parameters will live here.</span>
                  </div>
                )}

                {V.isModTab && (
                  <div style={css("display:flex;flex-direction:column;gap:10px;padding:12px 10px")}>
                    <div style={css("position:relative")}>
                      <Hv tag="button" onClick={V.toggleModifierMenu} s="width:100%;height:22px;display:flex;align-items:center;justify-content:space-between;background:#2a2a2a;border:1px solid #363636;border-radius:2px;padding:0 8px;font-size:11px;color:#dedede;cursor:pointer" h="background:#333333;border-color:#404040">
                        <span>Add Modifier</span><Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="M4.5 6.25 8 9.75l3.5-3.5" fill="none" stroke="currentColor" stroke-width="1.4"></path></svg>`} />
                      </Hv>
                      {V.modifierMenu && <div style={css("position:absolute;top:100%;left:0;right:0;margin-top:2px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:3px 0;z-index:400")}>
                        {V.modifierOptions.map((o:any)=><Hv key={o.label} title={o.reason} onClick={()=>{if (!o.disabled) { o.onClick(); V.closeModifierMenu(); }}} s={`padding:4px 10px;font-size:11px;color:${o.disabled ? "#616161" : "#d6d6d6"};opacity:${o.disabled ? .65 : 1};cursor:${o.disabled ? "default" : "pointer"}`} h={o.disabled ? "" : "background:#2e2e2e"}>{o.label}</Hv>)}
                      </div>}
                    </div>

                    {V.blendNode && <div>
                      <div onClick={V.toggleBlendOpen} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                        <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.blendOpen?this.IC.caretD:this.IC.caretR}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`}/><span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Blend</span><span style={css("flex:1")}/><Hv onClick={(e:any)=>{e.stopPropagation();V.removeBlend();}} s="font-size:10px;color:#a0a0a0;padding:2px 4px;cursor:default" h="color:#c4433b">Remove</Hv>
                      </div>
                      {V.blendOpen && <div style={css("padding:8px 10px 10px")}><NumericSliderRow cfg={V.blendCfg} value={V.blendNode.blendRadius} open={!!V.modifierHelp.blend} caretPath={V.modifierHelp.blend?this.IC.caretD:this.IC.caretR} onToggleHelp={()=>V.toggleModifierHelp("blend")} onBegin={()=>V.beginProperty("Change Blend Radius")} onCommit={V.commitProperty} onChange={V.setBlendAmount}/></div>}
                    </div>}

                    {V.roundnessNode && <div>
                      <div onClick={V.toggleRoundnessOpen} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                        <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.roundnessOpen?this.IC.caretD:this.IC.caretR}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`}/><span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Roundness</span><span style={css("flex:1")}/><Hv onClick={(e:any)=>{e.stopPropagation();V.toggleRoundness();}} s="font-size:10px;color:#a0a0a0;padding:2px 4px;cursor:default" h="color:#dedede">{V.roundnessNode.modifierEnabled?"Enabled":"Disabled"}</Hv><Hv onClick={(e:any)=>{e.stopPropagation();V.removeRoundness();}} s="font-size:10px;color:#a0a0a0;padding:2px 4px;cursor:default" h="color:#c4433b">Remove</Hv>
                      </div>
                      {V.roundnessOpen && <div style={css(`padding:8px 10px 10px;opacity:${V.roundnessNode.modifierEnabled?1:.4}`)}><NumericSliderRow cfg={V.roundnessCfg} value={V.roundnessNode.modifierAmount} disabled={!V.roundnessNode.modifierEnabled} open={!!V.modifierHelp.roundness} caretPath={V.modifierHelp.roundness?this.IC.caretD:this.IC.caretR} onToggleHelp={()=>V.toggleModifierHelp("roundness")} onBegin={()=>V.beginProperty("Change Roundness Amount")} onCommit={V.commitProperty} onChange={V.setRoundnessAmount}/></div>}
                    </div>}

                    {!V.hasModifierTarget && <span style={css("font-size:11px;color:#616161;text-align:center;padding:16px 0")}>Select a supported scene node.</span>}
                  </div>
                )}

                {V.isRenTab && (
                  <div style={css("display:flex;flex-direction:column;gap:10px;padding:10px")}>
                    {!this.state.render ? (
                      <span style={css("font-size:11px;color:#616161;text-align:center;padding:16px 0")}>Loading render settings…</span>
                    ) : (
                      <>
                        <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Performance</span>
                        <span style={css("font-size:10px;line-height:1.5;color:#7a7a7a;padding:0 2px")}>Render Resolution — the fastest way to speed up a slow viewport. Lower renders fewer pixels (the biggest cost) and upscales; the image gets softer but the framerate climbs.</span>
                        <div style={css("display:flex;align-items:center;justify-content:space-between;gap:8px")}>
                          <div style={css("display:flex;gap:4px")}>
                            {[["50%", 0.5], ["75%", 0.75], ["100%", 1]].map(([lbl, sc]) => {
                              const active = (this.state.renderScale || 1) === sc;
                              return (
                                <Hv key={String(sc)} onClick={() => this.setRenderScale(sc as number)}
                                  s={`padding:4px 12px;font-size:11px;border-radius:3px;cursor:default;border:1px solid ${active ? "#e8912d" : "#3a3a3a"};background:${active ? "#e8912d" : "#151515"};color:${active ? "#151515" : "#b0b0b0"}`}
                                  h={active ? "" : "border-color:#565656;color:#dedede"}>{lbl as string}</Hv>
                              );
                            })}
                          </div>
                          <span style={css("font-size:10px;color:#616161")}>{this.state.vpW}×{this.state.vpH}</span>
                        </div>

                        <span style={css("font-size:10px;line-height:1.5;color:#7a7a7a;padding:2px 2px 0")}>Turn off effects you don't need — each skips its whole per-pixel march. Shadows are the most expensive.</span>
                        <div style={css("display:flex;flex-direction:column;gap:6px")}>
                          {[["Shadows", RenderSetting.ShadowsEnabled, "shadowsEnabled"], ["Ambient Occlusion", RenderSetting.AoEnabled, "aoEnabled"]].map(([lbl, id, key]) => {
                            const on = (this.state.render as any)?.[key as string] !== false;
                            return (
                              <div key={key as string} style={css("display:flex;align-items:center;justify-content:space-between;gap:8px")}>
                                <span style={css("font-size:11px;color:#c0c0c0")}>{lbl as string}</span>
                                <Hv onClick={() => this.rsBool(id as number, key as string, !on)}
                                  s={`width:34px;height:18px;border-radius:9px;cursor:default;position:relative;flex:0 0 auto;background:${on ? "#e8912d" : "#3a3a3a"}`} h="">
                                  <span style={css(`position:absolute;top:2px;left:${on ? "18px" : "2px"};width:14px;height:14px;border-radius:50%;background:#f0f0f0`)} />
                                </Hv>
                              </div>
                            );
                          })}
                        </div>

                        <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9;margin-top:4px")}>Lighting &amp; Look</span>
                        <span style={css("font-size:10px;line-height:1.5;color:#7a7a7a;padding:0 2px")}>Click a setting's name for a short explanation. These affect the look and cost little or no performance.</span>
                        <div style={css("display:flex;flex-direction:column;gap:8px")}>
                          {this.RENDER_SETTINGS.map((cfg: any) => this.rsRow(cfg))}
                        </div>

                        <Hv onClick={() => this.setState((s: any) => ({ advancedRender: !s.advancedRender }))} s="display:flex;align-items:center;gap:7px;margin-top:2px;padding:7px 4px;border-top:1px solid #2a2a2a;cursor:default;font-size:11px;color:#a0a0a0" h="color:#dedede">
                          <span style={css(`width:13px;height:13px;flex:0 0 auto;border:1px solid ${this.state.advancedRender ? "#e8912d" : "#454545"};border-radius:2px;background:${this.state.advancedRender ? "#e8912d" : "transparent"};display:flex;align-items:center;justify-content:center`)}>
                            {this.state.advancedRender && <Ico d={`<svg width="9" height="9" viewBox="0 0 16 16"><path d="M3 8.5l3 3 7-7.5" fill="none" stroke="#151515" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />}
                          </span>
                          <span>Advanced Settings (Quality / Performance)</span>
                        </Hv>

                        {this.state.advancedRender && (
                          <div style={css("display:flex;flex-direction:column;gap:8px")}>
                            <span style={css("font-size:10px;line-height:1.5;color:#7a7a7a;padding:0 2px")}>These trade rendering speed for quality/range. Lower them if the viewport feels slow on your machine; raise them for crisper, more accurate images.</span>
                            {this.RENDER_ADVANCED.map((cfg: any) => this.rsRow(cfg))}
                          </div>
                        )}
                      </>
                    )}
                  </div>
                )}
              </div>
            </div>
          </div>
        </div>

        {/* ============ STATUS BAR ============ */}
        <footer style={css("height:24px;flex:0 0 auto;display:flex;align-items:center;gap:16px;background:#1c1c1c;border-top:1px solid #0c0c0c;padding:0 10px")}>
          {V.showHints && <span style={css("font-size:11px;color:#7a7a7a")}>{V.hintText}</span>}
          <span style={css("font-size:11px;color:#a0a0a0")}>{V.statusMsg}</span>
          <div style={css("flex:1")} />
          <span style={css("font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a")}>{V.objCountText}</span>
          <span style={css("width:1px;height:12px;background:#333333")} />
          <span style={css("font-family:'IBM Plex Mono',monospace;font-size:10px;color:#7a7a7a")}>v0.1.0</span>
        </footer>

        {/* ============ POPUP LAYER ============ */}
        {V.anyPopup && <div onPointerDown={V.closeAll} style={css("position:fixed;inset:0;z-index:300")} />}

        {V.ctxOpen && (
          <div style={css(`position:fixed;left:${V.ctxX};top:${V.ctxY};min-width:190px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:4px 0;z-index:400`)}>
            {V.ctxItems.map((it: any, i: number) =>
              it.sep ? (
                <div key={i} style={css("height:1px;background:#333333;margin:4px 8px")} />
              ) : (
                <Hv key={i} onClick={it.onClick} s={`display:flex;align-items:center;gap:24px;padding:4px 12px;font-size:12px;color:${it.fg};cursor:default`} h="background:#2e2e2e">
                  <span>{it.label}</span>
                  <span style={css("margin-left:auto;color:#616161;font-family:'IBM Plex Mono',monospace;font-size:10px")}>{it.key}</span>
                </Hv>
              )
            )}
          </div>
        )}

        {V.pickerOpen && (
          <div style={css(`position:fixed;left:${V.pickerX};top:${V.pickerY};width:200px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 10px 30px rgba(0,0,0,0.6);padding:10px;z-index:400`)}>
            <div onPointerDown={V.sv.down} onPointerMove={V.sv.move} onPointerUp={V.sv.up} style={css(`height:120px;position:relative;cursor:crosshair;border:1px solid #0c0c0c;background:${V.svBg}`)}>
              <div style={css(`position:absolute;left:${V.svX};top:${V.svY};width:10px;height:10px;margin:-5px 0 0 -5px;border:1.5px solid #ffffff;border-radius:50%;box-shadow:0 0 0 1px rgba(0,0,0,0.6);pointer-events:none`)} />
            </div>
            <div onPointerDown={V.hue.down} onPointerMove={V.hue.move} onPointerUp={V.hue.up} style={css("height:10px;position:relative;margin-top:8px;cursor:ew-resize;border:1px solid #0c0c0c;background:linear-gradient(to right,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)")}>
              <div style={css(`position:absolute;left:${V.hueX};top:-2px;bottom:-2px;width:3px;margin-left:-1.5px;background:#ffffff;box-shadow:0 0 0 1px rgba(0,0,0,0.6);pointer-events:none`)} />
            </div>
            <div style={css("display:flex;align-items:center;gap:8px;margin-top:8px")}>
              <div style={css(`width:22px;height:22px;border:1px solid #363636;border-radius:2px;background:${V.colorCss}`)} />
              <input value={V.hexShown} onChange={V.onHexInput} onKeyDown={V.onHexKey} onBlur={V.onHexBlur} style={css("flex:1;height:22px;background:#151515;border:1px solid #303030;border-radius:2px;color:#dedede;font-family:'IBM Plex Mono',monospace;font-size:11px;padding:0 7px;user-select:text")} />
            </div>
          </div>
        )}

        {V.overlayOpen && (
          <div data-screen-label="Shortcuts overlay" onClick={V.closeOverlay} style={css("position:fixed;inset:0;z-index:500;background:rgba(12,12,12,0.84);display:flex;align-items:center;justify-content:center;animation:sdfFadeIn 0.15s ease")}>
            <div style={css("width:700px;max-width:90vw;background:#1f1f1f;border:1px solid #363636;border-radius:2px;box-shadow:0 20px 60px rgba(0,0,0,0.6);padding:22px 26px")}>
              <div style={css("display:flex;align-items:baseline;gap:12px;margin-bottom:18px")}>
                <span style={css("font-size:13px;font-weight:600")}>Keyboard Shortcuts</span>
                <span style={css("font-size:11px;color:#616161")}>F1 or click to close</span>
              </div>
              <Raw html={SHORTCUTS_GRID_HTML} />
            </div>
          </div>
        )}

        {this.state.confirm && (
          <div onClick={() => this.cancelDiscard()} style={css("position:fixed;inset:0;z-index:600;background:rgba(12,12,12,0.84);display:flex;align-items:center;justify-content:center;animation:sdfFadeIn 0.15s ease")}>
            <div onClick={(e: any) => e.stopPropagation()} style={css("width:380px;max-width:90vw;background:#1f1f1f;border:1px solid #363636;border-radius:2px;box-shadow:0 20px 60px rgba(0,0,0,0.6);padding:20px 22px")}>
              <div style={css("font-size:13px;font-weight:600;margin-bottom:10px")}>{this.state.confirm.title}</div>
              <div style={css("font-size:12px;color:#a0a0a0;line-height:1.5;margin-bottom:20px")}>{this.state.confirm.body}</div>
              <div style={css("display:flex;justify-content:flex-end;gap:8px")}>
                <Hv onClick={() => this.cancelDiscard()} s="padding:6px 14px;font-size:12px;color:#dedede;border:1px solid #3a3a3a;border-radius:2px;cursor:default" h="background:#2e2e2e">Cancel</Hv>
                <Hv onClick={() => this.proceedDiscard()} s="padding:6px 14px;font-size:12px;color:#151515;font-weight:600;background:#e8912d;border:1px solid #e8912d;border-radius:2px;cursor:default" h="background:#f0a24a">{this.state.confirm.okLabel}</Hv>
              </div>
            </div>
          </div>
        )}
      </div>
    );
  }
}
