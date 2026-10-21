// ============================================================
//  Core tests against the synthetic fixture (make_fixture.py).
//  Simulates what 3ds Max does with the prepared data - node
//  hierarchy, Skin (v * inverse(bind) * bone), linear keys -
//  and compares with answers computed independently in Python.
//
//  usage: test_core <fixture dir> [<fake cli> <work dir>]
// ============================================================
#include "../src/core/anim.h"
#include "../src/core/animprep.h"
#include "../src/core/assembler.h"
#include "../src/core/json.h"
#include "../src/core/meshdata.h"
#include "../src/core/prep.h"
#include "../src/core/red.h"
#include "../src/core/util.h"
#include "../src/core/wolvenkit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace cp77;

static int g_fail = 0, g_ok = 0;
#define CHECK(cond, ...) do { if (cond) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static bool NearV(const Vec3& a, const Vec3& b, double tol) { return (a - b).length() <= tol; }

static Xf FromRows12(const json::Value& v) {   // truth: 3x4 column-vector rows
    Xf m;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m.m[c][r] = v.at(static_cast<size_t>(r * 4 + c)).asDouble();
    m.setRow(3, Vec3(v.at(3).asDouble(), v.at(7).asDouble(), v.at(11).asDouble()));
    return m;
}

static void TestJson() {
    json::Value v;
    std::string err;
    CHECK(json::Parse("{\"a\":[1,2.5,-3e2],\"m\":18446744073709551613,\"s\":\"x\\u00e9\\n\",\"B\":true,\"n\":null}", v, &err), "parse: %s", err.c_str());
    CHECK(v["a"].size() == 3 && Near(v["a"].at(2).asDouble(), -300, 1e-9), "array");
    CHECK(v["m"].asUInt64() == 18446744073709551613ull, "uint64 survives");
    CHECK(v["s"].asString() == "x\xc3\xa9\n", "escapes");
    CHECK(v["b"].asBool() == true, "case-insensitive key");
    CHECK(v["zz"]["deeper"].isNull(), "missing chain is null");
    CHECK(!json::Parse("{\"a\":}", v, &err), "rejects bad json");
}

static void TestXform() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-1, 1);
    double worst = 0;
    for (int i = 0; i < 200; ++i) {
        Quatd q = Quatd(u(rng), u(rng), u(rng), u(rng)).normalized();
        Vec3 t(u(rng), u(rng), u(rng));
        // game transform directly
        Xf game = FromTRS(t, q, Vec3(1, 1, 1));
        // WolvenKit's export: t -> (x, z, -y), q -> (i, k, -j, r); then our way back
        Xf gl = FromTRS(Vec3(t.x, t.z, -t.y), Quatd(q.x, q.z, -q.y, q.w), Vec3(1, 1, 1));
        worst = std::max(worst, FromGltf(gl).maxDiff(game));
        // rotation round trip
        Quatd r = RotationOf(game);
        Xf back = FromTRS(t, r, Vec3(1, 1, 1));
        worst = std::max(worst, back.maxDiff(game));
        // inverse
        Xf id = game * game.inverse();
        worst = std::max(worst, id.maxDiff(Xf()));
    }
    CHECK(worst < 1e-9, "glTF <-> game similarity matches WolvenKit's formulas (worst %g)", worst);
    CHECK(NearV(PointFromGltf(Vec3(1, 2, 3)), Vec3(1, -3, 2), 1e-12), "point mapping");
    Xf s;
    CHECK(XfFromString(XfToString(FromTRS(Vec3(1, 2, 3), Quatd(0.1, 0.2, 0.3, 0.9), Vec3(1, 1, 1))), s) &&
          s.maxDiff(FromTRS(Vec3(1, 2, 3), Quatd(0.1, 0.2, 0.3, 0.9), Vec3(1, 1, 1))) < 1e-7, "xf string round trip");
    int c, l;
    CHECK(ParseSubmeshName("h0_000_pwa_c__basehead_submesh_03_LOD_1", c, l) && c == 3 && l == 1, "submesh name");
    CHECK(ParseSubmeshName("submesh_12_LOD_2", c, l) && c == 12 && l == 2, "submesh name 2");
    CHECK(ChunkVisible(~2ull, 0) && !ChunkVisible(~2ull, 1) && ChunkVisible(0, -1), "chunk mask bits");
}

