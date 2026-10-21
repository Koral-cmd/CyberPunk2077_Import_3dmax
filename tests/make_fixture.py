#!/usr/bin/env python3
# ============================================================
#  Synthetic test data in the exact shapes WolvenKit writes:
#    *.ent.json / *.app.json / *.rig.json   (RedJsonSerializer layout)
#    *.glb   mesh export (MeshOnly: flat joints at the bind pose + IBMs)
#    *.anims.glb   animset export (Armature + skin extras + animations)
#  plus truth.json: game-space answers computed here, independently
#  of the C++ code (column-vector math, own quaternion code).
#
#  usage: make_fixture.py <outdir>
# ============================================================
import json, math, os, struct, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "fixture"

# ---------------- math (column vectors, 4x4) ----------------
def qaxis(ax, deg):
    n = math.sqrt(sum(a * a for a in ax)); a = math.radians(deg) / 2
    return (ax[0] / n * math.sin(a), ax[1] / n * math.sin(a), ax[2] / n * math.sin(a), math.cos(a))

def qmat(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]

def trs(t, q, s=(1, 1, 1)):
    R = qmat(q)
    return [[R[r][0] * s[0], R[r][1] * s[1], R[r][2] * s[2], t[r]] for r in range(3)] + [[0, 0, 0, 1]]

def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

def inv(m):  # rigid/affine 4x4 inverse via 3x3
    a = [row[:3] for row in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    c = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3] - a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det
          for j in range(3)] for i in range(3)]
    t = [m[0][3], m[1][3], m[2][3]]
    ti = [-sum(c[r][k] * t[k] for k in range(3)) for r in range(3)]
    return [c[0] + [ti[0]], c[1] + [ti[1]], c[2] + [ti[2]], [0, 0, 0, 1]]

def apply(m, p):
    return [sum(m[r][k] * p[k] for k in range(3)) + m[r][3] for r in range(3)]

def slerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0: b = tuple(-x for x in b); d = -d
    if d > 0.9995:
        r = [x + (y - x) * t for x, y in zip(a, b)]; n = math.sqrt(sum(x * x for x in r)); return tuple(x / n for x in r)
    th = math.acos(d); s = math.sin(th)
    return tuple(x * math.sin((1 - t) * th) / s + y * math.sin(t * th) / s for x, y in zip(a, b))

# WolvenKit's conversions (RigTools.cs / MeshTools.cs)
def g2gl_p(p): return (p[0], p[2], -p[1])
def g2gl_q(q): return (q[0], q[2], -q[1], q[3])
def g2gl_m(m):
    # similarity with C: game -> glTF, C = [[1,0,0],[0,0,1],[0,-1,0]]
    C = [[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]]
    Ci = [[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]]
    return mul(mul(C, m), Ci)

def colmajor(m): return [m[r][c] for c in range(4) for r in range(4)]

# ---------------- RED JSON helpers ----------------
def cname(s): return {"$type": "CName", "$storage": "string", "$value": s}
def ref(p): return {"DepotPath": {"$type": "ResourcePath", "$storage": "string", "$value": p}, "Flags": "Default"}
def wrap(root): return {"Header": {"WolvenKitVersion": "8.x-fixture", "WKitJsonVersion": "0.0.9", "DataType": "CR2W"},
                        "Data": {"Version": 195, "BuildVersion": 0, "RootChunk": root, "EmbeddedFiles": []}}
_hid = [0]
def handle(data):
    _hid[0] += 1
    return {"HandleId": str(_hid[0]), "Data": data}
def qs(t, q):
    return {"$type": "QsTransform", "Translation": {"$type": "Vector4", "X": t[0], "Y": t[1], "Z": t[2], "W": 0},
            "Rotation": {"$type": "Quaternion", "i": q[0], "j": q[1], "k": q[2], "r": q[3]},
            "Scale": {"$type": "Vector4", "X": 1, "Y": 1, "Z": 1, "W": 1}}
def fixed(v): return {"$type": "FixedPoint", "Bits": int(round(v * 131072))}
def wt(t=(0, 0, 0), q=(0, 0, 0, 1)):
    return {"$type": "WorldTransform", "Position": {"$type": "WorldPosition", "x": fixed(t[0]), "y": fixed(t[1]), "z": fixed(t[2])},
            "Orientation": {"$type": "Quaternion", "i": q[0], "j": q[1], "k": q[2], "r": q[3]}}

