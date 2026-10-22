// ============================================================
//  Step 2: animations onto an imported skeleton.
//
//  The skeleton is found through the user properties written on
//  import (cp77_skel on helper and bones, cp77_rest = local rest
//  pose). Bones are matched by NAME, so any animset whose rig
//  shares the bone names fits - body anims on the body, weapon
//  anims on the weapon.
//
//  Keys: fresh linear position/rotation controllers per bone,
//  SetValue(CTRL_ABSOLUTE) in animate mode; the rotation comes
//  from Quat(Matrix3) (Max quaternions are left-handed - never
//  copy components), signs kept continuous between keys.
// ============================================================
#include "plugin_internal.h"

#include "../core/anim.h"
#include "../core/animprep.h"

#include <notetrck.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

using namespace cp77;

namespace cp77max {

namespace {

std::string Prop(INode* n, const MCHAR* key) {
    MSTR v;
    if (!n || !n->GetUserPropString(MSTR(key), v)) return std::string();
    return U(v.data());
}

void CollectHelpers(INode* n, std::vector<INode*>& out) {
    if (!n) return;
    if (!n->IsRootNode() && Prop(n, CP77_PROP_HELPER) == "1") out.push_back(n);
    for (int i = 0; i < n->NumberOfChildren(); ++i) CollectHelpers(n->GetChildNode(i), out);
}

void CollectBones(INode* n, const std::string& id, std::vector<INode*>& out) {
    for (int i = 0; i < n->NumberOfChildren(); ++i) {
        INode* c = n->GetChildNode(i);
        if (!c) continue;
        if (Prop(c, CP77_PROP_HELPER) == "1") continue;          // another skeleton (weapon on the hand)
        if (Prop(c, CP77_PROP_SKEL) == id) out.push_back(c);
        CollectBones(c, id, out);
    }
}

struct SetCache {
    std::map<std::string, std::shared_ptr<AnimSet>> sets;
    std::vector<std::string> order;
};
SetCache g_sets;

std::shared_ptr<AnimSet> LoadSet(const std::string& anims, std::string& error) {
    std::string glb;
    const std::string native = NativePath(anims);
    if (IEndsWith(anims, ".glb") && FileExists(native)) glb = native;
    else {
        const std::string depot = NormalizeDepotPath(anims);
        if (Extension(depot) != ".anims") { error = "not an .anims path: " + anims; return nullptr; }
        if (!Cli()->ensureGlb(Store(), { depot }, error)) return nullptr;
        glb = Store().glbFile(depot);
        if (glb.empty()) { error = "WolvenKit did not export " + depot; return nullptr; }
    }
    auto it = g_sets.sets.find(glb);
    if (it != g_sets.sets.end()) return it->second;
    auto set = std::make_shared<AnimSet>();
    if (!set->load(glb, error)) return nullptr;
    g_sets.sets[glb] = set;
    g_sets.order.push_back(glb);
    while (g_sets.order.size() > 3) { g_sets.sets.erase(g_sets.order.front()); g_sets.order.erase(g_sets.order.begin()); }
    Log("animset %s: %zu clips, rig %s", glb.c_str(), set->clipCount(), set->rigPath.c_str());
    return set;
}

bool FreshControllers(Interface* ip, INode* n, Control*& rot, Control*& pos) {
    Control* tm = n->GetTMController();
    if (!tm) return false;
    Control* r = static_cast<Control*>(ip->CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    Control* p = static_cast<Control*>(ip->CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
    if (r) tm->SetRotationController(r);
    if (p) tm->SetPositionController(p);
    rot = tm->GetRotationController();
    pos = tm->GetPositionController();
    return rot && pos;
}

void ToMaxKey(const Xf& local, Quat& q, Point3& p) {
    const Xf o = local.orthonormalized();
    Matrix3 m = ToMax(o);
    m.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    q = Quat(m);
    const Vec3 t = local.translation();
    p = Point3(static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z));
}

struct Target {
    INode* helper = nullptr;
    std::vector<INode*> bones;
    std::vector<TargetBone> info;
    size_t own = 0;                  // bones[0..own) belong to the chosen skeleton, the rest to
                                     // skeletons imported with it (the weapon of a character)
};

std::string Stamp(const std::string& id) {     // "<import stamp>_<skeleton>"
    const size_t u = id.find_last_of('_');
    return u == std::string::npos ? id : id.substr(0, u);
}

bool FindTarget(int handle, Target& t, std::string& error) {
    Interface* ip = GetCOREInterface();
    if (!ip) { error = "no 3ds Max interface"; return false; }
    INode* h = ip->GetINodeByHandle(static_cast<ULONG>(handle));
    if (!h || Prop(h, CP77_PROP_HELPER) != "1") {
        // no handle given: the first imported skeleton in the scene
        std::vector<INode*> all;
        CollectHelpers(ip->GetRootNode(), all);
        if (all.empty()) { error = "No imported skeleton in the scene - import a character first."; return false; }
        h = all[0];
    }
    t.helper = h;
    const std::string id = Prop(h, CP77_PROP_SKEL);
    CollectBones(h, id, t.bones);
    if (t.bones.empty()) { error = "the skeleton has no bones"; return false; }
    t.own = t.bones.size();
    // boss animsets also move the weapon: bones of the other skeletons of the same import
    std::vector<INode*> helpers;
    CollectHelpers(ip->GetRootNode(), helpers);
    std::set<std::string> names;
    for (INode* b : t.bones) names.insert(U(b->GetName()));
    for (INode* o : helpers) {
        const std::string oid = Prop(o, CP77_PROP_SKEL);
        if (o == h || oid == id || Stamp(oid) != Stamp(id)) continue;
        std::vector<INode*> more;
        CollectBones(o, oid, more);
        for (INode* b : more) if (names.insert(U(b->GetName())).second) t.bones.push_back(b);
    }
    for (INode* b : t.bones) {
        TargetBone tb;
        tb.name = U(b->GetName());
        if (!XfFromString(Prop(b, CP77_PROP_REST), tb.restLocal)) {
            INode* p = b->GetParentNode();
            tb.restLocal = FromMax(b->GetNodeTM(0)) * (p ? FromMax(p->GetNodeTM(0)).inverse() : Xf());
        }
        t.info.push_back(tb);
    }
    return true;
}

// Loaded clips are recorded as a note track on the skeleton helper: two keys with the
// same value "Name" (start, end) - the format WhiteoutDex reads from the scene root.
struct Loaded {
    std::string name;
    TimeValue t0 = 0, t1 = 0;
};

std::vector<Loaded> ReadLoaded(INode* holder) {
    std::vector<Loaded> out;
    if (!holder || holder->NumNoteTracks() < 1) return out;
    DefNoteTrack* nt = static_cast<DefNoteTrack*>(holder->GetNoteTrack(0));
    if (!nt) return out;
    std::vector<std::pair<TimeValue, std::string>> keys;
    for (int i = 0; i < nt->keys.Count(); ++i)
        if (nt->keys[i]) keys.emplace_back(nt->keys[i]->time, U(nt->keys[i]->note.data()));
    std::sort(keys.begin(), keys.end());
    std::vector<bool> used(keys.size(), false);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (used[i]) continue;
        const std::string& v = keys[i].second;
        if (v.size() < 3 || v.front() != '"') continue;
        for (size_t j = i + 1; j < keys.size(); ++j) {
            if (used[j] || keys[j].second != v) continue;
            used[i] = used[j] = true;
            const size_t q = v.find('"', 1);
            out.push_back({ v.substr(1, q == std::string::npos ? std::string::npos : q - 1), keys[i].first, keys[j].first });
            break;
        }
    }
    return out;
}

void WriteLoaded(INode* holder, const std::vector<Loaded>& clips) {
    if (!holder) return;
    for (int i = holder->NumNoteTracks() - 1; i >= 0; --i) holder->DeleteNoteTrack(holder->GetNoteTrack(i), TRUE);
    if (clips.empty()) return;
    DefNoteTrack* nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
    if (!nt) return;
    for (const Loaded& c : clips) {
        const MSTR v = M("\"" + c.name + "\"");
        NoteKey* a = new NoteKey(c.t0, v, 0);
        NoteKey* b = new NoteKey(c.t1, v, 0);
        nt->keys.Append(1, &a);
        nt->keys.Append(1, &b);
    }
    holder->AddNoteTrack(nt);
}

// helper (the record) and scene root (WhiteoutDex / NeoDex sequences)
void PublishLoaded(INode* helper, const std::vector<Loaded>& clips) {
    WriteLoaded(helper, clips);
    if (Interface* ip = GetCOREInterface()) WriteLoaded(ip->GetRootNode(), clips);
}

bool IsLinear(Control* c, bool rotation) {
    return c && c->ClassID() == Class_ID(rotation ? LININTERP_ROTATION_CLASS_ID : LININTERP_POSITION_CLASS_ID, 0);
}

struct AnimMode {
    AnimMode() { AnimateOn(); }
    ~AnimMode() { AnimateOff(); }
};

std::wstring Apply(const std::string& anims, const std::vector<size_t>& clipIdx, int handle, bool timeline) {
    StartLog(timeline ? "animations (all)" : "animation");
    Interface* ip = GetCOREInterface();
    std::string err;
    Target tg;
    if (!FindTarget(handle, tg, err)) return W("ERROR\t" + err);
    auto set = LoadSet(anims, err);
    if (!set) return W("ERROR\t" + err);
    if (clipIdx.empty()) return W(std::string("ERROR\tno clip selected"));

    std::vector<Clip> clips(clipIdx.size());
    std::vector<double> durations;
    for (size_t i = 0; i < clipIdx.size(); ++i) {
        if (!set->clip(clipIdx[i], clips[i], err)) return W("ERROR\t" + err);
        durations.push_back(clips[i].duration);
    }
    // companion bones (weapon) only when the clips animate them
    {
        std::set<std::string> animated;
        for (const auto& c : clips) for (const auto& tr : c.tracks) animated.insert(tr.joint);
        size_t w = tg.own;
        for (size_t i = tg.own; i < tg.bones.size(); ++i)
            if (animated.count(tg.info[i].name)) { tg.bones[w] = tg.bones[i]; tg.info[w] = tg.info[i]; ++w; }
        tg.bones.resize(w);
        tg.info.resize(w);
    }
    size_t matched = 0, total = 0;
    for (const auto& c : clips) { size_t m = 0; MatchRatio(c, tg.info, &m); matched += m; total += c.tracks.size(); }
    // fits when it covers most of the clip - or most of a small skeleton (a weapon in a boss animset)
    if (total > 0 && matched * 3 < total && (matched == 0 || matched * 3 < tg.own)) {
        return W(Format("ERROR\tthe animset does not fit \"%s\": only %zu of %zu animated bones exist on it. Nothing changed.",
                        U(tg.helper->GetName()).c_str(), matched, total));
    }
    const Settings& st = Config();
    const int fps = std::max(1, st.getInt(Settings::FrameRate, 30));
    const double gapS = std::max(0, st.getInt(Settings::GapFrames, 10)) / static_cast<double>(fps);
    // one clip is added after the ones already loaded; "all" replaces everything
    std::vector<Loaded> loaded = timeline ? std::vector<Loaded>() : ReadLoaded(tg.helper);
    const bool append = !loaded.empty();
    Timeline tl = LayoutClips(durations, timeline ? gapS : 0.0);
    const TimeValue now = std::max<TimeValue>(0, ip->GetTime());
    TimeValue startT = now;    // first clip: at the time slider, rest pose before it
    if (append) {
        TimeValue lastEnd = 0;
        for (const Loaded& l : loaded) lastEnd = std::max(lastEnd, l.t1);
        startT = std::max(now, lastEnd + static_cast<TimeValue>(std::lround(gapS * TIME_TICKSPERSEC)));
        startT -= startT % GetTicksPerFrame();
        if (startT <= lastEnd) startT = lastEnd + GetTicksPerFrame();
    }
    const double offset = static_cast<double>(startT) / TIME_TICKSPERSEC;
    for (double& t : tl.starts) t += offset;
    std::vector<ClipPlacement> place;
    for (size_t i = 0; i < clips.size(); ++i) place.push_back({ &clips[i], tl.starts[i] });
    AnimPrepResult res;
    PrepAnimation(place, *set, tg.info, UnitsPerMeter(), res, append ? 0.0 : 1.0 / fps, true);

    if (GetFrameRate() != fps && TIME_TICKSPERSEC % fps == 0) SetFrameRate(fps);
    theHold.Suspend();
    ip->DisableSceneRedraw();
    SuspendAnimate();
    size_t keys = 0, flips = 0;
    for (const BoneKeys& bk : res.keys) {
        INode* n = tg.bones[static_cast<size_t>(bk.bone)];
        Control* tm = n->GetTMController();
        Control* rot = tm ? tm->GetRotationController() : nullptr;
        Control* pos = tm ? tm->GetPositionController() : nullptr;
        Quat q;
        Point3 p;
        const bool keep = append && IsLinear(rot, true) && IsLinear(pos, false);   // earlier clips stay
        if (!keep) {
            if (!FreshControllers(ip, n, rot, pos)) continue;
            ToMaxKey(bk.local.front(), q, p);
            rot->SetValue(0, &q, 1, CTRL_ABSOLUTE);     // base value, outside animate mode
            pos->SetValue(0, &p, 1, CTRL_ABSOLUTE);
        }
        AnimMode mode;
        Quat prev;
        for (size_t i = 0; i < bk.times.size(); ++i) {
            const TimeValue t = static_cast<TimeValue>(std::lround(bk.times[i] * TIME_TICKSPERSEC));
            ToMaxKey(bk.local[i], q, p);
            if (i > 0 && q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0.0f) { q = Quat(-q.x, -q.y, -q.z, -q.w); ++flips; }
            prev = q;
            rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
            pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
            ++keys;
        }
    }
    ResumeAnimate();

    // names on note tracks: the helper (Dope Sheet) and the scene root (WhiteoutDex sequences)
    for (size_t i = 0; i < clips.size(); ++i) {
        Loaded l;
        l.name = clips[i].name;
        l.t0 = static_cast<TimeValue>(std::lround(tl.starts[i] * TIME_TICKSPERSEC));
        l.t1 = std::max<TimeValue>(l.t0 + GetTicksPerFrame(), static_cast<TimeValue>(std::lround((tl.starts[i] + clips[i].duration) * TIME_TICKSPERSEC)));
        loaded.push_back(l);
    }
    PublishLoaded(tg.helper, loaded);
    TimeValue end = GetTicksPerFrame();
    for (const Loaded& l : loaded) end = std::max(end, l.t1);
    ip->SetAnimRange(Interval(0, end));
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->SetTime(startT);
    ip->RedrawViews(ip->GetTime());

    const int tpf = GetTicksPerFrame();
    std::string r = Format("%zu clip(s) on \"%s\": %zu bones keyed, %zu keys, frames %d-%d (%d fps)", clips.size(),
                           U(tg.helper->GetName()).c_str(), res.keys.size(), keys, static_cast<int>(startT / tpf),
                           static_cast<int>(loaded.back().t1 / tpf), fps);
    if (clips.size() == 1) r = "\"" + clips[0].name + "\" " + r;
    if (append) r += Format("\nadded after the %zu clip(s) already loaded", loaded.size() - clips.size());
    else if (startT > 0) r += "\nrest pose before the clip";
    if (!res.unmatched.empty()) r += Format("\n%zu animated bones not on the skeleton: ", res.unmatched.size()) + Join(res.unmatched, ", ");
    if (res.scaleTracks) r += Format("\n%zu tracks with scale (not applied)", res.scaleTracks);
    if (tg.bones.size() > tg.own) r += Format("\n%zu bones of the weapon animated too", tg.bones.size() - tg.own);
    Log("%s (sign flips %zu)", r.c_str(), flips);
    return W(r);
}

} // namespace

std::wstring ListRigs() {
    Interface* ip = GetCOREInterface();
    if (!ip) return std::wstring();
    std::vector<INode*> all;
    CollectHelpers(ip->GetRootNode(), all);
    std::string out;
    for (INode* h : all) {
        if (!out.empty()) out += "\n";
        out += Format("%lu\t", static_cast<unsigned long>(h->GetHandle())) + U(h->GetName()) + "\t" + Prop(h, CP77_PROP_OWNER) + "\t" +
               Prop(h, CP77_PROP_ENTITY) + "\t" + Prop(h, CP77_PROP_ANIMSETS);
    }
    return W(out);
}

std::wstring Clips(const std::wstring& anims) {
    StartLog("clips");
    std::string err;
    auto set = LoadSet(Narrow(anims), err);
    if (!set) return W("ERROR\t" + err);
    std::string out;
    const auto names = set->clipNames();
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out += "\n";
        out += names[i] + "\t" + Num(set->clipDuration(i), 6);
    }
    return W(out);
}

