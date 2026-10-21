#include "material.h"
#include "image.h"
#include "json.h"
#include "red.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cctype>
#include <cmath>

namespace cp77 {

using json::Value;

const MaterialDef* MaterialFile::find(const std::string& name) const {
    for (const auto& m : materials) if (m.name == name) return &m;
    for (const auto& m : materials) if (IEquals(m.name, name)) return &m;
    return nullptr;
}

int MaterialFile::appearanceIndex(const std::string& meshAppearance) const {
    if (appearances.empty()) return -1;
    const std::string want = Lower(meshAppearance);
    for (size_t i = 0; i < appearances.size(); ++i) {
        // keys are the appearance name followed by its index: "default0", "black1"
        std::string key = appearances[i].first;
        size_t e = key.size();
        while (e > 0 && std::isdigit(static_cast<unsigned char>(key[e - 1]))) --e;
        if (Lower(key.substr(0, e)) == want || Lower(key) == want) return static_cast<int>(i);
    }
    if (want.empty() || want == "default") return 0;
    return -1;
}

bool ReadMaterialFile(const std::string& path, MaterialFile& out, std::string& error) {
    out = MaterialFile();
    out.path = path;
    Value doc;
    if (!json::ParseFile(path, doc, &error)) return false;
    const Value& mats = doc["Materials"];
    if (!mats.isArray()) { error = path + ": no Materials"; return false; }
    for (size_t i = 0; i < mats.size(); ++i) {
        const Value& m = mats.at(i);
        MaterialDef d;
        d.name = m["Name"].asString();
        d.base = NormalizeDepotPath(m["BaseMaterial"].asString());
        d.tmpl = NormalizeDepotPath(m["MaterialTemplate"].asString());
        for (const auto& kv : m["Data"].obj)
            if (kv.second.isString()) d.strings[kv.first] = kv.second.str;
        out.materials.push_back(std::move(d));
    }
    for (const auto& kv : doc["Appearances"].obj) {
        std::vector<std::string> names;
        for (size_t i = 0; i < kv.second.size(); ++i) names.push_back(kv.second.at(i).asString());
        out.appearances.emplace_back(kv.first, std::move(names));
    }
    // keys carry their index: keep that order even if a writer sorted them
    std::stable_sort(out.appearances.begin(), out.appearances.end(), [](const auto& a, const auto& b) {
        auto idx = [](const std::string& k) {
            size_t e = k.size();
            while (e > 0 && std::isdigit(static_cast<unsigned char>(k[e - 1]))) --e;
            return e < k.size() ? std::atoi(k.c_str() + e) : 0;
        };
        return idx(a.first) < idx(b.first);
    });
    return true;
}

static std::string StripExt(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    const size_t sep = p.find_last_of("\\/");
    return (dot == std::string::npos || (sep != std::string::npos && dot < sep)) ? p : p.substr(0, dot);
}

std::string TextureFile(const std::string& cacheRoot, const std::string& depot) {
    if (depot.empty() || depot[0] == '#') return std::string();
    const std::string lower = Lower(depot);
    if (lower.find("engine\\textures\\editor\\") == 0 || lower.find("engine/textures/editor/") == 0) return std::string();   // flat placeholders
    const std::string stem = StripExt(depot);
    for (const char* ext : { ".png", ".tga", ".jpg", ".bmp" }) {
        const std::string f = JoinPath(cacheRoot, NativePath(stem + ext));
        if (FileExists(f)) return f;
    }
    return std::string();
}

static bool UpToDate(const std::string& out, const std::vector<std::string>& sources) {
    const int64_t t = FileTime(out);
    if (t == 0) return false;
    for (const auto& s : sources) if (FileTime(s) > t) return false;
    return true;
}

// game normal maps keep X/Y only (BC5); Max wants the full vector
static std::string ConvertNormal(const std::string& src, const TextureOptions& opt, std::string& note) {
    const std::string out = JoinPath(opt.outDir, FileStem(src) + (opt.flipGreen ? "_cp77ng.png" : "_cp77n.png"));
    if (UpToDate(out, { src })) return out;
    Image img;
    std::string err;
    if (!LoadImage(src, img, err)) { note += "normal: " + err + "; "; return std::string(); }
    size_t blue = 0;
    for (size_t i = 2; i < img.px.size(); i += 4) blue += img.px[i];
    const bool twoChannel = blue < img.px.size() / 4 * 8;     // blue practically empty
    if (!twoChannel && !opt.flipGreen) return src;
    for (size_t i = 0; i < img.px.size(); i += 4) {
        uint8_t* p = &img.px[i];
        if (twoChannel) {
            const double x = p[0] / 127.5 - 1.0, y = p[1] / 127.5 - 1.0;
            const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
            p[2] = static_cast<uint8_t>(std::lround((z * 0.5 + 0.5) * 255.0));
        }
        if (opt.flipGreen) p[1] = static_cast<uint8_t>(255 - p[1]);
        p[3] = 255;
    }
    if (!SavePng(out, img, err)) { note += "normal: " + err + "; "; return std::string(); }
    return out;
}

namespace {
struct Layer {
    double r = 1, g = 1, b = 1, opacity = 1;
    Image mask;
};
}

// Multilayer: every layer is a surface template tinted by a named colour,
// painted over the previous ones through its mask. The bake keeps the colour
// (times the template's average albedo); tiling detail is left out.
static std::string BakeMultilayer(const MaterialDef& m, const TextureOptions& opt, std::string& note) {
    auto get = [&](const char* k) { auto it = m.strings.find(k); return it == m.strings.end() ? std::string() : it->second; };
    const std::string setup = get("MultilayerSetup"), maskRes = get("MultilayerMask");
    if (setup.empty()) return std::string();
    const std::string setupJson = JoinPath(opt.cacheRoot, NativePath(setup + ".json"));
    if (!FileExists(setupJson)) { note += "no " + setup + ".json; "; return std::string(); }
    const std::string out = JoinPath(opt.outDir, FileStem(setup) + "_cp77d.png");
    if (UpToDate(out, { setupJson })) return out;

    Value doc;
    std::string err;
    if (!json::ParseFile(setupJson, doc, &err)) { note += err + "; "; return std::string(); }
    const Value& layers = doc["Data"]["RootChunk"]["layers"];
    if (!layers.isArray() || layers.size() == 0) { note += setup + ": no layers; "; return std::string(); }

    const std::string maskStem = maskRes.empty() ? std::string() : StripExt(maskRes);
    std::map<std::string, Value> templates;
    std::map<std::string, std::array<double, 3>> albedo;
    std::vector<Layer> ls(layers.size());
    int size = std::max(64, opt.bakeSize);
    size_t missingMasks = 0, missingTemplates = 0;
    for (size_t i = 0; i < layers.size(); ++i) {
        const Value& L = layers.at(i);
        Layer& l = ls[i];
        l.opacity = L["opacity"].asDouble(1.0);
        const std::string tp = red::DepotPath(L["material"]);
        const std::string colour = red::CName(L["colorScale"]);
        auto ti = templates.find(tp);
        if (ti == templates.end()) {
            Value t;
            const std::string tf = JoinPath(opt.cacheRoot, NativePath(tp + ".json"));
            if (!FileExists(tf) || !json::ParseFile(tf, t, nullptr)) ++missingTemplates;
            ti = templates.emplace(tp, std::move(t)).first;
        }
        const Value& rc = ti->second["Data"]["RootChunk"];
        const Value& cs = rc["overrides"]["colorScale"];
        bool found = false;
        for (size_t c = 0; c < cs.size() && !found; ++c) {
            if (red::CName(cs.at(c)["n"]) != colour) continue;
            const Value& e = cs.at(c)["v"]["Elements"];
            l.r = e.at(0).asDouble(1); l.g = e.at(1).asDouble(1); l.b = e.at(2).asDouble(1);
            found = true;
        }
        if (!found && rc.isObject()) { l.r = l.g = l.b = 0.5; }
        // average albedo of the template's tiling texture
        const std::string ct = red::DepotPath(rc["colorTexture"]);
        auto ai = albedo.find(ct);
        if (ai == albedo.end()) {
            std::array<double, 3> a{ 1.0, 1.0, 1.0 };
            Image img;
            const std::string f = TextureFile(opt.cacheRoot, ct);
            if (!f.empty() && LoadImage(f, img, err)) {
                double s[3] = { 0, 0, 0 };
                const size_t n = img.px.size() / 4;
                for (size_t p = 0; p < img.px.size(); p += 4) for (int c = 0; c < 3; ++c) s[c] += img.px[p + static_cast<size_t>(c)];
                for (int c = 0; c < 3; ++c) a[static_cast<size_t>(c)] = n ? s[c] / (255.0 * static_cast<double>(n)) : 1.0;
            }
            ai = albedo.emplace(ct, a).first;
        }
        l.r *= ai->second[0]; l.g *= ai->second[1]; l.b *= ai->second[2];
        if (i > 0 && !maskStem.empty()) {
            const std::string mf = JoinPath(opt.cacheRoot, NativePath(maskStem + "_layers\\" + FileStem(maskStem) + "_" + std::to_string(i) + ".png"));
            if (!FileExists(mf) || !LoadImage(mf, l.mask, err)) { ++missingMasks; l.opacity = 0.0; }
            else size = std::max(size, std::max(l.mask.w, l.mask.h));
        }
    }
    size = std::min(size, 4096);
    if (missingMasks) note += Format("%zu mask layer(s) missing; ", missingMasks);
    if (missingTemplates) note += Format("%zu layer template(s) missing; ", missingTemplates);

    Image img;
    img.w = img.h = size;
    img.px.assign(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 255);
    std::vector<double> acc(static_cast<size_t>(size) * static_cast<size_t>(size) * 3);
    for (size_t p = 0; p < acc.size(); p += 3) { acc[p] = ls[0].r; acc[p + 1] = ls[0].g; acc[p + 2] = ls[0].b; }
    for (size_t i = 1; i < ls.size(); ++i) {
        const Layer& l = ls[i];
        if (l.opacity <= 0.0 || l.mask.empty()) continue;
        const bool same = l.mask.w == size && l.mask.h == size;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double w = l.opacity * (same ? l.mask.at(x, y)[0] / 255.0
                                                   : SampleBilinear(l.mask, (x + 0.5) / size, (y + 0.5) / size, 0));
                if (w <= 0.0) continue;
                double* a = &acc[(static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 3];
                a[0] += (l.r - a[0]) * w; a[1] += (l.g - a[1]) * w; a[2] += (l.b - a[2]) * w;
            }
    }
    for (size_t p = 0, q = 0; p < acc.size(); p += 3, q += 4)
        for (int c = 0; c < 3; ++c) {
            const double lin = std::min(1.0, std::max(0.0, acc[p + static_cast<size_t>(c)]));
            img.px[q + static_cast<size_t>(c)] = static_cast<uint8_t>(std::lround(std::pow(lin, 1.0 / 2.2) * 255.0));
        }
    if (!SavePng(out, img, err)) { note += err + "; "; return std::string(); }
    return out;
}

void ResolveTextures(const MaterialDef& m, const TextureOptions& opt, MaterialTextures& out) {
    out = MaterialTextures();
    auto first = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
            auto it = m.strings.find(k);
            if (it == m.strings.end()) continue;
            const std::string f = TextureFile(opt.cacheRoot, it->second);
            if (!f.empty()) return f;
        }
        return std::string();
    };
    if (m.strings.count("MultilayerSetup")) {
        out.diffuse = BakeMultilayer(m, opt, out.note);
        out.baked = !out.diffuse.empty();
    }
    if (out.diffuse.empty()) out.diffuse = first({ "BaseColor", "Albedo", "DiffuseTexture", "Diffuse", "DiffuseMap", "MainTexture" });
    const std::string n = first({ "Normal", "NormalTexture", "GlobalNormal", "NormalMap", "DetailNormal" });
    if (!n.empty()) out.normal = ConvertNormal(n, opt, out.note);
    const std::string lt = Lower(m.base + " " + m.tmpl);
    if (!out.diffuse.empty() && !out.baked && (lt.find("decal") != std::string::npos || lt.find("hair") != std::string::npos))
        out.opacity = out.diffuse;
    if (out.diffuse.empty()) out.note += "no diffuse texture; ";
}

} // namespace cp77
