#include "anim.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace cp77 {

size_t Clip::keyCount() const {
    size_t n = 0;
    for (const auto& t : tracks) n += t.times.size();
    return n;
}

bool AnimSet::load(const std::string& p, std::string& error) {
    path = p;
    model_ = std::make_shared<gltf::Model>();
    if (!model_->load(p, error)) { model_.reset(); return false; }
    const gltf::Model& m = *model_;
    jointNames.clear();
    restLocal.clear();
    nodeJoint_.clear();

    std::vector<int> jointNodes;
    for (const auto& sk : m.skins) {
        if (sk.extras && rigPath.empty()) rigPath = NormalizeDepotPath((*sk.extras)["rigPath"].asString());
        for (int j : sk.joints) jointNodes.push_back(j);
    }
    if (jointNodes.empty()) {
        // no skin: everything below "Armature" (or every node that is animated)
        const int arm = m.findNode("Armature");
        std::vector<int> stack;
        if (arm >= 0) stack = m.nodes[static_cast<size_t>(arm)].children;
        while (!stack.empty()) {
            const int n = stack.back();
            stack.pop_back();
            if (n < 0 || static_cast<size_t>(n) >= m.nodes.size()) continue;
            jointNodes.push_back(n);
            for (int c : m.nodes[static_cast<size_t>(n)].children) stack.push_back(c);
        }
    }
    for (const auto& a : m.animations)
        for (const auto& c : a.channels)
            if (c.node >= 0 && std::find(jointNodes.begin(), jointNodes.end(), c.node) == jointNodes.end()) jointNodes.push_back(c.node);

    std::set<std::string> seen;
    for (int n : jointNodes) {
        if (n < 0 || static_cast<size_t>(n) >= m.nodes.size()) continue;
        const gltf::Node& node = m.nodes[static_cast<size_t>(n)];
        nodeJoint_[n] = node.name;
        if (seen.insert(node.name).second) {
            jointNames.push_back(node.name);
            restLocal[node.name] = FromGltf(node.local());
        }
    }

    durations_.clear();
    for (const auto& a : m.animations) {
        double d = 0.0;
        for (const auto& s : a.samplers) {
            std::vector<float> t;
            int comps = 0;
            if (m.readFloats(s.input, t, comps) && !t.empty()) d = std::max(d, static_cast<double>(t.back()));
        }
        durations_.push_back(d);
    }
    return true;
}

std::vector<std::string> AnimSet::clipNames() const {
    std::vector<std::string> out;
    if (!model_) return out;
    for (size_t i = 0; i < model_->animations.size(); ++i) {
        const std::string& n = model_->animations[i].name;
        out.push_back(n.empty() ? Format("clip_%zu", i) : n);
    }
    return out;
}

double AnimSet::clipDuration(size_t i) const { return i < durations_.size() ? durations_[i] : 0.0; }

int AnimSet::findClip(const std::string& name) const {
    const std::vector<std::string> n = clipNames();
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == name) return static_cast<int>(i);
    for (size_t i = 0; i < n.size(); ++i) if (IEquals(n[i], name)) return static_cast<int>(i);
    return -1;
}

namespace {
struct Curve {
    std::vector<float> t;
    std::vector<float> v;   // comps per key (cubic: 3 * comps per key)
    int comps = 0;
    enum { Linear, Step, Cubic } mode = Linear;
    bool valid() const { return !t.empty() && comps > 0; }

    // value at time `x` into out[comps]
    void eval(double x, double* out) const {
        const size_t n = t.size();
        const size_t stride = static_cast<size_t>(comps) * (mode == Cubic ? 3 : 1);
        const size_t off = mode == Cubic ? static_cast<size_t>(comps) : 0;
        auto val = [&](size_t i, int c) { return static_cast<double>(v[i * stride + off + static_cast<size_t>(c)]); };
        if (x <= t[0] || n == 1) { for (int c = 0; c < comps; ++c) out[c] = val(0, c); return; }
        if (x >= t[n - 1]) { for (int c = 0; c < comps; ++c) out[c] = val(n - 1, c); return; }
        const size_t hi = static_cast<size_t>(std::upper_bound(t.begin(), t.end(), static_cast<float>(x)) - t.begin());
        const size_t lo = hi - 1;
        const double span = static_cast<double>(t[hi]) - t[lo];
        const double u = span > 0 ? (x - t[lo]) / span : 0.0;
        if (mode == Step || u <= 0.0) { for (int c = 0; c < comps; ++c) out[c] = val(lo, c); return; }
        if (comps == 4) {
            const Quatd q = Slerp(Quatd(val(lo, 0), val(lo, 1), val(lo, 2), val(lo, 3)),
                                  Quatd(val(hi, 0), val(hi, 1), val(hi, 2), val(hi, 3)), u);
            out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
            return;
        }
        for (int c = 0; c < comps; ++c) out[c] = val(lo, c) + (val(hi, c) - val(lo, c)) * u;
    }
};
}

