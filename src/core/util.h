// ============================================================
//  CP2077 Import - small helpers shared by the core and the plugin.
//  Everything in the core is UTF-8 std::string; the plugin layer
//  converts to wide strings only at the 3ds Max API boundary.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cp77 {

std::wstring Widen(const std::string& utf8);
std::string  Narrow(const std::wstring& wide);

std::string Lower(std::string s);
bool IEquals(const std::string& a, const std::string& b);
bool StartsWith(const std::string& s, const std::string& p);
bool EndsWith(const std::string& s, const std::string& p);
bool IEndsWith(const std::string& s, const std::string& p);
std::string Trim(const std::string& s);
std::vector<std::string> Split(const std::string& s, char sep);
std::string Join(const std::vector<std::string>& v, const std::string& sep);
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to);
std::string Format(const char* fmt, ...);

// Depot paths are stored the way the game writes them: backslashes,
// lower case is NOT forced (the archive keeps the original case).
std::string NormalizeDepotPath(std::string p);      // '/' -> '\', trims
std::string DepotKey(const std::string& p);           // normalized + lower case, for maps
std::string FileStem(const std::string& path);        // "a\b\c.mesh" -> "c"
std::string DirName(const std::string& path);         // "a\b\c.mesh" -> "a\b"
std::string Extension(const std::string& path);       // ".mesh" (lower case)

// File system (UTF-8 paths, both separators accepted).
std::string JoinPath(const std::string& a, const std::string& b);
std::string NativePath(const std::string& p);         // separators for this OS
bool FileExists(const std::string& path);
bool DirExists(const std::string& path);
bool MakeDirs(const std::string& path);
bool ReadFile(const std::string& path, std::string& out);
bool WriteFile(const std::string& path, const std::string& data);
bool RemoveFile(const std::string& path);
int64_t FileTime(const std::string& path);            // 0 if missing

// Numbers independent of the C locale. 3ds Max switches the process to the user's
// locale; with a Russian locale strtod("0.6996") stops at the '.' and returns 0 -
// every bone transform then became an integer (Adam Smasher's skeleton exploded).
bool ParseDouble(const char* begin, const char* end, double& out, const char** stop = nullptr);
bool ParseDouble(const std::string& s, double& out);
std::string Num(double v, int significant = 9);     // always '.' as decimal point

// .NET regex literal for an exact path: escapes every metacharacter.
std::string RegexEscape(const std::string& s);

// ------------------------------------------------------------
//  Log - one file per session (settings decide where).
// ------------------------------------------------------------
void LogOpen(const std::string& path);
void Log(const char* fmt, ...);
std::string LogPath();

} // namespace cp77
