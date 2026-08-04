#!/usr/bin/env python3
"""Generates the .sdfp benchmark scenes in this folder (see README.md).

Byte format written here mirrors the engine exactly:
  "SDFP" | i32 count | count * SdfObjectData(80B) | tree section
  tree:  i32 -2 (format marker) | i32 nodeCount | nodeCount * CsgNode(52B)
         | i32 root | i32 nextNodeId
The editor-attrs section is omitted on purpose — DeserializeProject is
EOF-tolerant and rebuilds default names/params from the geometry.

Run:  python generate_perf_tests.py   (any Python 3; emsdk's bundled one works:
      deps/emsdk/python/3.13.3_64bit/python.exe)
"""

import math
import os
import random
import struct

OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---- engine constants ------------------------------------------------------

# SdfObjectType
SPHERE, BOX, CYLINDER, TORUS, LINK, CONE, CAPSULE, SOLID_ANGLE, PYRAMID = range(9)
# CsgNodeType
N_PRIM, N_UNION, N_SUBTRACT, N_INTERSECT, N_GROUP = range(5)
# BlendMode
HARD, SMOOTH = 0, 1
# ModifierType
MOD_NONE, MOD_ROUNDNESS = 0, 1

FOCAL = 2.414  # must match the shader

# Default camera (Camera.h): yaw=0.5, pitch=0.5, distance=8, target=(0,0,0)
def _cam_basis():
    yaw, pitch, dist = 0.5, 0.5, 8.0
    eye = (dist * math.cos(pitch) * math.sin(yaw),
           dist * math.sin(pitch),
           dist * math.cos(pitch) * math.cos(yaw))
    f = _norm(tuple(-c for c in eye))                       # forward (to origin)
    r = _norm(_cross(f, (0.0, 1.0, 0.0)))                   # right
    u = _cross(r, f)                                        # up
    return eye, f, r, u

def _norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)

def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])

def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))

EYE, FWD, RIGHT, UP = _cam_basis()

def project_uv(pos):
    """Camera-space projection like the shader: returns (uv.x, uv.y, z_cam)."""
    rel = tuple(p - e for p, e in zip(pos, EYE))
    z = _dot(rel, FWD)
    if z <= 1e-4:
        return (0.0, 0.0, z)
    return (_dot(rel, RIGHT) * FOCAL / z, _dot(rel, UP) * FOCAL / z, z)

def is_offscreen(pos, radius, ratio_max=2.6):
    """True if the whole bounding sphere is outside any window up to 21:9."""
    ux, uy, z = project_uv(pos)
    if z <= 0.0:
        # behind the camera plane; also require it not to poke through the plane
        rel = tuple(p - e for p, e in zip(pos, EYE))
        return _dot(rel, FWD) < -radius
    ang_margin = FOCAL * radius / z          # generous angular size of the bound
    return (abs(ux) - ang_margin > ratio_max) or (abs(uy) - ang_margin > 1.0)

def is_onscreen(pos, ratio_min=1.2):
    ux, uy, z = project_uv(pos)
    return z > 0.0 and abs(ux) < ratio_min * 0.95 and abs(uy) < 0.95

def quat_axis_angle(axis, degrees):
    axis = _norm(axis)
    h = math.radians(degrees) * 0.5
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))

IDENTITY = (0.0, 0.0, 0.0, 1.0)

PALETTE = [
    (0.85, 0.33, 0.31), (0.36, 0.61, 0.84), (0.45, 0.75, 0.36),
    (0.93, 0.69, 0.25), (0.66, 0.47, 0.83), (0.33, 0.77, 0.72),
    (0.83, 0.45, 0.65), (0.72, 0.72, 0.40),
]

# ---- scene builder ---------------------------------------------------------

