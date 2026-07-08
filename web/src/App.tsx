import React from "react";
import { Engine, PrimitiveType, CsgOp, ShapeParam } from "./engine";

// UI type-name <-> engine PrimitiveType enum.
const TYPE_ENUM: Record<string, PrimitiveType> = {
  sphere: PrimitiveType.Sphere,
  box: PrimitiveType.Box,
  cylinder: PrimitiveType.Cylinder,
  torus: PrimitiveType.Torus,
};
const TYPE_NAME: Record<number, string> = { 0: "sphere", 1: "box", 2: "cylinder", 3: "torus" };

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
  return (
    <div onPointerDown={f.down} onPointerMove={f.move} onPointerUp={f.up} style={css(wrapS)}>
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
      {f.show && <span style={css(showS)}>{f.text}</span>}
    </div>
  );
}

// ---- The editor component (logic ported verbatim from the mockup) ---------

export default class App extends React.Component<any, any> {
  // Icon path table.
  IC: any = {
    collection: "M1.75 3.25h4.5l1.5 1.9h6.5v7.6H1.75V3.25z",
    sphere: "M8 1.75a6.25 6.25 0 1 0 0 12.5 6.25 6.25 0 0 0 0-12.5z M1.9 8c1.5 1.7 3.7 2.5 6.1 2.5s4.6-.8 6.1-2.5",
    box: "M8 1.75 14 4.9v6.2L8 14.25 2 11.1V4.9L8 1.75z M2 4.9l6 3.1 6-3.1 M8 8v6.25",
    cylinder: "M3.75 4.2C3.75 3 5.65 2 8 2s4.25 1 4.25 2.2v7.6C12.25 13 10.35 14 8 14s-4.25-1-4.25-2.2V4.2z M3.75 4.2C3.75 5.4 5.65 6.4 8 6.4s4.25-1 4.25-2.2",
    torus: "M8 4.25c3.7 0 6.75 1.7 6.75 3.75S11.7 11.75 8 11.75 1.25 10.05 1.25 8 4.3 4.25 8 4.25z M5.75 8c0-.7 1-1.25 2.25-1.25S10.25 7.3 10.25 8 9.25 9.25 8 9.25 5.75 8.7 5.75 8z",
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
  };
  PRM_ROWS: any = {
    sphere: [["Radius", "radius", 0.01]],
    box: [["Round", "round", 0.005]],
    cylinder: [["Radius", "radius", 0.01], ["Height", "height", 0.01]],
    torus: [["Radius", "radius", 0.01], ["Thickness", "thickness", 0.005]],
  };

  // Property-row param name -> engine ShapeParam id (see engine.ts).
  PARAM_ID: Record<string, ShapeParam> = {
    radius: ShapeParam.Radius,
    height: ShapeParam.Height,
    thickness: ShapeParam.Thickness,
    round: ShapeParam.Roundness,
  };

  // Transient (non-render) scratch handles used by drag helpers.
  _dg: any = null;
  _sl: any = null;
  _sp: any = null;
  _rl = false;
  _sv = false;
  _hu = false;
  _kd: any = null;
  _pt: any = null;

  // Engine / canvas plumbing.
  canvasRef = React.createRef<HTMLCanvasElement>();
  viewportRef = React.createRef<HTMLDivElement>();
  engine: Engine | null = null;
  _ro: ResizeObserver | null = null;
  _camDrag = false;
  _camPan = false;
  _downPos: { x: number; y: number } | null = null;  
  _didDrag = false;                                  

