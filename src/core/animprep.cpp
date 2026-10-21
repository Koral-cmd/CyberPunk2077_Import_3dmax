#include "animprep.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace cp77 {

static const double kTick = 1.0 / 4800.0;

double MatchRatio(const Clip& clip, const std::vector<TargetBone>& target, size_t* matched) {
    std::set<std::string> names;
    for (const auto& t : target) names.insert(t.name);
    size_t m = 0;
    for (const auto& tr : clip.tracks) if (names.count(tr.joint)) ++m;
    if (matched) *matched = m;
    return clip.tracks.empty() ? 0.0 : static_cast<double>(m) / static_cast<double>(clip.tracks.size());
}

void PrepAnimation(const std::vector<ClipPlacement>& clips, const AnimSet& set,
                   const std::vector<TargetBone>& target, double s, AnimPrepResult& out) {
    out = AnimPrepResult();
    std::map<std::string, int> index;
    for (size_t i = 0; i < target.size(); ++i) index.emplace(target[i].name, static_cast<int>(i));
    std::vector<BoneKeys> keys(target.size());
    for (size_t i = 0; i < target.size(); ++i) keys[i].bone = static_cast<int>(i);

    auto rest = [&](size_t bone) -> Xf {
        auto it = set.restLocal.find(target[bone].name);
        if (it != set.restLocal.end()) return ScaledTranslation(it->second, s);
        return target[bone].restLocal;
    };
    auto push = [&](size_t bone, double t, const Xf& m) {
        BoneKeys& k = keys[bone];
        if (!k.times.empty() && t <= k.times.back() + kTick * 0.5) {
            if (std::fabs(t - k.times.back()) < kTick * 0.5) { k.local.back() = m; return; }   // same tick: last one wins
            return;                                                                            // out of order: drop
        }
        k.times.push_back(t);
        k.local.push_back(m);
    };

    std::set<std::string> animatedJoints, unmatched;
    for (const ClipPlacement& cp : clips) {
        if (!cp.clip) continue;
        const Clip& c = *cp.clip;
        std::vector<char> has(target.size(), 0);
        for (const auto& tr : c.tracks) {
            animatedJoints.insert(tr.joint);
            auto it = index.find(tr.joint);
            if (it == index.end()) { unmatched.insert(tr.joint); continue; }
            has[static_cast<size_t>(it->second)] = 1;
            if (tr.nonUnitScale) ++out.scaleTracks;
        }
        // keys in time order per bone: first the static bones at the clip's start
        for (size_t b = 0; b < target.size(); ++b) {
            if (has[b]) continue;
            const Xf r = rest(b);
            push(b, cp.start, r);
            if (c.duration > kTick && clips.size() > 1) push(b, cp.start + c.duration, r);
        }
        for (const auto& tr : c.tracks) {
            auto it = index.find(tr.joint);
            if (it == index.end()) continue;
            const size_t b = static_cast<size_t>(it->second);
            for (size_t i = 0; i < tr.times.size(); ++i) {
                const double t = cp.start + tr.times[i];
                Xf m = ScaledTranslation(tr.local[i], s);
                if (tr.step && i > 0) push(b, t - kTick, ScaledTranslation(tr.local[i - 1], s));   // hold until the next key
                push(b, t, m);
            }
            // keep the last pose to the end of the clip when the track stops early
            if (!tr.times.empty() && clips.size() > 1 && tr.times.back() + kTick < c.duration)
                push(b, cp.start + c.duration, ScaledTranslation(tr.local.back(), s));
        }
        out.end = std::max(out.end, cp.start + c.duration);
    }
    out.clipJoints = animatedJoints.size();
    out.matched = animatedJoints.size() - unmatched.size();
    out.unmatched.assign(unmatched.begin(), unmatched.end());
    for (auto& k : keys) if (!k.times.empty()) out.keys.push_back(std::move(k));
}

std::string XfToString(const Xf& m) {
    std::string s;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) s += (r == 0 && c == 0 ? "" : " ") + Num(m.m[r][c]);
    return s;
}

bool XfFromString(const std::string& str, Xf& m) {
    const char* p = str.c_str();
    const char* end = p + str.size();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) {
            double v = 0.0;
            const char* stop = p;
            if (!ParseDouble(p, end, v, &stop)) return false;
            m.m[r][c] = v;
            p = stop;
        }
    return true;
}

} // namespace cp77