class Scene:
    def __init__(self, name):
        self.name = name
        self.objects = []   # dicts
        self.nodes = []     # dicts
        self.root = self.add_op(N_UNION, parent=-1)

    # objects ---------------------------------------------------------------
    def add_obj(self, otype, pos, size, color=None, rot=IDENTITY):
        oid = len(self.objects)
        if color is None:
            color = PALETTE[oid % len(PALETTE)]
        self.objects.append(dict(type=otype, objectId=oid, pos=pos,
                                 rot=rot, size=size, color=color))
        return oid

    # tree ------------------------------------------------------------------
    def _attach(self, idx, parent):
        n = self.nodes[idx]
        n["parent"] = parent
        if parent == -1:
            return
        p = self.nodes[parent]
        if p["firstChild"] == -1:
            p["firstChild"] = idx
        else:
            last = p["lastChild"]
            self.nodes[last]["nextSibling"] = idx
            n["previousSibling"] = last
        p["lastChild"] = idx

    def _new_node(self, ntype, objectId, parent, blendMode, blendRadius,
                  modType, modAmount):
        idx = len(self.nodes)
        self.nodes.append(dict(type=ntype, nodeId=idx, objectId=objectId,
                               parent=-1, previousSibling=-1, nextSibling=-1,
                               firstChild=-1, lastChild=-1,
                               blendMode=blendMode, blendRadius=blendRadius,
                               modifierType=modType, modifierAmount=modAmount,
                               modifierEnabled=True, alive=True))
        self._attach(idx, parent)
        return idx

    def add_op(self, ntype, parent, blend=HARD, radius=0.0):
        return self._new_node(ntype, -1, parent, blend, radius, MOD_NONE, 0.0)

    def add_prim(self, objectId, parent, roundness=None):
        modType = MOD_ROUNDNESS if roundness is not None else MOD_NONE
        return self._new_node(N_PRIM, objectId, parent, HARD, 0.0,
                              modType, roundness or 0.0)

    def prim(self, otype, pos, size, parent=None, color=None, rot=IDENTITY,
             roundness=None):
        """add_obj + add_prim in one call; defaults to a child of the root."""
        oid = self.add_obj(otype, pos, size, color, rot)
        self.add_prim(oid, self.root if parent is None else parent, roundness)
        return oid

    # serialization ---------------------------------------------------------
    def write(self):
        buf = bytearray()
        buf += b"SDFP"
        buf += struct.pack("<i", len(self.objects))
        for o in self.objects:
            sx, sy, sz = o["size"]
            r, g, b = o["color"]
            buf += struct.pack("<4i16f", o["type"], o["objectId"], -1, 0,
                               o["pos"][0], o["pos"][1], o["pos"][2], 0.0,
                               *o["rot"], sx, sy, sz, 0.0, r, g, b, 1.0)
        buf += struct.pack("<ii", -2, len(self.nodes))
        for n in self.nodes:
            buf += struct.pack("<9ifif2B2x",
                               n["type"], n["nodeId"], n["objectId"],
                               n["parent"], n["previousSibling"],
                               n["nextSibling"], n["firstChild"],
                               n["lastChild"], n["blendMode"],
                               n["blendRadius"], n["modifierType"],
                               n["modifierAmount"],
                               1 if n["modifierEnabled"] else 0,
                               1 if n["alive"] else 0)
        buf += struct.pack("<ii", self.root, len(self.nodes))
        self._validate()
        path = os.path.join(OUT_DIR, self.name + ".sdfp")
        with open(path, "wb") as f:
            f.write(buf)
        print(f"  {self.name}.sdfp  objects={len(self.objects):3d}  "
              f"nodes={len(self.nodes):3d}  bytes={len(buf)}")

    def _validate(self):
        """Mirror the engine's load-time checks so a bad file fails HERE."""
        n = len(self.nodes)
        ok = lambda i: -1 <= i < n
        assert ok(self.root) and self.root != -1
        obj_ids = {o["objectId"] for o in self.objects}
        seen = []
        for nd in self.nodes:
            assert all(ok(nd[k]) for k in ("parent", "previousSibling",
                                           "nextSibling", "firstChild",
                                           "lastChild")), "link out of range"
            if nd["type"] == N_PRIM:
                assert nd["objectId"] in obj_ids, "primitive -> missing object"
                seen.append(nd["objectId"])
            else:
                assert nd["firstChild"] != -1, "operation node with no children"
        assert sorted(seen) == sorted(obj_ids), "object without a tree node"


# ---- shared building blocks ------------------------------------------------

def baseline_five(s):
    """The 5-object reference cluster used by A and B (identical geometry)."""
    s.prim(SPHERE,   (-1.4, 0.60,  0.0), (0.90, 0, 0))
    s.prim(BOX,      ( 1.2, 0.70,  0.3), (0.70, 0.70, 0.70),
           rot=quat_axis_angle((0, 1, 0), 30))
    s.prim(CYLINDER, ( 0.0, 0.80, -1.5), (0.50, 1.60, 0))
    s.prim(TORUS,    ( 0.2, 0.40,  1.6), (0.80, 0.25, 0))
    s.prim(CAPSULE,  (-0.3, 1.90,  0.4), (0.35, 0.60, 0))