def write_json(rel, root):
    p = os.path.join(OUT, rel.replace("\\", "/") + ".json")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f: json.dump(wrap(root), f, indent=1)

# ---------------- rigs (game space) ----------------
BODY = [  # name, parent, ref (t, q), apose (t, q)
    ("Root", -1, ((0, 0, 0), (0, 0, 0, 1)), ((0, 0, 0), (0, 0, 0, 1))),
    ("Hips", 0, ((0, 0, 1.0), qaxis((0, 0, 1), 30)), ((0, 0, 1.0), qaxis((0, 0, 1), 30))),
    ("Spine", 1, ((0, 0.05, 0.2), qaxis((1, 0, 0), 20)), ((0, 0.05, 0.2), qaxis((1, 0, 0), 25))),
    ("Head", 2, ((0, 0, 0.5), qaxis((0, 1, 0), -15)), ((0, 0, 0.5), qaxis((0, 1, 0), -15))),
    ("WeaponRight", 2, ((0.4, 0, 0), qaxis((0, 0, 1), 90)), ((0.4, 0, 0), qaxis((0, 0, 1), 90))),
]
DANGLE = [
    ("Root", -1, ((0, 0, 0), (0, 0, 0, 1)), None),
    ("Head", 0, ((0, 0, 1.7), (0, 0, 0, 1)), None),           # differs from body on purpose: body wins
    ("dyng_hair", 1, ((0, -0.1, 0.1), qaxis((1, 1, 0), 40)), None),
]
# deformation rig: listed BEFORE the base rig in the entity and with deliberately different
# transforms for the shared bones - the base ("root", its controlBinding target) must win.
DEFORM = [
    ("Root", -1, ((0, 0, 0), (0, 0, 0, 1)), ((0, 0, 0), (0, 0, 0, 1))),
    ("Hips", 0, ((5, 5, 5), qaxis((1, 0, 0), 70)), ((5, 5, 5), qaxis((1, 0, 0), 70))),
    ("Spine", 1, ((0, 3, 0), (0, 0, 0, 1)), ((0, 3, 0), (0, 0, 0, 1))),
    ("deform_belly", 2, ((0, 0.1, 0.05), (0, 0, 0, 1)), ((0, 0.1, 0.05), (0, 0, 0, 1))),
] + [("deform_%d" % i, 2, ((0.01 * i, 0, 0), (0, 0, 0, 1)), ((0.01 * i, 0, 0), (0, 0, 0, 1))) for i in range(6)]
# (more bones than the base rig, like man_massive_deformations.rig: size must not decide the order)
GUN = [
    ("Root", -1, ((0, 0, 0), (0, 0, 0, 1)), None),
    ("Trigger", 0, ((0.1, 0, 0.02), qaxis((0, 1, 0), 10)), None),
]

def rig_json(path, bones):
    root = {"$type": "animRig", "boneNames": [cname(b[0]) for b in bones],
            "boneParentIndexes": [b[1] for b in bones],
            "boneTransforms": [qs(*b[2]) for b in bones], "trackNames": []}
    if all(b[3] is not None for b in bones):
        root["aPoseLS"] = [qs(*b[3]) for b in bones]
        root["aPoseMS"] = []
    write_json(path, root)

def worlds(bones, use_apose):
    w = []
    for name, par, ref_, ap in bones:
        t, q = (ap if (use_apose and ap) else ref_)
        loc = trs(t, q)
        w.append(mul(w[par], loc) if par >= 0 else loc)
    return w