  state: any = {
    openMenu: null,
    tool: "select",
    shading: "solid",
    grid: true,
    snap: false,
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
    secB: true,
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
      if (e.key === "F1") {
        e.preventDefault();
        this.setState((s: any) => ({ overlay: !s.overlay }));
        return;
      }
      if (e.key === "Escape") {
        this.setState({ overlay: false, openMenu: null, ctx: null, picker: null, dd: false, opDd: false, editing: null, renaming: null });
        return;
      }
      const t = e.target as HTMLElement;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA")) return;
      const k = e.key.toLowerCase();
      if (k === "q") this.setState({ tool: "select" });
      else if (k === "g") this.setState({ tool: "move" });
      else if (k === "r") this.setState({ tool: "rotate" });
      else if (k === "s") this.setState({ tool: "scale" });
      else if (k === "f2") {
        const o = this.selObj();
        if (o) {
          const id = String(o.id);
          this.setState({ renaming: id, renameText: this.state.meta[id]?.name || o.typeName });
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
        this.seedDemo();
      })
      .catch((err: unknown) => {
        this.log("error", "engine failed: " + String(err));
        this.status("Engine failed to load — check console");
      });
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
    this.refresh();
    this.setState({ saved: true });
    this.log("scene", "untitled.sdfo — " + specs.length + " objects");
  }

  syncCanvasSize() {
    const canvas = this.canvasRef.current;
    const viewport = this.viewportRef.current;
    if (!canvas || !viewport || !this.engine) return;
    const rect = viewport.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    canvas.width = w;
    canvas.height = h;
    this.engine.resize(w, h);
  }

  camDown = (e: React.PointerEvent) => {
    if (e.button === 2) return; // right-click → context menu
    this._camDrag = true;
    this._camPan = e.button === 1 || e.ctrlKey || e.shiftKey;
    this._downPos = { x: e.clientX, y: e.clientY };   // YENİ
    this._didDrag = false;                             // YENİ
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  };
  _pickInFlight = false;
  camUp = (e: React.PointerEvent) => {
    // YENİ: sürükleme olmadan bırakılan sol tık = seçim tıklaması
    const wasClick = this._camDrag && !this._didDrag && e.button === 0;

    this._camDrag = false;
    this._camPan = false;
    this._downPos = null;
    try {
      (e.target as HTMLElement).releasePointerCapture(e.pointerId);
    } catch (_) {}

    if (wasClick && this.engine && !this._pickInFlight) {   // YENİ koşul
    const canvas = this.canvasRef.current;
    if (canvas) {
      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      // Ctrl/Shift/Cmd click adds to the selection; captured now since the
      // event may be recycled by the time the pick promise resolves.
      const additive = e.shiftKey || e.ctrlKey || e.metaKey;
      this._pickInFlight = true;
      this.engine.pickObject(x, y)
        .then((objectId) => this.selectByPick(objectId, additive))
        .finally(() => { this._pickInFlight = false; });
      }
    }
  };

