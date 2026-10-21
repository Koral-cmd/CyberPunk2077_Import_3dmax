// ============================================================
//  Geometry from a WolvenKit mesh GLB ("uncook --mesh-export-type
//  MeshOnly"), converted to game space (= 3ds Max space, metres).
//
//  WolvenKit writes one glTF node per submesh, named
//      <meshname>_submesh_<NN>_LOD_<L>      (or submesh_NN_LOD_L)
//  NN is the chunk index the entity's chunkMask refers to.
//  The skin holds the mesh's own bones ("orphan rig": flat
//  joints at their bind pose); names = the mesh's BoneNames.
// ============================================================
#pragma once
#include "gltf.h"
#include "xform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cp77 {

struct SubMesh {
    std::string name;
    int chunk = -1;                 // NN from the name, -1 if unknown
    int lod = 1;
    std::vector<Vec3> pos;          // game space, metres
    std::vector<Vec3> normal;       // may be empty
    std::vector<float> uv0, uv1;    // 2 per vertex, V already flipped for Max (1 - v)
    std::vector<uint32_t> tris;     // 3 per triangle
    // Skinning: up to 8 influences per vertex.
    int influences = 0;             // 0 = rigid
    std::vector<uint16_t> joint;    // influences per vertex, index into MeshData::jointNames
    std::vector<float> weight;
    std::vector<std::string> materials;   // extras.materialNames (one per mesh appearance)
};

struct MeshData {
    std::string path;
    std::vector<SubMesh> parts;
    std::vector<std::string> jointNames;
    std::vector<Xf> jointBind;      // bind pose world of each joint (game space), from the IBMs
    bool bindHasRotation = false;   // false when every bind rotation is identity (older WolvenKit)
};

bool LoadMeshGlb(const std::string& path, MeshData& out, std::string& error);

// Chunk visible under the mask? Unknown chunks (-1) are always shown.
inline bool ChunkVisible(uint64_t mask, int chunk) {
    if (chunk < 0 || chunk >= 64) return true;
    return ((mask >> chunk) & 1ull) != 0;
}

// "abc_submesh_03_LOD_1" -> chunk 3, lod 1. Returns false if the pattern is absent.
bool ParseSubmeshName(const std::string& name, int& chunk, int& lod);

} // namespace cp77