bool AnimSet::clip(size_t index, Clip& out, std::string& error) const {
    out = Clip();
    if (!model_ || index >= model_->animations.size()) { error = "no such clip"; return false; }
    const gltf::Model& m = *model_;
    const gltf::Animation& a = m.animations[index];
    out.name = a.name.empty() ? Format("clip_%zu", index) : a.name;
    out.duration = clipDuration(index);
    if (a.extras) out.animationType = (*a.extras)["AnimationType"].asString();

    struct NodeCurves { Curve t, r, s; };
    std::map<int, NodeCurves> per;
    for (const auto& ch : a.channels) {
        if (ch.node < 0 || ch.sampler < 0 || static_cast<size_t>(ch.sampler) >= a.samplers.size()) continue;
        const gltf::Sampler& s = a.samplers[static_cast<size_t>(ch.sampler)];
        Curve* cv = nullptr;
        NodeCurves& nc = per[ch.node];
        if (ch.path == "translation") cv = &nc.t;
        else if (ch.path == "rotation") cv = &nc.r;
        else if (ch.path == "scale") cv = &nc.s;
        else continue;   // weights (morph targets) are not used
        int ct = 0;
        if (!m.readFloats(s.input, cv->t, ct) || !m.readFloats(s.output, cv->v, cv->comps)) { *cv = Curve(); continue; }
        cv->mode = s.interpolation == "STEP" ? Curve::Step : s.interpolation == "CUBICSPLINE" ? Curve::Cubic : Curve::Linear;
        const size_t need = cv->t.size() * static_cast<size_t>(cv->comps) * (cv->mode == Curve::Cubic ? 3u : 1u);
        if (cv->v.size() < need) { *cv = Curve(); continue; }
    }

    for (const auto& kv : per) {
        auto jn = nodeJoint_.find(kv.first);
        if (jn == nodeJoint_.end() || static_cast<size_t>(kv.first) >= m.nodes.size()) continue;
        const gltf::Node& node = m.nodes[static_cast<size_t>(kv.first)];
        const NodeCurves& nc = kv.second;
        JointTrack tr;
        tr.joint = jn->second;
        tr.hasT = nc.t.valid(); tr.hasR = nc.r.valid(); tr.hasS = nc.s.valid();
        if (!tr.hasT && !tr.hasR && !tr.hasS) continue;
        tr.step = (tr.hasT && nc.t.mode == Curve::Step) || (tr.hasR && nc.r.mode == Curve::Step);
        std::vector<float> times;
        for (const Curve* c : { &nc.t, &nc.r, &nc.s })
            if (c->valid()) times.insert(times.end(), c->t.begin(), c->t.end());
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](float x, float y) { return std::fabs(x - y) < 1e-6f; }), times.end());
        for (float tm : times) {
            double t3[3] = { node.t[0], node.t[1], node.t[2] };
            double r4[4] = { node.r[0], node.r[1], node.r[2], node.r[3] };
            double s3[3] = { node.s[0], node.s[1], node.s[2] };
            if (tr.hasT && nc.t.comps == 3) nc.t.eval(tm, t3);
            if (tr.hasR && nc.r.comps == 4) nc.r.eval(tm, r4);
            if (tr.hasS && nc.s.comps == 3) nc.s.eval(tm, s3);
            if (std::fabs(s3[0] - 1) > 1e-4 || std::fabs(s3[1] - 1) > 1e-4 || std::fabs(s3[2] - 1) > 1e-4) tr.nonUnitScale = true;
            const Xf g = FromTRS(Vec3(t3[0], t3[1], t3[2]), Quatd(r4[0], r4[1], r4[2], r4[3]), Vec3(s3[0], s3[1], s3[2]));
            tr.times.push_back(tm);
            tr.local.push_back(FromGltf(g));
        }
        out.tracks.push_back(std::move(tr));
    }
    return true;
}

Timeline LayoutClips(const std::vector<double>& durations, double gap) {
    Timeline tl;
    double t = 0.0;
    for (double d : durations) {
        tl.starts.push_back(t);
        t += std::max(d, 0.0) + gap;
    }
    tl.end = durations.empty() ? 0.0 : t - gap;
    return tl;
}

} // namespace cp77