// local transform at time t from key lists (what Max's linear controllers do)
static Xf Sample(const BoneKeys& k, double t) {
    if (t <= k.times.front()) return k.local.front();
    if (t >= k.times.back()) return k.local.back();
    size_t hi = 1;
    while (hi < k.times.size() && k.times[hi] < t) ++hi;
    const size_t lo = hi - 1;
    const double u = (t - k.times[lo]) / (k.times[hi] - k.times[lo]);
    const Quatd q = Slerp(RotationOf(k.local[lo]), RotationOf(k.local[hi]), u);
    return FromTRS(Lerp(k.local[lo].translation(), k.local[hi].translation(), u), q, Vec3(1, 1, 1));
}

static void TestPipeline(const std::string& dir) {
    json::Value truth;
    std::string err;
    CHECK(json::ParseFile(JoinPath(dir, "truth.json"), truth, &err), "truth: %s", err.c_str());
    const std::string ch = truth["files"]["character"].asString();
    const std::string gun = truth["files"]["weapon"].asString();
    const std::string anims = truth["files"]["anims"].asString();

    Settings st;
    st.set(Settings::RestPose, "APose");
    FileStore store(dir);
    Assembler as(store, nullptr, st);

    EntitySummary es;
    CHECK(as.describe(ch, es, err), "describe: %s", err.c_str());
    CHECK(es.appearances.size() == 2 && es.defaultAppearance == "default", "appearances listed (%zu)", es.appearances.size());

    ScenePlan plan;
    CHECK(as.build(ch, "", gun, "", "", plan, err), "build: %s", err.c_str());
    std::printf("%s", plan.summary().c_str());
    CHECK(plan.skeletons.size() == 2, "character + weapon skeletons (%zu)", plan.skeletons.size());
    if (plan.skeletons.size() < 2) return;
    const PlanSkeleton& body = plan.skeletons[0];
    CHECK(body.bones.size() == 6, "body + dangle merged by name: 6 bones (%zu)", body.bones.size());
    CHECK(body.find("dyng_hair") >= 0 && body.bones[static_cast<size_t>(body.find("dyng_hair"))].parent == body.find("Head"), "dangle bone under Head");
    CHECK(body.animsets.size() == 1, "animset collected");
    CHECK(plan.meshes.size() == 3, "three mesh components (%zu)", plan.meshes.size());
    const PlanMesh* hair = nullptr;
    for (const auto& m : plan.meshes) if (m.component == "hair") hair = &m;
    CHECK(hair && hair->appearance == "blonde", "partsOverride applied to the hair");
    CHECK(hair && hair->skeleton == 0, "hair skinned to the merged skeleton");
    const PlanSkeleton& w = plan.skeletons[1];
    CHECK(w.owner == "weapon" && w.parentSkeleton == 0 && w.parentBone == "WeaponRight", "weapon on WeaponRight (%s)", w.parentBone.c_str());
    CHECK(NearV(w.offset.translation(), Vec3(0.01, 0.02, 0), 1e-9), "slot offset");
    for (const auto& kv : truth["rest_apose"].obj) {
        const int b = body.find(kv.first);
        CHECK(b >= 0 && body.bones[static_cast<size_t>(b)].restWorld.maxDiff(FromRows12(kv.second)) < 1e-9, "A-pose world of %s", kv.first.c_str());
    }

    // ---- prep -------------------------------------------------------------
    const double s = 100.0;   // centimetres
    PrepOptions opt;
    opt.unitsPerMeter = s;
    PrepScene scene;
    PrepSkeletons(plan, opt, scene);
    CHECK(scene.nodes.size() == 2 + 6 + 2, "nodes: 2 helpers + 8 bones (%zu)", scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i)
        CHECK(scene.nodes[i].parent < static_cast<int>(i), "parent before child (%zu)", i);
    {
        const Xf expect = ScaledTranslation(FromRows12(truth["weapon_root_world"]), s);
        const int hw = scene.skeletonHelper[1];
        CHECK(scene.nodes[static_cast<size_t>(hw)].world.maxDiff(expect) < 1e-7, "weapon helper at the slot (%g)", scene.nodes[static_cast<size_t>(hw)].world.maxDiff(expect));
    }

    PrepMesh bodyMesh;
    size_t bodyIndex = 0;
    for (size_t i = 0; i < plan.meshes.size(); ++i) if (plan.meshes[i].component == "t0_body") bodyIndex = i;
    MeshData md;
    CHECK(LoadMeshGlb(plan.meshes[bodyIndex].glb, md, err), "body glb: %s", err.c_str());
    CHECK(md.parts.size() == 2 && md.bindHasRotation, "two submeshes with bind rotations");
    CHECK(PrepMeshFrom(plan, bodyIndex, md, opt, scene, bodyMesh, err), "prep body: %s", err.c_str());
    CHECK(bodyMesh.chunksShown == 1 && bodyMesh.chunksHidden == 1, "chunk 1 hidden by the mask");
    CHECK(bodyMesh.verts.size() == 3, "UV-seam duplicate welded: 3 verts (%zu)", bodyMesh.verts.size());
    CHECK(bodyMesh.uv0.size() == 4 * 2 && bodyMesh.tvFaces.size() == bodyMesh.faces.size(), "tverts stay per source vertex");
    CHECK(bodyMesh.bindOverrides.empty(), "A-pose rest = mesh bind: no overrides (%zu)", bodyMesh.bindOverrides.size());
    CHECK(bodyMesh.materials.size() == 1 && bodyMesh.materials[0] == "mat_skin", "material name");

    // ---- animation + skin simulation ------------------------------------
    AnimSet set;
    std::string animGlb = store.glbFile(anims);
    CHECK(!animGlb.empty() && set.load(animGlb, err), "anims: %s", err.c_str());
    CHECK(set.clipCount() == 2 && EndsWith(set.rigPath, "body.rig"), "clips and rig path (%s)", set.rigPath.c_str());
    Clip idle;
    CHECK(set.clip(static_cast<size_t>(set.findClip("idle")), idle, err), "clip idle");
    CHECK(Near(idle.duration, 1.0, 1e-6) && idle.tracks.size() == 2, "idle: 1 s, 2 tracks");

    std::vector<TargetBone> target;
    std::vector<int> targetNode;
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const PrepNode& n = scene.nodes[i];
        if (n.helper || n.skeleton != 0) continue;
        TargetBone tb;
        tb.name = n.name;
        tb.restLocal = n.world * scene.nodes[static_cast<size_t>(n.parent)].world.inverse();
        target.push_back(tb);
        targetNode.push_back(static_cast<int>(i));
    }
    CHECK(Near(MatchRatio(idle, target), 1.0, 1e-9), "idle fits the skeleton");

    AnimPrepResult ar;
    PrepAnimation({ { &idle, 0.0 } }, set, target, s, ar);
    CHECK(ar.keys.size() == target.size(), "every bone keyed in single-clip mode");
    std::vector<int> keyOf(scene.nodes.size(), -1);
    for (size_t k = 0; k < ar.keys.size(); ++k) keyOf[static_cast<size_t>(targetNode[static_cast<size_t>(ar.keys[k].bone)])] = static_cast<int>(k);

    double worst = 0;
    for (const auto& d : truth["deform"].arr) {
        const double t = d["t"].asDouble();
        // animated worlds through the Max hierarchy
        std::vector<Xf> W(scene.nodes.size());
        for (size_t i = 0; i < scene.nodes.size(); ++i) {
            const PrepNode& n = scene.nodes[i];
            if (keyOf[i] >= 0) W[i] = Sample(ar.keys[static_cast<size_t>(keyOf[i])], t) * (n.parent >= 0 ? W[static_cast<size_t>(n.parent)] : Xf());
            else W[i] = n.parent >= 0 ? (n.world * scene.nodes[static_cast<size_t>(n.parent)].world.inverse()) * W[static_cast<size_t>(n.parent)] : n.world;
        }
        const Vec3 rest(d["rest"].at(0).asDouble() * s, d["rest"].at(1).asDouble() * s, d["rest"].at(2).asDouble() * s);
        size_t vi = 0;
        double best = 1e30;
        for (size_t v = 0; v < bodyMesh.verts.size(); ++v) {
            const double dd = (bodyMesh.verts[v] - rest).length();
            if (dd < best) { best = dd; vi = v; }
        }
        CHECK(best < 1e-3, "vertex found at rest (%g)", best);
        Vec3 out;
        for (int i = 0; i < bodyMesh.influences; ++i) {
            const int node = bodyMesh.infNode[vi * static_cast<size_t>(bodyMesh.influences) + static_cast<size_t>(i)];
            const float wgt = bodyMesh.infWeight[vi * static_cast<size_t>(bodyMesh.influences) + static_cast<size_t>(i)];
            if (node < 0 || wgt <= 0) continue;
            Xf bind = scene.nodes[static_cast<size_t>(node)].world;
            for (const auto& o : bodyMesh.bindOverrides) if (o.node == node) bind = o.world;
            out = out + (bind.inverse() * W[static_cast<size_t>(node)]).point(bodyMesh.verts[vi]) * wgt;
        }
        const Vec3 expect(d["deformed"].at(0).asDouble() * s, d["deformed"].at(1).asDouble() * s, d["deformed"].at(2).asDouble() * s);
        worst = std::max(worst, (out - expect).length());
    }
    CHECK(worst < 1e-3, "skinned + animated vertices match the game-space truth (worst %g units)", worst);

    // reference pose -> the mesh bind differs at Spine: overrides must appear and still deform right
    {
        Settings st2;
        st2.set(Settings::RestPose, "Reference");
        Assembler as2(store, nullptr, st2);
        ScenePlan p2;
        as2.build(ch, "", "", "", "", p2, err);
        PrepScene sc2;
        PrepSkeletons(p2, opt, sc2);
        PrepMesh pm2;
        size_t bi = 0;
        for (size_t i = 0; i < p2.meshes.size(); ++i) if (p2.meshes[i].component == "t0_body") bi = i;
        PrepMeshFrom(p2, bi, md, opt, sc2, pm2, err);
        CHECK(!pm2.bindOverrides.empty(), "reference rest pose -> bind overrides (%zu)", pm2.bindOverrides.size());
        // at rest, Max skin must leave the mesh where the bones are: v * inv(bind) * rest
        double moved = 0;
        for (size_t v = 0; v < pm2.verts.size(); ++v) {
            for (int i = 0; i < pm2.influences; ++i) {
                const int node = pm2.infNode[v * static_cast<size_t>(pm2.influences) + static_cast<size_t>(i)];
                if (node < 0) continue;
                for (const auto& o : pm2.bindOverrides)
                    if (o.node == node) moved = std::max(moved, ((o.world.inverse() * sc2.nodes[static_cast<size_t>(node)].world).point(pm2.verts[v]) - pm2.verts[v]).length());
            }
        }
        CHECK(moved > 0.1, "mesh follows the reference pose (moves %g units)", moved);
    }

    // all clips on one timeline
    Clip wave;
    set.clip(static_cast<size_t>(set.findClip("wave")), wave, err);
    const Timeline tl = LayoutClips({ idle.duration, wave.duration }, 10.0 / 30.0);
    CHECK(Near(tl.starts[1], 1.0 + 10.0 / 30.0, 1e-9), "second clip after the gap");
    AnimPrepResult all;
    PrepAnimation({ { &idle, tl.starts[0] }, { &wave, tl.starts[1] } }, set, target, s, all);
    bool ascending = true;
    for (const auto& k : all.keys)
        for (size_t i = 1; i < k.times.size(); ++i) if (k.times[i] <= k.times[i - 1]) ascending = false;
    CHECK(ascending, "key times ascending");
    const BoneKeys* head = nullptr;
    for (const auto& k : all.keys) if (target[static_cast<size_t>(k.bone)].name == "Head") head = &k;
    CHECK(head != nullptr, "head keyed");
    if (head) {
        // STEP: value held until just before 0.2 s of the wave clip
        const double t1 = tl.starts[1] + 0.2 - 1.0 / 4800.0;
        bool hold = false;
        for (size_t i = 0; i < head->times.size(); ++i)
            if (Near(head->times[i], t1, 1e-6) && Near(head->local[i].translation().z, 0.5 * s, 1e-6)) hold = true;
        CHECK(hold, "STEP key holds the previous value");
        // during idle the head is static at its rest pose: keys at idle start and end
        CHECK(Near(head->times[0], 0.0, 1e-12) && Near(head->times[1], 1.0, 1e-9), "static bone keyed at clip start and end");
    }
}