  camMove = (e: React.PointerEvent) => {
    if (!this._camDrag || !this.engine) return;
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
          meta[gk] = s.meta[gk] || this.defaultGroupMeta(nd.type, counts);
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
      const next: any = { objects: snap.objects, tree, meta, grid: !!snap.showGrid, counts };
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

  // Viewport pick result -> selection. The pick buffer stores the object's
  // vector index (the CSG compile writes the array index into the id target),
  // not its objectId; -1 means empty space.
  selectByPick(pickIndex: number, additive: boolean) {
    if (pickIndex === -1) {
      if (!additive) this.selectKey("scene");
      return;
    }
    const o = this.state.objects.find((x: any) => x.index === pickIndex);
    if (!o) return;
    this.selectKey(String(o.id), additive);
  }

  // Default UI meta for a CSG operation (group) node the UI hasn't named yet.
  defaultGroupMeta(nodeType: number, counts: any): any {
    const opName = nodeType === 2 ? "Subtract" : nodeType === 3 ? "Intersect" : "Union";
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
    if (!isNaN(v)) this.sf(k, v);
    this.setState({ editing: null });
  }
  field(key: string, step: number): any {
    const editing = this.state.editing === key;
    const v = this.gf(key);
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
        if (d.moved < 3) this.setState({ editing: key, editText: this.gf(key).toFixed(2) });
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
    const idx = e.addPrimitive(TYPE_ENUM[typeName]);
    const p = o.position;
    e.setObjectPosition(idx, p[0] + 0.5, p[1], p[2]);
    e.setObjectScale(idx, o.scale[0], o.scale[1], o.scale[2]);
    e.setObjectParam(idx, ShapeParam.Radius, o.params.radius);
    e.setObjectParam(idx, ShapeParam.Height, o.params.height);
    e.setObjectParam(idx, ShapeParam.Thickness, o.params.thickness);
    e.setObjectParam(idx, ShapeParam.Roundness, o.params.roundness);
    e.setObjectRotation(idx, o.rotation[0], o.rotation[1], o.rotation[2]);
    e.setObjectColor(idx, o.color[0], o.color[1], o.color[2]);
    e.setObjectName(idx, (o.name || typeName) + " copy");
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
      // Primitive names are engine-owned; group names stay UI-only for now.
      this.engine?.setObjectName(o.index, name);
      this.setState({ renaming: null, saved: false });
      this.refresh();
    } else {
      this.setState((s: any) => ({ meta: { ...s.meta, [id]: { ...s.meta[id], name } }, renaming: null, saved: false }));
    }
  }
  newScene() {
    this.engine?.newScene();
    this.setState({
      meta: {},
      counts: { sphere: 0, box: 0, cylinder: 0, torus: 0, collection: 0 },
      sel: "scene",
      saved: true,
      openMenu: null,
    });
    this.refresh(true);
    this.log("scene", "New scene");
    this.status("New scene");
  }
  openScene() {
    this.engine?.openScene();
    this.setState({ meta: {}, counts: { sphere: 0, box: 0, cylinder: 0, torus: 0, collection: 0 }, sel: "scene", openMenu: null });
    this.refresh(true);
    this.log("scene", "Open scene — reading from engine");
    this.status("Open scene");
  }
  saveScene() {
    this.engine?.saveScene();
    this.setState({ saved: true, openMenu: null });
    this.log("scene", "Saved untitled.sdfo (" + this.countObjects() + " objects)");
    this.status("Saved");
  }
  csg(op: string) {
    const e = this.engine;
    if (!e) return;
    const codeMap: Record<string, CsgOp> = {
      Union: CsgOp.Union,
      Subtract: CsgOp.Subtract,
      Intersect: CsgOp.Intersect,
    };
    const code = codeMap[op];
    if (!code) {
      // Smooth Union etc. are not wired into the CSG tree yet.
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
  toggleGrid() {
    const grid = !this.state.grid;
    this.engine?.setShowGrid(grid);
    this.setState({ grid });
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

    const rgb = d ? this.hsv2rgb(d.hsv.h, d.hsv.s, d.hsv.v) : [128, 128, 128];
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
    const trRow = (label: string, base: string, step: number) => ({
      label,
      axes: [0, 1, 2].map((i) => ({ axis: axisNames[i], color: axisColors[i], ...this.field(base + "." + i, step) })),
    });
    const transformRows = isObj ? [trRow("Position", "pos", 0.02), trRow("Rotation", "rot", 0.5), trRow("Scale", "scl", 0.01)] : [];
    const paramRows = isObj ? (this.PRM_ROWS[selTypeName] || []).map(([label, k, step]: any) => ({ label, ...this.field("prm." + k, step) })) : [];

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
      e.removeObject(o.index);
      const idx = e.addPrimitive(TYPE_ENUM[t]);
      e.setObjectPosition(idx, pos[0], pos[1], pos[2]);
      e.setObjectColor(idx, col[0], col[1], col[2]);
      e.setObjectRotation(idx, rot[0], rot[1], rot[2]);
      e.setObjectName(idx, name);
      this.refresh();
      this.setState({ dd: false, saved: false });
    };
    // Blend operation is a planned CSG feature — UI-only for now.
    const opSet = (o: string) => () => {
      const so = this.selObj();
      if (!so) {
        this.setState({ opDd: false });
        return;
      }
      const id = String(so.id);
      this.setState((s: any) => ({ opDd: false, saved: false, meta: { ...s.meta, [id]: { ...s.meta[id], op: o } } }));
    };

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

    // Push an HSV value to the selected object's meta AND the engine color.
    const applyHsv = (hsv: any) => {
      if (!selObj || !selId) return;
      const c = this.hsv2rgb(hsv.h, hsv.s, hsv.v);
      this.engine?.setObjectColor(selObj.index, c[0] / 255, c[1] / 255, c[2] / 255);
      this.setState((s: any) => ({ hexEdit: null, saved: false, meta: { ...s.meta, [selId!]: { ...s.meta[selId!], hsv } } }));
    };
    const svSet = (e: any) => {
      if (!d) return;
      const r = e.currentTarget.getBoundingClientRect();
      const sN = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
      const vN = 1 - Math.max(0, Math.min(1, (e.clientY - r.top) / r.height));
      applyHsv({ ...d.hsv, s: sN, v: vN });
    };
    const hueSet = (e: any) => {
      if (!d) return;
      const r = e.currentTarget.getBoundingClientRect();
      const h = Math.max(0, Math.min(359.9, ((e.clientX - r.left) / r.width) * 360));
      applyHsv({ ...d.hsv, h });
    };
    const commitHex = () => {
      const t = (this.state.hexEdit || "").trim().replace(/^#/, "");
      if (/^[0-9a-f]{6}$/i.test(t) && d) {
        applyHsv(this.rgb2hsv(parseInt(t.slice(0, 2), 16), parseInt(t.slice(2, 4), 16), parseInt(t.slice(4, 6), 16)));
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
          sep,
          mi("Frame Selected", ".", () => this.status("Frame Selected — camera op (engine)")),
          mi("Frame All", "Home", () => this.status("Frame All — camera op (engine)")),
          mi("Reset Camera", "", () => this.status("Camera reset")),
          sep,
          mi(S.grid ? "✓ Grid" : "Grid", "", () => this.toggleGrid()),
        ]
      : [
          mi("Rename", "F2", () => {
            if (selObj && selId) this.setState({ renaming: selId, renameText: d?.name || selObj.typeName });
          }, !isObj),
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
        mi("Open…", "Ctrl O", () => this.openScene()),
        mi("Save", "Ctrl S", () => this.saveScene()),
        mi("Save As…", "", () => this.saveScene()),
        sep,
        mi("Import .sdfo", "", () => {}, true),
        mi("Export", "", () => {}, true),
      ],
      mEdit: { bg: mE.bg }, editOpen: mE.open, mtEdit: mE.mt, mhEdit: mE.mh,
      editItems: [
        mi("Undo", "Ctrl Z", () => {}, true),
        mi("Redo", "Ctrl ⇧ Z", () => {}, true),
        sep,
        mi("Preferences…", "", () => {}, true),
      ],
      mObj: { bg: mO.bg }, objOpen: mO.open, mtObj: mO.mt, mhObj: mO.mh,
      objItems: [
        mi("Add Sphere", "", () => this.addPrimitive("sphere")),
        mi("Add Box", "", () => this.addPrimitive("box")),
        mi("Add Cylinder", "", () => this.addPrimitive("cylinder")),
        mi("Add Torus", "", () => this.addPrimitive("torus")),
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

      // left toolbar
      addSphere: () => this.addPrimitive("sphere"),
      addBox: () => this.addPrimitive("box"),
      addCyl: () => this.addPrimitive("cylinder"),
      addTor: () => this.addPrimitive("torus"),
      csgUnion: () => this.csg("Union"),
      csgSub: () => this.csg("Subtract"),
      csgInt: () => this.csg("Intersect"),
      csgSmooth: () => this.csg("Smooth Union"),

      // viewport
      vpCtx: (e: any) => {
        e.preventDefault();
        this.setState({ ctx: { x: e.clientX, y: e.clientY, kind: "vp" }, openMenu: null, picker: null });
      },
      tSel: toolSt(S.tool === "select"), setSel: () => this.setState({ tool: "select" }),
      tMove: toolSt(S.tool === "move"), setMove: () => this.setState({ tool: "move" }),
      tRot: toolSt(S.tool === "rotate"), setRot: () => this.setState({ tool: "rotate" }),
      tScl: toolSt(S.tool === "scale"), setScl: () => this.setState({ tool: "scale" }),
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
      hasObj: isObj, isCollSel: isColl, noSel: S.sel == null,
      objIconPath: isObj ? this.IC[selTypeName] : "",
      objName: isObj ? (d?.name || selObj.typeName) : "",
      objTypeChip: isObj ? "SDF " + this.cap(selTypeName) : "",
      secTCaret: S.secT ? this.IC.caretD : this.IC.caretR, secTOpen: S.secT, onSecT: () => this.setState((s: any) => ({ secT: !s.secT })),
      secSCaret: S.secS ? this.IC.caretD : this.IC.caretR, secSOpen: S.secS, onSecS: () => this.setState((s: any) => ({ secS: !s.secS })),
      secBCaret: S.secB ? this.IC.caretD : this.IC.caretR, secBOpen: S.secB, onSecB: () => this.setState((s: any) => ({ secB: !s.secB })),
      secCCaret: S.secC ? this.IC.caretD : this.IC.caretR, secCOpen: S.secC, onSecC: () => this.setState((s: any) => ({ secC: !s.secC })),
      transformRows, paramRows,
      typeName: isObj ? this.cap(selTypeName) : "",
      ddToggle: (e: any) => {
        e.stopPropagation();
        this.setState((s: any) => ({ dd: !s.dd, opDd: false, openMenu: null }));
      },
      ddOpen: S.dd,
      typeOptions: ["sphere", "box", "cylinder", "torus"].map((t) => ({ label: this.cap(t), fg: isObj && selTypeName === t ? "#e8912d" : "#d6d6d6", onClick: typeSet(t) })),
      opName: d ? d.op : "",
      opToggle: (e: any) => {
        e.stopPropagation();
        this.setState((s: any) => ({ opDd: !s.opDd, dd: false, openMenu: null }));
      },
      opOpen: S.opDd,
      opOptions: ["Union", "Subtract", "Intersect", "Smooth Union"].map((o) => ({ label: o, fg: d && d.op === o ? "#e8912d" : "#d6d6d6", onClick: opSet(o) })),
      smoothPct: d ? d.smooth * 100 + "%" : "0%",
      smoothText: d ? d.smooth.toFixed(2) : "",
      ...(() => {
        const h = this.sliderH("smooth");
        return { smDown: h.down, smMove: h.move, smUp: h.up };
      })(),
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
      anyPopup: !!(S.openMenu || S.ctx || S.picker || S.dd || S.opDd),
      closeAll: () => this.setState({ openMenu: null, ctx: null, picker: null, dd: false, opDd: false }),
      ctxOpen: !!S.ctx,
      ctxX: S.ctx ? Math.min(S.ctx.x, window.innerWidth - 200) + "px" : "0px",
      ctxY: S.ctx ? Math.min(S.ctx.y, window.innerHeight - 240) + "px" : "0px",
      ctxItems,
      pickerOpen: !!S.picker,
      pickerX: S.picker ? S.picker.x + "px" : "0px",
      pickerY: S.picker ? S.picker.y + "px" : "0px",
      svBg: d ? "linear-gradient(to top, #000000, rgba(0,0,0,0)), linear-gradient(to right, #ffffff, hsl(" + d.hsv.h + ", 100%, 50%))" : "#000",
      svX: d ? d.hsv.s * 100 + "%" : "0%",
      svY: d ? (1 - d.hsv.v) * 100 + "%" : "0%",
      sv: {
        down: (e: any) => {
          e.preventDefault();
          try {
            e.currentTarget.setPointerCapture(e.pointerId);
          } catch (_) {}
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
        },
      },
      hueX: d ? (d.hsv.h / 360) * 100 + "%" : "0%",
      hue: {
        down: (e: any) => {
          e.preventDefault();
          try {
            e.currentTarget.setPointerCapture(e.pointerId);
          } catch (_) {}
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
            <span style={css("font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>untitled.sdfo</span>
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
              ["Boolean Union", V.csgUnion, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
              ["Boolean Subtract", V.csgSub, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2"></path><path d="M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 2"></path></svg>`],
              ["Boolean Intersect", V.csgInt, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M6 4.25a3.75 3.75 0 1 0 0 7.5 3.75 3.75 0 0 0 0-7.5z M10 4.25a3.75 3.75 0 1 1 0 7.5 3.75 3.75 0 0 1 0-7.5z" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 2" opacity="0.55"></path><path d="M8 4.83A3.75 3.75 0 0 1 8 11.17A3.75 3.75 0 0 1 8 4.83z" fill="none" stroke="currentColor" stroke-width="1.2"></path></svg>`],
              ["Smooth Union", V.csgSmooth, `<svg width="16" height="16" viewBox="0 0 16 16"><path d="M5.5 4.5a3.5 3.5 0 0 0 0 7c.9 0 1.8-.3 2.5-.9.7.6 1.6.9 2.5.9a3.5 3.5 0 0 0 0-7c-.9 0-1.8.3-2.5.9C7.3 4.8 6.4 4.5 5.5 4.5z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"></path></svg>`],
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
              <span style={css("position:absolute;top:10px;left:12px;font-size:11px;color:#6b6b6b;pointer-events:none")}>User Perspective</span>

              {/* Viewport overlay toolbar */}
              <div style={css("position:absolute;top:8px;left:50%;transform:translateX(-50%);display:flex;align-items:center;gap:1px;background:rgba(28,28,28,0.92);border:1px solid #333333;border-radius:3px;padding:2px")}>
                {this.toolButton("Select (Q)", V.setSel, V.tSel, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M5.25 2.25v10.25l2.65-2.4 1.7 3.65 1.7-.8-1.7-3.55 3.4-.3z" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round"></path></svg>`)}
                {this.toolButton("Move (G)", V.setMove, V.tMove, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M8 1.5v13 M1.5 8h13 M8 1.5 6.2 3.3 M8 1.5l1.8 1.8 M8 14.5l-1.8-1.8 M8 14.5l1.8-1.8 M1.5 8l1.8-1.8 M1.5 8l1.8 1.8 M14.5 8l-1.8-1.8 M14.5 8l-1.8 1.8" fill="none" stroke="currentColor" stroke-width="1.1" stroke-linecap="round"></path></svg>`)}
                {this.toolButton("Rotate (R)", V.setRot, V.tRot, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M13.5 8a5.5 5.5 0 1 1-2.05-4.28 M11.6 1.7l.25 2.45 2.45-.35" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"></path></svg>`)}
                {this.toolButton("Scale (S)", V.setScl, V.tScl, `<svg width="15" height="15" viewBox="0 0 16 16"><path d="M2.25 9.5v4.25H6.5 M13.75 6.5V2.25H9.5 M13.75 2.25 2.25 13.75" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"></path></svg>`)}
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
                                <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>{p.label}</span>
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

                        {/* Blend */}
                        <div onClick={V.onSecB} style={css("display:flex;align-items:center;gap:5px;height:24px;padding:0 8px;background:#262626;border-top:1px solid #0c0c0c;border-bottom:1px solid #0c0c0c;cursor:default")}>
                          <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="${V.secBCaret}" fill="none" stroke="#8f8f8f" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"></path></svg>`} />
                          <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Blend</span>
                        </div>
                        {V.secBOpen && (
                          <div style={css("display:flex;flex-direction:column;gap:6px;padding:8px 10px 10px")}>
                            <div style={css("display:flex;align-items:center;gap:8px")}>
                              <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Operation</span>
                              <div style={css("flex:1;position:relative")}>
                                <Hv onClick={V.opToggle} s="height:22px;display:flex;align-items:center;justify-content:space-between;background:#2a2a2a;border:1px solid #363636;border-radius:2px;padding:0 8px;font-size:11px;color:#dedede;cursor:default" h="background:#333333">
                                  <span>{V.opName}</span>
                                  <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="M4.5 6.25 8 9.75l3.5-3.5" fill="none" stroke="#a0a0a0" stroke-width="1.4"></path></svg>`} />
                                </Hv>
                                {V.opOpen && (
                                  <div style={css("position:absolute;top:100%;left:0;right:0;margin-top:2px;background:#232323;border:1px solid #3a3a3a;border-radius:2px;box-shadow:0 8px 24px rgba(0,0,0,0.55);padding:3px 0;z-index:400")}>
                                    {V.opOptions.map((o: any, oi: number) => (
                                      <Hv key={oi} onClick={o.onClick} s={`padding:4px 10px;font-size:11px;color:${o.fg};cursor:default`} h="background:#2e2e2e">{o.label}</Hv>
                                    ))}
                                  </div>
                                )}
                              </div>
                            </div>
                            <div style={css("display:flex;align-items:center;gap:8px")}>
                              <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Smooth</span>
                              <div onPointerDown={V.smDown} onPointerMove={V.smMove} onPointerUp={V.smUp} style={css("flex:1;height:22px;position:relative;background:#151515;border:1px solid #303030;border-radius:2px;overflow:hidden;cursor:ew-resize")}>
                                <div style={css(`position:absolute;left:0;top:0;bottom:0;width:${V.smoothPct};background:rgba(232,145,45,0.4)`)} />
                                <span style={css("position:absolute;right:8px;top:0;bottom:0;display:flex;align-items:center;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#dedede")}>{V.smoothText}</span>
                              </div>
                            </div>
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
                    <div style={css("height:22px;display:flex;align-items:center;justify-content:space-between;background:#2a2a2a;border:1px solid #2e2e2e;border-radius:2px;padding:0 8px;font-size:11px;color:#616161;opacity:0.7;cursor:default")}>
                      <span>Add Modifier</span>
                      <Ico d={`<svg width="11" height="11" viewBox="0 0 16 16"><path d="M4.5 6.25 8 9.75l3.5-3.5" fill="none" stroke="#616161" stroke-width="1.4"></path></svg>`} />
                    </div>
                    <span style={css("font-size:11px;color:#616161;text-align:center;padding:16px 0")}>Modifier stack — planned</span>
                  </div>
                )}

                {V.isRenTab && (
                  <div style={css("display:flex;flex-direction:column;gap:8px;padding:12px 10px;opacity:0.55")}>
                    <span style={css("font-size:10px;text-transform:uppercase;letter-spacing:0.08em;font-weight:600;color:#c9c9c9")}>Output</span>
                    <div style={css("display:flex;align-items:center;gap:8px")}>
                      <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Resolution</span>
                      <div style={css("flex:1;height:22px;display:flex;align-items:center;justify-content:flex-end;background:#151515;border:1px solid #303030;border-radius:2px;padding:0 8px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>1920</div>
                      <div style={css("flex:1;height:22px;display:flex;align-items:center;justify-content:flex-end;background:#151515;border:1px solid #303030;border-radius:2px;padding:0 8px;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>1080</div>
                    </div>
                    <div style={css("display:flex;align-items:center;gap:8px")}>
                      <span style={css("flex:0 0 64px;font-size:11px;color:#a0a0a0")}>Samples</span>
                      <div style={css("flex:1;height:22px;position:relative;background:#151515;border:1px solid #303030;border-radius:2px;overflow:hidden")}>
                        <div style={css("position:absolute;left:0;top:0;bottom:0;width:25%;background:rgba(232,145,45,0.25)")} />
                        <span style={css("position:absolute;right:8px;top:0;bottom:0;display:flex;align-items:center;font-family:'IBM Plex Mono',monospace;font-size:11px;color:#a0a0a0")}>64</span>
                      </div>
                    </div>
                    <span style={css("font-size:11px;color:#616161;text-align:center;padding:12px 0 0")}>Render export — planned</span>
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
      </div>
    );
  }
}
