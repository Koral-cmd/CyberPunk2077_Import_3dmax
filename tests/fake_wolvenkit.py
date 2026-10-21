#!/usr/bin/env python3
# ============================================================
#  Stand-in for WolvenKit.CLI in the tests: same commands and
#  options the plugin uses, served from a fixture folder
#  ($FAKE_WK_STORE) instead of the game archives.
#    archive <dir> --list -r <rx>
#    unbundle <dirs> -o <out> -r <rx>
#    convert serialize <files...>
#    uncook <dirs> -o <out> -r <rx> --mesh-export-type MeshOnly [...]
#  Every call is appended to $FAKE_WK_STORE/calls.txt.
# ============================================================
import os, re, shutil, sys

store = os.environ["FAKE_WK_STORE"]
args = sys.argv[1:]
with open(os.path.join(store, "calls.txt"), "a") as f:
    f.write(" ".join(args) + "\n")

def opt(name, short=None):
    for i, a in enumerate(args):
        if a in (name, short) and i + 1 < len(args):
            return args[i + 1]
    return None

def depots():
    out = []
    for root, _, files in os.walk(store):
        for fn in files:
            p = os.path.join(root, fn)
            rel = os.path.relpath(p, store).replace("/", "\\")
            if rel.endswith(".json") and not rel.endswith("truth.json"):
                out.append(rel[:-5])
            elif rel.endswith(".anims.glb"):
                out.append(rel[:-4])
            elif rel.endswith(".glb"):
                out.append(rel[:-4] + ".mesh")
    return sorted(set(out))

def rx():
    r = opt("--regex", "-r")
    flags = 0
    if r.startswith("(?i)"):
        r = r[4:]; flags = re.I
    return re.compile(r, flags)

def copy(src_rel, out_dir, dst_rel=None):
    src = os.path.join(store, src_rel.replace("\\", "/"))
    if not os.path.exists(src):
        return False
    dst = os.path.join(out_dir, (dst_rel or src_rel).replace("\\", "/"))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    return True

print("[00:00:00 INF] fake WolvenKit " + args[0])
cmd = args[0]
if cmd == "archive":
    r = rx()
    for d in depots():
        if r.search(d):
            print(d)
elif cmd == "unbundle":
    r = rx(); out = opt("--outpath", "-o")
    for d in depots():
        if r.search(d) and os.path.exists(os.path.join(store, d.replace("\\", "/") + ".json")):
            p = os.path.join(out, d.replace("\\", "/")); os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(b"CR2W-fake")
elif cmd == "convert" and args[1] == "serialize":
    for f in args[2:]:
        norm = f.replace("\\", "/")
        i = norm.find("base/")
        rel = norm[i:]
        shutil.copyfile(os.path.join(store, rel + ".json"), f + ".json")
elif cmd == "uncook":
    r = rx(); out = opt("--outpath", "-o")
    assert "MeshOnly" in args
    for d in depots():
        if not r.search(d):
            continue
        name = d[:-5] + ".glb" if d.endswith(".mesh") else d + ".glb"
        copy(name, out)
else:
    print("unknown command", file=sys.stderr); sys.exit(2)
