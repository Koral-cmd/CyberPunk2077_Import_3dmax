// ============================================================
//  Animation keys ready for 3ds Max - one key list per bone of
//  the target skeleton, in Max units, times in seconds.
//
//  Single clip:  every bone of the skeleton gets keys; bones the
//                clip does not animate get one key with their
//                rest pose (from the animset's armature, else the
//                pose stored on the bone at import).
//  All clips:    the clips are laid out one after another with a
//                gap; bones without a track in a clip get a key at
//                the clip's start and end so nothing bleeds over
//                from the previous clip.
// ============================================================
#pragma once
#include "anim.h"

#include <string>
#include <vector>

namespace cp77 {

struct TargetBone {
    std::string name;
    Xf restLocal;                    // Max units
};

struct BoneKeys {
    int bone = -1;                   // index into the TargetBone list
    std::vector<double> times;       // seconds, ascending
    std::vector<Xf> local;           // Max units
};

struct ClipPlacement {
    const Clip* clip = nullptr;
    double start = 0.0;              // seconds on the Max timeline
};

struct AnimPrepResult {
    std::vector<BoneKeys> keys;
    size_t matched = 0;              // clip joints found on the target
    size_t clipJoints = 0;           // joints the clips animate
    std::vector<std::string> unmatched;
    size_t scaleTracks = 0;          // tracks with non-unit scale (not applied)
    double end = 0.0;                // seconds
};

// Fraction of the joints animated by `clip` that exist on the target.
double MatchRatio(const Clip& clip, const std::vector<TargetBone>& target, size_t* matched = nullptr);

void PrepAnimation(const std::vector<ClipPlacement>& clips, const AnimSet& set,
                   const std::vector<TargetBone>& target, double unitsPerMeter, AnimPrepResult& out);

// Serialise a rest pose for the bone's user properties (12 numbers).
std::string XfToString(const Xf& m);
bool XfFromString(const std::string& s, Xf& m);

} // namespace cp77
