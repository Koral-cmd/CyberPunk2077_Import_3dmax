#pragma once
#include "max.h"
class Interface_ID { public: Interface_ID(ULONG a = 0, ULONG b = 0) : a_(a), b_(b) {} private: ULONG a_, b_; };
typedef int StringResID;
class FPInterface { public: virtual ~FPInterface() {} };
class FPInterfaceDesc : public FPInterface {};
class FPStaticInterface : public FPInterfaceDesc {};
#define FP_CORE 0x0004
enum ParamType2 { TYPE_FLOAT = 0, TYPE_INT = 1, TYPE_BOOL = 4, TYPE_STRING = 8 };
enum { p_end = -1 };
// every function in FUNCTION_MAP is called with arguments of the declared types
#define TYPE_STRING_STUB_ARG static_cast<const MCHAR*>(nullptr)
#define TYPE_INT_STUB_ARG 0
#define TYPE_BOOL_STUB_ARG FALSE
#define TYPE_FLOAT_STUB_ARG 0.0f
#define TYPE_STRING_STUB_RET const MCHAR*
#define TYPE_INT_STUB_RET int
#define TYPE_BOOL_STUB_RET BOOL
#define DECLARE_DESCRIPTOR(cls) public: cls(Interface_ID, const MCHAR*, StringResID, ClassDesc*, unsigned short, ...) {}
#define BEGIN_FUNCTION_MAP public: int StubDispatch(int fid) { switch (fid) {
#define FN_0(id, r, f) case id: { r##_STUB_RET v = f(); (void)v; return 0; }
#define FN_1(id, r, f, a) case id: { r##_STUB_RET v = f(a##_STUB_ARG); (void)v; return 0; }
#define FN_2(id, r, f, a, b) case id: { r##_STUB_RET v = f(a##_STUB_ARG, b##_STUB_ARG); (void)v; return 0; }
#define FN_3(id, r, f, a, b, c) case id: { r##_STUB_RET v = f(a##_STUB_ARG, b##_STUB_ARG, c##_STUB_ARG); (void)v; return 0; }
#define FN_4(id, r, f, a, b, c, d) case id: { r##_STUB_RET v = f(a##_STUB_ARG, b##_STUB_ARG, c##_STUB_ARG, d##_STUB_ARG); (void)v; return 0; }
#define FN_5(id, r, f, a, b, c, d, e) case id: { r##_STUB_RET v = f(a##_STUB_ARG, b##_STUB_ARG, c##_STUB_ARG, d##_STUB_ARG, e##_STUB_ARG); (void)v; return 0; }
#define END_FUNCTION_MAP } return -1; }
inline FPInterface* GetCOREInterface(Interface_ID) { return nullptr; }
inline void RegisterCOREInterface(FPInterface*) {}