# ---------------- glTF writer ----------------
class Glb:
    def __init__(self):
        self.bin = bytearray(); self.views = []; self.accs = []
        self.doc = {"asset": {"version": "2.0", "generator": "fixture"}, "scene": 0, "scenes": [{"nodes": []}], "nodes": []}
    def acc(self, fmt, comps, ctype, vals, count, typ):
        while len(self.bin) % 4: self.bin.append(0)
        off = len(self.bin)
        self.bin += struct.pack("<%d%s" % (len(vals), fmt), *vals)
        self.views.append({"buffer": 0, "byteOffset": off, "byteLength": len(self.bin) - off})
        a = {"bufferView": len(self.views) - 1, "componentType": ctype, "count": count, "type": typ}
        self.accs.append(a); return len(self.accs) - 1
    def f(self, vals, comps):
        typ = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}[comps]
        return self.acc("f", comps, 5126, vals, len(vals) // comps, typ)
    def node(self, n, root=False):
        self.doc["nodes"].append(n); i = len(self.doc["nodes"]) - 1
        if root: self.doc["scenes"][0]["nodes"].append(i)
        return i
    def save(self, rel):
        self.doc["bufferViews"] = self.views; self.doc["accessors"] = self.accs
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.doc).encode()
        while len(js) % 4: js += b" "
        while len(self.bin) % 4: self.bin.append(0)
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        p = os.path.join(OUT, rel.replace("\\", "/")); os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as fh:
            fh.write(struct.pack("<4sII", b"glTF", 2, total))
            fh.write(struct.pack("<II", len(js), 0x4E4F534A)); fh.write(js)
            fh.write(struct.pack("<II", len(self.bin), 0x004E4942)); fh.write(self.bin)

def mesh_glb(rel, stem, joints, bind_world, submeshes, materials):
    """joints: names; bind_world: game 4x4 per joint; submeshes: list of (chunk, verts[(pos, [(j, w)])], tris)"""
    g = Glb()
    arm = g.node({"name": "Armature", "children": []}, root=True)
    jn = []
    for name, W in zip(joints, bind_world):
        gw = g2gl_m(W)
        # TRS of the glTF world (joints are flat under the armature, like WolvenKit's orphan rig)
        t = (gw[0][3], gw[1][3], gw[2][3])
        q = rot_to_quat(gw)
        jn.append(g.node({"name": name, "translation": list(t), "rotation": list(q)}))
        g.doc["nodes"][arm]["children"].append(jn[-1])
    ibm = []
    for W in bind_world:
        ibm += colmajor(inv(g2gl_m(W)))
    g.doc["skins"] = [{"joints": jn, "inverseBindMatrices": g.f(ibm, 16), "name": "Armature"}]
    g.doc["meshes"] = []
    for chunk, verts, tris in submeshes:
        pos, nrm, uv, jj, ww = [], [], [], [], []
        for (p, inf) in verts:
            pos += g2gl_p(p); nrm += g2gl_p((0, 0, 1)); uv += [p[0] * 0.5, p[2] * 0.5]
            js = [0, 0, 0, 0]; wsum = [0.0, 0.0, 0.0, 0.0]
            for k, (j, w) in enumerate(inf): js[k] = j; wsum[k] = w
            jj += js; ww += wsum
        prim = {"attributes": {"POSITION": g.f(pos, 3), "NORMAL": g.f(nrm, 3), "TEXCOORD_0": g.f(uv, 2),
                               "JOINTS_0": g.acc("H", 4, 5123, jj, len(verts), "VEC4"),
                               "WEIGHTS_0": g.f(ww, 4)},
                "indices": g.acc("H", 1, 5123, tris, len(tris), "SCALAR")}
        name = "%s_submesh_%02d_LOD_1" % (stem, chunk)
        g.doc["meshes"].append({"name": name, "primitives": [prim], "extras": {"materialNames": materials}})
        g.node({"name": name, "mesh": len(g.doc["meshes"]) - 1, "skin": 0}, root=True)
    g.save(rel)

def rot_to_quat(m):
    R = [row[:3] for row in m[:3]]
    tr = R[0][0] + R[1][1] + R[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1) * 2; return ((R[2][1] - R[1][2]) / s, (R[0][2] - R[2][0]) / s, (R[1][0] - R[0][1]) / s, 0.25 * s)
    if R[0][0] > R[1][1] and R[0][0] > R[2][2]:
        s = math.sqrt(1 + R[0][0] - R[1][1] - R[2][2]) * 2; return (0.25 * s, (R[0][1] + R[1][0]) / s, (R[0][2] + R[2][0]) / s, (R[2][1] - R[1][2]) / s)
    if R[1][1] > R[2][2]:
        s = math.sqrt(1 + R[1][1] - R[0][0] - R[2][2]) * 2; return ((R[0][1] + R[1][0]) / s, 0.25 * s, (R[1][2] + R[2][1]) / s, (R[0][2] - R[2][0]) / s)
    s = math.sqrt(1 + R[2][2] - R[0][0] - R[1][1]) * 2; return ((R[0][2] + R[2][0]) / s, (R[1][2] + R[2][1]) / s, 0.25 * s, (R[1][0] - R[0][1]) / s)

