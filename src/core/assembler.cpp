#include "assembler.h"
#include "util.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace cp77 {

int PlanSkeleton::find(const std::string& bone) const {
    for (size_t i = 0; i < bones.size(); ++i) if (bones[i].name == bone) return static_cast<int>(i);
    for (size_t i = 0; i < bones.size(); ++i) if (IEquals(bones[i].name, bone)) return static_cast<int>(i);
    return -1;
}

std::string FallbackHandBone(const PlanSkeleton& s) {
    const char* names[] = { "WeaponRight", "weapon_r", "RightHand", "r_hand", "Hand_R", "hand_r", "RightHandIndex1" };
    for (const char* n : names) if (s.find(n) >= 0) return s.bones[static_cast<size_t>(s.find(n))].name;
    return std::string();
}

std::string ScenePlan::summary() const {
    std::string s;
    for (size_t i = 0; i < skeletons.size(); ++i) {
        const PlanSkeleton& k = skeletons[i];
        s += Format("skeleton %zu \"%s\" (%s): %zu bones from %zu rig(s)", i, k.name.c_str(), k.owner.c_str(), k.bones.size(), k.rigs.size());
        if (k.parentSkeleton >= 0) s += Format(", hangs on skeleton %d bone \"%s\"", k.parentSkeleton, k.parentBone.c_str());
        s += Format(", %zu animset(s)\n", k.animsets.size());
    }
    for (const auto& m : meshes)
        s += Format("mesh %s [%s] %s app=\"%s\" mask=%016llx %s\n", m.component.c_str(), m.owner.c_str(), m.mesh.c_str(),
                    m.appearance.c_str(), static_cast<unsigned long long>(m.chunkMask),
                    m.skeleton >= 0 ? Format("skinned->%d", m.skeleton).c_str() : Format("rigid->%d:%s", m.attachSkeleton, m.attachBone.c_str()).c_str());
    for (const auto& w : warnings) s += "warning: " + w + "\n";
    return s;
}

Assembler::Assembler(FileStore& store, const WolvenKit* wk, const Settings& s) : store_(store), wk_(wk), s_(s) {}

bool Assembler::needJson(const std::vector<std::string>& depots, std::string& error) {
    if (wk_) return wk_->ensureJson(store_, depots, error);
    std::vector<std::string> miss;
    for (const auto& d : depots) if (!d.empty() && d[0] != '#' && store_.jsonFile(d).empty()) miss.push_back(d);
    if (miss.empty()) return true;
    error = "missing (no WolvenKit configured): " + Join(miss, ", ");
    return false;
}

bool Assembler::loadEnt(const std::string& path, red::EntInfo& out, std::string& error) {
    if (!needJson({ path }, error)) return false;
    red::Doc d;
    if (!d.load(store_.jsonFile(path), error)) return false;
    return red::ReadEnt(d, path, out);
}

bool Assembler::describe(const std::string& ent, EntitySummary& out, std::string& error) {
    red::EntInfo info;
    if (!loadEnt(ent, info, error)) return false;
    out = EntitySummary();
    out.path = ent;
    out.defaultAppearance = info.defaultAppearance;
    for (const auto& a : info.appearances) out.appearances.push_back(a.name);
    if (out.appearances.empty() && !info.includes.empty()) {
        // appearances may come from an included template
        if (needJson(info.includes, error)) {
            for (const auto& inc : info.includes) {
                red::EntInfo ii;
                std::string e;
                if (!loadEnt(inc, ii, e)) continue;
                for (const auto& a : ii.appearances) out.appearances.push_back(a.name);
                if (out.defaultAppearance.empty()) out.defaultAppearance = ii.defaultAppearance;
            }
        }
        error.clear();
    }
    return true;
}

