// ============================================================
//  Assembler - from an entity (.ent + appearance) to a plan the
//  plugin turns into a 3ds Max scene. No Max SDK in here.
//
//  How the pieces find each other (the game's own wiring):
//    .ent  appearances[name]  -> .app + appearanceName
//    .app  appearance         -> components (compiledData)
//                             -> partsValues (more .ent with components)
//                             -> partsOverrides (meshAppearance / chunkMask)
//    entAnimatedComponent     -> .rig (+ animsets for step 2)
//    entSkinnedMeshComponent  -> .mesh, skinning.bindName -> animated component
//    entMeshComponent         -> .mesh, parentTransform -> bone / slot
//    entSlotComponent         -> slots (WeaponRight -> bone + offset)
//
//  All rigs of one entity are merged BY BONE NAME into one
//  skeleton (body, face, dangle rigs share Root/Hips/.../Head),
//  as WolvenKit's own rig export does (RigTools.CombineRigs).
//  A rig that shares no bone with it stays a separate skeleton
//  and hangs off its parentTransform bone/slot.
//
//  The weapon is a second entity; its skeleton hangs off the
//  character's weapon slot (default WeaponRight).
// ============================================================
#pragma once
#include "red.h"
#include "wolvenkit.h"
#include "xform.h"

#include <string>
#include <vector>

namespace cp77 {

struct PlanBone {
    std::string name;
    int parent = -1;
    Xf restWorld;                    // skeleton space, game space, metres
};

struct PlanSkeleton {
    std::string name;                // shown in Max (root helper name)
    std::string owner;               // "character" | "weapon"
    std::string entity;              // .ent path
    std::vector<PlanBone> bones;
    std::vector<std::string> rigs;
    std::vector<std::string> animsets;
    // attachment (parentSkeleton < 0: placed at the scene origin)
    int parentSkeleton = -1;
    std::string parentBone;
    Xf offset;                       // local to the parent bone
    int find(const std::string& bone) const;
};

struct PlanMesh {
    std::string component;
    std::string owner;
    std::string mesh;                // .mesh depot path
    std::string appearance;          // meshAppearance
    uint64_t chunkMask = ~0ull;
    int skeleton = -1;               // skinned against this skeleton (-1: rigid)
    // rigid placement
    int attachSkeleton = -1;
    std::string attachBone;
    Xf offset;
    std::string glb;                 // local file once extracted
};

struct ScenePlan {
    std::vector<PlanSkeleton> skeletons;
    std::vector<PlanMesh> meshes;
    std::vector<std::string> warnings;
    std::string character, characterAppearance;
    std::string weapon, weaponAppearance, weaponSlot;
    std::string summary() const;
};

struct EntitySummary {               // for the UI
    std::string path;
    std::string defaultAppearance;
    std::vector<std::string> appearances;
};

class Assembler {
public:
    Assembler(FileStore& store, const WolvenKit* wk, const Settings& s);

    bool describe(const std::string& ent, EntitySummary& out, std::string& error);
    // Builds the whole plan, extracting what is missing.
    bool build(const std::string& characterEnt, const std::string& characterApp,
               const std::string& weaponEnt, const std::string& weaponApp,
               const std::string& weaponSlot, ScenePlan& plan, std::string& error);
    // Slot names a weapon can hang on (after build or describe+resolve of the character).
    std::vector<std::string> slotNames() const { return slotNames_; }

private:
    FileStore& store_;
    const WolvenKit* wk_;
    const Settings& s_;
    std::vector<std::string> slotNames_;

    bool needJson(const std::vector<std::string>& depots, std::string& error);
    void prefetch(const std::vector<std::pair<std::string, std::string>>& ents);
    bool loadEnt(const std::string& path, red::EntInfo& out, std::string& error);
    struct Entity {
        std::vector<red::Component> comps;
        std::vector<std::string> warnings;
    };
    bool collect(const std::string& ent, const std::string& app, Entity& e, std::string& error);
    bool assemble(const std::string& owner, const std::string& entPath, Entity& e, ScenePlan& plan, std::string& error);
    bool resolveSlot(const ScenePlan& plan, const std::vector<red::Component>& comps, const std::string& owner,
                     const std::string& bindName, const std::string& slotName,
                     int& skeleton, std::string& bone, Xf& offset) const;
    std::vector<red::Component> charComps_;
};

// Picks the bone a weapon goes to when the slot is not found.
std::string FallbackHandBone(const PlanSkeleton& s);

} // namespace cp77