std::wstring ApplyClip(const std::wstring& anims, const std::wstring& clip, int rig) {
    std::string err;
    auto set = LoadSet(Narrow(anims), err);
    if (!set) return W("ERROR\t" + err);
    const int idx = set->findClip(Narrow(clip));
    if (idx < 0) return W("ERROR\tno clip \"" + Narrow(clip) + "\" in the animset");
    return Apply(Narrow(anims), { static_cast<size_t>(idx) }, rig, false);
}

std::wstring ApplyAll(const std::wstring& anims, const std::wstring& filter, int rig) {
    std::string err;
    auto set = LoadSet(Narrow(anims), err);
    if (!set) return W("ERROR\t" + err);
    std::vector<size_t> idx;
    const std::string f = Lower(Trim(Narrow(filter)));
    const auto names = set->clipNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (f.empty() || Lower(names[i]).find(f) != std::string::npos) idx.push_back(i);
    if (idx.empty()) return W("ERROR\tno clip matches \"" + f + "\"");
    return Apply(Narrow(anims), idx, rig, true);
}

std::wstring ResetPose(int rig) {
    Interface* ip = GetCOREInterface();
    std::string err;
    Target tg;
    if (!FindTarget(rig, tg, err)) return W("ERROR\t" + err);
    theHold.Suspend();
    ip->DisableSceneRedraw();
    SuspendAnimate();
    for (size_t i = 0; i < tg.bones.size(); ++i) {
        Control* rot = nullptr;
        Control* pos = nullptr;
        if (!FreshControllers(ip, tg.bones[i], rot, pos)) continue;
        Quat q;
        Point3 p;
        ToMaxKey(tg.info[i].restLocal, q, p);
        rot->SetValue(0, &q, 1, CTRL_ABSOLUTE);
        pos->SetValue(0, &p, 1, CTRL_ABSOLUTE);
    }
    ResumeAnimate();
    PublishLoaded(tg.helper, {});
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());
    return W(Format("rest pose restored on %zu bones", tg.bones.size()));
}