def offscreen_pack(s, count):
    """Objects provably outside the default view (behind / far left-right /
    far above-below). Verified analytically against the shader's projection."""
    placed = 0
    back = _norm(EYE)  # from origin through the camera, i.e. behind it
    spots = []
    for d in (6, 10, 14, 18, 22, 26, 30, 34):                # behind camera
        spots.append(tuple(e + b * d for e, b in zip(EYE, back)))
    for side in (-1, 1):                                     # far left/right
        for d in (35, 45, 55):
            spots.append(tuple(side * d * r + (0, 0.5, 0)[i]
                               for i, r in enumerate(RIGHT)))
    for y in (60, 80, -60, -80):                             # far above/below
        spots.append((5.0, y, -3.0))
    for x in (-70, 70):
        spots.append((x, 5.0, -60))
    types = [SPHERE, BOX, CYLINDER, TORUS, CAPSULE, CONE]
    for i, pos in enumerate(spots):
        if placed >= count:
            break
        t = types[i % len(types)]
        size = {SPHERE: (0.8, 0, 0), BOX: (0.6, 0.6, 0.6),
                CYLINDER: (0.5, 1.4, 0), TORUS: (0.7, 0.22, 0),
                CAPSULE: (0.35, 0.55, 0), CONE: (1.0, 0.45, 0)}[t]
        assert is_offscreen(pos, 1.2), f"{s.name}: spot {pos} not off-screen!"
        s.prim(t, pos, size)
        placed += 1
    assert placed == count, f"{s.name}: only {placed}/{count} spots"


# ---- the scenes ------------------------------------------------------------

def scene_a():
    s = Scene("A_baseline_5")
    baseline_five(s)
    s.write()

def scene_b():
    s = Scene("B_offscreen_25")
    baseline_five(s)          # identical visible content to A
    offscreen_pack(s, 20)     # + 20 objects the camera cannot see
    s.write()

def scene_c():
    s = Scene("C_spread_16")
    types = [SPHERE, BOX, CYLINDER, CAPSULE]
    step = 1.6
    for gx in range(4):
        for gz in range(4):
            i = gx * 4 + gz
            pos = ((gx - 1.5) * step, 0.35, (gz - 1.5) * step)
            assert is_onscreen(pos), f"C: {pos} outside default view"
            t = types[i % 4]
            size = {SPHERE: (0.32, 0, 0), BOX: (0.26, 0.26, 0.26),
                    CYLINDER: (0.22, 0.7, 0), CAPSULE: (0.18, 0.3, 0)}[t]
            s.prim(t, pos, size)
    s.write()

def scene_d():
    s = Scene("D_cluster_12")
    # 8 primitives smooth-unioned into one blob (k = 0.35)
    blob = s.add_op(N_UNION, s.root, blend=SMOOTH, radius=0.35)
    ang = 0.0
    for i in range(8):
        ang += 2.399963  # golden angle: even spread around the blob core
        pos = (0.9 * math.cos(ang), 1.0 + 0.35 * math.sin(i * 1.7),
               0.9 * math.sin(ang))
        if i % 2 == 0:
            s.prim(SPHERE, pos, (0.55, 0, 0), parent=blob)
        else:
            s.prim(BOX, pos, (0.42, 0.42, 0.42), parent=blob,
                   rot=quat_axis_angle((1, 1, 0), 25 + 20 * i))
    # a smooth-subtract pair biting into the blob region
    sub = s.add_op(N_SUBTRACT, s.root, blend=SMOOTH, radius=0.25)
    s.prim(BOX, (0.0, 0.35, 0.0), (1.3, 0.35, 1.3), parent=sub)   # base
    s.prim(SPHERE, (0.5, 0.55, 0.4), (0.55, 0, 0), parent=sub)    # cutter
    # two loose primitives so the flat-union path is present too
    s.prim(TORUS, (-1.9, 0.35, 1.2), (0.55, 0.18, 0))
    s.prim(CYLINDER, (1.9, 0.55, -1.3), (0.35, 1.1, 0))
    s.write()

