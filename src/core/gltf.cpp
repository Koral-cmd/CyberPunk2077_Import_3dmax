#include "gltf.h"
#include "util.h"

#include <cstring>

namespace cp77 {
namespace gltf {

Xf Node::local() const {
    if (hasMatrix) return FromGltfMatrix(matrix);
    return FromTRS(Vec3(t[0], t[1], t[2]), Quatd(r[0], r[1], r[2], r[3]), Vec3(s[0], s[1], s[2]));
}

int Primitive::attribute(const char* name) const {
    for (const auto& a : attributes) if (a.first == name) return a.second;
    return -1;
}

static uint32_t U32(const std::string& d, size_t o) {
    uint32_t v;
    std::memcpy(&v, d.data() + o, 4);
    return v;   // GLB is little endian; so is every machine Max runs on
}

static bool Base64(const std::string& in, std::string& out) {
    static int8_t tbl[256];
    static bool init = false;
    if (!init) {
        std::memset(tbl, -1, sizeof tbl);
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) tbl[static_cast<unsigned char>(a[i])] = static_cast<int8_t>(i);
        init = true;
    }
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        const int8_t v = tbl[static_cast<unsigned char>(c)];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((acc >> bits) & 0xFF)); }
    }
    return true;
}

bool Model::load(const std::string& path, std::string& error) {
    std::string data;
    if (!ReadFile(path, data)) { error = "cannot read " + path; return false; }
    return loadFromMemory(data, DirName(path), error);
}

bool Model::loadFromMemory(const std::string& data, const std::string& baseDir, std::string& error) {
    *this = Model();
    if (data.size() >= 12 && std::memcmp(data.data(), "glTF", 4) == 0) {
        const uint32_t total = U32(data, 8);
        if (total > data.size()) { error = "GLB truncated"; return false; }
        size_t o = 12;
        std::string jsonText, bin;
        bool haveBin = false;
        while (o + 8 <= total) {
            const uint32_t len = U32(data, o);
            const uint32_t type = U32(data, o + 4);
            o += 8;
            if (o + len > total) { error = "GLB chunk out of range"; return false; }
            if (type == 0x4E4F534A) jsonText.assign(data, o, len);         // JSON
            else if (type == 0x004E4942 && !haveBin) { bin.assign(data, o, len); haveBin = true; } // BIN
            o += (len + 3u) & ~3u;
        }
        if (jsonText.empty()) { error = "GLB without JSON chunk"; return false; }
        std::string perr;
        if (!json::Parse(jsonText, doc, &perr)) { error = "GLB JSON: " + perr; return false; }
        return parse(baseDir, haveBin ? &bin : nullptr, error);
    }
    std::string perr;
    if (!json::Parse(data, doc, &perr)) { error = "glTF JSON: " + perr; return false; }
    return parse(baseDir, nullptr, error);
}

