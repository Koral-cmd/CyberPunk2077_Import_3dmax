#include "settings.h"
#include "util.h"

#include <cstdlib>

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

static std::string Env(const char* name) {
#ifdef _WIN32
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(Widen(name).c_str(), buf, 2048);
    if (n == 0 || n >= 2048) return std::string();
    return Narrow(std::wstring(buf, n));
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

std::string Settings::DefaultDir() {
    const std::string over = Env("CP2077IMPORT_HOME");
    if (!over.empty()) return over;
#ifdef _WIN32
    std::string base = Env("LOCALAPPDATA");
    if (base.empty()) base = Env("APPDATA");
    return JoinPath(base, "CP2077Import");
#else
    return JoinPath(Env("HOME"), ".cp2077import");
#endif
}

bool Settings::load(const std::string& p) {
    path_ = p;
    values_.clear();
    std::string text;
    if (!ReadFile(p, text)) return false;
    for (std::string line : Split(text, '\n')) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values_[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }
    return true;
}

bool Settings::save() const {
    std::string text = "; CP2077 Import settings - edit with Max closed or from the importer window\r\n[CP2077Import]\r\n";
    for (const auto& kv : values_) text += kv.first + "=" + kv.second + "\r\n";
    return WriteFile(path_, text);
}

std::string Settings::get(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return (it == values_.end() || it->second.empty()) ? def : it->second;
}
double Settings::getDouble(const std::string& key, double def) const {
    const std::string v = get(key);
    if (v.empty()) return def;
    double d;
    return ParseDouble(ReplaceAll(v, ",", "."), d) ? d : def;   // "0,5" typed by hand also works
}
int Settings::getInt(const std::string& key, int def) const { return static_cast<int>(getDouble(key, def)); }
bool Settings::getBool(const std::string& key, bool def) const {
    const std::string v = Lower(get(key));
    if (v.empty()) return def;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
void Settings::set(const std::string& key, const std::string& value) { values_[key] = Trim(value); }
std::vector<std::string> Settings::keys() const {
    std::vector<std::string> k;
    for (const auto& kv : values_) k.push_back(kv.first);
    return k;
}

std::vector<std::string> Settings::archiveFolders() const {
    std::vector<std::string> out;
    const std::string game = get(GamePath);
    if (!game.empty()) {
        const char* subs[] = { "archive/pc/content", "archive/pc/ep1" };
        for (const char* s : subs) {
            const std::string p = JoinPath(game, s);
            if (DirExists(p)) out.push_back(p);
        }
    }
    for (const std::string& e : Split(get(ExtraArchives), ';')) {
        const std::string t = Trim(e);
        if (!t.empty() && (DirExists(t) || FileExists(t))) out.push_back(NativePath(t));
    }
    return out;
}

std::string Settings::cacheDir() const {
    const std::string c = get(CacheDir);
    return c.empty() ? JoinPath(DefaultDir(), "cache") : NativePath(c);
}

} // namespace cp77
