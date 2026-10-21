#include "meshdata.h"
#include "util.h"

#include <cmath>
#include <cstdlib>
#include <map>

namespace cp77 {

bool ParseSubmeshName(const std::string& name, int& chunk, int& lod) {
    const std::string low = Lower(name);
    const size_t p = low.rfind("submesh_");
    if (p == std::string::npos) return false;
    const char* s = low.c_str() + p + 8;
    char* e = nullptr;
    const long c = std::strtol(s, &e, 10);
    if (e == s) return false;
    chunk = static_cast<int>(c);
    lod = 1;
    const size_t l = low.find("_lod_", p);
    if (l != std::string::npos) lod = std::atoi(low.c_str() + l + 5);
    return true;
}

bool LoadMeshGlb(const std::string& path, MeshData& out, std::string& error) {
    out = MeshData();
    out.path = path;
    gltf::Model m;
    if (!m.load(path, error)) return false;

    std::map<std::string, uint16_t> jointIndex;
    auto jointOf = [&](const std::string& name, const Xf& bind) -> uint16_t {
        auto it = jointIndex.find(name);
        if (it != jointIndex.end()) return it->second;
        const uint16_t idx = static_cast<uint16_t>(out.jointNames.size());
        jointIndex[name] = idx;
        out.jointNames.push_back(name);
        out.jointBind.push_back(bind);
        return idx;
    };

    // Per skin: local joint slot -> global joint index.
    std::vector<std::vector<uint16_t>> skinMap(m.skins.size());
    for (size_t s = 0; s < m.skins.size(); ++s) {
        const gltf::Skin& sk = m.skins[s];
        std::vector<float> ibm;
        int comps = 0;
        const bool haveIbm = sk.inverseBindMatrices >= 0 && m.readFloats(sk.inverseBindMatrices, ibm, comps) && comps == 16 &&
                             ibm.size() >= sk.joints.size() * 16;
        for (size_t j = 0; j < sk.joints.size(); ++j) {
            const int node = sk.joints[j];
            if (node < 0 || static_cast<size_t>(node) >= m.nodes.size()) { skinMap[s].push_back(0); continue; }
            Xf bindG = haveIbm ? FromGltfMatrix(&ibm[j * 16]).inverse() : m.worldOf(node);
            skinMap[s].push_back(jointOf(m.nodes[static_cast<size_t>(node)].name, FromGltf(bindG)));
        }
    }
    for (const Xf& b : out.jointBind) {
        Xf r = b;
        r.setRow(3, Vec3());
        if (r.maxDiff(Xf()) > 1e-4) { out.bindHasRotation = true; break; }
    }

    for (size_t ni = 0; ni < m.nodes.size(); ++ni) {
        const gltf::Node& node = m.nodes[ni];
        if (node.mesh < 0 || static_cast<size_t>(node.mesh) >= m.meshes.size()) continue;
        const gltf::Mesh& mesh = m.meshes[static_cast<size_t>(node.mesh)];
        const bool skinned = node.skin >= 0 && static_cast<size_t>(node.skin) < m.skins.size();
        const Xf nodeWorld = FromGltf(m.worldOf(static_cast<int>(ni)));

        std::vector<std::string> mats;
        if (mesh.extras) {
            const json::Value& mn = (*mesh.extras)["materialNames"];
            for (size_t k = 0; k < mn.size(); ++k) mats.push_back(mn.at(k).asString());
        }

        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const gltf::Primitive& pr = mesh.primitives[pi];
            if (pr.mode != 4) { Log("mesh %s: primitive mode %d skipped (only triangles)", path.c_str(), pr.mode); continue; }
            SubMesh sm;
            sm.name = node.name.empty() ? mesh.name : node.name;
            if (mesh.primitives.size() > 1) sm.name += Format("_p%zu", pi);
            if (!ParseSubmeshName(sm.name, sm.chunk, sm.lod)) ParseSubmeshName(mesh.name, sm.chunk, sm.lod);
            sm.materials = mats;
            if (pr.material >= 0 && static_cast<size_t>(pr.material) < m.materials.size() && sm.materials.empty())
                sm.materials.push_back(m.materials[static_cast<size_t>(pr.material)]);

            std::vector<float> f;
            int comps = 0;
            if (!m.readFloats(pr.attribute("POSITION"), f, comps) || comps != 3) {
                Log("mesh %s: %s without positions, skipped", path.c_str(), sm.name.c_str());
                continue;
            }
            const size_t nv = f.size() / 3;
            sm.pos.resize(nv);
            for (size_t v = 0; v < nv; ++v) {
                Vec3 p = PointFromGltf(Vec3(f[v * 3], f[v * 3 + 1], f[v * 3 + 2]));
                if (!skinned) p = nodeWorld.point(p);
                sm.pos[v] = p;
            }
            if (m.readFloats(pr.attribute("NORMAL"), f, comps) && comps == 3 && f.size() == nv * 3) {
                sm.normal.resize(nv);
                for (size_t v = 0; v < nv; ++v) {
                    Vec3 n = PointFromGltf(Vec3(f[v * 3], f[v * 3 + 1], f[v * 3 + 2]));
                    if (!skinned) n = nodeWorld.vector(n);
                    sm.normal[v] = n;
                }
            }
            for (int set = 0; set < 2; ++set) {
                std::vector<float>& uv = set == 0 ? sm.uv0 : sm.uv1;
                if (m.readFloats(pr.attribute(set == 0 ? "TEXCOORD_0" : "TEXCOORD_1"), f, comps) && comps == 2 && f.size() == nv * 2) {
                    uv.resize(nv * 2);
                    for (size_t v = 0; v < nv; ++v) { uv[v * 2] = f[v * 2]; uv[v * 2 + 1] = 1.0f - f[v * 2 + 1]; }
                }
            }
            std::vector<uint32_t> idx;
            if (pr.indices >= 0) {
                if (!m.readUInts(pr.indices, idx, comps)) { Log("mesh %s: bad indices in %s", path.c_str(), sm.name.c_str()); continue; }
            } else {
                idx.resize(nv);
                for (size_t v = 0; v < nv; ++v) idx[v] = static_cast<uint32_t>(v);
            }
            sm.tris.reserve(idx.size());
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                if (idx[t] >= nv || idx[t + 1] >= nv || idx[t + 2] >= nv) continue;
                sm.tris.push_back(idx[t]); sm.tris.push_back(idx[t + 1]); sm.tris.push_back(idx[t + 2]);
            }

            if (skinned) {
                std::vector<uint32_t> j0, j1;
                std::vector<float> w0, w1;
                int cj = 0, cw = 0;
                const bool s0 = m.readUInts(pr.attribute("JOINTS_0"), j0, cj) && m.readFloats(pr.attribute("WEIGHTS_0"), w0, cw) &&
                                cj == 4 && cw == 4 && j0.size() == nv * 4 && w0.size() == nv * 4;
                const bool s1 = s0 && m.readUInts(pr.attribute("JOINTS_1"), j1, cj) && m.readFloats(pr.attribute("WEIGHTS_1"), w1, cw) &&
                                cj == 4 && cw == 4 && j1.size() == nv * 4 && w1.size() == nv * 4;
                if (s0) {
                    const std::vector<uint16_t>& map = skinMap[static_cast<size_t>(node.skin)];
                    sm.influences = s1 ? 8 : 4;
                    sm.joint.assign(nv * static_cast<size_t>(sm.influences), 0);
                    sm.weight.assign(nv * static_cast<size_t>(sm.influences), 0.0f);
                    for (size_t v = 0; v < nv; ++v) {
                        float sum = 0.0f;
                        for (int k = 0; k < sm.influences; ++k) {
                            const uint32_t jl = k < 4 ? j0[v * 4 + static_cast<size_t>(k)] : j1[v * 4 + static_cast<size_t>(k - 4)];
                            float w = k < 4 ? w0[v * 4 + static_cast<size_t>(k)] : w1[v * 4 + static_cast<size_t>(k - 4)];
                            if (!(w > 0.0f) || jl >= map.size()) w = 0.0f;
                            const size_t o = v * static_cast<size_t>(sm.influences) + static_cast<size_t>(k);
                            sm.joint[o] = jl < map.size() ? map[jl] : 0;
                            sm.weight[o] = w;
                            sum += w;
                        }
                        if (sum > 0.0f && std::fabs(sum - 1.0f) > 1e-4f)
                            for (int k = 0; k < sm.influences; ++k) sm.weight[v * static_cast<size_t>(sm.influences) + static_cast<size_t>(k)] /= sum;
                    }
                }
            }
            out.parts.push_back(std::move(sm));
        }
    }
    if (out.parts.empty()) { error = "no triangle meshes in " + path; return false; }
    return true;
}

} // namespace cp77