bool Assembler::collect(const std::string& entPath, const std::string& appName, Entity& e, std::string& error) {
    red::EntInfo info;
    if (!loadEnt(entPath, info, error)) return false;
    e.comps = info.components;

    // includes (templates inheriting from other templates), two levels deep
    std::vector<std::string> level = info.includes;
    std::set<std::string> seen = { DepotKey(entPath) };
    std::vector<red::EntAppearance> apps = info.appearances;
    std::string defApp = info.defaultAppearance;
    for (int depth = 0; depth < 3 && !level.empty(); ++depth) {
        std::vector<std::string> todo;
        for (const auto& p : level) if (seen.insert(DepotKey(p)).second) todo.push_back(p);
        if (todo.empty()) break;
        std::string err;
        if (!needJson(todo, err)) { e.warnings.push_back("includes: " + err); break; }
        std::vector<std::string> next;
        for (const auto& p : todo) {
            red::EntInfo ii;
            if (!loadEnt(p, ii, err)) { e.warnings.push_back(err); continue; }
            e.comps.insert(e.comps.end(), ii.components.begin(), ii.components.end());
            if (apps.empty()) apps = ii.appearances;
            if (defApp.empty()) defApp = ii.defaultAppearance;
            next.insert(next.end(), ii.includes.begin(), ii.includes.end());
        }
        level = next;
    }

    if (apps.empty()) {
        Log("%s: no appearances, using the template's own components (%zu)", entPath.c_str(), e.comps.size());
        return true;
    }
    std::string want = appName.empty() ? defApp : appName;
    const red::EntAppearance* chosen = nullptr;
    for (const auto& a : apps) if (a.name == want) { chosen = &a; break; }
    if (!chosen) for (const auto& a : apps) if (IEquals(a.name, want)) { chosen = &a; break; }
    if (!chosen) {
        chosen = &apps[0];
        if (!want.empty()) e.warnings.push_back("appearance \"" + want + "\" not in " + entPath + ", using \"" + chosen->name + "\"");
    }
    Log("%s: appearance \"%s\" -> %s / \"%s\"", entPath.c_str(), chosen->name.c_str(), chosen->appPath.c_str(), chosen->appName.c_str());
    if (chosen->appPath.empty()) return true;

    if (!needJson({ chosen->appPath }, error)) return false;
    red::Doc ad;
    if (!ad.load(store_.jsonFile(chosen->appPath), error)) return false;
    red::AppInfo app;
    red::ReadApp(ad, chosen->appPath, app);
    const red::AppAppearance* aa = app.find(chosen->appName);
    if (!aa) aa = app.find(chosen->name);
    if (!aa && !app.appearances.empty()) {
        aa = &app.appearances[0];
        e.warnings.push_back("appearance \"" + chosen->appName + "\" not in " + chosen->appPath + ", using \"" + aa->name + "\"");
    }
    if (!aa) { e.warnings.push_back(chosen->appPath + " has no appearances"); return true; }
    e.comps.insert(e.comps.end(), aa->components.begin(), aa->components.end());

    if (!aa->parts.empty()) {
        std::string err;
        if (!needJson(aa->parts, err)) e.warnings.push_back("parts: " + err);
        for (const auto& p : aa->parts) {
            red::EntInfo pi;
            if (store_.jsonFile(p).empty() || !loadEnt(p, pi, err)) { e.warnings.push_back("part missing: " + p); continue; }
            e.comps.insert(e.comps.end(), pi.components.begin(), pi.components.end());
        }
    }
    size_t applied = 0;
    for (auto& c : e.comps) {
        for (const auto& o : aa->overrides) {
            if (o.componentName != c.name) continue;
            if (!o.partResource.empty() && DepotKey(o.partResource) != DepotKey(c.source)) continue;
            if (!o.meshAppearance.empty()) c.meshAppearance = o.meshAppearance;
            if (o.hasChunkMask) c.chunkMask = o.chunkMask;
            ++applied;
        }
    }
    Log("%s: %zu components, %zu parts, %zu overrides applied", entPath.c_str(), e.comps.size(), aa->parts.size(), applied);

    // morph target heads: the geometry is the .morphtarget's baseMesh
    std::vector<std::string> morphs;
    for (const auto& c : e.comps) if (c.kind == red::CompKind::MorphMesh && !c.mesh.empty()) morphs.push_back(c.mesh);
    if (!morphs.empty()) {
        std::string err;
        if (!needJson(morphs, err)) e.warnings.push_back("morph targets: " + err);
        for (auto& c : e.comps) {
            if (c.kind != red::CompKind::MorphMesh) continue;
            red::Doc md;
            std::string base;
            if (!store_.jsonFile(c.mesh).empty() && md.load(store_.jsonFile(c.mesh), err)) base = red::DepotPath(md.root()["baseMesh"]);
            if (base.empty()) { e.warnings.push_back("no baseMesh for " + c.mesh); c.kind = red::CompKind::Other; continue; }
            c.mesh = base;
            c.kind = red::CompKind::SkinnedMesh;
        }
    }
    return true;
}

