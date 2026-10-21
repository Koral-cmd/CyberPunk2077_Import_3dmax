// ============================================================
//  Reading WolvenKit's JSON exports of REDengine files
//  (.ent / .app / .rig / .morphtarget, written by
//  "uncook -u -s" or "convert serialize").
//
//  The layout, from WolvenKit's RedJsonSerializer:
//    { "Header": {...}, "Data": { "RootChunk": { "$type": ... } } }
//  CName       {"$type":"CName","$storage":"string","$value":"x"}
//  ResourceRef {"DepotPath":{"$type":"ResourcePath","$value":"base\\..."},"Flags":...}
//  Handle      {"HandleId":"3","Data":{...}}  later  {"HandleRefId":"3"}
//  Buffers     "compiledData": {"Data": {"Chunks": [ components... ]}}
//
//  Older WolvenKit versions wrote CNames and paths as plain
//  strings - both forms are accepted everywhere. All key lookups
//  are case-insensitive. The reader walks generically instead of
//  hard-coding one layout, so small schema drifts between
//  WolvenKit versions do not break it.
// ============================================================
#pragma once
#include "json.h"
#include "xform.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cp77 {
namespace red {

// A loaded JSON file with its handle table.
class Doc {
public:
    bool load(const std::string& path, std::string& error);
    bool loadText(const std::string& text, std::string& error);
    const json::Value& root() const;                 // Data.RootChunk (or the document)
    std::string rootType() const;
    // Follows {"HandleId","Data"} and {"HandleRefId"}; returns the value itself otherwise.
    const json::Value& deref(const json::Value& v) const;
    json::Value doc;
private:
    std::map<std::string, const json::Value*> handles_;
    void index(const json::Value& v);
};

std::string CName(const json::Value& v);            // "" for None
std::string DepotPath(const json::Value& v);        // ResourceReference / ResourcePath / string
std::string TypeOf(const json::Value& v);           // "$type"
Vec3 ReadVec3(const json::Value& v);                // X/Y/Z or x/y/z
Quatd ReadQuat(const json::Value& v);               // i/j/k/r (game) or x/y/z/w
double ReadFixed(const json::Value& v);             // FixedPoint {"Bits"} or plain number
Xf ReadQsTransform(const json::Value& v);           // Translation / Rotation / Scale
Xf ReadWorldTransform(const json::Value& v);        // Position (FixedPoint) / Orientation

// ------------------------------------------------------------
//  Rig
// ------------------------------------------------------------
struct Rig {
    std::string path;
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<Xf> local;          // reference pose (BoneTransforms), game space
    std::vector<Xf> aposeLocal;     // APoseLS, empty if the rig has none
    std::vector<Xf> world(bool useAPose) const;
    int find(const std::string& name) const;
};
bool ReadRig(const Doc& d, Rig& out, std::string& error);

// ------------------------------------------------------------
//  Components (the parts of an entity that matter for import)
// ------------------------------------------------------------
enum class CompKind { Mesh, SkinnedMesh, MorphMesh, Animated, Slot, Other };

struct SlotDef {
    std::string slotName, boneName;
    Xf offset;                       // relativePosition / relativeRotation
};

struct Component {
    CompKind kind = CompKind::Other;
    std::string type;                // $type
    std::string name;
    // meshes
    std::string mesh;                // .mesh (for MorphMesh: the .morphtarget)
    std::string meshAppearance;
    uint64_t chunkMask = ~0ull;
    bool visible = true;
    std::string skinBind;            // skinning.bindName -> animated component
    // transform binding (rigid meshes, skeletons, slot components)
    std::string parentBind;          // parentTransform.bindName
    std::string parentSlot;          // parentTransform.slotName
    Xf localTransform;
    // animated component
    std::string rig;
    std::vector<std::string> animsets;
    // slot component
    std::vector<SlotDef> slots;
    std::string source;              // file the component came from (for the log)
};

// Every component found anywhere under `v` (compiledData chunks,
// "components" arrays, nested handles).
void CollectComponents(const Doc& d, const json::Value& v, const std::string& source,
                       std::vector<Component>& out);

// ------------------------------------------------------------
//  Entity template (.ent) and appearance resource (.app)
// ------------------------------------------------------------
struct EntAppearance {
    std::string name;                // the entity's appearance name
    std::string appPath;             // appearanceResource
    std::string appName;             // appearanceName inside the .app
};
struct EntInfo {
    std::string path;
    std::string defaultAppearance;
    std::vector<EntAppearance> appearances;
    std::vector<std::string> includes;   // other .ent pulled in
    std::vector<Component> components;   // from compiledData
};
bool ReadEnt(const Doc& d, const std::string& path, EntInfo& out);

struct PartOverride {
    std::string partResource;        // the part .ent the override targets ("" = all)
    std::string componentName;
    std::string meshAppearance;      // "" = keep
    bool hasChunkMask = false;
    uint64_t chunkMask = ~0ull;
};
struct AppAppearance {
    std::string name;
    std::vector<Component> components;
    std::vector<std::string> parts;  // partsValues -> .ent
    std::vector<PartOverride> overrides;
};
struct AppInfo {
    std::string path;
    std::vector<AppAppearance> appearances;
    const AppAppearance* find(const std::string& name) const;
};
bool ReadApp(const Doc& d, const std::string& path, AppInfo& out);

} // namespace red
} // namespace cp77
