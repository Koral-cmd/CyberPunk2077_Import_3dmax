// Shared between the plugin's .cpp files - not part of the MAXScript surface.
#pragma once
#include "plugin.h"

#include "../core/assembler.h"
#include "../core/prep.h"
#include "../core/settings.h"
#include "../core/util.h"
#include "../core/wolvenkit.h"

#include <string>
#include <vector>

namespace cp77max {

cp77::Settings& Config();                    // loaded on first use
cp77::FileStore& Store();                    // cache + extra roots
const cp77::WolvenKit* Cli();                // nullptr if not configured
double UnitsPerMeter();                      // setting, else from the system unit
void StartLog(const char* what);             // new log file for this operation

inline MSTR M(const std::string& utf8) { return MSTR(cp77::Widen(utf8).c_str()); }
inline std::string U(const MCHAR* w) { return w ? cp77::Narrow(std::wstring(w)) : std::string(); }
inline std::wstring W(const std::string& utf8) { return cp77::Widen(utf8); }

Matrix3 ToMax(const cp77::Xf& x);
cp77::Xf FromMax(const Matrix3& m);

// User properties written on import, read by the animation side.
#define CP77_PROP_SKEL     _T("cp77_skel")      // skeleton id, same on helper and bones
#define CP77_PROP_HELPER   _T("cp77_helper")    // 1 on the skeleton's root helper
#define CP77_PROP_ENTITY   _T("cp77_entity")
#define CP77_PROP_OWNER    _T("cp77_owner")
#define CP77_PROP_RIGS     _T("cp77_rigs")
#define CP77_PROP_ANIMSETS _T("cp77_animsets")
#define CP77_PROP_REST     _T("cp77_rest")      // 12 numbers, local rest pose in Max units
#define CP77_PROP_SOURCE   _T("cp77_mesh")

// Builds the scene from a plan; returns the report.
std::wstring BuildScene(const cp77::ScenePlan& plan);

} // namespace cp77max
