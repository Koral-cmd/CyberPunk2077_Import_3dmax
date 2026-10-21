// ============================================================
//  The Max scene from a plan: helpers, bones, meshes, Skin,
//  materials. All numbers come ready from core/prep.
// ============================================================
#include "plugin_internal.h"

#include "../core/animprep.h"
#include "../core/meshdata.h"

#include <iskin.h>
#include <stdmat.h>

#include <algorithm>
#include <ctime>
#include <cmath>
#include <map>
#include <unordered_map>

using namespace cp77;

namespace cp77max {

namespace {

void SetWorld(INode* n, const Xf& x) {
    Matrix3 m = ToMax(x);
    n->SetNodeTM(0, m);
}

INode* MakeHelper(Interface* ip, const std::string& name, float size) {
    Object* obj = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
    if (!obj) return nullptr;
    INode* n = ip->CreateObjectNode(obj);
    if (!n) return nullptr;
    n->SetName(M(name).data());
    IParamBlock2* pb = obj->GetParamBlockByID(0);   // pointobj_size is the first parameter
    if (pb) pb->SetValue(0, 0, size);
    return n;
}

INode* MakeBone(Interface* ip, const std::string& name, float width, float length) {
    Object* obj = static_cast<Object*>(ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
    bool bone = obj != nullptr;
    if (!obj) obj = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
    if (!obj) return nullptr;
    INode* n = ip->CreateObjectNode(obj);
    if (!n) return nullptr;
    n->SetName(M(name).data());
    n->ShowBone(1);
    n->SetBoneNodeOnOff(TRUE, 0);
    // Max would otherwise turn child offsets into parent rotations
    n->SetBoneAutoAlign(FALSE);
    n->SetBoneFreezeLen(TRUE);
    n->SetRenderable(FALSE);
    if (bone) {
        // GetParamBlockByID: Object::GetParamBlock() hides the Animatable overload.
        // boneobj_width, boneobj_height, boneobj_taper, boneobj_length
        IParamBlock2* pb = obj->GetParamBlockByID(0);
        if (pb) {
            pb->SetValue(0, 0, width);
            pb->SetValue(1, 0, width);
            pb->SetValue(3, 0, length);
        }
    }
    return n;
}

Color MatColor(size_t i) {
    static const float pal[8][3] = { { 0.75f, 0.62f, 0.55f }, { 0.35f, 0.45f, 0.70f }, { 0.55f, 0.70f, 0.40f }, { 0.80f, 0.70f, 0.30f },
                                     { 0.60f, 0.40f, 0.65f }, { 0.40f, 0.65f, 0.65f }, { 0.70f, 0.40f, 0.35f }, { 0.50f, 0.50f, 0.50f } };
    const float* c = pal[i % 8];
    return Color(c[0], c[1], c[2]);
}

Mtl* MakeMaterial(const std::string& name, const std::vector<std::string>& subs, std::map<std::string, Mtl*>& cache) {
    auto std_ = [&](const std::string& n) -> Mtl* {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        StdMat2* m = NewDefaultStdMat();
        if (!m) return nullptr;
        m->SetName(M(n));
        m->SetDiffuse(MatColor(cache.size()), 0);
        cache[n] = m;
        return m;
    };
    if (subs.size() == 1) return std_(subs[0]);
    MultiMtl* mm = NewDefaultMultiMtl();
    if (!mm) return nullptr;
    mm->SetName(M(name));
    mm->SetNumSubMtls(static_cast<int>(subs.size()));
    for (size_t i = 0; i < subs.size(); ++i) {
        MSTR sub = M(subs[i]);
        mm->SetSubMtlAndName(static_cast<int>(i), std_(subs[i]), sub);
    }
    return mm;
}

INode* MakeMesh(Interface* ip, const PrepMesh& pm) {
    TriObject* tri = CreateNewTriObject();
    if (!tri) return nullptr;
    Mesh& mesh = tri->GetMesh();
    const int nv = static_cast<int>(pm.verts.size());
    const int nf = static_cast<int>(pm.faces.size() / 3);
    mesh.setNumVerts(nv);
    for (int v = 0; v < nv; ++v) {
        const Vec3& p = pm.verts[static_cast<size_t>(v)];
        mesh.setVert(v, static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }
    mesh.setNumFaces(nf);
    for (int f = 0; f < nf; ++f) {
        mesh.faces[f].setVerts(static_cast<int>(pm.faces[static_cast<size_t>(f) * 3]), static_cast<int>(pm.faces[static_cast<size_t>(f) * 3 + 1]),
                               static_cast<int>(pm.faces[static_cast<size_t>(f) * 3 + 2]));
        mesh.faces[f].setEdgeVisFlags(1, 1, 1);
        mesh.faces[f].setSmGroup(1);
        mesh.faces[f].setMatID(pm.matId[static_cast<size_t>(f)]);
    }
    const int nt = static_cast<int>(pm.uv0.size() / 2);
    if (nt > 0) {
        mesh.setNumTVerts(nt);
        for (int t = 0; t < nt; ++t) mesh.setTVert(t, pm.uv0[static_cast<size_t>(t) * 2], pm.uv0[static_cast<size_t>(t) * 2 + 1], 0.0f);
        mesh.setNumTVFaces(nf);
        for (int f = 0; f < nf; ++f)
            mesh.tvFace[f].setTVerts(static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3]), static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3 + 1]),
                                     static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3 + 2]));
    }
    const int nt2 = static_cast<int>(pm.uv1.size() / 2);
    if (nt2 > 0 && nt2 == nt) {
        mesh.setMapSupport(2, TRUE);
        mesh.setNumMapVerts(2, nt2);
        for (int t = 0; t < nt2; ++t) mesh.setMapVert(2, t, Point3(pm.uv1[static_cast<size_t>(t) * 2], pm.uv1[static_cast<size_t>(t) * 2 + 1], 0.0f));
        mesh.setNumMapFaces(2, nf);
        TVFace* mf = mesh.mapFaces(2);
        for (int f = 0; f < nf; ++f)
            mf[f].setTVerts(static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3]), static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3 + 1]),
                            static_cast<int>(pm.tvFaces[static_cast<size_t>(f) * 3 + 2]));
    }
    mesh.InvalidateTopologyCache();
    mesh.InvalidateGeomCache();
    mesh.buildNormals();
    INode* node = ip->CreateObjectNode(tri);
    if (node) node->SetName(M(pm.name).data());
    return node;
}