bool Assembler::resolveSlot(const ScenePlan& plan, const std::vector<red::Component>& comps, const std::string& owner,
                            const std::string& bindName, const std::string& slotName,
                            int& skeleton, std::string& bone, Xf& offset) const {
    (void)owner;
    offset = Xf();
    bone.clear();
    auto skelOfComp = [&](const std::string& name) -> int {
        for (size_t i = 0; i < plan.skeletons.size(); ++i)
            for (const auto& r : plan.skeletons[i].rigs)
                if (r == "comp:" + name) return static_cast<int>(i);
        return -1;
    };
    const red::Component* target = nullptr;
    for (const auto& c : comps) if (!bindName.empty() && c.name == bindName) { target = &c; break; }
    if (target && target->kind == red::CompKind::Slot) {
        for (const auto& s : target->slots) {
            if (IEquals(s.slotName, slotName)) {
                bone = s.boneName;
                offset = s.offset;
                const int k = skelOfComp(target->parentBind);
                if (k >= 0) skeleton = k;
                break;
            }
        }
        if (bone.empty()) return false;
    } else if (target && target->kind == red::CompKind::Animated) {
        const int k = skelOfComp(target->name);
        if (k >= 0) skeleton = k;
        bone = slotName;
    } else {
        bone = slotName;
    }
    if (skeleton < 0 || static_cast<size_t>(skeleton) >= plan.skeletons.size()) return false;
    if (!bone.empty() && plan.skeletons[static_cast<size_t>(skeleton)].find(bone) < 0) {
        Log("slot %s/%s: bone \"%s\" not in skeleton %d", bindName.c_str(), slotName.c_str(), bone.c_str(), skeleton);
        bone.clear();
    }
    return true;
}

