// ============================================================
//  glTF 2.0 / GLB reader - only what WolvenKit writes:
//  nodes, meshes with skinning attributes, skins, animations.
//  Accessors are decoded to float (normalised integers scaled,
//  plain integers converted), sparse accessors are supported.
// ============================================================
#pragma once
#include "json.h"
#include "xform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cp77 {
namespace gltf {

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    int mesh = -1;
    int skin = -1;
    bool hasMatrix = false;
    float matrix[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float t[3] = { 0, 0, 0 };
    float r[4] = { 0, 0, 0, 1 };
    float s[3] = { 1, 1, 1 };
    Xf local() const;                 // in glTF space
};

struct Primitive {
    // attribute name -> accessor index
    std::vector<std::pair<std::string, int>> attributes;
    int indices = -1;
    int material = -1;
    int mode = 4;
    int attribute(const char* name) const;
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    const json::Value* extras = nullptr;
};

struct Skin {
    std::string name;
    std::vector<int> joints;
    int inverseBindMatrices = -1;
    int skeleton = -1;
    const json::Value* extras = nullptr;
};

struct Channel { int sampler = -1; int node = -1; std::string path; };
struct Sampler { int input = -1; int output = -1; std::string interpolation = "LINEAR"; };
struct Animation {
    std::string name;
    std::vector<Channel> channels;
    std::vector<Sampler> samplers;
    const json::Value* extras = nullptr;
};

class Model {
public:
    bool load(const std::string& path, std::string& error);
    bool loadFromMemory(const std::string& data, const std::string& baseDir, std::string& error);

    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Skin> skins;
    std::vector<Animation> animations;
    std::vector<std::string> materials;
    std::vector<int> sceneRoots;
    json::Value doc;                   // keeps extras alive

    // Accessor decoding. `comps` receives the component count.
    bool readFloats(int accessor, std::vector<float>& out, int& comps) const;
    bool readUInts(int accessor, std::vector<uint32_t>& out, int& comps) const;
    size_t accessorCount(int accessor) const;

    Xf worldOf(int node) const;        // glTF space, through the parents
    int findNode(const std::string& name) const;

private:
    std::vector<std::string> buffers_;
    struct View { int buffer = -1; size_t offset = 0, length = 0, stride = 0; };
    std::vector<View> views_;
    struct Acc {
        int view = -1; size_t offset = 0, count = 0;
        int compType = 5126; int comps = 1; bool normalized = false;
        int sparseCount = 0; int sparseIdxView = -1; size_t sparseIdxOffset = 0; int sparseIdxType = 5125;
        int sparseValView = -1; size_t sparseValOffset = 0;
    };
    std::vector<Acc> accs_;
    bool decode(int accessor, std::vector<double>& out, int& comps) const;
    bool parse(const std::string& baseDir, const std::string* glbBin, std::string& error);
};

} // namespace gltf
} // namespace cp77
