"""
Euphoria Engine - "Hero" player character generator.

A low-poly, faceted human (Killer7 / PS2-era style) with a 17-bone rig and four actions:
Idle, Walk, Run and Jump. The same data drives two back ends:

  * Inside Blender (3.6 / 4.x): builds a real mesh, armature, materials and actions you can
    edit, then exports a .glb with the glTF exporter.
        - Scripting tab -> Open this file -> Run Script, or
        - blender --background --python tools/blender/euphoria_hero.py -- out.glb
  * Plain Python 3 (no Blender needed): writes the .glb directly.
        python3 tools/blender/euphoria_hero.py projects/Demo/Models/Hero/Hero.glb

Conventions (match what Blender's glTF exporter produces):
  * 1 unit = 1 metre, feet on the ground at y = 0, ~1.85 m tall.
  * glTF space is +Y up and the character faces +Z (Blender: faces -Y, the standard front view).
  * Action names Idle / Walk / Run / Jump are what the engine's "Auto-setup locomotion" looks for.

Everything below is in glTF space; the Blender back end converts with G2B().
"""
import json
import math
import os
import struct
import sys

FPS = 30

# --------------------------------------------------------------------------- math
def qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def qaxis(axis, deg):
    h = math.radians(deg) * 0.5
    s = math.sin(h)
    return (math.cos(h), axis[0] * s, axis[1] * s, axis[2] * s)


def qeuler(x=0.0, y=0.0, z=0.0):
    """Rotation in degrees: yaw (Y), then pitch (X), then roll (Z), all in the parent frame."""
    return qmul(qaxis((0, 1, 0), y), qmul(qaxis((1, 0, 0), x), qaxis((0, 0, 1), z)))


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = math.sqrt(dot(a, a))
    return scale(a, 1.0 / l) if l > 1e-12 else (0.0, 1.0, 0.0)


# Pose helpers. The character faces +Z, so "forward" swings are negative rotations about X.
def swing(deg): return qeuler(x=-deg)          # limb swings forward (+Z) by deg
def bend_back(deg): return qeuler(x=deg)        # knee: lower leg folds backward
def lean(deg): return qeuler(x=deg)             # torso / head tips forward


# --------------------------------------------------------------------------- skeleton
# (name, parent, head position in model space). Rest rotations are identity, so each joint's
# local translation is simply head - parent head. Left = +X (the character's own left).
BONES = [
    ("Hips", None, (0.0, 0.97, 0.0)),
    ("Spine", "Hips", (0.0, 1.08, 0.0)),
    ("Chest", "Spine", (0.0, 1.26, 0.0)),
    ("Neck", "Chest", (0.0, 1.50, 0.0)),
    ("Head", "Neck", (0.0, 1.60, 0.0)),
]
for side, sx in (("L", 1.0), ("R", -1.0)):
    BONES += [
        ("UpperArm." + side, "Chest", (0.205 * sx, 1.46, 0.0)),
        ("LowerArm." + side, "UpperArm." + side, (0.225 * sx, 1.19, 0.0)),
        ("Hand." + side, "LowerArm." + side, (0.235 * sx, 0.96, 0.0)),
        ("UpperLeg." + side, "Hips", (0.095 * sx, 0.93, 0.0)),
        ("LowerLeg." + side, "UpperLeg." + side, (0.105 * sx, 0.53, 0.0)),
        ("Foot." + side, "LowerLeg." + side, (0.11 * sx, 0.12, 0.0)),
    ]
BONE_INDEX = {b[0]: i for i, b in enumerate(BONES)}
BONE_POS = {b[0]: b[2] for b in BONES}
# Bone tails (Blender only - glTF has no bone length).
TAILS = {"Hips": "Spine", "Spine": "Chest", "Chest": "Neck", "Neck": "Head"}
for s in ("L", "R"):
    TAILS.update({"UpperArm." + s: "LowerArm." + s, "LowerArm." + s: "Hand." + s,
                  "UpperLeg." + s: "LowerLeg." + s, "LowerLeg." + s: "Foot." + s})