bool Assembler::assemble(const std::string& owner, const std::string& entPath, Entity& e, ScenePlan& plan, std::string& error) {
    const size_t firstSkel = plan.skeletons.size();
    // ---- rigs ------------------------------------------------------------
    std::vector<std::string> rigPaths;
    for (const auto& c : e.comps) if (c.kind == red::CompKind::Animated && !c.rig.empty()) rigPaths.push_back(c.rig);
    if (!rigPaths.empty()) {
        std::string err;
        if (!needJson(rigPaths, err)) e.warnings.push_back("rigs: " + err);
    }
    struct RigComp { const red::Component* comp; red::Rig rig; };
    std::vector<RigComp> rigs;
    for (const auto& c : e.comps) {
        if (c.kind != red::CompKind::Animated || c.rig.empty()) continue;
        RigComp rc;
        rc.comp = &c;
        red::Doc d;
        std::string err;
        const std::string jf = store_.jsonFile(c.rig);
        if (jf.empty() || !d.load(jf, err) || !red::ReadRig(d, rc.rig, err)) {
            e.warnings.push_back("rig not readable: " + c.rig + (err.empty() ? "" : " (" + err + ")"));
            continue;
        }
        rc.rig.path = c.rig;
        rigs.push_back(std::move(rc));
    }
    std::stable_sort(rigs.begin(), rigs.end(), [](const RigComp& a, const RigComp& b) {
        const int ha = a.rig.find("Hips") >= 0 ? 1 : 0, hb = b.rig.find("Hips") >= 0 ? 1 : 0;
        if (ha != hb) return ha > hb;
        return a.rig.names.size() > b.rig.names.size();
    });
    const bool apose = !IEquals(s_.get(Settings::RestPose, "APose"), "Reference");

    std::vector<std::pair<const red::Component*, size_t>> separate;   // component -> skeleton index
    for (size_t ri = 0; ri < rigs.size(); ++ri) {
        const red::Rig& rig = rigs[ri].rig;
        const std::vector<Xf> world = rig.world(apose);
        const std::vector<Xf>& loc = (apose && rig.aposeLocal.size() == rig.names.size()) ? rig.aposeLocal : rig.local;
        PlanSkeleton* target = nullptr;
        if (plan.skeletons.size() > firstSkel) {
            PlanSkeleton& main = plan.skeletons[firstSkel];
            size_t overlap = 0;
            for (const auto& n : rig.names) if (main.find(n) >= 0) ++overlap;
            if (overlap > 0) target = &main;
        }
        if (!target) {
            PlanSkeleton s;
            s.owner = owner;
            s.entity = entPath;
            s.name = plan.skeletons.size() == firstSkel ? FileStem(entPath) : FileStem(entPath) + "_" + rigs[ri].comp->name;
            plan.skeletons.push_back(s);
            target = &plan.skeletons.back();
            if (plan.skeletons.size() - 1 > firstSkel) separate.emplace_back(rigs[ri].comp, plan.skeletons.size() - 1);
        }
        target->rigs.push_back(rig.path);
        target->rigs.push_back("comp:" + rigs[ri].comp->name);
        for (const auto& a : rigs[ri].comp->animsets)
            if (std::find(target->animsets.begin(), target->animsets.end(), a) == target->animsets.end()) target->animsets.push_back(a);
        // add bones parent-first
        std::vector<char> added(rig.names.size(), 0);
        size_t newBones = 0;
        std::function<int(size_t, int)> add = [&](size_t i, int depth) -> int {
            const int have = target->find(rig.names[i]);
            if (have >= 0) { added[i] = 1; return have; }
            int parent = -1;
            const int p = rig.parents[i];
            if (p >= 0 && static_cast<size_t>(p) < rig.names.size() && static_cast<size_t>(p) != i && depth < 1024)
                parent = add(static_cast<size_t>(p), depth + 1);
            PlanBone b;
            b.name = rig.names[i];
            b.parent = parent;
            b.restWorld = parent >= 0 ? loc[i] * target->bones[static_cast<size_t>(parent)].restWorld : world[i];
            target->bones.push_back(b);
            added[i] = 1;
            ++newBones;
            return static_cast<int>(target->bones.size()) - 1;
        };
        for (size_t i = 0; i < rig.names.size(); ++i) if (!added[i]) add(i, 0);
        Log("rig %s (%s): %zu bones, %zu new in \"%s\"%s", rig.path.c_str(), rigs[ri].comp->name.c_str(), rig.names.size(),
            newBones, target->name.c_str(), rig.aposeLocal.empty() ? "" : (apose ? ", A-pose" : ", reference pose"));
    }
    if (plan.skeletons.size() == firstSkel) {
        // no rig at all (simple prop / rigid weapon): one root bone to hang everything on
        PlanSkeleton s;
        s.owner = owner;
        s.entity = entPath;
        s.name = FileStem(entPath);
        PlanBone b;
        b.name = "Root";
        s.bones.push_back(b);
        plan.skeletons.push_back(s);
        e.warnings.push_back(entPath + ": no rig found, meshes are placed rigidly");
    }
    const int mainSkel = static_cast<int>(firstSkel);

    // ---- separate skeletons hang off their parentTransform -----------------
    for (const auto& sp : separate) {
        int sk = mainSkel;
        std::string bone;
        Xf off;
        if (resolveSlot(plan, e.comps, owner, sp.first->parentBind, sp.first->parentSlot, sk, bone, off)) {
            PlanSkeleton& s = plan.skeletons[sp.second];
            s.parentSkeleton = sk;
            s.parentBone = bone;
            s.offset = sp.first->localTransform * off;
        } else {
            plan.skeletons[sp.second].parentSkeleton = mainSkel;
        }
    }

    // ---- meshes ----------------------------------------------------------
    std::map<std::string, int> compSkel;
    for (size_t i = firstSkel; i < plan.skeletons.size(); ++i)
        for (const auto& r : plan.skeletons[i].rigs)
            if (StartsWith(r, "comp:")) compSkel[r.substr(5)] = static_cast<int>(i);
    size_t hidden = 0;
    for (const auto& c : e.comps) {
        if (c.kind != red::CompKind::SkinnedMesh && c.kind != red::CompKind::Mesh) continue;
        if (c.mesh.empty() || Extension(c.mesh) != ".mesh") continue;
        if (c.chunkMask == 0) { ++hidden; Log("component %s: chunkMask 0, skipped", c.name.c_str()); continue; }
        PlanMesh m;
        m.component = c.name;
        m.owner = owner;
        m.mesh = c.mesh;
        m.appearance = c.meshAppearance;
        m.chunkMask = c.chunkMask;
        if (c.kind == red::CompKind::SkinnedMesh) {
            auto it = compSkel.find(c.skinBind);
            m.skeleton = it != compSkel.end() ? it->second : mainSkel;
        } else {
            int sk = mainSkel;
            std::string bone;
            Xf off;
            if (!resolveSlot(plan, e.comps, owner, c.parentBind, c.parentSlot, sk, bone, off)) { sk = mainSkel; bone.clear(); off = Xf(); }
            m.attachSkeleton = sk;
            m.attachBone = bone;
            m.offset = c.localTransform * off;
        }
        plan.meshes.push_back(m);
    }
    if (hidden) Log("%zu mesh components hidden by chunkMask 0", hidden);
    for (const auto& w : e.warnings) plan.warnings.push_back(w);
    (void)error;
    return true;
}