static void TestListing() {
    const std::string out =
        "[12:00:01 INF] Loading archives...\n"
        "base\\characters\\entities\\gang\\gang__maelstrom_ma.ent\r\n"
        "  base\\animations\\npc\\gameplay\\man_average\\locomotion\\walk.anims\n"
        "1234567890123\n"
        "C:\\Games\\Cyberpunk 2077\\archive\\pc\\content\\basegame_1_engine.archive\n"
        "base\\characters\\entities\\gang\\gang__maelstrom_ma.ent\n";
    const auto v = WolvenKit::ParseListing(out);
    CHECK(v.size() == 2, "listing keeps depot paths only (%zu)", v.size());
    CHECK(WolvenKit::QuoteArg("a b\\") == "\"a b\\\\\"", "quote trailing backslash");
    CHECK(WolvenKit::QuoteArg("x\"y") == "\"x\\\"y\"", "quote inner quote");
    CHECK(WolvenKit::QuoteArg("C:\\a\\b") == "C:\\a\\b", "no quotes needed");
    const auto rx = WolvenKit::ExactRegexes({ "base\\a.ent", "base\\b(1).mesh" }, 1000);
    CHECK(rx.size() == 1 && rx[0] == "(?i)^(?:base\\\\a\\.ent|base\\\\b\\(1\\)\\.mesh)$", "exact regex: %s", rx.empty() ? "" : rx[0].c_str());
    CHECK(FileStore::GlbName("base\\x\\body.mesh") == "base\\x\\body.glb" && FileStore::GlbName("a\\b.anims") == "a\\b.anims.glb", "glb names");
    ArchiveIndex idx;
    idx.set({ "base\\weapons\\rifle\\w_rifle.ent", "base\\characters\\entities\\judy.ent", "base\\animations\\a.anims", "base\\characters\\entities\\panam.ent" });
    CHECK(idx.search("character", "", 10).size() == 2 && idx.search("character", "JUDY", 10).size() == 1, "index search");
    CHECK(idx.search("weapon", "", 10).size() == 1 && idx.search("anims", "", 10).size() == 1, "index kinds");
}

