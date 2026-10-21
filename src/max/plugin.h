// ============================================================
//  CP2077 Import - 3ds Max plugin (2022 and newer)
//
//  The plugin is a thin layer: src/core does the reading,
//  assembling and maths (tested without Max), this layer creates
//  nodes, copies numbers and drives the Skin modifier.
//
//  MAXScript interface "CP77Cpp" (see plugin_dll.cpp); the
//  windows are MAXScript rollouts in scripts/CP2077Import.mcr.
// ============================================================
#pragma once

#include <max.h>
#include <iparamb2.h>
#include <impexp.h>
#include <istdplug.h>

#include <string>

#define CP77IMPORT_VERSION      100
#define CP77IMPORT_VERSION_STR  _T("1.0.0")

// Drawn once for this plugin; never change them (scripts and saved scenes refer to them).
#define CP77IMPORT_SCENE_CLASS_ID Class_ID(0x4c7a19e3, 0x2b61d0f7)
#define CP77IMPORT_FP_ID          Interface_ID(0x6f13a2c9, 0x51e04b88)

extern HINSTANCE hInstance;

namespace cp77max {

// ---- MAXScript entry points (all return a report or a list as text) ----
std::wstring Version();
std::wstring GetSetting(const std::wstring& key);
bool SetSetting(const std::wstring& key, const std::wstring& value);
std::wstring SettingsPath();
std::wstring LogPath();

std::wstring BuildIndex();                       // runs WolvenKit "archive --list"
int IndexCount();
std::wstring Search(const std::wstring& kind, const std::wstring& filter, int max);
std::wstring Appearances(const std::wstring& ent);   // "default\t<name>" then one name per line

std::wstring ImportCharacter(const std::wstring& ent, const std::wstring& app,
                             const std::wstring& weaponEnt, const std::wstring& weaponApp,
                             const std::wstring& slot);
std::wstring ImportJson(const std::wstring& entJsonPath);     // straight from a WolvenKit project

std::wstring ListRigs();                         // "handle\tname\towner\tentity\tanimset;animset"
std::wstring Clips(const std::wstring& anims);   // "name\tseconds" per line, or "ERROR\t..."
std::wstring ApplyClip(const std::wstring& anims, const std::wstring& clip, int rigHandle);
std::wstring ApplyAll(const std::wstring& anims, const std::wstring& filter, int rigHandle);
std::wstring ResetPose(int rigHandle);

// Used by File -> Import (.ent.json)
int ImportFromFile(const MCHAR* path, BOOL suppressPrompts);

} // namespace cp77max
