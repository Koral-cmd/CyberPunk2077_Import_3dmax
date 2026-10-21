// ============================================================
//  cp77probe - the plugin's core without 3ds Max.
//
//  Uses the same settings.ini as the plugin, so it shows exactly
//  what the import would do - handy when something looks wrong
//  in Max, and the output can be sent along with import.log.
//
//    cp77probe index                         build the archive index
//    cp77probe find <kind> <words...>        kind: character | weapon | anims | ent
//    cp77probe apps <entity.ent>             appearances of an entity
//    cp77probe plan <entity.ent> [appearance] [weapon.ent] [weaponAppearance]
//    cp77probe mesh <file.glb>               submeshes, bones, bind pose
//    cp77probe clips <animset.anims | file.anims.glb>
// ============================================================
#include "../src/core/anim.h"
#include "../src/core/assembler.h"
#include "../src/core/meshdata.h"
#include "../src/core/settings.h"
#include "../src/core/util.h"
#include "../src/core/wolvenkit.h"

#include <cstdio>

using namespace cp77;

static int Usage() {
    std::puts("cp77probe index | find <kind> <words> | apps <ent> | plan <ent> [app] [weapon.ent] [weaponApp] | mesh <glb> | clips <anims|glb>");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    Settings st;
    const std::string ini = JoinPath(Settings::DefaultDir(), "settings.ini");
    if (!st.load(ini)) std::printf("note: %s not found - using defaults\n", ini.c_str());
    LogOpen(JoinPath(Settings::DefaultDir(), "probe.log"));
    WolvenKit wk(st);
    FileStore store(st.cacheDir());
    std::string err;

    if (cmd == "index") {
        std::vector<std::string> paths;
        if (!wk.listArchive("\\.(ent|anims)$", paths, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        ArchiveIndex idx;
        idx.set(paths);
        idx.save(JoinPath(Settings::DefaultDir(), "index.txt"));
        std::printf("%zu paths indexed\n", idx.size());
        return 0;
    }
    if (cmd == "find" && argc >= 3) {
        ArchiveIndex idx;
        idx.load(JoinPath(Settings::DefaultDir(), "index.txt"));
        std::string words;
        for (int i = 3; i < argc; ++i) words += std::string(argv[i]) + " ";
        for (const auto& p : idx.search(argv[2], words, 200)) std::printf("%s\n", p.c_str());
        return 0;
    }
    if (cmd == "apps" && argc >= 3) {
        Assembler as(store, &wk, st);
        EntitySummary es;
        if (!as.describe(NormalizeDepotPath(argv[2]), es, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        std::printf("default: %s\n", es.defaultAppearance.c_str());
        for (const auto& a : es.appearances) std::printf("  %s\n", a.c_str());
        return 0;
    }
    if (cmd == "plan" && argc >= 3) {
        Assembler as(store, &wk, st);
        ScenePlan plan;
        if (!as.build(NormalizeDepotPath(argv[2]), argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "", argc > 5 ? argv[5] : "", "", plan, err)) {
            std::printf("ERROR %s\n", err.c_str());
            return 1;
        }
        std::printf("%s", plan.summary().c_str());
        for (const auto& m : plan.meshes) {
            if (m.glb.empty()) continue;
            MeshData md;
            if (!LoadMeshGlb(m.glb, md, err)) { std::printf("  %s: %s\n", m.component.c_str(), err.c_str()); continue; }
            size_t v = 0, shown = 0;
            for (const auto& p : md.parts) { v += p.pos.size(); if (ChunkVisible(m.chunkMask, p.chunk)) ++shown; }
            std::printf("  %-30s %zu submeshes (%zu shown), %zu verts, %zu bones%s\n", m.component.c_str(), md.parts.size(), shown, v,
                        md.jointNames.size(), md.bindHasRotation ? "" : ", bind without rotations");
        }
        const auto slots = as.slotNames();
        if (!slots.empty()) std::printf("slots: %s\n", Join(slots, ", ").c_str());
        return 0;
    }
    if (cmd == "mesh" && argc >= 3) {
        MeshData md;
        if (!LoadMeshGlb(argv[2], md, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        for (const auto& p : md.parts)
            std::printf("%-50s chunk %2d lod %d  %6zu verts %6zu tris  %d influences  material %s\n", p.name.c_str(), p.chunk, p.lod,
                        p.pos.size(), p.tris.size() / 3, p.influences, p.materials.empty() ? "-" : p.materials[0].c_str());
        for (size_t j = 0; j < md.jointNames.size(); ++j) {
            const Vec3 t = md.jointBind[j].translation();
            std::printf("  bone %-32s bind at %8.4f %8.4f %8.4f\n", md.jointNames[j].c_str(), t.x, t.y, t.z);
        }
        return 0;
    }
    if (cmd == "clips" && argc >= 3) {
        std::string glb = argv[2];
        if (!IEndsWith(glb, ".glb")) {
            if (!wk.ensureGlb(store, { NormalizeDepotPath(glb) }, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
            glb = store.glbFile(NormalizeDepotPath(glb));
        }
        AnimSet set;
        if (!set.load(glb, err)) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        std::printf("rig %s, %zu joints, %zu clips\n", set.rigPath.c_str(), set.jointNames.size(), set.clipCount());
        const auto names = set.clipNames();
        for (size_t i = 0; i < names.size(); ++i) {
            Clip c;
            set.clip(i, c, err);
            std::printf("  %-48s %7.3f s  %3zu tracks  %6zu keys %s\n", names[i].c_str(), c.duration, c.tracks.size(), c.keyCount(), c.animationType.c_str());
        }
        return 0;
    }
    return Usage();
}