# ---------------- files ----------------
CH = "base\\characters\\entities\\test\\npc_test.ent"
APP = "base\\characters\\appearances\\test\\npc_test.app"
PART = "base\\characters\\garment\\test\\hair_part.ent"
BODY_RIG = "base\\characters\\base_entities\\test\\body.rig"
DANGLE_RIG = "base\\characters\\base_entities\\test\\dangle.rig"
DEFORM_RIG = "base\\characters\\base_entities\\test\\deformations_rig\\body_deformations.rig"
BODY_MESH = "base\\characters\\common\\test\\body.mesh"
HAIR_MESH = "base\\characters\\common\\test\\hair.mesh"
ANIMS = "base\\animations\\npc\\test\\npc_test.anims"
GUN_ENT = "base\\weapons\\test\\w_test_gun.ent"
GUN_APP = "base\\weapons\\test\\w_test_gun.app"
GUN_RIG = "base\\weapons\\test\\w_test_gun.rig"
GUN_MESH = "base\\weapons\\test\\w_test_gun.mesh"

rig_json(BODY_RIG, BODY)
rig_json(DANGLE_RIG, DANGLE)
rig_json(DEFORM_RIG, DEFORM)
rig_json(GUN_RIG, GUN)

anim_root = handle({"$type": "entAnimatedComponent", "name": cname("root"), "rig": ref(BODY_RIG),
                    "animations": {"$type": "animAnimSetup", "gameplay": [{"$type": "animAnimSetupEntry", "priority": 0, "animSet": ref(ANIMS)}], "cinematics": []}})
slots = {"$type": "entSlotComponent", "name": cname("Slots"),
         "parentTransform": handle({"$type": "entHardTransformBinding", "bindName": cname("root"), "slotName": cname("")}),
         "slots": [{"$type": "entSlot", "slotName": cname("WeaponRight"), "boneName": cname("WeaponRight"),
                    "relativePosition": {"$type": "Vector3", "X": 0.01, "Y": 0.02, "Z": 0.0},
                    "relativeRotation": {"$type": "Quaternion", "i": 0, "j": 0, "k": 0, "r": 1}}]}
write_json(CH, {"$type": "entEntityTemplate", "defaultAppearance": cname("default"),
                "appearances": [{"$type": "entTemplateAppearance", "name": cname("default"), "appearanceResource": ref(APP), "appearanceName": cname("npc_default")},
                                {"$type": "entTemplateAppearance", "name": cname("other"), "appearanceResource": ref(APP), "appearanceName": cname("npc_other")}],
                "includes": [],
                "compiledData": {"BufferId": "0", "Flags": 0, "Type": "WolvenKit.RED4.Archive.Buffer.RedPackage, WolvenKit.RED4",
                                 "Data": {"Version": 4, "Sections": 6, "CruidIndex": -1, "CruidDict": {},
                                          "Chunks": [{"$type": "entEntity"},
                                                     {"$type": "entAnimatedComponent", "name": cname("deformations"), "rig": ref(DEFORM_RIG),
                                                      "animations": {"gameplay": [], "cinematics": []},
                                                      "controlBinding": handle({"$type": "entComponentBinding", "bindName": cname("root")})},
                                                     anim_root["Data"], slots]}}})
body_comp = {"$type": "entSkinnedMeshComponent", "name": cname("t0_body"), "mesh": ref(BODY_MESH), "meshAppearance": cname("default"),
             "chunkMask": 18446744073709551613,  # every chunk but #1
             "skinning": handle({"$type": "entSkinningBinding", "bindName": cname("root")})}