bool Model::parse(const std::string& baseDir, const std::string* glbBin, std::string& error) {
    const json::Value& bufs = doc["buffers"];
    for (size_t i = 0; i < bufs.size(); ++i) {
        const json::Value& b = bufs.at(i);
        const std::string& uri = b["uri"].asString();
        std::string content;
        if (uri.empty()) {
            if (glbBin) content = *glbBin;
        } else if (StartsWith(uri, "data:")) {
            const size_t comma = uri.find(',');
            if (comma != std::string::npos) Base64(uri.substr(comma + 1), content);
        } else {
            std::string u = uri;   // percent-decoding of spaces only (WolvenKit writes plain names)
            u = ReplaceAll(u, "%20", " ");
            if (!ReadFile(JoinPath(baseDir, u), content)) { error = "missing buffer " + u; return false; }
        }
        buffers_.push_back(std::move(content));
    }
    const json::Value& views = doc["bufferViews"];
    for (size_t i = 0; i < views.size(); ++i) {
        const json::Value& v = views.at(i);
        View w;
        w.buffer = static_cast<int>(v["buffer"].asInt(-1));
        w.offset = static_cast<size_t>(v["byteOffset"].asInt(0));
        w.length = static_cast<size_t>(v["byteLength"].asInt(0));
        w.stride = static_cast<size_t>(v["byteStride"].asInt(0));
        views_.push_back(w);
    }
    const json::Value& accs = doc["accessors"];
    for (size_t i = 0; i < accs.size(); ++i) {
        const json::Value& a = accs.at(i);
        Acc c;
        c.view = a.has("bufferView") ? static_cast<int>(a["bufferView"].asInt(-1)) : -1;
        c.offset = static_cast<size_t>(a["byteOffset"].asInt(0));
        c.count = static_cast<size_t>(a["count"].asInt(0));
        c.compType = static_cast<int>(a["componentType"].asInt(5126));
        c.normalized = a["normalized"].asBool(false);
        const std::string& t = a["type"].asString();
        c.comps = t == "SCALAR" ? 1 : t == "VEC2" ? 2 : t == "VEC3" ? 3 : t == "VEC4" ? 4 : t == "MAT2" ? 4 : t == "MAT3" ? 9 : t == "MAT4" ? 16 : 1;
        const json::Value& sp = a["sparse"];
        if (sp.isObject()) {
            c.sparseCount = static_cast<int>(sp["count"].asInt(0));
            c.sparseIdxView = static_cast<int>(sp["indices"]["bufferView"].asInt(-1));
            c.sparseIdxOffset = static_cast<size_t>(sp["indices"]["byteOffset"].asInt(0));
            c.sparseIdxType = static_cast<int>(sp["indices"]["componentType"].asInt(5125));
            c.sparseValView = static_cast<int>(sp["values"]["bufferView"].asInt(-1));
            c.sparseValOffset = static_cast<size_t>(sp["values"]["byteOffset"].asInt(0));
        }
        accs_.push_back(c);
    }

    const json::Value& ns = doc["nodes"];
    nodes.resize(ns.size());
    for (size_t i = 0; i < ns.size(); ++i) {
        const json::Value& n = ns.at(i);
        Node& d = nodes[i];
        d.name = n["name"].asString();
        d.mesh = n.has("mesh") ? static_cast<int>(n["mesh"].asInt(-1)) : -1;
        d.skin = n.has("skin") ? static_cast<int>(n["skin"].asInt(-1)) : -1;
        const json::Value& ch = n["children"];
        for (size_t k = 0; k < ch.size(); ++k) d.children.push_back(static_cast<int>(ch.at(k).asInt(-1)));
        if (n["matrix"].size() == 16) {
            d.hasMatrix = true;
            for (int k = 0; k < 16; ++k) d.matrix[k] = static_cast<float>(n["matrix"].at(static_cast<size_t>(k)).asDouble());
        }
        if (n["translation"].size() == 3) for (int k = 0; k < 3; ++k) d.t[k] = static_cast<float>(n["translation"].at(static_cast<size_t>(k)).asDouble());
        if (n["rotation"].size() == 4) for (int k = 0; k < 4; ++k) d.r[k] = static_cast<float>(n["rotation"].at(static_cast<size_t>(k)).asDouble());
        if (n["scale"].size() == 3) for (int k = 0; k < 3; ++k) d.s[k] = static_cast<float>(n["scale"].at(static_cast<size_t>(k)).asDouble(1.0));
    }
    for (size_t i = 0; i < nodes.size(); ++i)
        for (int c : nodes[i].children)
            if (c >= 0 && static_cast<size_t>(c) < nodes.size()) nodes[static_cast<size_t>(c)].parent = static_cast<int>(i);

    const json::Value& ms = doc["meshes"];
    for (size_t i = 0; i < ms.size(); ++i) {
        const json::Value& m = ms.at(i);
        Mesh d;
        d.name = m["name"].asString();
        d.extras = &m["extras"];
        const json::Value& ps = m["primitives"];
        for (size_t k = 0; k < ps.size(); ++k) {
            const json::Value& p = ps.at(k);
            Primitive pr;
            for (const auto& kv : p["attributes"].obj) pr.attributes.emplace_back(kv.first, static_cast<int>(kv.second.asInt(-1)));
            pr.indices = p.has("indices") ? static_cast<int>(p["indices"].asInt(-1)) : -1;
            pr.material = p.has("material") ? static_cast<int>(p["material"].asInt(-1)) : -1;
            pr.mode = static_cast<int>(p["mode"].asInt(4));
            d.primitives.push_back(pr);
        }
        meshes.push_back(std::move(d));
    }
    const json::Value& sk = doc["skins"];
    for (size_t i = 0; i < sk.size(); ++i) {
        const json::Value& s = sk.at(i);
        Skin d;
        d.name = s["name"].asString();
        d.extras = &s["extras"];
        d.inverseBindMatrices = s.has("inverseBindMatrices") ? static_cast<int>(s["inverseBindMatrices"].asInt(-1)) : -1;
        d.skeleton = s.has("skeleton") ? static_cast<int>(s["skeleton"].asInt(-1)) : -1;
        for (size_t k = 0; k < s["joints"].size(); ++k) d.joints.push_back(static_cast<int>(s["joints"].at(k).asInt(-1)));
        skins.push_back(std::move(d));
    }
    const json::Value& an = doc["animations"];
    for (size_t i = 0; i < an.size(); ++i) {
        const json::Value& a = an.at(i);
        Animation d;
        d.name = a["name"].asString();
        d.extras = &a["extras"];
        for (size_t k = 0; k < a["channels"].size(); ++k) {
            const json::Value& c = a["channels"].at(k);
            Channel ch;
            ch.sampler = static_cast<int>(c["sampler"].asInt(-1));
            ch.node = c["target"].has("node") ? static_cast<int>(c["target"]["node"].asInt(-1)) : -1;
            ch.path = c["target"]["path"].asString();
            d.channels.push_back(ch);
        }
        for (size_t k = 0; k < a["samplers"].size(); ++k) {
            const json::Value& s = a["samplers"].at(k);
            Sampler sm;
            sm.input = static_cast<int>(s["input"].asInt(-1));
            sm.output = static_cast<int>(s["output"].asInt(-1));
            if (s["interpolation"].isString()) sm.interpolation = s["interpolation"].asString();
            d.samplers.push_back(sm);
        }
        animations.push_back(std::move(d));
    }
    const json::Value& mats = doc["materials"];
    for (size_t i = 0; i < mats.size(); ++i) materials.push_back(mats.at(i)["name"].asString());

    const json::Value& scenes = doc["scenes"];
    const size_t si = static_cast<size_t>(doc["scene"].asInt(0));
    const json::Value& sc = scenes.at(si);
    for (size_t k = 0; k < sc["nodes"].size(); ++k) sceneRoots.push_back(static_cast<int>(sc["nodes"].at(k).asInt(-1)));
    if (sceneRoots.empty())
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].parent < 0) sceneRoots.push_back(static_cast<int>(i));
    return true;
}

