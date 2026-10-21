#include "wolvenkit.h"
#include "util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace cp77 {

// ---- FileStore -----------------------------------------------------------
void FileStore::addRoot(const std::string& root) {
    if (!root.empty() && std::find(roots_.begin(), roots_.end(), root) == roots_.end()) roots_.push_back(root);
}

std::string FileStore::find(const std::string& rel) const {
    const std::string c = JoinPath(cache_, rel);
    if (FileExists(c)) return c;
    for (const auto& r : roots_) {
        const std::string p = JoinPath(r, rel);
        if (FileExists(p)) return p;
    }
    return std::string();
}

std::string FileStore::jsonFile(const std::string& depot) const { return find(NormalizeDepotPath(depot) + ".json"); }
std::string FileStore::rawFile(const std::string& depot) const { return find(NormalizeDepotPath(depot)); }

std::string FileStore::GlbName(const std::string& depot) {
    const std::string d = NormalizeDepotPath(depot);
    if (Extension(d) == ".mesh") return d.substr(0, d.size() - 5) + ".glb";
    return d + ".glb";   // x.anims -> x.anims.glb, x.morphtarget -> x.morphtarget.glb
}
std::string FileStore::glbFile(const std::string& depot) const { return find(GlbName(depot)); }

// ---- WolvenKit -----------------------------------------------------------
WolvenKit::WolvenKit(const Settings& s) : s_(s) {}

bool WolvenKit::configured(std::string& why) const {
    const std::string exe = s_.get(Settings::WolvenKitCli);
    if (exe.empty()) { why = "WolvenKit.CLI is not set (Settings -> WolvenKit CLI)."; return false; }
    if (!FileExists(exe)) { why = "WolvenKit.CLI not found: " + exe; return false; }
    if (s_.archiveFolders().empty()) { why = "Game folder not set or has no archive\\pc\\content."; return false; }
    return true;
}

std::string WolvenKit::QuoteArg(const std::string& a) {
    // CommandLineToArgvW rules: backslashes are literal unless they precede a quote.
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string out = "\"";
    for (size_t i = 0;; ++i) {
        size_t bs = 0;
        while (i < a.size() && a[i] == '\\') { ++bs; ++i; }
        if (i == a.size()) { out.append(bs * 2, '\\'); break; }
        if (a[i] == '"') { out.append(bs * 2 + 1, '\\'); out += '"'; }
        else { out.append(bs, '\\'); out += a[i]; }
    }
    out += '"';
    return out;
}

std::vector<std::string> WolvenKit::archiveArgs() const { return s_.archiveFolders(); }

int WolvenKit::run(const std::vector<std::string>& args, std::string& output, std::string& error) const {
    const std::string exe = s_.get(Settings::WolvenKitCli);
    const std::string outFile = JoinPath(Settings::DefaultDir(), "wolvenkit_last.txt");
    MakeDirs(Settings::DefaultDir());
    std::string cmd = QuoteArg(NativePath(exe));
    for (const auto& a : args) cmd += " " + QuoteArg(a);
    Log("WolvenKit> %s", cmd.c_str());
    const int timeoutSec = std::max(10, s_.getInt(Settings::ExtractTimeout, 900));
    int code = -1;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileW(Widen(outFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = "cannot create " + outFile; return -1; }
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    std::wstring wcmd = Widen(cmd);
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(0);
    // Working directory = the CLI's folder (it looks for its oodle dll and appsettings there).
    const std::wstring wdir = Widen(DirName(NativePath(exe)));
    const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    if (!ok) {
        CloseHandle(h);
        error = Format("cannot start WolvenKit.CLI (error %lu)", static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    const DWORD w = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutSec) * 1000u);
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        error = Format("WolvenKit.CLI did not finish within %d s (setting ExtractTimeout)", timeoutSec);
    }
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    code = static_cast<int>(ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(h);
#else
    std::string sh;
    auto q = [](const std::string& a) { return "'" + ReplaceAll(a, "'", "'\\''") + "'"; };
    sh = q(NativePath(exe));
    for (const auto& a : args) sh += " " + q(a);
    sh = "timeout " + std::to_string(timeoutSec) + " " + sh + " > " + q(outFile) + " 2>&1";
    const int r = std::system(sh.c_str());
    code = (r == -1) ? -1 : (r >> 8) & 0xFF;
    if (code == 124) error = Format("WolvenKit.CLI did not finish within %d s (setting ExtractTimeout)", timeoutSec);
#endif
    ReadFile(outFile, output);
    // keep the log readable: the tail is what matters
    Log("WolvenKit exit %d, %zu bytes of output%s", code, output.size(), output.size() > 4000 ? " (tail below)" : "");
    Log("%s", output.size() > 4000 ? output.substr(output.size() - 4000).c_str() : output.c_str());
    return code;
}

std::vector<std::string> WolvenKit::ExactRegexes(const std::vector<std::string>& depots, size_t maxLen) {
    std::vector<std::string> out;
    std::string cur;
    for (const auto& d : depots) {
        const std::string e = RegexEscape(NormalizeDepotPath(d));
        if (!cur.empty() && cur.size() + e.size() + 8 > maxLen) { out.push_back("(?i)^(?:" + cur + ")$"); cur.clear(); }
        if (!cur.empty()) cur += "|";
        cur += e;
    }
    if (!cur.empty()) out.push_back("(?i)^(?:" + cur + ")$");
    return out;
}

static std::vector<std::string> Resolvable(const std::vector<std::string>& depots, std::vector<std::string>& hashes) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& d : depots) {
        if (d.empty()) continue;
        if (d[0] == '#') { hashes.push_back(d); continue; }
        if (seen.insert(DepotKey(d)).second) out.push_back(NormalizeDepotPath(d));
    }
    return out;
}

