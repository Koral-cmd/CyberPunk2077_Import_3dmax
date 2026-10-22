// ============================================================
//  WolvenKit.CLI driver, local file store and archive index.
//
//  The game data never goes through our own archive reader -
//  WolvenKit.CLI (cp77tools) does all of that:
//
//    index    archive <folders> --list --regex "\.(ent|anims)$"
//    JSON     unbundle <folders> -o <cache> --regex <exact paths>
//             convert serialize <cache>\<file> ...   -> <file>.json
//    GLB      uncook <folders> -o <cache> --regex <exact paths>
//             --mesh-export-type MeshOnly [--mesh-export-lod-filter]
//             -> x.mesh => x.glb,  x.anims => x.anims.glb
//
//  Every call is batched: one call per stage for all paths the
//  stage needs, because each call loads the archives again.
// ============================================================
#pragma once
#include "settings.h"

#include <string>
#include <vector>

namespace cp77 {

// Where extracted files live. The cache comes first; extra roots
// allow importing straight from a WolvenKit project
// (<project>\source\archive and <project>\source\raw).
class FileStore {
public:
    explicit FileStore(const std::string& cache) : cache_(cache) {}
    void addRoot(const std::string& root);
    const std::string& cache() const { return cache_; }

    std::string jsonFile(const std::string& depot) const;   // "" if not present
    std::string rawFile(const std::string& depot) const;
    std::string glbFile(const std::string& depot) const;    // .mesh -> .glb, .anims -> .anims.glb
    static std::string GlbName(const std::string& depot);   // relative name of the GLB
private:
    std::string cache_;
    std::vector<std::string> roots_;
    std::string find(const std::string& rel) const;
};

class WolvenKit {
public:
    explicit WolvenKit(const Settings& s);

    bool configured(std::string& why) const;
    // Runs the CLI; output (stdout+stderr) is returned and logged.
    int run(const std::vector<std::string>& args, std::string& output, std::string& error) const;
    int runOnce(const std::vector<std::string>& args, std::string& output, std::string& error) const;

    // Make sure <depot>.json exists in the store for each path.
    bool ensureJson(FileStore& store, const std::vector<std::string>& depots, std::string& error) const;
    // Make sure the GLB exists for each .mesh / .anims path.
    bool ensureGlb(FileStore& store, const std::vector<std::string>& depots, std::string& error) const;

    // Lists every .ent and .anims depot path of the configured archives.
    bool listArchive(const std::string& regex, std::vector<std::string>& paths, std::string& error) const;

    static std::vector<std::string> ParseListing(const std::string& output);
    static std::string QuoteArg(const std::string& a);       // Windows command line rules
    static std::vector<std::string> ExactRegexes(const std::vector<std::string>& depots, size_t maxLen);
private:
    const Settings& s_;
    std::vector<std::string> archiveArgs() const;
};

// ------------------------------------------------------------
//  Index of depot paths (cached in <home>\index.txt).
// ------------------------------------------------------------
class ArchiveIndex {
public:
    bool load(const std::string& file);
    bool save(const std::string& file) const;
    void set(std::vector<std::string> paths);
    size_t size() const { return paths_.size(); }
    // kind: "character" | "weapon" | "anims" | "ent" | "" (all)
    // filter: space-separated words, all must occur (case-insensitive)
    std::vector<std::string> search(const std::string& kind, const std::string& filter, size_t max) const;
private:
    std::vector<std::string> paths_;
    std::vector<std::string> lower_;
};

} // namespace cp77