// Best effort: pull the JSON of both entities level by level in as few
// WolvenKit calls as possible (every call loads the archives again).
// Errors are ignored here - collect() reports them properly.
void Assembler::prefetch(const std::vector<std::pair<std::string, std::string>>& ents) {
    if (!wk_) return;
    std::string err;
    std::vector<std::string> level;
    for (const auto& e : ents) if (!e.first.empty()) level.push_back(e.first);
    if (level.empty()) return;
    needJson(level, err);
    // level 2: appearance resources, includes, rigs named in the templates
    std::vector<std::string> next, apps;
    for (const auto& e : ents) {
        if (e.first.empty()) continue;
        red::EntInfo info;
        if (!loadEnt(e.first, info, err)) continue;
        next.insert(next.end(), info.includes.begin(), info.includes.end());
        for (const auto& c : info.components) if (c.kind == red::CompKind::Animated && !c.rig.empty()) next.push_back(c.rig);
        const red::EntAppearance* chosen = nullptr;
        const std::string want = e.second.empty() ? info.defaultAppearance : e.second;
        for (const auto& a : info.appearances) if (IEquals(a.name, want)) chosen = &a;
        if (!chosen && !info.appearances.empty()) chosen = &info.appearances[0];
        if (chosen && !chosen->appPath.empty()) { next.push_back(chosen->appPath); apps.push_back(chosen->appPath + "|" + chosen->appName); }
    }
    if (next.empty()) return;
    needJson(next, err);
    // level 3: parts, rigs and morph targets named in the appearances
    std::vector<std::string> third;
    for (const auto& a : apps) {
        const size_t bar = a.find('|');
        const std::string path = a.substr(0, bar), name = a.substr(bar + 1);
        red::Doc d;
        if (store_.jsonFile(path).empty() || !d.load(store_.jsonFile(path), err)) continue;
        red::AppInfo info;
        red::ReadApp(d, path, info);
        const red::AppAppearance* aa = info.find(name);
        if (!aa && !info.appearances.empty()) aa = &info.appearances[0];
        if (!aa) continue;
        third.insert(third.end(), aa->parts.begin(), aa->parts.end());
        for (const auto& c : aa->components) {
            if (c.kind == red::CompKind::Animated && !c.rig.empty()) third.push_back(c.rig);
            if (c.kind == red::CompKind::MorphMesh && !c.mesh.empty()) third.push_back(c.mesh);
        }
    }
    if (third.empty()) return;
    needJson(third, err);
    // level 4: what the parts bring along
    std::vector<std::string> fourth;
    for (const auto& p : third) {
        if (Extension(p) != ".ent") continue;
        red::EntInfo pi;
        if (store_.jsonFile(p).empty() || !loadEnt(p, pi, err)) continue;
        for (const auto& c : pi.components) {
            if (c.kind == red::CompKind::Animated && !c.rig.empty()) fourth.push_back(c.rig);
            if (c.kind == red::CompKind::MorphMesh && !c.mesh.empty()) fourth.push_back(c.mesh);
        }
    }
    if (!fourth.empty()) needJson(fourth, err);
}

