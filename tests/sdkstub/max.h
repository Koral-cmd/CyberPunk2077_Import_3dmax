// ============================================================
//  Compile-check stand-in for the 3ds Max SDK (2022+ API shape).
//  Declares ONLY what the plugin uses, with the SDK's signatures,
//  so typos, wrong types and wrong argument counts show up on any
//  machine. It proves nothing about runtime behaviour - the real
//  build against the Autodesk SDK is the actual test.
// ============================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

typedef int BOOL;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* LPVOID;
typedef wchar_t WCHAR;
typedef wchar_t TCHAR;
typedef wchar_t MCHAR;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define DLL_PROCESS_ATTACH 1
#define MB_ICONERROR 0x10
#define MB_ICONINFORMATION 0x40
#define _T(x) L##x
#ifndef __declspec
#define __declspec(x)
#endif
inline int MessageBox(HWND, const MCHAR*, const MCHAR*, UINT) { return 0; }
inline BOOL DisableThreadLibraryCalls(HINSTANCE) { return TRUE; }

class MSTR {
public:
    MSTR() {}
    MSTR(const wchar_t* s) : s_(s ? s : L"") {}
    const wchar_t* data() const { return s_.c_str(); }
    int Length() const { return static_cast<int>(s_.size()); }
private:
    std::wstring s_;
};
typedef MSTR TSTR;

class Point3 { public: float x = 0, y = 0, z = 0; Point3() {} Point3(float a, float b, float c) : x(a), y(b), z(c) {} };
class Color { public: float r = 0, g = 0, b = 0; Color() {} Color(float x, float y, float z) : r(x), g(y), b(z) {} };
class Matrix3 {
public:
    Matrix3() { IdentityMatrix(); }
    void IdentityMatrix() { for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) m[r][c] = (r == c) ? 1.0f : 0.0f; }
    void SetRow(int i, Point3 p) { m[i][0] = p.x; m[i][1] = p.y; m[i][2] = p.z; }
    Point3 GetRow(int i) const { return Point3(m[i][0], m[i][1], m[i][2]); }