def bone_tail(name):
    if name in TAILS: return BONE_POS[TAILS[name]]
    p = BONE_POS[name]
    if name == "Head": return (p[0], 1.86, p[2])
    if name.startswith("Hand"): return (p[0], 0.84, p[2])
    return (p[0], 0.03, 0.15)   # feet point forward


# --------------------------------------------------------------------------- materials
# Base colours in sRGB (as you'd pick them in a colour picker).
MATERIALS = [
    ("Skin", (0.86, 0.66, 0.52)),
    ("Hair", (0.09, 0.07, 0.07)),
    ("Jacket", (0.78, 0.58, 0.16)),
    ("Shirt", (0.93, 0.92, 0.88)),
    ("Pants", (0.15, 0.16, 0.21)),
    ("Boots", (0.28, 0.17, 0.10)),
    ("Lens", (0.95, 0.72, 0.12)),
]
MAT_INDEX = {m[0]: i for i, m in enumerate(MATERIALS)}


def srgb_to_linear(c): return tuple(pow(v, 2.2) for v in c)


# --------------------------------------------------------------------------- geometry
# A part is an indexed polygon mesh rigidly bound to one bone (PS2-style rigid skinning;
# parts overlap at the joints so bending never opens a gap).
class Part:
    def __init__(self, material, bone):
        self.material, self.bone = material, bone
        self.verts, self.polys = [], []

    def finish(self):
        """Make every polygon wind counter-clockwise seen from outside (parts are convex)."""
        c = scale(tuple(map(sum, zip(*self.verts))), 1.0 / len(self.verts))
        fixed = []
        for p in self.polys:
            pts = [self.verts[i] for i in p]
            n = (0.0, 0.0, 0.0)
            for i in range(len(pts)):            # Newell normal
                a, b = pts[i], pts[(i + 1) % len(pts)]
                n = add(n, ((a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])))
            fc = scale(tuple(map(sum, zip(*pts))), 1.0 / len(pts))
            fixed.append(list(reversed(p)) if dot(n, sub(fc, c)) < 0 else p)
        self.polys = fixed
        return self


def loft(material, bone, rings, sides, twist=0.0):
    """rings: [(center, rx, rz), ...] horizontal cross-sections from one end to the other."""
    part = Part(material, bone)
    for (c, rx, rz) in rings:
        for k in range(sides):
            a = 2.0 * math.pi * k / sides + math.radians(twist)
            part.verts.append((c[0] + rx * math.sin(a), c[1], c[2] + rz * math.cos(a)))
    for r in range(len(rings) - 1):
        for k in range(sides):
            k2 = (k + 1) % sides
            part.polys.append([r * sides + k, r * sides + k2, (r + 1) * sides + k2, (r + 1) * sides + k])
    part.polys.append(list(range(sides)))
    part.polys.append([(len(rings) - 1) * sides + k for k in range(sides)])
    return part.finish()


def box(material, bone, center, size):
    part = Part(material, bone)
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    for i in range(8):
        part.verts.append((center[0] + (hx if i & 1 else -hx), center[1] + (hy if i & 2 else -hy), center[2] + (hz if i & 4 else -hz)))
    part.polys = [[0, 1, 3, 2], [4, 5, 7, 6], [0, 1, 5, 4], [2, 3, 7, 6], [0, 2, 6, 4], [1, 3, 7, 5]]
    return part.finish()


