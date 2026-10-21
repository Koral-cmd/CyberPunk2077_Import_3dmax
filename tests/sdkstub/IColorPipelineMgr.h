#pragma once
#include "max.h"
namespace MaxSDK { namespace ColorManagement {
enum class ColSpaceSource { NotSet = 0, InputRules, User, SystemHeuristics };
enum class ColSpaceStatus { NotSet = 0, Normal, ForcedDefault, Invalid };
class IModeSettings { public: virtual ~IModeSettings() {} virtual MSTR GetDataColorSpaceName() const { return MSTR(L"Raw"); } };
class IColorPipelineMgr { public: static IColorPipelineMgr* GetInstance() { return nullptr; } IModeSettings* Settings() const { return nullptr; } };
} }