private:
    float m[4][3];
};
class Quat {
public:
    float x = 0, y = 0, z = 0, w = 1;
    Quat() {}
    Quat(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    explicit Quat(const Matrix3&) {}
};

typedef int TimeValue;
#define TIME_TICKSPERSEC 4800
class Interval { public: Interval() {} Interval(TimeValue s, TimeValue e) : s_(s), e_(e) {} TimeValue Start() const { return s_; } TimeValue End() const { return e_; } private: TimeValue s_ = 0, e_ = 0; };
inline int GetFrameRate() { return 30; }
inline void SetFrameRate(int) {}
inline int GetTicksPerFrame() { return 160; }

class Class_ID { public: Class_ID() {} Class_ID(ULONG a, ULONG b) : a_(a), b_(b) {} private: ULONG a_ = 0, b_ = 0; };
typedef ULONG SClass_ID;
#define GEOMOBJECT_CLASS_ID ((SClass_ID)0x10)
#define HELPER_CLASS_ID ((SClass_ID)0xFC0)
#define OSM_CLASS_ID ((SClass_ID)0x810)
#define SCENE_IMPORT_CLASS_ID ((SClass_ID)0xA00)
#define CTRL_POSITION_CLASS_ID ((SClass_ID)0x9004)
#define CTRL_ROTATION_CLASS_ID ((SClass_ID)0x9005)
#define LININTERP_POSITION_CLASS_ID 0x2002
#define LININTERP_ROTATION_CLASS_ID 0x2003
#define POINTHELP_CLASS_ID 0x00002013
#define BONE_OBJ_CLASSID Class_ID(0x28bf6e8d, 0x2ecca840)
#define VERSION_3DSMAX ((ULONG)27000)
#ifndef MAX_RELEASE
#define MAX_RELEASE 27000
#endif
#define UNITS_METERS 5
inline double GetSystemUnitScale(int) { return 0.0254; }

typedef short BlockID;
typedef short ParamID;
class Texmap;
class ParamBlockDesc2 { public: int NameToIndex(const MCHAR*) { return -1; } };
class IParamBlock2 {
public:
    BOOL SetValue(ParamID, TimeValue, float, int = 0) { return TRUE; }
    BOOL SetValue(ParamID, TimeValue, Texmap*, int = 0) { return TRUE; }
    ParamBlockDesc2* GetDesc() { return nullptr; }
    ParamID IndextoID(int) { return 0; }
};

template <class T> class Tab {
public:
    int Append(int n, T* el, int = 0) { for (int i = 0; i < n; ++i) v_.push_back(el[i]); return static_cast<int>(v_.size()) - n; }
    int Count() const { return static_cast<int>(v_.size()); }
    T& operator[](int i) { return v_[static_cast<size_t>(i)]; }
private:
    std::vector<T> v_;
};

class Animatable {
public:
    virtual ~Animatable() {}
    virtual void* GetInterface(ULONG) { return nullptr; }
    virtual IParamBlock2* GetParamBlockByID(BlockID) { return nullptr; }
    virtual int NumParamBlocks() { return 0; }
    virtual IParamBlock2* GetParamBlock(int) { return nullptr; }
};
class ReferenceTarget : public Animatable {};
class BaseObject : public ReferenceTarget { public: using ReferenceTarget::GetParamBlock; IParamBlock2* GetParamBlock() { return nullptr; } };
class Object : public BaseObject {};

#include "control.h"

class Texmap;
#define MTL_TEX_DISPLAY_ENABLED (1 << 3)
#define MTL_HW_MAT_ENABLED (1 << 30)
#define TEXMAP_CLASS_ID ((SClass_ID)0xc10)
#define MATERIAL_CLASS_ID ((SClass_ID)0xc00)
class MtlBase : public ReferenceTarget {
public:
    void SetName(const MSTR&) {}
    void SetMtlFlag(int, BOOL = TRUE) {}
    virtual void SetSubTexmap(int, Texmap*) {}
};
class Mtl : public MtlBase {};
class Texmap : public MtlBase {};
class NoteTrack;
struct ObjectState { Object* obj = nullptr; };

class INode : public ReferenceTarget {
public:
    void SetName(const MCHAR*) {}
    const MCHAR* GetName() { return L""; }
    void SetNodeTM(TimeValue, const Matrix3&) {}
    Matrix3 GetNodeTM(TimeValue, Interval* = nullptr) { return Matrix3(); }
    void AttachChild(INode*, int keepTM = 1) { (void)keepTM; }
    INode* GetParentNode() { return nullptr; }
    int IsRootNode() { return 0; }
    void ShowBone(int) {}
    void SetBoneNodeOnOff(BOOL, TimeValue) {}
    void SetBoneAutoAlign(BOOL) {}
    void SetBoneFreezeLen(BOOL) {}
    void SetRenderable(BOOL) {}
    Object* GetObjectRef() { return nullptr; }
    void SetMtl(Mtl*) {}
    const ObjectState& EvalWorldState(TimeValue, BOOL evalHidden = TRUE) { (void)evalHidden; static ObjectState s; return s; }
    Control* GetTMController() { return nullptr; }
    ULONG GetHandle() { return 0; }
    int NumNoteTracks() { return 0; }
    NoteTrack* GetNoteTrack(int) { return nullptr; }
    void DeleteNoteTrack(NoteTrack*, BOOL delNote = TRUE) { (void)delNote; }
    void AddNoteTrack(NoteTrack*) {}
    void SetUserPropString(const MSTR&, const MSTR&) {}
    BOOL GetUserPropString(const MSTR&, MSTR&) { return FALSE; }
    int NumberOfChildren() { return 0; }
    INode* GetChildNode(int) { return nullptr; }
};

class Face { public: DWORD v[3]; void setVerts(int, int, int) {} void setEdgeVisFlags(int, int, int) {} void setSmGroup(DWORD) {} void setMatID(unsigned short) {} };
class TVFace { public: DWORD t[3]; void setTVerts(int, int, int) {} };
class Mesh {
public:
    Face* faces = nullptr;
    TVFace* tvFace = nullptr;
    BOOL setNumVerts(int, BOOL = FALSE, BOOL = FALSE) { return TRUE; }
    BOOL setNumFaces(int, BOOL = FALSE, BOOL = FALSE) { return TRUE; }
    void setVert(int, float, float, float) {}
    BOOL setNumTVerts(int, BOOL = FALSE, BOOL = FALSE) { return TRUE; }
    void setTVert(int, float, float, float) {}
    BOOL setNumTVFaces(int, BOOL = FALSE, int = 0) { return TRUE; }
    void setMapSupport(int, BOOL = TRUE) {}
    void setNumMapVerts(int, int, BOOL = FALSE) {}
    void setMapVert(int, int, const Point3&) {}
    void setNumMapFaces(int, int, BOOL = FALSE, int = 0) {}
    TVFace* mapFaces(int) { return nullptr; }
    void InvalidateGeomCache() {}
    void InvalidateTopologyCache() {}
    void buildNormals() {}
};
class TriObject : public Object { public: Mesh& GetMesh() { return mesh_; } private: Mesh mesh_; };
inline TriObject* CreateNewTriObject() { return nullptr; }

class Modifier : public BaseObject {};
class ImpInterface { public: virtual ~ImpInterface() {} };

class Interface {
public:
    virtual ~Interface() {}
    void* CreateInstance(SClass_ID, Class_ID) { return nullptr; }
    INode* CreateObjectNode(Object*) { return nullptr; }
    INode* GetRootNode() { return nullptr; }
    HWND GetMAXHWnd() { return nullptr; }
    TimeValue GetTime() { return 0; }
    void RedrawViews(TimeValue, DWORD = 0, ReferenceTarget* = nullptr) {}
    INode* GetINodeByHandle(ULONG) { return nullptr; }
    void SetAnimRange(Interval) {}
    void SetTime(TimeValue, BOOL = TRUE) {}
    void DisableSceneRedraw() {}
    void EnableSceneRedraw() {}
    void ActivateTexture(MtlBase*, Mtl*, int = -1) {}
    void PutMtlToMtlEditor(MtlBase*, int = -1) {}
};
inline Interface* GetCOREInterface() { return nullptr; }
class Interface7 : public Interface {
public:
    enum ResCode { kRES_INTERNAL_ERROR = -3, kRES_MOD_NOT_FOUND = -2, kRES_MOD_NOT_APPLICABLE = -1, kRES_SUCCESS = 0 };
    ResCode AddModifier(INode&, Modifier&, int = 0) { return kRES_SUCCESS; }
};
inline Interface7* GetCOREInterface7() { return nullptr; }

inline void SuspendAnimate() {}
inline void ResumeAnimate() {}
inline void AnimateOn() {}
inline void AnimateOff() {}
struct HoldStub { void Suspend() {} void Resume() {} };
extern HoldStub theHold;

class ClassDesc { public: virtual ~ClassDesc() {} };
class ClassDesc2 : public ClassDesc {
public:
    virtual int IsPublic() = 0;
    virtual void* Create(BOOL loading = FALSE) = 0;
    virtual const MCHAR* ClassName() = 0;
    virtual const MCHAR* NonLocalizedClassName() = 0;
    virtual SClass_ID SuperClassID() = 0;
    virtual Class_ID ClassID() = 0;
    virtual const MCHAR* Category() = 0;
    virtual const MCHAR* InternalName() { return L""; }
    virtual HINSTANCE HInstance() { return nullptr; }
};
class SceneImport {
public:
    virtual ~SceneImport() {}
    virtual int ExtCount() = 0;
    virtual const MCHAR* Ext(int n) = 0;
    virtual const MCHAR* LongDesc() = 0;
    virtual const MCHAR* ShortDesc() = 0;
    virtual const MCHAR* AuthorName() = 0;
    virtual const MCHAR* CopyrightMessage() = 0;
    virtual const MCHAR* OtherMessage1() = 0;
    virtual const MCHAR* OtherMessage2() = 0;
    virtual unsigned int Version() = 0;
    virtual void ShowAbout(HWND hWnd) = 0;
    virtual int DoImport(const MCHAR* name, ImpInterface* ii, Interface* i, BOOL suppressPrompts = FALSE) = 0;
};
