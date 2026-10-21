#include "prep.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace cp77 {

void PrepSkeletons(const ScenePlan& plan, const PrepOptions& opt, PrepScene& out) {
    const double s = opt.unitsPerMeter;
    out.unitsPerMeter = s;
    out.skeletonHelper.assign(plan.skeletons.size(), -1);
    out.skeletonBones.assign(plan.skeletons.size(), std::vector<int>());
    std::vector<char> done(plan.skeletons.size(), 0);
    for (size_t pass = 0; pass <= plan.skeletons.size(); ++pass) {
        bool progress = false;
        for (size_t k = 0; k < plan.skeletons.size(); ++k) {
            if (done[k]) continue;
            const PlanSkeleton& sk = plan.skeletons[k];
            int parentNode = -1;
            Xf parentWorld;
            if (sk.parentSkeleton >= 0 && static_cast<size_t>(sk.parentSkeleton) < plan.skeletons.size() &&
                static_cast<size_t>(sk.parentSkeleton) != k) {
                const size_t p = static_cast<size_t>(sk.parentSkeleton);
                if (!done[p] && pass < plan.skeletons.size()) continue;      // parent first
                if (done[p]) {
                    const int b = plan.skeletons[p].find(sk.parentBone);
                    parentNode = (b >= 0) ? out.skeletonBones[p][static_cast<size_t>(b)] : out.skeletonHelper[p];
                    parentWorld = out.nodes[static_cast<size_t>(parentNode)].world;
                }
            }
            PrepNode h;
            h.name = sk.name;
            h.parent = parentNode;
            h.helper = true;
            h.skeleton = static_cast<int>(k);
            h.world = parentNode >= 0 ? ScaledTranslation(sk.offset, s) * parentWorld : Xf();
            out.nodes.push_back(h);
            const int helperIdx = static_cast<int>(out.nodes.size()) - 1;
            out.skeletonHelper[k] = helperIdx;
            const Xf hw = out.nodes.back().world;
            std::vector<int>& map = out.skeletonBones[k];
            map.assign(sk.bones.size(), -1);
            // plan bones are parent-first by construction, but do not rely on it
            std::vector<char> placed(sk.bones.size(), 0);
            for (size_t guard = 0; guard <= sk.bones.size(); ++guard) {
                bool any = false;
                for (size_t i = 0; i < sk.bones.size(); ++i) {
                    if (placed[i]) continue;
                    const int pb = sk.bones[i].parent;
                    if (pb >= 0 && static_cast<size_t>(pb) < sk.bones.size() && !placed[static_cast<size_t>(pb)] && guard < sk.bones.size()) continue;
                    PrepNode n;
                    n.name = sk.bones[i].name;
                    n.skeleton = static_cast<int>(k);
                    n.parent = (pb >= 0 && static_cast<size_t>(pb) < sk.bones.size() && placed[static_cast<size_t>(pb)]) ? map[static_cast<size_t>(pb)] : helperIdx;
                    n.world = ScaledTranslation(sk.bones[i].restWorld, s) * hw;
                    out.nodes.push_back(n);
                    map[i] = static_cast<int>(out.nodes.size()) - 1;
                    placed[i] = 1;
                    any = true;
                }
                if (!any) break;
            }
            done[k] = 1;
            progress = true;
        }
        if (!progress) break;
    }
}

namespace {
struct WeldKey {
    float p[3];
    int16_t n[3];
    uint16_t j[8];
    uint16_t w[8];
    bool operator==(const WeldKey& o) const { return std::memcmp(this, &o, sizeof(WeldKey)) == 0; }
};
struct WeldHash {
    size_t operator()(const WeldKey& k) const {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&k);
        size_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(WeldKey); ++i) { h ^= b[i]; h *= 1099511628211ull; }
        return h;
    }
};
}

