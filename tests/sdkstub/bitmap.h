#pragma once
#include "max.h"
#define BMM_CUSTOM_GAMMA (1 << 2)
class BitmapInfo {
public:
    void SetName(const MCHAR*) {}
    void SetCustomFlag(DWORD) {}
    void SetCustomGamma(float) {}
};