std::wstring ListLoaded(int rig) {
    std::string err;
    Target tg;
    if (!FindTarget(rig, tg, err)) return W("ERROR\t" + err);
    const int tpf = GetTicksPerFrame();
    std::string out;
    for (const Loaded& l : ReadLoaded(tg.helper)) {
        if (!out.empty()) out += "\n";
        out += l.name + Format("\t%d\t%d", static_cast<int>(l.t0 / tpf), static_cast<int>(l.t1 / tpf));
    }
    return W(out);
}

std::wstring DeleteLoaded(int rig, int index) {
    StartLog("delete clip");
    Interface* ip = GetCOREInterface();
    std::string err;
    Target tg;
    if (!FindTarget(rig, tg, err)) return W("ERROR\t" + err);
    std::vector<Loaded> loaded = ReadLoaded(tg.helper);
    if (index < 0 || static_cast<size_t>(index) >= loaded.size()) return W(std::string("ERROR\tpick a loaded clip"));
    const Loaded gone = loaded[static_cast<size_t>(index)];
    loaded.erase(loaded.begin() + index);
    theHold.Suspend();
    ip->DisableSceneRedraw();
    SuspendAnimate();
    // the clip's keys go; the time is not closed up, the other clips stay where they are
    const Interval iv(gone.t0, gone.t1);
    for (INode* b : tg.bones) {
        Control* tm = b->GetTMController();
        if (!tm) continue;
        for (Control* c : { tm->GetRotationController(), tm->GetPositionController() })
            if (c) c->DeleteTime(iv, TIME_INCLEFT | TIME_INCRIGHT | TIME_NOSLIDE);
    }
    ResumeAnimate();
    PublishLoaded(tg.helper, loaded);
    TimeValue end = GetTicksPerFrame() * 100;
    if (!loaded.empty()) { end = GetTicksPerFrame(); for (const Loaded& l : loaded) end = std::max(end, l.t1); }
    ip->SetAnimRange(Interval(0, end));
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->SetTime(loaded.empty() ? 0 : loaded.back().t0);
    ip->RedrawViews(ip->GetTime());
    const std::string r = "\"" + gone.name + Format("\" removed (frames %d-%d), %zu clip(s) left", static_cast<int>(gone.t0 / GetTicksPerFrame()),
                                                     static_cast<int>(gone.t1 / GetTicksPerFrame()), loaded.size());
    Log("%s", r.c_str());
    return W(r);
}

} // namespace cp77max