bool WolvenKit::ensureJson(FileStore& store, const std::vector<std::string>& depotsIn, std::string& error) const {
    std::vector<std::string> hashes;
    const std::vector<std::string> depots = Resolvable(depotsIn, hashes);
    for (const auto& h : hashes) Log("unresolved path hash %s - cannot be extracted by name", h.c_str());
    std::vector<std::string> needRaw, needConvert;
    for (const auto& d : depots) {
        if (!store.jsonFile(d).empty()) continue;
        if (store.rawFile(d).empty()) needRaw.push_back(d);
        else needConvert.push_back(d);
    }
    if (needRaw.empty() && needConvert.empty()) return true;
    std::string why;
    if (!configured(why)) {
        std::vector<std::string> miss = needRaw;
        miss.insert(miss.end(), needConvert.begin(), needConvert.end());
        error = why + " Missing: " + Join(miss, ", ");
        return false;
    }
    if (!needRaw.empty()) {
        for (const auto& rx : ExactRegexes(needRaw, 12000)) {
            std::vector<std::string> args = { "unbundle" };
            for (const auto& f : archiveArgs()) args.push_back(f);
            args.insert(args.end(), { "-o", store.cache(), "-r", rx });
            std::string out, err;
            run(args, out, err);
            if (!err.empty()) { error = err; return false; }
        }
        for (const auto& d : needRaw) if (!store.rawFile(d).empty()) needConvert.push_back(d);
    }
    // convert serialize: file list in batches (command line limit 32k)
    std::vector<std::string> batch;
    size_t len = 0;
    auto flush = [&]() -> bool {
        if (batch.empty()) return true;
        std::vector<std::string> args = { "convert", "serialize" };
        args.insert(args.end(), batch.begin(), batch.end());
        std::string out, err;
        run(args, out, err);
        batch.clear();
        len = 0;
        if (!err.empty()) { error = err; return false; }
        return true;
    };
    for (const auto& d : needConvert) {
        const std::string raw = store.rawFile(d);
        if (raw.empty()) continue;
        if (len + raw.size() > 12000 && !flush()) return false;
        batch.push_back(raw);
        len += raw.size() + 3;
    }
    if (!flush()) return false;

    std::vector<std::string> still;
    for (const auto& d : depots) if (store.jsonFile(d).empty()) still.push_back(d);
    if (!still.empty()) {
        error = "WolvenKit did not produce: " + Join(still, ", ") + " (see " + JoinPath(Settings::DefaultDir(), "wolvenkit_last.txt") + ")";
        return false;
    }
    return true;
}