def build_parts():
    P = []
    # Torso
    P.append(loft("Pants", "Hips", [((0, 0.84, 0), 0.155, 0.10), ((0, 1.01, 0), 0.165, 0.11)], 8, 22.5))
    P.append(loft("Jacket", "Spine", [((0, 0.96, 0), 0.168, 0.118), ((0, 1.23, 0.0), 0.165, 0.112)], 8, 22.5))
    P.append(loft("Jacket", "Chest", [((0, 1.20, 0), 0.17, 0.115), ((0, 1.42, 0), 0.215, 0.128), ((0, 1.52, -0.005), 0.165, 0.10)], 8, 22.5))
    P.append(box("Shirt", "Spine", (0, 1.11, 0.104), (0.10, 0.25, 0.03)))     # open jacket shows the shirt
    P.append(box("Shirt", "Chest", (0, 1.33, 0.115), (0.12, 0.25, 0.03)))
    # Neck, head, hair, glasses
    P.append(loft("Skin", "Neck", [((0, 1.47, 0.0), 0.05, 0.05), ((0, 1.62, 0.0), 0.048, 0.05)], 6))
    P.append(loft("Skin", "Head", [((0, 1.585, 0.012), 0.072, 0.075), ((0, 1.66, 0.008), 0.098, 0.105),
                                   ((0, 1.78, 0.0), 0.104, 0.11), ((0, 1.83, -0.005), 0.08, 0.088)], 8, 22.5))
    P.append(loft("Hair", "Head", [((0, 1.755, -0.012), 0.113, 0.12), ((0, 1.86, -0.004), 0.10, 0.108),
                                   ((0, 1.935, 0.02), 0.045, 0.06)], 8, 22.5))
    P.append(box("Hair", "Head", (0, 1.69, -0.085), (0.18, 0.15, 0.05)))      # back of the hair
    P.append(box("Lens", "Head", (0, 1.715, 0.104), (0.19, 0.042, 0.02)))     # sunglasses
    P.append(box("Skin", "Head", (0, 1.672, 0.112), (0.026, 0.045, 0.03)))    # nose
    for side, sx in (("L", 1.0), ("R", -1.0)):
        x = lambda v: v * sx
        # Arms: jacket sleeves, hands
        P.append(loft("Jacket", "UpperArm." + side, [((x(0.205), 1.50, 0), 0.062, 0.064), ((x(0.226), 1.18, 0), 0.052, 0.055)], 6))
        P.append(loft("Jacket", "LowerArm." + side, [((x(0.225), 1.21, 0), 0.051, 0.053), ((x(0.236), 0.97, 0.005), 0.043, 0.046)], 6))
        P.append(box("Skin", "Hand." + side, (x(0.238), 0.905, 0.012), (0.045, 0.115, 0.085)))
        # Legs: trousers, boots
        P.append(loft("Pants", "UpperLeg." + side, [((x(0.092), 0.98, 0), 0.085, 0.09), ((x(0.105), 0.51, 0), 0.062, 0.066)], 6))
        P.append(loft("Pants", "LowerLeg." + side, [((x(0.105), 0.55, 0), 0.062, 0.064), ((x(0.11), 0.15, 0), 0.05, 0.052)], 6))
        P.append(loft("Boots", "LowerLeg." + side, [((x(0.11), 0.10, 0), 0.056, 0.058), ((x(0.11), 0.22, 0), 0.058, 0.06)], 6))
        P.append(box("Boots", "Foot." + side, (x(0.11), 0.06, 0.035), (0.105, 0.12, 0.26)))
    return P


# --------------------------------------------------------------------------- animation
# Each clip: (duration seconds, loops?, pose(t) -> ({bone: quat}, hips offset)).
def _idle(t):
    p = 2.0 * math.pi * t / 3.0
    b = math.sin(p)
    pose = {
        "Spine": lean(1.0 + 0.8 * b),
        "Chest": lean(0.8 * b),
        "Neck": lean(-1.0),
        "Head": qeuler(x=1.5 - 1.0 * b, y=4.0 * math.sin(p * 0.5)),
        "UpperArm.L": qeuler(z=7.0 + 1.5 * b, x=-2.0), "UpperArm.R": qeuler(z=-7.0 - 1.5 * b, x=-2.0),
        "LowerArm.L": swing(12.0), "LowerArm.R": swing(12.0),
        "UpperLeg.L": qeuler(z=2.0), "UpperLeg.R": qeuler(z=-2.0),
        "Hips": qeuler(z=1.2 * math.sin(p * 0.5)),
    }
    return pose, (0.012 * math.sin(p * 0.5), -0.004 + 0.004 * b, 0.0)


