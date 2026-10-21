#pragma once
#include "max.h"
#include "IColorPipelineMgr.h"
#define BMM_CUSTOM_GAMMA (1 << 2)
class BitmapInfo {
public:
    void SetName(const MCHAR*) {}
    void SetCustomFlag(DWORD) {}
    void SetCustomGamma(float) {}
#if MAX_RELEASE >= 26000
    MaxSDK::ColorManagement::ColSpaceStatus SetRequestedColorSpace(const MSTR&, MaxSDK::ColorManagement::ColSpaceSource) { return MaxSDK::ColorManagement::ColSpaceStatus::Normal; }
#endif
};
