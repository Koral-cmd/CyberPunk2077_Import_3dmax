#include "red.h"
#include "util.h"

#include <functional>
#include <set>

namespace cp77 {
namespace red {

using json::Value;

bool Doc::load(const std::string& path, std::string& error) {
    std::string text;
    if (!ReadFile(path, text)) { error = "cannot read " + path; return false; }
    if (!loadText(text, error)) { error = path + ": " + error; return false; }
    return true;
}

bool Doc::loadText(const std::string& text, std::string& error) {
    handles_.clear();
    if (!json::Parse(text, doc, &error)) return false;
    index(doc);
    return true;
}

void Doc::index(const Value& v) {
    if (v.isObject()) {
        const Value& id = v["HandleId"];
        if (!id.isNull() && v.has("Data")) handles_[id.str] = &v["Data"];
        for (const auto& kv : v.obj) index(kv.second);
    } else if (v.isArray()) {
        for (const auto& e : v.arr) index(e);
    }
}

const Value& Doc::root() const {
    const Value& rc = doc["Data"]["RootChunk"];
    if (rc.isObject()) return rc;
    const Value& rc2 = doc["RootChunk"];
    if (rc2.isObject()) return rc2;
    return doc;
}

std::string Doc::rootType() const { return TypeOf(root()); }

const Value& Doc::deref(const Value& v) const {
    if (!v.isObject()) return v;
    if (v.has("HandleRefId")) {
        auto it = handles_.find(v["HandleRefId"].str);
        return it != handles_.end() ? *it->second : json::NullValue();
    }
    if (v.has("HandleId") && v.has("Data")) return v["Data"];
    return v;
}

std::string TypeOf(const Value& v) { return v["$type"].asString(); }

std::string CName(const Value& v) {
    if (v.isString()) return v.str == "None" ? std::string() : v.str;
    if (v.isObject()) {
        const Value& val = v["$value"];
        if (val.isString() || val.isNumber()) {
            const std::string& st = v["$storage"].asString();
            if (st == "uint64" || st == "hash") return "#" + val.str;   // unresolved hash
            return val.str == "None" ? std::string() : val.str;
        }
        if (v.has("value")) return CName(v["value"]);
    }
    if (v.isNumber()) return v.str == "0" ? std::string() : "#" + v.str;
    return std::string();
}

std::string DepotPath(const Value& v) {
    if (v.isString()) return NormalizeDepotPath(v.str);
    if (v.isObject()) {
        if (v.has("DepotPath")) return DepotPath(v["DepotPath"]);
        const Value& val = v["$value"];
        if (val.isString()) {
            const std::string& st = v["$storage"].asString();
            if (st == "uint64") return val.str == "0" ? std::string() : "#" + val.str;
            return NormalizeDepotPath(val.str);
        }
        if (val.isNumber()) return val.str == "0" ? std::string() : "#" + val.str;
    }
    if (v.isNumber()) return v.str == "0" ? std::string() : "#" + v.str;
    return std::string();
}

Vec3 ReadVec3(const Value& v) {
    return Vec3(v["X"].asDouble(), v["Y"].asDouble(), v["Z"].asDouble());
}

Quatd ReadQuat(const Value& v) {
    if (v.has("r") || v.has("i")) return GameQuat(v["i"].asDouble(), v["j"].asDouble(), v["k"].asDouble(), v["r"].asDouble(1.0));
    if (v.has("W")) return Quatd(v["X"].asDouble(), v["Y"].asDouble(), v["Z"].asDouble(), v["W"].asDouble(1.0));
    return Quatd();
}

double ReadFixed(const Value& v) {
    if (v.isObject()) {
        if (v.has("Bits")) return static_cast<double>(v["Bits"].asInt(0)) / 131072.0;   // 15.17 fixed point
        if (v.has("$value")) return v["$value"].asDouble();
    }
    return v.asDouble();
}

Xf ReadQsTransform(const Value& v) {
    const Vec3 t = ReadVec3(v["Translation"]);
    const Quatd q = ReadQuat(v["Rotation"]);
    Vec3 s(1, 1, 1);
    const Value& sv = v["Scale"];
    if (sv.isObject()) {
        s = ReadVec3(sv);
        if (s.x == 0 && s.y == 0 && s.z == 0) s = Vec3(1, 1, 1);
    }
    return FromTRS(t, q, s);
}

Xf ReadWorldTransform(const Value& v) {
    const Value& p = v["Position"];
    const Vec3 t(ReadFixed(p["x"]), ReadFixed(p["y"]), ReadFixed(p["z"]));
    const Value& o = v["Orientation"];
    const Quatd q = o.isObject() ? ReadQuat(o) : Quatd();
    return FromTRS(t, q, Vec3(1, 1, 1));
}

// ---- rig ---------------------------------------------------------------
std::vector<Xf> Rig::world(bool useAPose) const {
    const std::vector<Xf>& loc = (useAPose && aposeLocal.size() == names.size()) ? aposeLocal : local;
    std::vector<Xf> w(names.size());
    std::vector<char> done(names.size(), 0);
    std::function<void(size_t, int)> solve = [&](size_t i, int depth) {
        if (done[i]) return;
        const int p = parents[i];
        if (p >= 0 && static_cast<size_t>(p) < names.size() && static_cast<size_t>(p) != i && depth < 1024) {
            solve(static_cast<size_t>(p), depth + 1);
            w[i] = loc[i] * w[static_cast<size_t>(p)];
        } else {
            w[i] = loc[i];
        }
        done[i] = 1;
    };
    for (size_t i = 0; i < names.size(); ++i) solve(i, 0);
    return w;
}

int Rig::find(const std::string& name) const {
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == name) return static_cast<int>(i);
    return -1;
}

bool ReadRig(const Doc& d, Rig& out, std::string& error) {
    const Value& r = d.root();
    const Value& names = r["boneNames"];
    const Value& parents = r["boneParentIndexes"];
    const Value& tf = r["boneTransforms"];
    if (!names.isArray() || names.size() == 0) { error = "rig without boneNames"; return false; }
    out.names.clear(); out.parents.clear(); out.local.clear(); out.aposeLocal.clear();
    for (size_t i = 0; i < names.size(); ++i) {
        std::string n = CName(names.at(i));
        if (n.empty()) n = Format("bone_%zu", i);
        out.names.push_back(n);
        out.parents.push_back(i < parents.size() ? static_cast<int>(parents.at(i).asInt(-1)) : -1);
        out.local.push_back(i < tf.size() ? ReadQsTransform(tf.at(i)) : Xf());
    }
    const Value& ap = r["aPoseLS"];
    if (ap.isArray() && ap.size() == names.size()) {
        for (size_t i = 0; i < ap.size(); ++i) out.aposeLocal.push_back(ReadQsTransform(ap.at(i)));
    }
    return true;
}

// ---- components --------------------------------------------------------
static bool Contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static void ReadAnimsets(const Value& setup, std::vector<std::string>& out) {
    const char* lists[] = { "gameplay", "cinematics" };
    for (const char* l : lists) {
        const Value& arr = setup[l];
        for (size_t i = 0; i < arr.size(); ++i) {
            const std::string p = DepotPath(arr.at(i)["animSet"]);
            if (!p.empty()) out.push_back(p);
        }
    }
}

static bool ReadComponent(const Doc& d, const Value& v, const std::string& source, Component& c) {
    const std::string type = TypeOf(v);
    if (type.empty() || !StartsWith(type, "ent") || !Contains(type, "Component")) return false;
    c = Component();
    c.type = type;
    c.source = source;
    c.name = CName(v["name"]);
    const Value& lt = v["localTransform"];
    if (lt.isObject()) c.localTransform = ReadWorldTransform(lt);
    const Value& pt = d.deref(v["parentTransform"]);
    if (pt.isObject()) {
        c.parentBind = CName(pt["bindName"]);
        c.parentSlot = CName(pt["slotName"]);
    }

    if (Contains(type, "MorphTarget") && v.has("morphResource")) {
        c.kind = CompKind::MorphMesh;
        c.mesh = DepotPath(v["morphResource"]);
    } else if (v.has("mesh") && Contains(type, "Mesh")) {
        c.mesh = DepotPath(v["mesh"]);
        c.kind = Contains(type, "Skinned") ? CompKind::SkinnedMesh : CompKind::Mesh;
    } else if (Contains(type, "AnimatedComponent") || (v.has("rig") && v.has("animations"))) {
        c.kind = CompKind::Animated;
        c.rig = DepotPath(v["rig"]);
        ReadAnimsets(v["animations"], c.animsets);
    } else if (type == "entSlotComponent") {
        c.kind = CompKind::Slot;
        const Value& slots = v["slots"];
        for (size_t i = 0; i < slots.size(); ++i) {
            const Value& s = d.deref(slots.at(i));
            SlotDef sd;
            sd.slotName = CName(s["slotName"]);
            sd.boneName = CName(s["boneName"]);
            sd.offset = FromTRS(ReadVec3(s["relativePosition"]), s.has("relativeRotation") ? ReadQuat(s["relativeRotation"]) : Quatd(), Vec3(1, 1, 1));
            c.slots.push_back(sd);
        }
    } else {
        c.kind = CompKind::Other;
    }

    if (c.kind == CompKind::Mesh || c.kind == CompKind::SkinnedMesh || c.kind == CompKind::MorphMesh) {
        c.meshAppearance = CName(v["meshAppearance"]);
        const Value& cm = v["chunkMask"];
        if (!cm.isNull()) c.chunkMask = cm.asUInt64(~0ull);
        const Value& sk = d.deref(v["skinning"]);
        if (sk.isObject()) c.skinBind = CName(sk["bindName"]);
        const Value& en = v["isEnabled"];
        if (en.type == json::Type::Bool) c.visible = en.b;
    }
    return true;
}

void CollectComponents(const Doc& d, const Value& v, const std::string& source, std::vector<Component>& out) {
    if (v.isObject()) {
        if (v.has("HandleRefId")) return;      // already collected where the handle first appeared
        Component c;
        if (ReadComponent(d, v, source, c)) {
            if (c.kind != CompKind::Other) {
                for (const auto& e : out)
                    if (e.name == c.name && e.type == c.type && e.mesh == c.mesh && e.source == c.source) return;   // listed twice
                out.push_back(c);
            }
            return;                             // never descend into a component
        }
        for (const auto& kv : v.obj) CollectComponents(d, kv.second, source, out);
    } else if (v.isArray()) {
        for (const auto& e : v.arr) CollectComponents(d, e, source, out);
    }
}

// ---- .ent / .app -------------------------------------------------------
bool ReadEnt(const Doc& d, const std::string& path, EntInfo& out) {
    out = EntInfo();
    out.path = path;
    const Value& r = d.root();
    out.defaultAppearance = CName(r["defaultAppearance"]);
    const Value& apps = r["appearances"];
    for (size_t i = 0; i < apps.size(); ++i) {
        const Value& a = d.deref(apps.at(i));
        EntAppearance e;
        e.name = CName(a["name"]);
        e.appPath = DepotPath(a["appearanceResource"]);
        e.appName = CName(a["appearanceName"]);
        if (e.appName.empty()) e.appName = e.name;
        out.appearances.push_back(e);
    }
    const Value& inc = r["includes"];
    for (size_t i = 0; i < inc.size(); ++i) {
        const std::string p = DepotPath(inc.at(i));
        if (!p.empty()) out.includes.push_back(p);
    }
    CollectComponents(d, r["compiledData"], path, out.components);
    CollectComponents(d, r["components"], path, out.components);
    return true;
}

const AppAppearance* AppInfo::find(const std::string& name) const {
    for (const auto& a : appearances) if (a.name == name) return &a;
    for (const auto& a : appearances) if (IEquals(a.name, name)) return &a;
    return nullptr;
}

bool ReadApp(const Doc& d, const std::string& path, AppInfo& out) {
    out = AppInfo();
    out.path = path;
    const Value& apps = d.root()["appearances"];
    for (size_t i = 0; i < apps.size(); ++i) {
        const Value& def = d.deref(apps.at(i));
        if (!def.isObject()) continue;
        AppAppearance a;
        a.name = CName(def["name"]);
        CollectComponents(d, def["components"], path, a.components);
        CollectComponents(d, def["compiledData"], path, a.components);
        const Value& pv = def["partsValues"];
        for (size_t k = 0; k < pv.size(); ++k) {
            const Value& part = d.deref(pv.at(k));
            const std::string p = DepotPath(part["resource"]);
            if (!p.empty()) a.parts.push_back(p);
        }
        const Value& po = def["partsOverrides"];
        for (size_t k = 0; k < po.size(); ++k) {
            const Value& ov = d.deref(po.at(k));
            const std::string target = DepotPath(ov["partResource"]);
            const Value& co = ov["componentsOverrides"];
            for (size_t j = 0; j < co.size(); ++j) {
                const Value& c = d.deref(co.at(j));
                PartOverride o;
                o.partResource = target;
                o.componentName = CName(c["componentName"]);
                o.meshAppearance = CName(c["meshAppearance"]);
                const Value& cm = c["chunkMask"];
                if (!cm.isNull()) { o.hasChunkMask = true; o.chunkMask = cm.asUInt64(~0ull); }
                a.overrides.push_back(o);
            }
        }
        out.appearances.push_back(std::move(a));
    }
    return true;
}

} // namespace red
} // namespace cp77