def _walk(t):
    T = 1.0                                  # one full stride (two steps) per second
    p = 2.0 * math.pi * t / T
    s, c = math.sin(p), math.cos(p)
    pose = {"Spine": lean(4.0), "Chest": qeuler(y=-6.0 * s), "Hips": qeuler(y=6.0 * s), "Head": lean(-3.0)}
    for side, sign in (("L", 1.0), ("R", -1.0)):
        ls, lc = s * sign, c * sign          # the right leg is half a cycle behind the left
        pose["UpperLeg." + side] = swing(26.0 * ls)
        pose["LowerLeg." + side] = bend_back(6.0 + 45.0 * max(0.0, lc) ** 1.5)   # knee folds while the leg swings through
        pose["Foot." + side] = qeuler(x=-12.0 * ls)                             # toes up at heel strike
        pose["UpperArm." + side] = qmul(qeuler(z=6.0 * sign), swing(-22.0 * ls))
        pose["LowerArm." + side] = swing(18.0 + 14.0 * max(0.0, -ls))
    bob = 0.022 * math.cos(2.0 * p) - 0.008  # lowest when both feet are down
    return pose, (0.0, bob, 0.0)


def _run(t):
    T = 0.7
    p = 2.0 * math.pi * t / T
    s, c = math.sin(p), math.cos(p)
    pose = {"Spine": lean(10.0), "Chest": qeuler(x=4.0, y=-10.0 * s), "Hips": qeuler(y=9.0 * s), "Head": lean(-10.0)}
    for side, sign in (("L", 1.0), ("R", -1.0)):
        ls, lc = s * sign, c * sign
        pose["UpperLeg." + side] = swing(8.0 + 44.0 * ls)
        pose["LowerLeg." + side] = bend_back(20.0 + 85.0 * max(0.0, lc) ** 1.2)
        pose["Foot." + side] = qeuler(x=-18.0 * ls)
        pose["UpperArm." + side] = qmul(qeuler(z=10.0 * sign), swing(-42.0 * ls))
        pose["LowerArm." + side] = swing(80.0)
    bob = 0.05 * abs(math.cos(p)) - 0.04     # push off, float, land
    return pose, (0.0, bob, 0.0)


def _jump(t):
    # crouch (0) -> push off (0.15) -> tucked in the air (0.45, held)
    def mix(a, b, f): return a + (b - a) * f
    def smooth(f): f = min(max(f, 0.0), 1.0); return f * f * (3 - 2 * f)
    if t < 0.15:
        f = smooth(t / 0.15)
        knee, hip, arm, spine, drop = mix(55, 5, f), mix(35, -5, f), mix(-35, 60, f), mix(20, -3, f), mix(-0.14, 0.0, f)
    else:
        f = smooth((t - 0.15) / 0.30)
        knee, hip, arm, spine, drop = mix(5, 75, f), mix(-5, 45, f), mix(60, 25, f), mix(-3, 10, f), 0.0
    pose = {"Spine": lean(spine), "Head": lean(-spine * 0.5)}
    for side, sign in (("L", 1.0), ("R", -1.0)):
        stagger = 1.0 if side == "L" else 0.75
        pose["UpperLeg." + side] = swing(hip * stagger)
        pose["LowerLeg." + side] = bend_back(knee * stagger)
        pose["Foot." + side] = qeuler(x=15.0)
        pose["UpperArm." + side] = qmul(qeuler(z=14.0 * sign), swing(arm))
        pose["LowerArm." + side] = swing(35.0)
    return pose, (0.0, drop, 0.0)


