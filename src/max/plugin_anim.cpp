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

#include <cmath>
#include <map>
#include <memory>

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
};

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
    CollectBones(h, Prop(h, CP77_PROP_SKEL), t.bones);
    if (t.bones.empty()) { error = "the skeleton has no bones"; return false; }
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
    size_t matched = 0, total = 0;
    for (const auto& c : clips) { size_t m = 0; MatchRatio(c, tg.info, &m); matched += m; total += c.tracks.size(); }
    if (total > 0 && matched * 3 < total) {
        return W(Format("ERROR\tthe animset does not fit \"%s\": only %zu of %zu animated bones exist on it. Nothing changed.",
                        U(tg.helper->GetName()).c_str(), matched, total));
    }
    const Settings& st = Config();
    const int fps = std::max(1, st.getInt(Settings::FrameRate, 30));
    const double gap = timeline ? std::max(0, st.getInt(Settings::GapFrames, 10)) / static_cast<double>(fps) : 0.0;
    const Timeline tl = LayoutClips(durations, gap);
    std::vector<ClipPlacement> place;
    for (size_t i = 0; i < clips.size(); ++i) place.push_back({ &clips[i], tl.starts[i] });
    AnimPrepResult res;
    PrepAnimation(place, *set, tg.info, UnitsPerMeter(), res);

    if (GetFrameRate() != fps && TIME_TICKSPERSEC % fps == 0) SetFrameRate(fps);
    theHold.Suspend();
    ip->DisableSceneRedraw();
    SuspendAnimate();
    size_t keys = 0, flips = 0;
    for (const BoneKeys& bk : res.keys) {
        INode* n = tg.bones[static_cast<size_t>(bk.bone)];
        Control* rot = nullptr;
        Control* pos = nullptr;
        if (!FreshControllers(ip, n, rot, pos)) continue;
        Quat q;
        Point3 p;
        ToMaxKey(bk.local.front(), q, p);
        rot->SetValue(0, &q, 1, CTRL_ABSOLUTE);     // base value, outside animate mode
        pos->SetValue(0, &p, 1, CTRL_ABSOLUTE);
        if (bk.times.size() < 2 && !timeline) continue;
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

    // clip names on a note track of the helper: start and end key per clip
    if (timeline) {
        for (int i = tg.helper->NumNoteTracks() - 1; i >= 0; --i) tg.helper->DeleteNoteTrack(tg.helper->GetNoteTrack(i), TRUE);
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
        if (nt) {
            for (size_t i = 0; i < clips.size(); ++i) {
                const MSTR name = M(clips[i].name);
                NoteKey* a = new NoteKey(static_cast<TimeValue>(std::lround(tl.starts[i] * TIME_TICKSPERSEC)), name, 0);
                NoteKey* b = new NoteKey(static_cast<TimeValue>(std::lround((tl.starts[i] + clips[i].duration) * TIME_TICKSPERSEC)), name, 0);
                nt->keys.Append(1, &a);
                nt->keys.Append(1, &b);
            }
            tg.helper->AddNoteTrack(nt);
        }
    }
    const TimeValue end = std::max<TimeValue>(GetTicksPerFrame(), static_cast<TimeValue>(std::lround(res.end * TIME_TICKSPERSEC)));
    ip->SetAnimRange(Interval(0, end));
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->SetTime(0);
    ip->RedrawViews(ip->GetTime());

    std::string r = Format("%zu clip(s) on \"%s\": %zu bones keyed, %zu keys, %.2f s (%d fps)", clips.size(),
                           U(tg.helper->GetName()).c_str(), res.keys.size(), keys, res.end, fps);
    if (clips.size() == 1) r = "\"" + clips[0].name + "\" " + r;
    if (!res.unmatched.empty()) r += Format("\n%zu animated bones not on the skeleton: ", res.unmatched.size()) + Join(res.unmatched, ", ");
    if (res.scaleTracks) r += Format("\n%zu tracks with scale (not applied)", res.scaleTracks);
    if (timeline) r += "\nClip names are on the note track of " + U(tg.helper->GetName()) + ".";
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
    for (int i = tg.helper->NumNoteTracks() - 1; i >= 0; --i) tg.helper->DeleteNoteTrack(tg.helper->GetNoteTrack(i), TRUE);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());
    return W(Format("rest pose restored on %zu bones", tg.bones.size()));
}

} // namespace cp77max
