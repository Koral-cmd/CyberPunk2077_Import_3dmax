// ============================================================
//  Settings - a plain INI file, read and written without the
//  Windows profile API (MaxSDK::Util::GetPrivateProfileString
//  clashes with it since Max 2019).
//
//  Default location: %LOCALAPPDATA%\CP2077Import\settings.ini
// ============================================================
#pragma once
#include <map>
#include <string>
#include <vector>

namespace cp77 {

class Settings {
public:
    // keys used by the plugin (section [CP2077Import])
    static constexpr const char* GamePath       = "GamePath";        // Cyberpunk 2077 folder
    static constexpr const char* WolvenKitCli   = "WolvenKitCli";    // WolvenKit.CLI.exe / cp77tools.exe
    static constexpr const char* CacheDir       = "CacheDir";        // extracted files
    static constexpr const char* ExtraArchives  = "ExtraArchives";   // ';'-separated folders/.archive files (mods)
    static constexpr const char* UnitsPerMeter  = "UnitsPerMeter";   // 0 = from the Max system unit
    static constexpr const char* RestPose       = "RestPose";        // APose | Reference
    static constexpr const char* SkinBind       = "SkinBind";        // Rest (default) | Mesh: pose the Skin is bound in
    static constexpr const char* BoneSize       = "BoneSize";        // metres
    static constexpr const char* LodFilter      = "LodFilter";       // 1 = only LOD 1
    static constexpr const char* WeldVertices   = "WeldVertices";    // 1 = weld UV seams for smooth shading
    static constexpr const char* WeaponSlot     = "WeaponSlot";      // default slot for weapons
    static constexpr const char* GapFrames      = "GapFrames";       // between clips in "load all"
    static constexpr const char* FrameRate      = "FrameRate";       // 30
    static constexpr const char* ExtractTimeout = "ExtractTimeout";  // seconds per WolvenKit call

    bool load(const std::string& path);
    bool save() const;
    std::string path() const { return path_; }

    std::string get(const std::string& key, const std::string& def = std::string()) const;
    double getDouble(const std::string& key, double def) const;
    int getInt(const std::string& key, int def) const;
    bool getBool(const std::string& key, bool def) const;
    void set(const std::string& key, const std::string& value);
    std::vector<std::string> keys() const;

    // Folders with .archive files: <game>\archive\pc\content, ...\ep1, plus ExtraArchives.
    std::vector<std::string> archiveFolders() const;
    std::string cacheDir() const;     // resolved default if empty

    static std::string DefaultDir();  // %LOCALAPPDATA%\CP2077Import (or ~/.cp2077import)
private:
    std::string path_;
    std::map<std::string, std::string> values_;
};

} // namespace cp77
