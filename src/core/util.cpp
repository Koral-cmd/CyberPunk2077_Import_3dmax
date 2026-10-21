#include "util.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <errno.h>
#endif

namespace cp77 {

// ---- UTF-8 <-> wide, without the Windows API so the core stays portable.
// wchar_t is UTF-16 on Windows and UTF-32 elsewhere; both are handled.
std::wstring Widen(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xFFFD; }
        ++i;
        for (int k = 0; k < extra; ++k, ++i) {
            if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        }
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string Narrow(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
            const uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
        }
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
bool StartsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool EndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool IEndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && IEquals(s.substr(s.size() - p.size()), p); }

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= ' ') --b;
    return s.substr(a, b - a);
}
std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) { out.push_back(s.substr(start, i - start)); start = i + 1; }
    }
    return out;
}
std::string Join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
    return out;
}
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
    return s;
}
std::string Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<size_t>(n) + 1);
        std::vsnprintf(&out[0], out.size(), fmt, ap2);
        out.resize(static_cast<size_t>(n));
    }
    va_end(ap2);
    return out;
}

std::string NormalizeDepotPath(std::string p) {
    p = Trim(p);
    for (char& c : p) if (c == '/') c = '\\';
    while (!p.empty() && p[0] == '\\') p.erase(0, 1);
    return p;
}
std::string DepotKey(const std::string& p) { return Lower(NormalizeDepotPath(p)); }
std::string FileStem(const std::string& path) {
    size_t s = path.find_last_of("\\/");
    std::string f = (s == std::string::npos) ? path : path.substr(s + 1);
    size_t d = f.find('.');
    return d == std::string::npos ? f : f.substr(0, d);
}
std::string DirName(const std::string& path) {
    size_t s = path.find_last_of("\\/");
    return s == std::string::npos ? std::string() : path.substr(0, s);
}
std::string Extension(const std::string& path) {
    size_t s = path.find_last_of("\\/");
    size_t d = path.find_last_of('.');
    if (d == std::string::npos || (s != std::string::npos && d < s)) return std::string();
    return Lower(path.substr(d));
}

std::string NativePath(const std::string& p) {
    std::string r = p;
#ifdef _WIN32
    for (char& c : r) if (c == '/') c = '\\';
#else
    for (char& c : r) if (c == '\\') c = '/';
#endif
    return r;
}
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return NativePath(b);
    std::string r = a;
    if (r.back() != '\\' && r.back() != '/') r += '/';
    std::string tail = b;
    while (!tail.empty() && (tail[0] == '\\' || tail[0] == '/')) tail.erase(0, 1);
    return NativePath(r + tail);
}

#ifdef _WIN32
bool FileExists(const std::string& path) {
    const DWORD a = GetFileAttributesW(Widen(NativePath(path)).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::string& path) {
    const DWORD a = GetFileAttributesW(Widen(NativePath(path)).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool MakeOneDir(const std::string& p) {
    return CreateDirectoryW(Widen(p).c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}
int64_t FileTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(Widen(NativePath(path)).c_str(), GetFileExInfoStandard, &d)) return 0;
    return (static_cast<int64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
}
static std::FILE* OpenFile(const std::string& path, const char* mode) {
    return _wfopen(Widen(NativePath(path)).c_str(), Widen(mode).c_str());
}
#else
bool FileExists(const std::string& path) {
    struct stat st;
    return stat(NativePath(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool DirExists(const std::string& path) {
    struct stat st;
    return stat(NativePath(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
static bool MakeOneDir(const std::string& p) { return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST; }
int64_t FileTime(const std::string& path) {
    struct stat st;
    if (stat(NativePath(path).c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
}
static std::FILE* OpenFile(const std::string& path, const char* mode) { return std::fopen(NativePath(path).c_str(), mode); }
#endif

bool RemoveFile(const std::string& path) {
#ifdef _WIN32
    return _wremove(Widen(NativePath(path)).c_str()) == 0;
#else
    return std::remove(NativePath(path).c_str()) == 0;
#endif
}

bool MakeDirs(const std::string& path) {
    const std::string p = NativePath(path);
    if (p.empty() || DirExists(p)) return true;
    for (size_t i = 1; i < p.size(); ++i) {
        if (p[i] == '\\' || p[i] == '/') {
            const std::string part = p.substr(0, i);
            if (part.size() == 2 && part[1] == ':') continue;   // "C:"
            if (!DirExists(part)) MakeOneDir(part);
        }
    }
    return MakeOneDir(p) || DirExists(p);
}

bool ReadFile(const std::string& path, std::string& out) {
    std::FILE* f = OpenFile(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}
bool WriteFile(const std::string& path, const std::string& data) {
    MakeDirs(DirName(NativePath(path)));
    std::FILE* f = OpenFile(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

bool ParseDouble(const char* b, const char* e, double& out, const char** stop) {
    while (b < e && (*b == ' ' || *b == '\t')) ++b;
    if (b < e && *b == '+') ++b;
    double v = 0.0;
    const auto r = std::from_chars(b, e, v);
    if (stop) *stop = r.ptr;
    if (r.ec != std::errc() || r.ptr == b) return false;
    out = v;
    return true;
}
bool ParseDouble(const std::string& s, double& out) { return ParseDouble(s.data(), s.data() + s.size(), out); }
std::string Num(double v, int significant) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, significant);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::string("0");
}

std::string RegexEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::strchr("\\.^$|?*+()[]{}#", c) != nullptr && c != 0) out += '\\';
        if (c == ' ') { out += "\\x20"; continue; }   // keeps the argument free of spaces
        out += c;
    }
    return out;
}

// ---- log ------------------------------------------------------------
namespace {
std::mutex g_logMutex;
std::FILE* g_log = nullptr;
std::string g_logPath;
}
void LogOpen(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_log) { std::fclose(g_log); g_log = nullptr; }
    g_logPath = path;
    if (!path.empty()) {
        MakeDirs(DirName(NativePath(path)));
        g_log = OpenFile(path, "wb");
    }
}
std::string LogPath() { return g_logPath; }
void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

} // namespace cp77