def scene_e():
    s = Scene("E_background_10")
    for i in range(10):
        ang = i * 0.6283
        pos = (0.7 * math.cos(ang), 0.75 + 0.45 * math.sin(i * 2.1),
               0.7 * math.sin(ang))
        t = [SPHERE, BOX, CAPSULE][i % 3]
        size = {SPHERE: (0.22, 0, 0), BOX: (0.18, 0.18, 0.18),
                CAPSULE: (0.12, 0.22, 0)}[t]
        s.prim(t, pos, size)
    s.write()

def scene_f():
    s = Scene("F_shadow_12")
    # SDF floor slab (top at y=0.02, just above the analytic grid plane)
    s.prim(BOX, (0.0, -0.13, 0.0), (5.0, 0.15, 5.0), color=(0.55, 0.55, 0.58))
    # two standing slabs that receive long shadows
    s.prim(BOX, (-2.4, 1.0, -1.8), (0.25, 1.0, 1.4), color=(0.6, 0.5, 0.4))
    s.prim(BOX, (2.6, 0.8, 1.6), (1.2, 0.8, 0.22), color=(0.4, 0.5, 0.6))
    # floaters between the light (dir 0.5,0.8,0.6) and the floor -> penumbras
    floats = [((-1.6, 1.6, 0.6), SPHERE, (0.45, 0, 0)),
              ((0.2, 2.2, -0.8), SPHERE, (0.38, 0, 0)),
              ((1.4, 1.4, -0.2), CAPSULE, (0.28, 0.5, 0)),
              ((-0.4, 1.2, 1.5), BOX, (0.35, 0.35, 0.35)),
              ((0.9, 2.6, 0.9), TORUS, (0.5, 0.16, 0)),
              ((-1.1, 2.9, -1.2), SPHERE, (0.3, 0, 0)),
              ((2.0, 2.0, -1.6), BOX, (0.3, 0.3, 0.3)),
              ((-2.2, 2.3, 1.8), CAPSULE, (0.22, 0.45, 0)),
              ((0.0, 3.4, 0.2), SPHERE, (0.35, 0, 0))]
    for pos, t, size in floats:
        s.prim(t, pos, size)
    s.write()

def scene_g():
    s = Scene("G_correctness_17")
    # 1) rotated box smooth-minus rotated cylinder (big k -> tests the margin)
    sub = s.add_op(N_SUBTRACT, s.root, blend=SMOOTH, radius=0.30)
    s.prim(BOX, (-2.2, 0.7, 0.0), (0.7, 0.7, 0.7), parent=sub,
           rot=quat_axis_angle((1, 1, 0), 35))
    s.prim(CYLINDER, (-2.2, 0.9, 0.2), (0.4, 2.4, 0), parent=sub,
           rot=quat_axis_angle((1, 0, 0), 50))
    # 2) torus intersect sphere (hard)
    inter = s.add_op(N_INTERSECT, s.root)
    s.prim(TORUS, (0.0, 0.6, -1.8), (0.8, 0.3, 0), parent=inter,
           rot=quat_axis_angle((1, 0, 0), 40))
    s.prim(SPHERE, (0.3, 0.6, -1.8), (0.85, 0, 0), parent=inter)
    # 3) smooth-union chain with a LARGE blend radius (worst case for culling)
    su = s.add_op(N_UNION, s.root, blend=SMOOTH, radius=0.60)
    for i in range(3):
        s.prim(SPHERE, (1.8 + i * 0.75, 0.5 + 0.3 * i, 1.2), (0.5, 0, 0),
               parent=su)
    # 4) rounded box via the roundness modifier
    s.prim(BOX, (0.2, 0.45, 1.9), (0.45, 0.45, 0.45), roundness=0.12,
           rot=quat_axis_angle((0, 1, 0), 20))
    # 5) every remaining exotic type, rotated (exercises per-type bounds)
    s.prim(CONE, (-0.6, 1.15, 0.3), (1.0, 0.45, 0),
           rot=quat_axis_angle((0, 0, 1), 15))
    s.prim(LINK, (1.6, 1.6, -0.6), (0.4, 0.35, 0.13),
           rot=quat_axis_angle((1, 0, 0), 30))
    s.prim(SOLID_ANGLE, (-1.0, 0.0, -2.3), (0.7, 0.55, 0))
    s.prim(PYRAMID, (2.4, 0.0, -1.6), (0.5, 0.9, 0),
           rot=quat_axis_angle((0, 1, 0), 40))
    # 6) deliberately deep nesting (depth ~6) — guards any RPN stack shrink
    cur = s.add_op(N_GROUP, s.root)
    for depth in range(4):
        cur = s.add_op(N_UNION, cur, blend=SMOOTH if depth % 2 else HARD,
                       radius=0.2 if depth % 2 else 0.0)
        s.prim(SPHERE, (-0.2 + 0.45 * depth, 2.3 + 0.4 * depth, -0.9),
               (0.30, 0, 0), parent=cur)
    s.prim(SPHERE, (1.6, 4.0, -0.9), (0.30, 0, 0), parent=cur)
    s.write()