bool WolvenKit::ensureGlb(FileStore& store, const std::vector<std::string>& depotsIn, std::string& error) const {
    std::vector<std::string> hashes;
    const std::vector<std::string> depots = Resolvable(depotsIn, hashes);
    std::vector<std::string> need;
    for (const auto& d : depots) if (store.glbFile(d).empty()) need.push_back(d);
    if (need.empty()) return true;
    std::string why;
    if (!configured(why)) { error = why + " Missing: " + Join(need, ", "); return false; }
    for (const auto& rx : ExactRegexes(need, 12000)) {
        std::vector<std::string> args = { "uncook" };
        for (const auto& f : archiveArgs()) args.push_back(f);
        args.insert(args.end(), { "-o", store.cache(), "-r", rx, "--mesh-export-type", "MeshOnly" });
        if (s_.getBool(Settings::LodFilter, true)) args.push_back("--mesh-export-lod-filter");
        const std::string game = s_.get(Settings::GamePath);
        if (!game.empty()) { args.push_back("-gp"); args.push_back(NativePath(game)); }
        std::string out, err;
        run(args, out, err);
        if (!err.empty()) { error = err; return false; }
    }
    std::vector<std::string> still;
    for (const auto& d : need) if (store.glbFile(d).empty()) still.push_back(d);
    if (!still.empty()) {
        error = "WolvenKit did not export: " + Join(still, ", ") + " (see " + JoinPath(Settings::DefaultDir(), "wolvenkit_last.txt") + ")";
        return false;
    }
    return true;
}

std::vector<std::string> WolvenKit::ParseListing(const std::string& output) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (std::string line : Split(output, '\n')) {
        line = Trim(line);
        if (line.size() < 4) continue;
        const unsigned char c0 = static_cast<unsigned char>(line[0]);
        if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || (c0 >= '0' && c0 <= '9') || c0 == '_')) continue;
        if (line.find('\\') == std::string::npos) continue;        // log lines and unresolved hashes
        if (line.find(':') != std::string::npos) continue;         // timestamps, absolute paths
        if (Extension(line).empty()) continue;
        if (seen.insert(Lower(line)).second) out.push_back(line);
    }
    return out;
}

bool WolvenKit::listArchive(const std::string& regex, std::vector<std::string>& paths, std::string& error) const {
    std::string why;
    if (!configured(why)) { error = why; return false; }
    std::set<std::string> seen;
    paths.clear();
    for (const auto& f : archiveArgs()) {
        std::vector<std::string> args = { "archive", f, "--list", "-r", regex };
        std::string out, err;
        run(args, out, err);
        if (!err.empty()) { error = err; return false; }
        for (const auto& p : ParseListing(out))
            if (seen.insert(Lower(p)).second) paths.push_back(p);
    }
    if (paths.empty()) { error = "WolvenKit listed no files - see wolvenkit_last.txt"; return false; }
    return true;
}

// ---- index ---------------------------------------------------------------
bool ArchiveIndex::load(const std::string& file) {
    std::string text;
    if (!ReadFile(file, text)) return false;
    std::vector<std::string> p;
    for (const auto& l : Split(text, '\n')) { const std::string t = Trim(l); if (!t.empty() && t[0] != ';') p.push_back(t); }
    set(std::move(p));
    return !paths_.empty();
}
bool ArchiveIndex::save(const std::string& file) const {
    std::string text = "; CP2077 Import archive index\n";
    for (const auto& p : paths_) text += p + "\n";
    return WriteFile(file, text);
}
void ArchiveIndex::set(std::vector<std::string> paths) {
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    paths.erase(std::unique(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) { return IEquals(a, b); }), paths.end());
    paths_ = std::move(paths);
    lower_.clear();
    lower_.reserve(paths_.size());
    for (const auto& p : paths_) lower_.push_back(Lower(p));
}
std::vector<std::string> ArchiveIndex::search(const std::string& kind, const std::string& filter, size_t max) const {
    std::vector<std::string> words;
    for (const auto& w : Split(Lower(filter), ' ')) if (!Trim(w).empty()) words.push_back(Trim(w));
    std::vector<std::string> out;
    for (size_t i = 0; i < paths_.size() && out.size() < max; ++i) {
        const std::string& l = lower_[i];
        const bool ent = EndsWith(l, ".ent");
        bool ok = true;
        if (kind == "character") ok = ent && l.find("\\characters\\") != std::string::npos;
        else if (kind == "weapon") ok = ent && l.find("\\weapons\\") != std::string::npos;
        else if (kind == "anims") ok = EndsWith(l, ".anims");
        else if (kind == "ent") ok = ent;
        if (!ok) continue;
        for (const auto& w : words) if (l.find(w) == std::string::npos) { ok = false; break; }
        if (ok) out.push_back(paths_[i]);
    }
    return out;
}

} // namespace cp77