write_json(APP, {"$type": "appearanceAppearanceResource", "appearances": [
    handle({"$type": "appearanceAppearanceDefinition", "name": cname("npc_default"), "components": [],
            "compiledData": {"BufferId": "1", "Data": {"Chunks": [body_comp]}},
            "partsValues": [{"$type": "appearanceAppearancePart", "resource": ref(PART)}],
            "partsOverrides": [{"$type": "appearanceAppearancePartOverrides", "partResource": ref(PART),
                                "componentsOverrides": [{"$type": "appearancePartComponentOverrides", "componentName": cname("hair"),
                                                         "meshAppearance": cname("blonde"), "chunkMask": "18446744073709551615"}]}]}),
    handle({"$type": "appearanceAppearanceDefinition", "name": cname("npc_other"), "components": [], "partsValues": []})]})
write_json(PART, {"$type": "entEntityTemplate", "appearances": [], "compiledData": {"Data": {"Chunks": [
    {"$type": "entAnimatedComponent", "name": cname("dangle"), "rig": ref(DANGLE_RIG), "animations": {"gameplay": [], "cinematics": []},
     "parentTransform": handle({"$type": "entHardTransformBinding", "bindName": cname("root")})},
    {"$type": "entSkinnedMeshComponent", "name": cname("hair"), "mesh": ref(HAIR_MESH), "meshAppearance": cname("brown"),
     "chunkMask": 0 + 18446744073709551615, "skinning": {"HandleId": "900", "Data": {"$type": "entSkinningBinding", "bindName": cname("dangle")}}}]}}})

# weapon
write_json(GUN_ENT, {"$type": "entEntityTemplate", "defaultAppearance": cname("default"),
                     "appearances": [{"$type": "entTemplateAppearance", "name": cname("default"), "appearanceResource": ref(GUN_APP), "appearanceName": cname("default")}],
                     "compiledData": {"Data": {"Chunks": [
                         {"$type": "entAnimatedComponent", "name": cname("root"), "rig": ref(GUN_RIG), "animations": {"gameplay": [], "cinematics": []}}]}}})
write_json(GUN_APP, {"$type": "appearanceAppearanceResource", "appearances": [
    handle({"$type": "appearanceAppearanceDefinition", "name": cname("default"), "compiledData": {"Data": {"Chunks": [
        {"$type": "entSkinnedMeshComponent", "name": cname("gun_body"), "mesh": ref(GUN_MESH), "chunkMask": 18446744073709551615,
         "skinning": handle({"$type": "entSkinningBinding", "bindName": cname("root")})}]}}})]})

# meshes: bind pose = A-pose world of the body rig
bw = worlds(BODY, True)
names = [b[0] for b in BODY]
J = ["Hips", "Spine", "Head"]
JW = [bw[names.index(n)] for n in J]
body_sub0 = [((0.1, 0, 1.0), [(0, 1.0)]), ((0.1, 0.1, 1.2), [(0, 0.5), (1, 0.5)]), ((0, 0.1, 1.7), [(2, 1.0)]),
             ((0.1, 0, 1.0), [(0, 1.0)])]   # last vertex duplicates the first (UV seam) -> welded
body_sub1 = [((0.3, 0, 1.0), [(0, 1.0)]), ((0.3, 0.1, 1.0), [(0, 1.0)]), ((0.3, 0.1, 1.1), [(1, 1.0)])]
mesh_glb(BODY_MESH[:-5] + ".glb", "body", J, JW,
         [(0, body_sub0, [0, 1, 2, 3, 1, 2]), (1, body_sub1, [0, 1, 2])], ["mat_skin", "mat_skin_alt"])

dw = worlds(DANGLE, False)   # dangle bone: local from the dangle rig on top of the BODY head
head_w = bw[names.index("Head")]
hair_w = mul(head_w, trs(*DANGLE[2][2]))
mesh_glb(HAIR_MESH[:-5] + ".glb", "hair", ["Head", "dyng_hair"], [head_w, hair_w],
         [(0, [((0, 0, 1.75), [(0, 1.0)]), ((0, -0.1, 1.8), [(1, 1.0)]), ((0.05, -0.1, 1.8), [(0, 0.3), (1, 0.7)])], [0, 1, 2])], ["mat_hair"])