# ---- scale scenes (P1d: hierarchical subtree skipping) ---------------------
#
# Everything above tops out at 48 objects, where an O(N)-per-march-step program
# walk is invisible. These exist to make it visible: the whole point of P1d is
# that cost currently scales with TOTAL scene size instead of with what the ray
# can see, and you cannot measure that on 17 objects.
#
# All of them are a FLAT hard Union under the root, which is deliberate and is
# the realistic shape of a big scene. A flat union peaks at RPN stack depth 2
# no matter how many children, so before P1d there is no hierarchy to skip and
# distanceCalc walks all 2N-1 commands at every single march step.
#
# Two families, because they stress opposite things:
#   S_wide_*   constant local density, cloud GROWS with count. Most objects end
#              up far from any given ray -> the case hierarchical skipping wins
#              biggest, and the clearest demonstration of the complexity change.
#   S_dense_*  constant extent, density grows with count. Objects are near every
#              ray, so little can be skipped -> the honest worst case, and the
#              one that proves the traversal overhead does not cost more than
#              it saves.

def _cloud_cells(count, spacing, center):
    """The `count` lattice cells nearest the centre, so the cloud is roughly a
    ball and the framing stays comparable across counts. Deterministic."""
    k = int(math.ceil(count ** (1.0 / 3.0))) + 2      # +2: ball < cube, need slack
    half = (k - 1) * 0.5
    cells = []
    for ix in range(k):
        for iy in range(k):
            for iz in range(k):
                d = (ix - half) ** 2 + (iy - half) ** 2 + (iz - half) ** 2
                cells.append((d, ix, iy, iz))
    cells.sort()
    out = []
    for _, ix, iy, iz in cells[:count]:
        out.append((center[0] + (ix - half) * spacing,
                    center[1] + (iy - half) * spacing,
                    center[2] + (iz - half) * spacing))
    return out


def scale_cloud(name, count, spacing, radius, seed):
    s = Scene(name)
    rng = random.Random(seed)
    # Lifted off y=0 so the analytic grid plane still reads underneath.
    cells = _cloud_cells(count, spacing, (0.0, 1.6 + spacing * 0.5, 0.0))
    jitter = spacing * 0.30
    types = [SPHERE, BOX, CAPSULE, CYLINDER]
    for i, (cx, cy, cz) in enumerate(cells):
        pos = (cx + rng.uniform(-jitter, jitter),
               cy + rng.uniform(-jitter, jitter),
               cz + rng.uniform(-jitter, jitter))
        t = types[i % len(types)]
        r = radius * rng.uniform(0.75, 1.25)
        size = {SPHERE:   (r, 0, 0),
                BOX:      (r * 0.8, r * 0.8, r * 0.8),
                CAPSULE:  (r * 0.55, r * 0.9, 0),
                CYLINDER: (r * 0.7, r * 1.1, 0)}[t]
        # Rotate the non-spheres: an enclosing sphere is rotation-invariant, so
        # this must not change any bound P1d computes. Free regression check.
        rot = IDENTITY if t == SPHERE else quat_axis_angle(
            (rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)) ,
            rng.uniform(0, 360))
        s.prim(t, pos, size, rot=rot)
    s.write()


def scene_scale_wide():
    # Constant density (spacing 1.1, radius ~0.22): ~2% volume fill, so the
    # cloud is mostly empty space and its AABB is useless to P4.
    #
    # The 60/120 rungs exist because a weak GPU loses the device (Windows TDR,
    # 2s) well before 250 objects. Start at 60 and climb until it breaks — the
    # rung where it breaks is itself the "before" number worth recording.
    scale_cloud("S_wide_60",   60,   1.10, 0.22, seed=60)
    scale_cloud("S_wide_120",  120,  1.10, 0.22, seed=120)
    scale_cloud("S_wide_250",  250,  1.10, 0.22, seed=250)
    scale_cloud("S_wide_1000", 1000, 1.10, 0.22, seed=1000)
    scale_cloud("S_wide_4000", 4000, 1.10, 0.22, seed=4000)