// Skin modifier, filled through ISkinImportData (the order the SDK needs:
// add modifier, add bones, evaluate the node once, then the weights).
//
// Bind pose: Skin remembers each bone where it stands when it is added. The
// mesh's vertices sit in the mesh's OWN bind pose, which can differ from the
// skeleton's rest pose (A-pose). So the bones this mesh uses are moved to the
// mesh's bind pose first, the skin is built, and then the whole skeleton goes
// back to its rest pose - the mesh then follows the skeleton like in the game.
// (ISkinImportData::SetBoneTm was used before; it gave broken meshes in Max 2027.)
bool AddSkin(Interface* ip, INode* node, const PrepMesh& pm, const PrepScene& scene,
             const std::vector<INode*>& sceneNodes, std::string& note) {
    Modifier* mod = static_cast<Modifier*>(ip->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
    ISkinImportData* imp = mod ? static_cast<ISkinImportData*>(mod->GetInterface(I_SKINIMPORTDATA)) : nullptr;
    if (!mod || !imp) { note = "Skin modifier not available"; return false; }
    Interface7* ip7 = GetCOREInterface7();
    if (!ip7 || ip7->AddModifier(*node, *mod) != Interface7::kRES_SUCCESS) { note = "AddModifier failed"; return false; }

    // 1. bones to the mesh's bind pose (scene nodes are ordered parent before child)
    std::vector<const PrepMesh::Bind*> binds(sceneNodes.size(), nullptr);
    for (const auto& o : pm.bindOverrides)
        if (o.node >= 0 && static_cast<size_t>(o.node) < sceneNodes.size()) binds[static_cast<size_t>(o.node)] = &o;
    int moved = 0;
    for (size_t n = 0; n < sceneNodes.size(); ++n) {
        if (!binds[n] || !sceneNodes[n]) continue;
        SetWorld(sceneNodes[n], binds[n]->world);
        ++moved;
    }

    std::vector<char> used(sceneNodes.size(), 0);
    for (size_t i = 0; i < pm.infNode.size(); ++i)
        if (pm.infNode[i] >= 0 && pm.infWeight[i] > 0.0f) used[static_cast<size_t>(pm.infNode[i])] = 1;
    int bones = 0;
    for (size_t b = 0; b < used.size(); ++b) {
        if (!used[b] || !sceneNodes[b]) continue;
        imp->AddBoneEx(sceneNodes[b], FALSE);
        ++bones;
    }
    node->EvalWorldState(ip->GetTime());

    size_t set = 0;
    const size_t inf = static_cast<size_t>(pm.influences);
    for (size_t v = 0; v < pm.verts.size(); ++v) {
        Tab<INode*> bn;
        Tab<float> wt;
        for (size_t k = 0; k < inf; ++k) {
            const int n = pm.infNode[v * inf + k];
            float w = pm.infWeight[v * inf + k];
            if (n < 0 || w <= 0.0f || !sceneNodes[static_cast<size_t>(n)]) continue;
            INode* b = sceneNodes[static_cast<size_t>(n)];
            bn.Append(1, &b);
            wt.Append(1, &w);
        }
        if (bn.Count() > 0 && imp->AddWeights(node, static_cast<int>(v), bn, wt)) ++set;
    }
    node->EvalWorldState(ip->GetTime());

    // 2. the skeleton back to its rest pose
    if (moved > 0) {
        for (size_t n = 0; n < sceneNodes.size() && n < scene.nodes.size(); ++n)
            if (sceneNodes[n] && !scene.nodes[n].helper) SetWorld(sceneNodes[n], scene.nodes[n].world);
        node->EvalWorldState(ip->GetTime());
    }
    note = Format("%d bones, %zu/%zu vertices weighted, %d bones bound at the mesh's own pose (max offset %.2f units at %s)",
                  bones, set, pm.verts.size(), moved, pm.maxBindOffset, pm.maxBindBone.c_str());
    return true;
}

struct Redraw {   // scene redraw and undo off while building, always back on
    Interface* ip;
    explicit Redraw(Interface* i) : ip(i) { theHold.Suspend(); ip->DisableSceneRedraw(); SuspendAnimate(); AnimateOff(); }
    ~Redraw() { ResumeAnimate(); ip->EnableSceneRedraw(); theHold.Resume(); ip->RedrawViews(ip->GetTime()); }
};

} // namespace

