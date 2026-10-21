// ============================================================
//  Prep - the last step before 3ds Max: everything in Max space
//  and Max units, ready to be copied into SDK calls.
//
//  Kept out of the plugin so it can be tested without Max: the
//  plugin layer only creates nodes, copies numbers and calls
//  the Skin API.
//
//  Scene layout:
//    <skeleton helper>          Point helper, carries the user
//      Root                     properties (cp77_skel, rig, animsets)
//        Hips ...               bone objects
//  A skeleton that hangs on another one (weapon on WeaponRight)
//  has its helper parented to that bone at the slot offset.
// ============================================================
#pragma once
#include "assembler.h"
#include "meshdata.h"
#include "xform.h"

#include <string>
#include <vector>

namespace cp77 {

struct PrepNode {
    std::string name;
    int parent = -1;                 // index into PrepScene::nodes
    Xf world;                        // Max space, Max units
    int skeleton = -1;
    bool helper = false;             // skeleton root helper (else: bone)
};

struct PrepMesh {
    std::string name;
    std::string source;              // .mesh path
    std::string owner;
    int skeleton = -1;               // skinned against (-1 = rigid)
    int parentNode = -1;             // rigid: attach to this node
    Xf nodeWorld;                    // identity for skinned meshes
    std::vector<Vec3> verts;         // object space of the node
    std::vector<uint32_t> faces;     // 3 per face, into verts
    std::vector<uint16_t> matId;     // per face
    std::vector<float> uv0;          // 2 per tvert (tverts = source vertices)
    std::vector<float> uv1;
    std::vector<uint32_t> tvFaces;   // 3 per face, into the tverts
    std::vector<std::string> materials;   // per matId
    // skin: influences per Max vertex
    int influences = 0;
    std::vector<int> infNode;        // PrepScene::nodes index, -1 = unused
    std::vector<float> infWeight;
    struct Bind { int node; Xf world; };
    std::vector<Bind> bindOverrides; // bones whose bind pose differs from the rest pose
    double maxBindOffset = 0.0;      // largest bind-vs-rest distance (Max units) - diagnostics
    std::string maxBindBone;
    std::vector<std::string> missingBones;
    size_t chunksShown = 0, chunksHidden = 0;
    int lod = 1;                     // LOD actually used
    std::vector<int> lodsAvailable;
    size_t chunksSkipped = 0;        // left out by skipMaterials
};

struct PrepScene {
    std::vector<PrepNode> nodes;     // parents always before children
    std::vector<int> skeletonHelper; // per plan skeleton
    std::vector<std::vector<int>> skeletonBones;   // per plan skeleton: bone i -> node
    std::vector<PrepMesh> meshes;
    std::vector<std::string> warnings;
    double unitsPerMeter = 1.0;
};

struct PrepOptions {
    double unitsPerMeter = 1.0;
    bool weld = true;
    bool useMeshBind = true;
    int lod = 1;                     // wanted LOD (1 = full detail); nearest available is used
    int appearance = 0;              // index into each submesh's per-appearance material list
    std::vector<std::string> skipMaterials;   // submeshes with these materials are left out (decals)
};

// Nodes for all skeletons of the plan.
void PrepSkeletons(const ScenePlan& plan, const PrepOptions& opt, PrepScene& out);
// One Max mesh for one plan mesh (call after PrepSkeletons).
bool PrepMeshFrom(const ScenePlan& plan, size_t meshIndex, const MeshData& data, const PrepOptions& opt,
                  PrepScene& scene, PrepMesh& out, std::string& error);

} // namespace cp77