def scene_scale_dense():
    # 1000 objects squeezed into roughly the S_wide_250 volume.
    scale_cloud("S_dense_1000", 1000, 0.70, 0.16, seed=7000)


def scene_h():
    s = Scene("H_ultra_48")
    # -- smooth blob, 6 prims, k = 0.5 (large)
    blob = s.add_op(N_UNION, s.root, blend=SMOOTH, radius=0.50)
    for i in range(6):
        ang = i * 1.0472
        pos = (0.85 * math.cos(ang), 1.1 + 0.3 * math.sin(i * 2.0),
               0.85 * math.sin(ang))
        t = SPHERE if i % 2 == 0 else BOX
        size = (0.5, 0, 0) if t == SPHERE else (0.4, 0.4, 0.4)
        s.prim(t, pos, size, parent=blob,
               rot=IDENTITY if t == SPHERE else quat_axis_angle((1, 1, 0), 30 * i))
    # -- smooth subtract bite
    sub = s.add_op(N_SUBTRACT, s.root, blend=SMOOTH, radius=0.30)
    s.prim(BOX, (0.0, 0.3, 0.0), (1.5, 0.3, 1.5), parent=sub)
    s.prim(SPHERE, (0.4, 0.5, -0.3), (0.6, 0, 0), parent=sub)
    # -- spread grid (12), small + far apart within view
    types = [SPHERE, BOX, CYLINDER, CAPSULE]
    for gx in range(4):
        for gz in range(3):
            i = gx * 3 + gz
            pos = ((gx - 1.5) * 1.6, 0.30, (gz - 1.0) * 1.6 + 2.6)
            t = types[i % 4]
            size = {SPHERE: (0.28, 0, 0), BOX: (0.22, 0.22, 0.22),
                    CYLINDER: (0.2, 0.6, 0), CAPSULE: (0.16, 0.28, 0)}[t]
            s.prim(t, pos, size)
    # -- floor + shadow floaters (7)
    s.prim(BOX, (0.0, -0.13, -0.5), (5.5, 0.15, 5.5), color=(0.5, 0.5, 0.55))
    for i in range(6):
        ang = i * 1.047 + 0.4
        pos = (2.6 * math.cos(ang), 1.6 + 0.5 * (i % 3), 2.6 * math.sin(ang) - 0.5)
        s.prim(SPHERE if i % 2 == 0 else BOX, pos,
               (0.35, 0, 0) if i % 2 == 0 else (0.28, 0.28, 0.28))
    # -- rotated exotic types in a Group (5)
    grp = s.add_op(N_GROUP, s.root)
    s.prim(CONE, (-2.9, 1.15, 1.6), (1.0, 0.45, 0), parent=grp,
           rot=quat_axis_angle((0, 0, 1), 20))
    s.prim(TORUS, (3.0, 0.5, -2.0), (0.7, 0.22, 0), parent=grp,
           rot=quat_axis_angle((1, 0, 0), 55))
    s.prim(LINK, (-3.0, 0.7, -1.9), (0.4, 0.35, 0.13), parent=grp,
           rot=quat_axis_angle((0, 1, 1), 35))
    s.prim(SOLID_ANGLE, (3.1, 0.02, 1.9), (0.7, 0.55, 0), parent=grp)
    s.prim(PYRAMID, (0.1, 0.02, -3.0), (0.5, 0.9, 0), parent=grp,
           rot=quat_axis_angle((0, 1, 0), 25))
    # -- 16 off-screen objects (culling share of the stress)
    offscreen_pack(s, 16)
    s.write()


if __name__ == "__main__":
    print(f"camera eye ~ ({EYE[0]:.2f}, {EYE[1]:.2f}, {EYE[2]:.2f}) -> origin")
    print("writing benchmark scenes:")
    scene_a()
    scene_b()
    scene_c()
    scene_d()
    scene_e()
    scene_f()
    scene_g()
    scene_h()
    print("scale scenes (P1d):")
    scene_scale_wide()
    scene_scale_dense()
    print("done.")