bool PrepMeshFrom(const ScenePlan& plan, size_t mi, const MeshData& data, const PrepOptions& opt,
                  PrepScene& scene, PrepMesh& out, std::string& error) {
    out = PrepMesh();
    if (mi >= plan.meshes.size()) { error = "bad mesh index"; return false; }
    const PlanMesh& pm = plan.meshes[mi];
    const double s = opt.unitsPerMeter;
    out.name = pm.component.empty() ? FileStem(pm.mesh) : pm.component;
    out.source = pm.mesh;
    out.owner = pm.owner;

    int minLod = 1 << 30;
    for (const auto& p : data.parts) minLod = std::min(minLod, p.lod);
    std::vector<const SubMesh*> parts;
    for (const auto& p : data.parts) {
        if (p.lod != minLod) continue;
        if (!ChunkVisible(pm.chunkMask, p.chunk)) { ++out.chunksHidden; continue; }
        parts.push_back(&p);
        ++out.chunksShown;
    }
    if (parts.empty()) { error = out.name + ": every chunk hidden by the chunk mask"; return false; }

    bool weighted = false;
    for (const SubMesh* p : parts) if (p->influences > 0) weighted = true;
    const bool skinned = weighted && pm.skeleton >= 0 && static_cast<size_t>(pm.skeleton) < plan.skeletons.size();

    Xf base;   // object space -> world
    if (skinned) {
        out.skeleton = pm.skeleton;
        base = scene.nodes[static_cast<size_t>(scene.skeletonHelper[static_cast<size_t>(pm.skeleton)])].world;
    } else {
        int sk = pm.skeleton >= 0 ? pm.skeleton : pm.attachSkeleton;
        if (sk < 0 || static_cast<size_t>(sk) >= plan.skeletons.size()) sk = 0;
        int node = scene.skeletonHelper[static_cast<size_t>(sk)];
        const int b = plan.skeletons[static_cast<size_t>(sk)].find(pm.attachBone);
        if (b >= 0 && !pm.attachBone.empty()) node = scene.skeletonBones[static_cast<size_t>(sk)][static_cast<size_t>(b)];
        out.parentNode = node;
        out.nodeWorld = ScaledTranslation(pm.offset, s) * scene.nodes[static_cast<size_t>(node)].world;
    }

    // joint -> scene node
    std::vector<int> jointNode(data.jointNames.size(), -1);
    if (skinned) {
        for (size_t j = 0; j < data.jointNames.size(); ++j) {
            const std::string& jn = data.jointNames[j];
            int b = plan.skeletons[static_cast<size_t>(pm.skeleton)].find(jn);
            if (b >= 0) { jointNode[j] = scene.skeletonBones[static_cast<size_t>(pm.skeleton)][static_cast<size_t>(b)]; continue; }
            for (size_t k = 0; k < plan.skeletons.size() && jointNode[j] < 0; ++k) {
                if (plan.skeletons[k].owner != pm.owner) continue;
                b = plan.skeletons[k].find(jn);
                if (b >= 0) jointNode[j] = scene.skeletonBones[k][static_cast<size_t>(b)];
            }
        }
    }

    // ---- geometry, welded ----------------------------------------------
    const int inf = skinned ? 8 : 0;
    std::unordered_map<WeldKey, uint32_t, WeldHash> weld;
    std::vector<int> usedJoint(data.jointNames.size(), 0);
    size_t tvBase = 0;
    bool anyUv1 = false;
    for (const SubMesh* p : parts) if (!p->uv1.empty()) anyUv1 = true;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const SubMesh& p = *parts[pi];
        const size_t nv = p.pos.size();
        std::vector<uint32_t> remap(nv);
        for (size_t v = 0; v < nv; ++v) {
            WeldKey k;
            std::memset(&k, 0, sizeof k);
            Vec3 pos = p.pos[v] * s;
            pos = skinned ? base.point(pos) : pos;
            k.p[0] = static_cast<float>(pos.x); k.p[1] = static_cast<float>(pos.y); k.p[2] = static_cast<float>(pos.z);
            if (!p.normal.empty()) {
                k.n[0] = static_cast<int16_t>(std::lround(p.normal[v].x * 1000.0));
                k.n[1] = static_cast<int16_t>(std::lround(p.normal[v].y * 1000.0));
                k.n[2] = static_cast<int16_t>(std::lround(p.normal[v].z * 1000.0));
            }
            if (skinned && p.influences > 0) {
                for (int i = 0; i < p.influences && i < 8; ++i) {
                    const size_t o = v * static_cast<size_t>(p.influences) + static_cast<size_t>(i);
                    k.j[i] = p.joint[o];
                    k.w[i] = static_cast<uint16_t>(std::lround(p.weight[o] * 65535.0f));
                }
            }
            uint32_t idx;
            auto it = opt.weld ? weld.find(k) : weld.end();
            if (it != weld.end()) {
                idx = it->second;
            } else {
                idx = static_cast<uint32_t>(out.verts.size());
                out.verts.push_back(pos);
                if (opt.weld) weld.emplace(k, idx);
                if (skinned) {
                    for (int i = 0; i < inf; ++i) {
                        int node = -1;
                        float w = 0.0f;
                        if (i < p.influences) {
                            const size_t o = v * static_cast<size_t>(p.influences) + static_cast<size_t>(i);
                            const uint16_t j = p.joint[o];
                            w = p.weight[o];
                            if (w > 0.0f && j < jointNode.size()) {
                                node = jointNode[j];
                                if (node < 0) {
                                    if (!usedJoint[j]) out.missingBones.push_back(data.jointNames[j]);
                                    usedJoint[j] = 2;
                                    w = 0.0f;
                                } else if (!usedJoint[j]) {
                                    usedJoint[j] = 1;
                                }
                            } else {
                                w = 0.0f;
                            }
                        }
                        out.infNode.push_back(w > 0.0f ? node : -1);
                        out.infWeight.push_back(w > 0.0f ? w : 0.0f);
                    }
                }
            }
            remap[v] = idx;
        }
        for (size_t t = 0; t + 2 < p.tris.size(); t += 3) {
            out.faces.push_back(remap[p.tris[t]]);
            out.faces.push_back(remap[p.tris[t + 1]]);
            out.faces.push_back(remap[p.tris[t + 2]]);
            out.matId.push_back(static_cast<uint16_t>(pi));
            out.tvFaces.push_back(static_cast<uint32_t>(tvBase + p.tris[t]));
            out.tvFaces.push_back(static_cast<uint32_t>(tvBase + p.tris[t + 1]));
            out.tvFaces.push_back(static_cast<uint32_t>(tvBase + p.tris[t + 2]));
        }
        // tverts: one per source vertex, zero where a part has no UVs
        for (size_t v = 0; v < nv; ++v) {
            out.uv0.push_back(p.uv0.empty() ? 0.0f : p.uv0[v * 2]);
            out.uv0.push_back(p.uv0.empty() ? 0.0f : p.uv0[v * 2 + 1]);
            if (anyUv1) {
                out.uv1.push_back(p.uv1.empty() ? 0.0f : p.uv1[v * 2]);
                out.uv1.push_back(p.uv1.empty() ? 0.0f : p.uv1[v * 2 + 1]);
            }
        }
        tvBase += nv;
        std::string mat = p.materials.empty() ? p.name : p.materials[0];
        out.materials.push_back(mat);
    }
    if (skinned) {
        out.influences = inf;
        // a vertex left without any influence goes to the skeleton's first bone
        const std::vector<int>& bones = scene.skeletonBones[static_cast<size_t>(pm.skeleton)];
        const int fallback = bones.empty() ? -1 : bones[0];
        size_t orphans = 0;
        for (size_t v = 0; v < out.verts.size(); ++v) {
            float sum = 0.0f;
            for (int i = 0; i < inf; ++i) sum += out.infWeight[v * static_cast<size_t>(inf) + static_cast<size_t>(i)];
            if (sum <= 0.0f) {
                out.infNode[v * static_cast<size_t>(inf)] = fallback;
                out.infWeight[v * static_cast<size_t>(inf)] = fallback >= 0 ? 1.0f : 0.0f;
                ++orphans;
            } else if (std::fabs(sum - 1.0f) > 1e-4f) {
                for (int i = 0; i < inf; ++i) out.infWeight[v * static_cast<size_t>(inf) + static_cast<size_t>(i)] /= sum;
            }
        }
        if (orphans) scene.warnings.push_back(Format("%s: %zu vertices without a known bone, bound to the root", out.name.c_str(), orphans));
        if (!out.missingBones.empty())
            scene.warnings.push_back(out.name + ": bones not in the skeleton: " + Join(out.missingBones, ", "));

        // bind pose of the mesh against the rest pose of the bones
        if (opt.useMeshBind) {
            for (size_t j = 0; j < data.jointNames.size(); ++j) {
                if (usedJoint[j] != 1 || j >= data.jointBind.size()) continue;
                const int node = jointNode[j];
                const Xf& rest = scene.nodes[static_cast<size_t>(node)].world;
                Xf bind = ScaledTranslation(data.jointBind[j], s) * base;
                if (!data.bindHasRotation) {          // older WolvenKit: only positions are real
                    Xf r = rest;
                    r.setRow(3, bind.translation());
                    bind = r;
                }
                const double tolT = 0.001 * s;          // 1 mm
                const double dt = (bind.translation() - rest.translation()).length();
                Xf a = bind.orthonormalized(), b = rest.orthonormalized();
                a.setRow(3, Vec3()); b.setRow(3, Vec3());
                if (dt > tolT || a.maxDiff(b) > 1e-3) out.bindOverrides.push_back({ node, bind });
            }
        }
    }
    return true;
}

} // namespace cp77
