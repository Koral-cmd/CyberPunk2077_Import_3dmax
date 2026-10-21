#pragma once
// ============================================================
//  Materials - WolvenKit writes <mesh>.Material.json next to
//  every exported mesh, plus the textures as png (same depot
//  path, .png instead of .xbm). This turns one material entry
//  into image files Max can use directly:
//    diffuse  - plain texture, or baked from a multilayer setup
//               (layer colours from the .mltemplate overrides,
//               blended by the mask layers)
//    normal   - two-channel game normal map -> full RGB normal
//    opacity  - decals: the diffuse alpha
// ============================================================
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cp77 {

struct MaterialDef {
    std::string name, base, tmpl;
    std::map<std::string, std::string> strings;   // Data entries that are strings (texture paths ...)
};

struct MaterialFile {
    std::string path;
    std::vector<MaterialDef> materials;
    std::vector<std::pair<std::string, std::vector<std::string>>> appearances;   // "default0" -> per submesh
    const MaterialDef* find(const std::string& name) const;
    // Index of a mesh appearance ("default", "" = first). -1 when not listed.
    int appearanceIndex(const std::string& meshAppearance) const;
};

bool ReadMaterialFile(const std::string& path, MaterialFile& out, std::string& error);

struct MaterialTextures {
    std::string diffuse, normal, opacity;
    bool baked = false;          // diffuse came from a multilayer bake
    std::string note;            // what was missing, for the log
};

struct TextureOptions {
    std::string cacheRoot;       // WolvenKit output folder (depot root)
    std::string outDir;          // where generated images go
    bool flipGreen = false;      // normal maps
    int bakeSize = 1024;         // multilayer bake: minimum edge
};

// Never fails hard: missing pieces are left empty and named in note.
void ResolveTextures(const MaterialDef& m, const TextureOptions& opt, MaterialTextures& out);

// The png (or tga/jpg) WolvenKit exported for a depot texture path, "" if none.
std::string TextureFile(const std::string& cacheRoot, const std::string& depot);

} // namespace cp77