gw = worlds(GUN, False)
mesh_glb(GUN_MESH[:-5] + ".glb", "gun", ["Root", "Trigger"], gw,
         [(0, [((0, 0, 0), [(0, 1.0)]), ((0.3, 0, 0), [(0, 1.0)]), ((0.1, 0, 0.03), [(1, 1.0)])], [0, 1, 2])], ["mat_gun"])

# animset
g = Glb()
arm = g.node({"name": "Armature", "children": []}, root=True)
nodes = []
for i, (name, par, ref_, ap) in enumerate(BODY):
    t, q = ap
    n = g.node({"name": name, "translation": list(g2gl_p(t)), "rotation": list(g2gl_q(q))})
    nodes.append(n)
    if par < 0: g.doc["nodes"][arm]["children"].append(n)
    else: g.doc["nodes"][nodes[par]].setdefault("children", []).append(n)
g.doc["skins"] = [{"name": "Armature", "joints": nodes, "extras": {"rigPath": BODY_RIG, "boneNames": names,
                                                                   "boneParentIndexes": [b[1] for b in BODY], "trackNames": []}}]
hips_q = [qaxis((0, 0, 1), a) for a in (30, 60, 90)]
idle = {"name": "idle", "channels": [], "samplers": [], "extras": {"AnimationType": "Normal", "RootMotionType": "Unknown"}}
idle["samplers"].append({"input": g.f([0, 0.5, 1.0], 1), "output": g.f(sum([list(g2gl_q(q)) for q in hips_q], []), 4), "interpolation": "LINEAR"})
idle["channels"].append({"sampler": 0, "target": {"node": nodes[1], "path": "rotation"}})
idle["samplers"].append({"input": g.f([0, 1.0], 1), "output": g.f(list(g2gl_p((0, 0, 0))) + list(g2gl_p((0, 1, 0))), 3), "interpolation": "LINEAR"})
idle["channels"].append({"sampler": 1, "target": {"node": nodes[0], "path": "translation"}})
wave = {"name": "wave", "channels": [{"sampler": 0, "target": {"node": nodes[3], "path": "translation"}}],
        "samplers": [{"input": g.f([0, 0.2, 0.4], 1), "output": g.f(list(g2gl_p((0, 0, 0.5))) + list(g2gl_p((0, 0, 0.6))) + list(g2gl_p((0, 0, 0.6))), 3), "interpolation": "STEP"}]}
g.doc["animations"] = [idle, wave]
g.save(ANIMS + ".glb")

# ---------------- truth ----------------
# idle at t = 0.5: Hips rotation = 60 deg about Z, Root translation = (0, 0.5, 0); others A-pose
def anim_world(t):
    locs = []
    for i, (name, par, ref_, ap) in enumerate(BODY):
        tt, q = ap
        if name == "Hips": q = slerp(hips_q[0], hips_q[1], t / 0.5) if t <= 0.5 else slerp(hips_q[1], hips_q[2], (t - 0.5) / 0.5)
        if name == "Root": tt = (0, t, 0)
        locs.append(trs(tt, q))
    w = []
    for i, (name, par, _, _) in enumerate(BODY):
        w.append(mul(w[par], locs[i]) if par >= 0 else locs[i])
    return w

truth = {"files": {"character": CH, "weapon": GUN_ENT, "anims": ANIMS},
         "rest_apose": {n: [bw[i][r][c] for r in range(3) for c in range(4)] for i, n in enumerate(names)},
         "rest_reference": {n: [w[r][c] for r in range(3) for c in range(4)] for n, w in zip(names, worlds(BODY, False))},
         "deform": []}
for tq in (0.25, 0.5):
    aw = anim_world(tq)
    for (p, inf) in body_sub0[:3]:
        out = [0, 0, 0]
        for (j, w) in inf:
            jname = J[j]
            m = mul(aw[names.index(jname)], inv(JW[j]))
            q = apply(m, p)
            out = [o + w * x for o, x in zip(out, q)]
        truth["deform"].append({"t": tq, "rest": list(p), "deformed": out})
truth["weapon_root_world"] = [x for row in mul(bw[names.index("WeaponRight")], trs((0.01, 0.02, 0), (0, 0, 0, 1)))[:3] for x in row]
with open(os.path.join(OUT, "truth.json"), "w") as f: json.dump(truth, f, indent=1)
print("fixture written to", OUT)
