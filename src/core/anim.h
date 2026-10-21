// ============================================================
//  Animations from a WolvenKit animset GLB (x.anims -> x.anims.glb).
//
//  Layout (AnimationTools.cs): a node "Armature" holding the rig
//  hierarchy, a skin with extras {rigPath, boneNames, ...}, and
//  one glTF animation per clip with translation / rotation /
//  scale channels. Additive clips are already baked to absolute
//  by WolvenKit's default export; root motion sits on the root
//  joint's own channels.
//
//  Clips are returned as LOCAL transforms per joint, converted to
//  game space (= Max space, metres), sampled at the union of the
//  key times of that joint's channels.
// ============================================================
#pragma once
#include "gltf.h"
#include "xform.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cp77 {

struct JointTrack {
    std::string joint;
    std::vector<double> times;      // seconds, ascending
    std::vector<Xf> local;          // one per time, game space
    bool step = false;              // STEP interpolation in the source
    bool hasT = false, hasR = false, hasS = false;
    bool nonUnitScale = false;
};

struct Clip {
    std::string name;
    double duration = 0.0;          // seconds
    std::vector<JointTrack> tracks;
    std::string animationType;      // extras.AnimationType if present (Normal / Additive...)
    size_t keyCount() const;
};

class AnimSet {
public:
    bool load(const std::string& path, std::string& error);
    std::string path;
    std::string rigPath;            // skin extras
    std::vector<std::string> jointNames;      // armature joints
    std::map<std::string, Xf> restLocal;      // node default TRS per joint (game space)
    std::vector<std::string> clipNames() const;
    double clipDuration(size_t i) const;
    // Decodes one clip on demand (big sets hold hundreds).
    bool clip(size_t index, Clip& out, std::string& error) const;
    int findClip(const std::string& name) const;
    size_t clipCount() const { return model_ ? model_->animations.size() : 0; }
private:
    std::shared_ptr<gltf::Model> model_;
    std::map<int, std::string> nodeJoint_;    // glTF node -> joint name
    std::vector<double> durations_;
};

// Timeline for "load all": clip i starts at starts[i] (seconds).
struct Timeline {
    std::vector<double> starts;
    double end = 0.0;
};
Timeline LayoutClips(const std::vector<double>& durations, double gapSeconds);

} // namespace cp77
