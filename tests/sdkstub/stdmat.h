#pragma once
#include "max.h"
class StdMat : public Mtl { public: virtual void SetDiffuse(Color, TimeValue) {} };
class StdMat2 : public StdMat {};
class MultiMtl : public Mtl {
public:
    virtual void SetNumSubMtls(int) {}
    virtual void SetSubMtlAndName(int, Mtl*, MSTR&) {}
};
inline StdMat2* NewDefaultStdMat() { return nullptr; }
inline MultiMtl* NewDefaultMultiMtl() { return nullptr; }
