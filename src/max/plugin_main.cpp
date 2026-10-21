// ============================================================
//  Settings, index, appearance lists and the import entry points.
// ============================================================
#include "plugin_internal.h"

#include <memory>

using namespace cp77;

namespace cp77max {

namespace {
Settings g_settings;
bool g_loaded = false;
std::unique_ptr<FileStore> g_store;
std::string g_storeDir;
std::unique_ptr<WolvenKit> g_cli;
ArchiveIndex g_index;
bool g_indexLoaded = false;

std::string IndexFile() { return JoinPath(Settings::DefaultDir(), "index.txt"); }
}

Settings& Config() {
    if (!g_loaded) {
        g_loaded = true;
        const std::string path = JoinPath(Settings::DefaultDir(), "settings.ini");
        if (!g_settings.load(path)) {
            // first start: defaults that work for most installs
            g_settings.set(Settings::RestPose, "APose");
            g_settings.set(Settings::UseMeshBind, "1");
            g_settings.set(Settings::LodFilter, "1");
            g_settings.set(Settings::WeldVertices, "1");
            g_settings.set(Settings::WeaponSlot, "WeaponRight");
            g_settings.set(Settings::GapFrames, "10");
            g_settings.set(Settings::FrameRate, "30");
            g_settings.set(Settings::BoneSize, "0.01");
            g_settings.set(Settings::UnitsPerMeter, "0");
            g_settings.set(Settings::ExtractTimeout, "900");
            g_settings.set(Settings::GamePath, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Cyberpunk 2077");
            g_settings.save();
        }
    }
    return g_settings;
}

FileStore& Store() {
    const std::string dir = Config().cacheDir();
    if (!g_store || g_storeDir != dir) {
        g_store.reset(new FileStore(dir));
        g_storeDir = dir;
    }
    return *g_store;
}

const WolvenKit* Cli() {
    if (!g_cli) g_cli.reset(new WolvenKit(Config()));
    return g_cli.get();
}

double UnitsPerMeter() {
    const double fixedValue = Config().getDouble(Settings::UnitsPerMeter, 0.0);
    if (fixedValue > 0.0) return fixedValue;
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const double m = GetSystemUnitScale(UNITS_METERS);   // metres per system unit
#else
    const double m = GetMasterScale(UNITS_METERS);
#endif
    return m > 0.0 ? 1.0 / m : 1.0;
}

void StartLog(const char* what) {
    LogOpen(JoinPath(Settings::DefaultDir(), "import.log"));
    Log("CP2077 Import %s - %s, Max %d", Narrow(CP77IMPORT_VERSION_STR).c_str(), what, MAX_RELEASE);
}

Matrix3 ToMax(const Xf& x) {
    Matrix3 m;
    for (int r = 0; r < 4; ++r)
        m.SetRow(r, Point3(static_cast<float>(x.m[r][0]), static_cast<float>(x.m[r][1]), static_cast<float>(x.m[r][2])));
    return m;
}
Xf FromMax(const Matrix3& m) {
    Xf x;
    for (int r = 0; r < 4; ++r) {
        const Point3 p = m.GetRow(r);
        x.setRow(r, Vec3(p.x, p.y, p.z));
    }
    return x;
}

// ---- MAXScript surface -----------------------------------------------------
std::wstring Version() { return CP77IMPORT_VERSION_STR; }

std::wstring GetSetting(const std::wstring& key) { return W(Config().get(Narrow(key))); }

bool SetSetting(const std::wstring& key, const std::wstring& value) {
    Config().set(Narrow(key), Narrow(value));
    g_cli.reset();
    return Config().save();
}

std::wstring SettingsPath() { return W(Config().path()); }
std::wstring LogPath() { return W(JoinPath(Settings::DefaultDir(), "import.log")); }

static void LoadIndex() {
    if (!g_indexLoaded) { g_index.load(IndexFile()); g_indexLoaded = true; }
}

std::wstring BuildIndex() {
    StartLog("index");
    std::vector<std::string> paths;
    std::string err;
    if (!Cli()->listArchive("\\.(ent|anims)$", paths, err)) return W("ERROR\t" + err);
    g_index.set(paths);
    g_indexLoaded = true;
    g_index.save(IndexFile());
    return W(Format("%zu entities and animsets indexed", g_index.size()));
}

int IndexCount() { LoadIndex(); return static_cast<int>(g_index.size()); }

std::wstring Search(const std::wstring& kind, const std::wstring& filter, int max) {
    LoadIndex();
    const auto r = g_index.search(Narrow(kind), Narrow(filter), max > 0 ? static_cast<size_t>(max) : 500);
    return W(Join(r, "\n"));
}

std::wstring Appearances(const std::wstring& ent) {
    StartLog("appearances");
    Assembler as(Store(), Cli(), Config());
    EntitySummary es;
    std::string err;
    if (!as.describe(NormalizeDepotPath(Narrow(ent)), es, err)) return W("ERROR\t" + err);
    std::string out = "default\t" + es.defaultAppearance;
    for (const auto& a : es.appearances) out += "\n" + a;
    return W(out);
}

static std::wstring RunImport(const std::string& ent, const std::string& app, const std::string& wEnt,
                              const std::string& wApp, const std::string& slot) {
    Assembler as(Store(), Cli(), Config());
    ScenePlan plan;
    std::string err;
    if (!as.build(ent, app, wEnt, wApp, slot, plan, err)) {
        Log("ERROR %s", err.c_str());
        return W("ERROR\t" + err);
    }
    std::wstring report = BuildScene(plan);
    const auto slots = as.slotNames();
    if (!slots.empty()) report += W("\nSlots on the character: " + Join(slots, ", "));
    return report;
}

std::wstring ImportCharacter(const std::wstring& ent, const std::wstring& app, const std::wstring& weaponEnt,
                             const std::wstring& weaponApp, const std::wstring& slot) {
    StartLog("import");
    return RunImport(NormalizeDepotPath(Narrow(ent)), Narrow(app), NormalizeDepotPath(Narrow(weaponEnt)), Narrow(weaponApp), Narrow(slot));
}

// <project>\source\archive\base\...\x.ent.json  ->  roots + depot path
std::wstring ImportJson(const std::wstring& pathW) {
    StartLog("import json");
    std::string path = NormalizeDepotPath(Narrow(pathW));
    if (IEndsWith(path, ".json")) path = path.substr(0, path.size() - 5);
    const std::string low = Lower(path);
    size_t at = std::string::npos;
    for (const char* top : { "\\base\\", "\\ep1\\" }) {
        const size_t p = low.rfind(top);
        if (p != std::string::npos && (at == std::string::npos || p > at)) at = p;
    }
    if (at == std::string::npos) return W("ERROR\tthe file is not inside a base\\ or ep1\\ folder of a WolvenKit project or export");
    const std::string root = path.substr(0, at);
    const std::string depot = path.substr(at + 1);
    FileStore& st = Store();
    st.addRoot(root);
    const std::string lr = Lower(root);
    if (EndsWith(lr, "\\archive")) st.addRoot(root.substr(0, root.size() - 8) + "\\raw");
    if (EndsWith(lr, "\\raw")) st.addRoot(root.substr(0, root.size() - 4) + "\\archive");
    Log("project root %s, entity %s", root.c_str(), depot.c_str());
    if (Extension(depot) != ".ent") return W("ERROR\tnot an .ent.json: " + depot);
    return RunImport(depot, "", "", "", "");
}

int ImportFromFile(const MCHAR* path, BOOL suppressPrompts) {
    const std::wstring r = ImportJson(path ? std::wstring(path) : std::wstring());
    const bool failed = r.compare(0, 5, L"ERROR") == 0;
    if (!suppressPrompts) {
        Interface* ip = GetCOREInterface();
        MessageBox(ip ? ip->GetMAXHWnd() : nullptr, r.c_str(), _T("CP2077 Import"), failed ? MB_ICONERROR : MB_ICONINFORMATION);
    }
    return failed ? 0 : 1;   // IMPEXP_FAIL / IMPEXP_SUCCESS
}

} // namespace cp77max
