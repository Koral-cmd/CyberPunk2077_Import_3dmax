#pragma once
#include "max.h"
#include "bitmap.h"
#define ID_DI 1
#define ID_OP 6
#define ID_BU 8
#define ALPHA_FILE 0
#define ALPHA_NONE 3
class StdMat : public Mtl {
public:
    virtual void SetDiffuse(Color, TimeValue) {}
    virtual void EnableMap(int, BOOL) {}
    virtual void SetTexmapAmt(int, float, TimeValue) {}
};
class BitmapTex : public Texmap {
public:
    virtual void SetMapName(const MCHAR*, bool = false) {}
    virtual void SetBitmapInfo(const BitmapInfo&) {}
    virtual void SetAlphaSource(int) {}
    virtual void SetAlphaAsMono(BOOL) {}
};
inline BitmapTex* NewDefaultBitmapTex() { return nullptr; }
class StdMat2 : public StdMat { public: long StdIDToChannel(long id) { return id; } };
class MultiMtl : public Mtl {
public:
    virtual void SetNumSubMtls(int) {}
    virtual void SetSubMtlAndName(int, Mtl*, MSTR&) {}
};
inline StdMat2* NewDefaultStdMat() { return nullptr; }
inline MultiMtl* NewDefaultMultiMtl() { return nullptr; }