// End to end through the CLI driver, with tests/fake_wolvenkit.py standing in.
static void TestDriver(const std::string& dir, const std::string& fakeCli, const std::string& work) {
    json::Value truth;
    std::string err;
    json::ParseFile(JoinPath(dir, "truth.json"), truth, &err);
    MakeDirs(JoinPath(work, "game/archive/pc/content"));
    Settings st;
    st.set(Settings::WolvenKitCli, fakeCli);
    st.set(Settings::GamePath, JoinPath(work, "game"));
    st.set(Settings::CacheDir, JoinPath(work, "cache"));
    WolvenKit wk(st);
    std::string why;
    CHECK(wk.configured(why), "configured: %s", why.c_str());
    std::vector<std::string> list;
    CHECK(wk.listArchive("\\.(ent|anims)$", list, err), "list: %s", err.c_str());
    CHECK(list.size() == 4, "listing finds 3 .ent and 1 .anims (%zu)", list.size());
    FileStore store(st.cacheDir());
    Assembler as(store, &wk, st);
    ScenePlan plan;
    CHECK(as.build(truth["files"]["character"].asString(), "", truth["files"]["weapon"].asString(), "", "", plan, err), "build via CLI: %s", err.c_str());
    CHECK(plan.meshes.size() == 3, "meshes via CLI (%zu)", plan.meshes.size());
    size_t withGlb = 0;
    for (const auto& m : plan.meshes) if (!m.glb.empty()) ++withGlb;
    CHECK(withGlb == 3, "every mesh exported (%zu)", withGlb);
    CHECK(wk.ensureGlb(store, { truth["files"]["anims"].asString() }, err) && !store.glbFile(truth["files"]["anims"].asString()).empty(), "anims exported: %s", err.c_str());
    std::string calls;
    ReadFile(JoinPath(dir, "calls.txt"), calls);
    size_t n = 0;
    for (const auto& l : Split(calls, '\n')) if (!Trim(l).empty()) ++n;
    std::printf("fake WolvenKit called %zu times\n", n);
    CHECK(n <= 11, "calls are batched (%zu)", n);
    // second build: everything cached, no further calls
    ScenePlan again;
    as.build(truth["files"]["character"].asString(), "", truth["files"]["weapon"].asString(), "", "", again, err);
    std::string calls2;
    ReadFile(JoinPath(dir, "calls.txt"), calls2);
    CHECK(calls2 == calls, "cache hit: no new WolvenKit calls");
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "fixture";
    LogOpen(JoinPath(dir, "test.log"));
    TestJson();
    TestXform();
    TestListing();
    TestPipeline(dir);
    if (argc > 3) TestDriver(dir, argv[2], argv[3]);
    std::printf("\n%d checks passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