bool Assembler::build(const std::string& characterEnt, const std::string& characterApp,
                      const std::string& weaponEnt, const std::string& weaponApp,
                      const std::string& weaponSlot, ScenePlan& plan, std::string& error) {
    plan = ScenePlan();
    plan.character = NormalizeDepotPath(characterEnt);
    plan.characterAppearance = characterApp;
    plan.weapon = NormalizeDepotPath(weaponEnt);
    plan.weaponAppearance = weaponApp;
    slotNames_.clear();

    prefetch({ { plan.character, characterApp }, { plan.weapon, weaponApp } });

    Entity ch;
    if (!plan.character.empty()) {
        if (!collect(plan.character, characterApp, ch, error)) return false;
        if (!assemble("character", plan.character, ch, plan, error)) return false;
        charComps_ = ch.comps;
        for (const auto& c : ch.comps)
            if (c.kind == red::CompKind::Slot)
                for (const auto& s : c.slots)
                    if (std::find(slotNames_.begin(), slotNames_.end(), s.slotName) == slotNames_.end()) slotNames_.push_back(s.slotName);
    }
    if (!plan.weapon.empty()) {
        const size_t firstWeaponSkel = plan.skeletons.size();
        Entity we;
        if (!collect(plan.weapon, weaponApp, we, error)) return false;
        if (!assemble("weapon", plan.weapon, we, plan, error)) return false;
        if (!plan.character.empty() && firstWeaponSkel < plan.skeletons.size()) {
            std::string slot = weaponSlot.empty() ? s_.get(Settings::WeaponSlot, "WeaponRight") : weaponSlot;
            plan.weaponSlot = slot;
            int sk = 0;
            std::string bone;
            Xf off;
            bool found = false;
            // 1. a slot component of the character that knows this slot
            for (const auto& c : ch.comps) {
                if (c.kind != red::CompKind::Slot) continue;
                for (const auto& s : c.slots) if (IEquals(s.slotName, slot)) { found = true; break; }
                if (found) {
                    found = resolveSlot(plan, ch.comps, "character", c.name, slot, sk, bone, off) && !bone.empty();
                    if (found) break;
                }
            }
            // 2. a bone of that name, 3. a hand bone
            if (!found) {
                sk = 0;
                const PlanSkeleton& main = plan.skeletons[0];
                if (main.find(slot) >= 0) bone = main.bones[static_cast<size_t>(main.find(slot))].name;
                else {
                    bone = FallbackHandBone(main);
                    plan.warnings.push_back("slot \"" + slot + "\" not found on the character, weapon goes to " + (bone.empty() ? std::string("the root") : "bone " + bone));
                }
                off = Xf();
            }
            PlanSkeleton& ws = plan.skeletons[firstWeaponSkel];
            ws.parentSkeleton = sk;
            ws.parentBone = bone;
            ws.offset = off;
            Log("weapon %s -> skeleton %d bone \"%s\" (slot %s)", plan.weapon.c_str(), sk, bone.c_str(), slot.c_str());
        }
    }
    if (plan.skeletons.empty()) { error = "nothing to import"; return false; }

    // ---- geometry --------------------------------------------------------
    std::vector<std::string> meshes;
    for (const auto& m : plan.meshes) meshes.push_back(m.mesh);
    if (!meshes.empty()) {
        std::string err;
        if (wk_) {
            if (!wk_->ensureGlb(store_, meshes, err)) plan.warnings.push_back(err);
        }
        for (auto& m : plan.meshes) {
            m.glb = store_.glbFile(m.mesh);
            if (m.glb.empty()) plan.warnings.push_back("no GLB for " + m.mesh);
        }
    }
    Log("PLAN\n%s", plan.summary().c_str());
    return true;
}

} // namespace cp77