static size_t CompSize(int t) {
    switch (t) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; default: return 4; }
}

static double ReadComp(const char* p, int type, bool norm) {
    switch (type) {
    case 5120: { int8_t v; std::memcpy(&v, p, 1); return norm ? (v < -127 ? -1.0 : v / 127.0) : v; }
    case 5121: { uint8_t v; std::memcpy(&v, p, 1); return norm ? v / 255.0 : v; }
    case 5122: { int16_t v; std::memcpy(&v, p, 2); return norm ? (v < -32767 ? -1.0 : v / 32767.0) : v; }
    case 5123: { uint16_t v; std::memcpy(&v, p, 2); return norm ? v / 65535.0 : v; }
    case 5125: { uint32_t v; std::memcpy(&v, p, 4); return v; }
    default:   { float v; std::memcpy(&v, p, 4); return v; }
    }
}

size_t Model::accessorCount(int accessor) const {
    if (accessor < 0 || static_cast<size_t>(accessor) >= accs_.size()) return 0;
    return accs_[static_cast<size_t>(accessor)].count;
}

bool Model::decode(int accessor, std::vector<double>& out, int& comps) const {
    out.clear();
    comps = 0;
    if (accessor < 0 || static_cast<size_t>(accessor) >= accs_.size()) return false;
    const Acc& a = accs_[static_cast<size_t>(accessor)];
    comps = a.comps;
    const size_t cs = CompSize(a.compType);
    out.assign(a.count * static_cast<size_t>(a.comps), 0.0);
    if (a.view >= 0) {
        if (static_cast<size_t>(a.view) >= views_.size()) return false;
        const View& v = views_[static_cast<size_t>(a.view)];
        if (v.buffer < 0 || static_cast<size_t>(v.buffer) >= buffers_.size()) return false;
        const std::string& buf = buffers_[static_cast<size_t>(v.buffer)];
        size_t elem = cs * static_cast<size_t>(a.comps);
        // Matrices with 1/2-byte components have column padding; WolvenKit never writes those.
        const size_t stride = v.stride ? v.stride : elem;
        const size_t base = v.offset + a.offset;
        if (a.count > 0 && base + stride * (a.count - 1) + elem > buf.size()) return false;
        const char* p = buf.data() + base;
        for (size_t i = 0; i < a.count; ++i, p += stride)
            for (int c = 0; c < a.comps; ++c)
                out[i * static_cast<size_t>(a.comps) + static_cast<size_t>(c)] = ReadComp(p + cs * static_cast<size_t>(c), a.compType, a.normalized);
    }
    if (a.sparseCount > 0 && a.sparseIdxView >= 0 && a.sparseValView >= 0 &&
        static_cast<size_t>(a.sparseIdxView) < views_.size() && static_cast<size_t>(a.sparseValView) < views_.size()) {
        const View& iv = views_[static_cast<size_t>(a.sparseIdxView)];
        const View& vv = views_[static_cast<size_t>(a.sparseValView)];
        if (iv.buffer < 0 || vv.buffer < 0 || static_cast<size_t>(iv.buffer) >= buffers_.size() || static_cast<size_t>(vv.buffer) >= buffers_.size()) return false;
        const std::string& ib = buffers_[static_cast<size_t>(iv.buffer)];
        const std::string& vb = buffers_[static_cast<size_t>(vv.buffer)];
        const size_t ics = CompSize(a.sparseIdxType);
        const size_t elem = cs * static_cast<size_t>(a.comps);
        for (int s = 0; s < a.sparseCount; ++s) {
            const size_t io = iv.offset + a.sparseIdxOffset + ics * static_cast<size_t>(s);
            const size_t vo = vv.offset + a.sparseValOffset + elem * static_cast<size_t>(s);
            if (io + ics > ib.size() || vo + elem > vb.size()) return false;
            const size_t idx = static_cast<size_t>(ReadComp(ib.data() + io, a.sparseIdxType, false));
            if (idx >= a.count) continue;
            for (int c = 0; c < a.comps; ++c)
                out[idx * static_cast<size_t>(a.comps) + static_cast<size_t>(c)] = ReadComp(vb.data() + vo + cs * static_cast<size_t>(c), a.compType, a.normalized);
        }
    }
    return true;
}

bool Model::readFloats(int accessor, std::vector<float>& out, int& comps) const {
    std::vector<double> d;
    if (!decode(accessor, d, comps)) return false;
    out.resize(d.size());
    for (size_t i = 0; i < d.size(); ++i) out[i] = static_cast<float>(d[i]);
    return true;
}

bool Model::readUInts(int accessor, std::vector<uint32_t>& out, int& comps) const {
    std::vector<double> d;
    if (!decode(accessor, d, comps)) return false;
    out.resize(d.size());
    for (size_t i = 0; i < d.size(); ++i) out[i] = d[i] < 0 ? 0u : static_cast<uint32_t>(d[i] + 0.5);
    return true;
}

Xf Model::worldOf(int node) const {
    Xf w;
    int guard = 0;
    while (node >= 0 && static_cast<size_t>(node) < nodes.size() && guard++ < 4096) {
        w = w * nodes[static_cast<size_t>(node)].local();
        node = nodes[static_cast<size_t>(node)].parent;
    }
    return w;
}

int Model::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return static_cast<int>(i);
    return -1;
}

} // namespace gltf
} // namespace cp77