CLIPS = [("Idle", 3.0, True, _idle), ("Walk", 1.0, True, _walk), ("Run", 0.7, True, _run), ("Jump", 0.45, False, _jump)]


def sample_clip(fn, duration):
    """Keyframes at FPS, including the end frame (equal to frame 0 for loops)."""
    n = max(2, int(round(duration * FPS)) + 1)
    return [(duration * i / (n - 1), *fn(duration * i / (n - 1))) for i in range(n)]


# --------------------------------------------------------------------------- glTF writer
def write_glb(path):
    parts = build_parts()
    bin_data = bytearray()
    buffer_views, accessors = [], []

    def add_view(data, target=None):
        while len(bin_data) % 4: bin_data.append(0)
        view = {"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(data)}
        if target: view["target"] = target
        bin_data.extend(data)
        buffer_views.append(view)
        return len(buffer_views) - 1

    def add_accessor(fmt, comp_type, type_, values, count, target=None, minmax=None):
        view = add_view(struct.pack("<%d%s" % (len(values), fmt), *values), target)
        acc = {"bufferView": view, "componentType": comp_type, "count": count, "type": type_}
        if minmax: acc["min"], acc["max"] = minmax
        accessors.append(acc)
        return len(accessors) - 1

    # Flat-shaded vertex streams, one primitive per material.
    streams = {m[0]: {"pos": [], "nrm": [], "jnt": [], "idx": []} for m in MATERIALS}
    for part in parts:
        st = streams[part.material]
        joint = BONE_INDEX[part.bone]
        for poly in part.polys:
            pts = [part.verts[i] for i in poly]
            n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
            base = len(st["pos"])
            for p in pts:
                st["pos"].append(p)
                st["nrm"].append(n)
                st["jnt"].append(joint)
            for k in range(1, len(pts) - 1):   # fan-triangulate (CCW kept)
                st["idx"] += [base, base + k, base + k + 1]

    primitives = []
    for mi, (mname, _) in enumerate(MATERIALS):
        st = streams[mname]
        if not st["pos"]: continue
        flat = [c for p in st["pos"] for c in p]
        mins = [min(p[i] for p in st["pos"]) for i in range(3)]
        maxs = [max(p[i] for p in st["pos"]) for i in range(3)]
        a_pos = add_accessor("f", 5126, "VEC3", flat, len(st["pos"]), 34962, (mins, maxs))
        a_nrm = add_accessor("f", 5126, "VEC3", [c for n in st["nrm"] for c in n], len(st["nrm"]), 34962)
        a_jnt = add_accessor("H", 5123, "VEC4", [v for j in st["jnt"] for v in (j, 0, 0, 0)], len(st["jnt"]), 34962)
        a_wgt = add_accessor("f", 5126, "VEC4", [v for _ in st["jnt"] for v in (1.0, 0.0, 0.0, 0.0)], len(st["jnt"]), 34962)
        a_idx = add_accessor("H", 5123, "SCALAR", st["idx"], len(st["idx"]), 34963)
        primitives.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "JOINTS_0": a_jnt, "WEIGHTS_0": a_wgt},
                           "indices": a_idx, "material": mi})

    # Nodes: 0 = Hero (root), 1 = HeroMesh, 2.. = joints
    nodes = [{"name": "Hero", "children": [1, 2]}, {"name": "HeroMesh", "mesh": 0, "skin": 0}]
    joint_node = {}
    for i, (name, parent, pos) in enumerate(BONES):
        joint_node[name] = 2 + i
        local = sub(pos, BONE_POS[parent]) if parent else pos
        nodes.append({"name": name, "translation": list(local)})
    for name, parent, _ in BONES:
        if parent: nodes[joint_node[parent]].setdefault("children", []).append(joint_node[name])

    ibm = []
    for name, _, pos in BONES:   # inverse bind = translate(-head), column-major
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pos[0], -pos[1], -pos[2], 1]
    a_ibm = add_accessor("f", 5126, "MAT4", ibm, len(BONES))
    skins = [{"name": "HeroRig", "joints": [joint_node[b[0]] for b in BONES], "inverseBindMatrices": a_ibm, "skeleton": joint_node["Hips"]}]

    animations = []
    for clip_name, duration, _loop, fn in CLIPS:
        keys = sample_clip(fn, duration)
        a_time = add_accessor("f", 5126, "SCALAR", [k[0] for k in keys], len(keys), None,
                              ([keys[0][0]], [keys[-1][0]]))
        samplers, channels = [], []
        for name, _, pos in BONES:
            rots = []
            prev = None
            for _, pose, _off in keys:
                q = pose.get(name, (1.0, 0.0, 0.0, 0.0))
                if prev and dot(q[1:], prev[1:]) + q[0] * prev[0] < 0: q = tuple(-v for v in q)   # keep hemisphere
                prev = q
                rots += [q[1], q[2], q[3], q[0]]   # glTF stores x, y, z, w
            a_rot = add_accessor("f", 5126, "VEC4", rots, len(keys))
            samplers.append({"input": a_time, "output": a_rot, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": joint_node[name], "path": "rotation"}})
        trans = [c for _, _p, off in keys for c in add(BONE_POS["Hips"], off)]
        a_tr = add_accessor("f", 5126, "VEC3", trans, len(keys))
        samplers.append({"input": a_time, "output": a_tr, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": joint_node["Hips"], "path": "translation"}})
        animations.append({"name": clip_name, "samplers": samplers, "channels": channels})

    gltf = {
        "asset": {"version": "2.0", "generator": "Euphoria Engine euphoria_hero.py"},
        "scene": 0, "scenes": [{"name": "Scene", "nodes": [0]}],
        "nodes": nodes,
        "meshes": [{"name": "HeroMesh", "primitives": primitives}],
        "materials": [{"name": n, "pbrMetallicRoughness": {"baseColorFactor": list(srgb_to_linear(c)) + [1.0],
                                                          "metallicFactor": 0.0, "roughnessFactor": 0.85}} for n, c in MATERIALS],
        "skins": skins, "animations": animations,
        "accessors": accessors, "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_data)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(bin_data) % 4: bin_data.append(0)
    out = bytearray(b"glTF") + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(bin_data))
    out += struct.pack("<I", len(js)) + b"JSON" + js
    out += struct.pack("<I", len(bin_data)) + b"BIN\0" + bin_data
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f: f.write(out)
    tris = sum(accessors[p["indices"]]["count"] for p in primitives) // 3
    print("Wrote %s  (%d triangles, %d bones, clips: %s)" % (path, tris, len(BONES), ", ".join(c[0] for c in CLIPS)))


# --------------------------------------------------------------------------- Blender
def build_in_blender(export_path):
    import bpy
    from mathutils import Quaternion, Vector

    def G2B(v): return Vector((v[0], -v[2], v[1]))                  # glTF (Y up, +Z front) -> Blender (Z up, -Y front)
    def qG2B(q): return Quaternion((q[0], q[1], -q[3], q[2]))

    scene = bpy.context.scene
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for name in ("HeroMesh", "HeroRig"):
        if name in bpy.data.objects: bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    for clip in CLIPS:
        if clip[0] in bpy.data.actions: bpy.data.actions.remove(bpy.data.actions[clip[0]])

    # Armature
    arm = bpy.data.armatures.new("HeroRig")
    rig = bpy.data.objects.new("HeroRig", arm)
    scene.collection.objects.link(rig)
    rig.show_in_front = True
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, pos in BONES:
        eb = arm.edit_bones.new(name)
        eb.head = G2B(pos)
        eb.tail = G2B(bone_tail(name))
        if parent: eb.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    rest = {b.name: b.matrix_local.to_quaternion() for b in arm.bones}

    # Mesh: one object, materials by polygon, one vertex group per bone (rigid weights)
    verts, faces, face_mat, groups = [], [], [], {b[0]: [] for b in BONES}
    for part in build_parts():
        base = len(verts)
        verts += [tuple(G2B(v)) for v in part.verts]
        groups[part.bone] += range(base, base + len(part.verts))
        for poly in part.polys:
            faces.append([base + i for i in poly])
            face_mat.append(MAT_INDEX[part.material])
    me = bpy.data.meshes.new("HeroMesh")
    me.from_pydata(verts, [], faces)
    me.update()
    for name, color in MATERIALS:
        mat = bpy.data.materials.get("Hero_" + name) or bpy.data.materials.new("Hero_" + name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*srgb_to_linear(color), 1.0)
            bsdf.inputs["Roughness"].default_value = 0.85
        mat.diffuse_color = (*srgb_to_linear(color), 1.0)
        me.materials.append(mat)
    me.polygons.foreach_set("material_index", face_mat)
    me.polygons.foreach_set("use_smooth", [False] * len(faces))    # faceted look
    obj = bpy.data.objects.new("HeroMesh", me)
    scene.collection.objects.link(obj)
    obj.parent = rig
    for bone, idx in groups.items():
        obj.vertex_groups.new(name=bone).add(list(idx), 1.0, "REPLACE")
    obj.modifiers.new("Armature", "ARMATURE").object = rig

    # Actions, each kept on a muted NLA track so the exporter writes all of them
    scene.render.fps = FPS
    rig.animation_data_create()
    for clip_name, duration, _loop, fn in CLIPS:
        act = bpy.data.actions.new(clip_name)
        act.use_fake_user = True
        rig.animation_data.action = act
        for t, pose, off in sample_clip(fn, duration):
            frame = 1 + t * FPS
            for pb in rig.pose.bones:
                pb.rotation_mode = "QUATERNION"
                R = rest[pb.name]
                pb.rotation_quaternion = R.inverted() @ qG2B(pose.get(pb.name, (1.0, 0.0, 0.0, 0.0))) @ R
                pb.keyframe_insert("rotation_quaternion", frame=frame, group=pb.name)
                if pb.name == "Hips":
                    pb.location = R.inverted() @ G2B(off)
                    pb.keyframe_insert("location", frame=frame, group=pb.name)
        track = rig.animation_data.nla_tracks.new()
        track.name = clip_name
        track.strips.new(clip_name, 1, act)
        track.mute = True
    rig.animation_data.action = bpy.data.actions["Idle"]
    scene.frame_start, scene.frame_end = 1, int(3.0 * FPS) + 1

    if export_path:
        bpy.ops.object.select_all(action="DESELECT")
        rig.select_set(True)
        obj.select_set(True)
        args = dict(filepath=export_path, export_format="GLB", use_selection=True,
                    export_animations=True, export_skins=True, export_yup=True)
        try:
            bpy.ops.export_scene.gltf(export_animation_mode="ACTIONS", **args)   # Blender 4.x
        except TypeError:
            bpy.ops.export_scene.gltf(**args)                                     # Blender 3.6
        print("Exported", export_path)


def _default_path():
    here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
    repo = os.path.normpath(os.path.join(here, "..", ".."))
    if os.path.isdir(os.path.join(repo, "projects", "Demo")):
        return os.path.join(repo, "projects", "Demo", "Models", "Hero", "Hero.glb")
    return os.path.join(os.path.expanduser("~"), "Hero.glb")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    try:
        import bpy  # noqa: F401
        in_blender = True
    except ImportError:
        in_blender = False
    if in_blender:
        build_in_blender(argv[0] if argv else _default_path())
    else:
        write_glb(argv[0] if argv else _default_path())