std::wstring BuildScene(const ScenePlan& plan) {
    Interface* ip = GetCOREInterface();
    if (!ip) return L"ERROR\tno 3ds Max interface";
    const Settings& st = Config();
    PrepOptions opt;
    opt.unitsPerMeter = UnitsPerMeter();
    opt.weld = st.getBool(Settings::WeldVertices, true);
    opt.useMeshBind = st.getBool(Settings::UseMeshBind, true);
    Log("units per metre %.4f", opt.unitsPerMeter);

    PrepScene scene;
    PrepSkeletons(plan, opt, scene);
    const float boneW = static_cast<float>(st.getDouble(Settings::BoneSize, 0.01) * opt.unitsPerMeter);

    Redraw redraw(ip);
    // ---- nodes ---------------------------------------------------------------
    std::vector<INode*> nodes(scene.nodes.size(), nullptr);
    static unsigned importCounter = 0;
    const std::string stamp = Format("%llx%u", static_cast<unsigned long long>(std::time(nullptr)), ++importCounter);
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const PrepNode& n = scene.nodes[i];
        double len = 0.0;   // to the first child, for the bone display
        for (size_t c = i + 1; c < scene.nodes.size(); ++c)
            if (scene.nodes[c].parent == static_cast<int>(i)) { len = (scene.nodes[c].world.translation() - n.world.translation()).length(); break; }
        if (len <= 0.0) len = boneW * 4.0;
        nodes[i] = n.helper ? MakeHelper(ip, n.name, boneW * 20.0f) : MakeBone(ip, n.name, boneW, static_cast<float>(len));
    }
    // hierarchy first, then the world matrices parent before child
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const int p = scene.nodes[i].parent;
        if (nodes[i] && p >= 0 && nodes[static_cast<size_t>(p)]) nodes[static_cast<size_t>(p)]->AttachChild(nodes[i], 0);
    }
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        if (!nodes[i]) continue;
        SetWorld(nodes[i], scene.nodes[i].world);
        const PrepNode& n = scene.nodes[i];
        const PlanSkeleton& sk = plan.skeletons[static_cast<size_t>(n.skeleton)];
        const std::string id = stamp + "_" + std::to_string(n.skeleton);
        nodes[i]->SetUserPropString(MSTR(CP77_PROP_SKEL), M(id));
        const Xf parentW = n.parent >= 0 ? scene.nodes[static_cast<size_t>(n.parent)].world : Xf();
        nodes[i]->SetUserPropString(MSTR(CP77_PROP_REST), M(XfToString(n.world * parentW.inverse())));
        if (n.helper) {
            nodes[i]->SetUserPropString(MSTR(CP77_PROP_HELPER), MSTR(_T("1")));
            nodes[i]->SetUserPropString(MSTR(CP77_PROP_ENTITY), M(sk.entity));
            nodes[i]->SetUserPropString(MSTR(CP77_PROP_OWNER), M(sk.owner));
            std::vector<std::string> rigs;
            for (const auto& r : sk.rigs) if (!StartsWith(r, "comp:")) rigs.push_back(r);
            nodes[i]->SetUserPropString(MSTR(CP77_PROP_RIGS), M(Join(rigs, ";")));
            nodes[i]->SetUserPropString(MSTR(CP77_PROP_ANIMSETS), M(Join(sk.animsets, ";")));
        }
    }

    // ---- meshes --------------------------------------------------------------
    std::map<std::string, Mtl*> mtlCache;
    size_t made = 0, skinned = 0, tris = 0, failed = 0;
    std::string report;
    std::map<std::string, MeshData> loaded;
    for (size_t mi = 0; mi < plan.meshes.size(); ++mi) {
        const PlanMesh& pm = plan.meshes[mi];
        if (pm.glb.empty()) { ++failed; continue; }
        auto it = loaded.find(pm.glb);
        if (it == loaded.end()) {
            MeshData md;
            std::string err;
            if (!LoadMeshGlb(pm.glb, md, err)) { Log("mesh %s: %s", pm.mesh.c_str(), err.c_str()); report += pm.component + ": " + err + "\n"; ++failed; continue; }
            it = loaded.emplace(pm.glb, std::move(md)).first;
        }
        PrepMesh prep;
        std::string err;
        if (!PrepMeshFrom(plan, mi, it->second, opt, scene, prep, err)) { Log("mesh %s: %s", pm.component.c_str(), err.c_str()); report += err + "\n"; continue; }
        INode* node = MakeMesh(ip, prep);
        if (!node) { ++failed; continue; }
        ++made;
        tris += prep.faces.size() / 3;
        node->SetUserPropString(MSTR(CP77_PROP_SOURCE), M(pm.mesh + (pm.appearance.empty() ? "" : " @" + pm.appearance)));
        Mtl* mtl = MakeMaterial(prep.name + "_mtl", prep.materials, mtlCache);
        if (mtl) node->SetMtl(mtl);
        if (prep.skeleton >= 0) {
            SetWorld(node, Xf());
            std::string note;
            if (AddSkin(ip, node, prep, scene, nodes, note)) ++skinned;
            Log("mesh %-32s %6zu verts %6zu tris, chunks %zu shown %zu hidden, skin: %s", prep.name.c_str(), prep.verts.size(),
                prep.faces.size() / 3, prep.chunksShown, prep.chunksHidden, note.c_str());
            // not parented: a skinned mesh under its skeleton would be moved twice
        } else {
            INode* parent = (prep.parentNode >= 0) ? nodes[static_cast<size_t>(prep.parentNode)] : nullptr;
            if (parent) parent->AttachChild(node, 0);
            SetWorld(node, prep.nodeWorld);
            Log("mesh %-32s %6zu verts %6zu tris, rigid on %s", prep.name.c_str(), prep.verts.size(), prep.faces.size() / 3,
                prep.parentNode >= 0 ? scene.nodes[static_cast<size_t>(prep.parentNode)].name.c_str() : "-");
        }
    }

    size_t boneCount = 0;
    for (const auto& n : scene.nodes) if (!n.helper) ++boneCount;
    std::string head = Format("%zu skeleton(s), %zu bones, %zu meshes (%zu skinned, %zu triangles)", plan.skeletons.size(),
                              boneCount, made, skinned, tris);
    if (failed) head += Format(", %zu mesh(es) missing", failed);
    for (const auto& w : plan.warnings) report += "warning: " + w + "\n";
    for (const auto& w : scene.warnings) report += "warning: " + w + "\n";
    Log("%s\n%s", head.c_str(), report.c_str());
    return W(head + "\n" + report + "Log: " + JoinPath(Settings::DefaultDir(), "import.log"));
}

} // namespace cp77max
