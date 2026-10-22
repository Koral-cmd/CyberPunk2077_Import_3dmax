// ============================================================
//  DLL entry, File -> Import class and the MAXScript interface.
//
//  The exports are not extern "C"; CP2077Import.def gives them
//  their plain names (Max looks those up).
// ============================================================
#include "plugin.h"

#include <iFnPub.h>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// ------------------------------------------------------------
//  File -> Import: a WolvenKit JSON of an entity (.ent.json).
// ------------------------------------------------------------
class CP77SceneImport : public SceneImport {
public:
    int ExtCount() override { return 1; }
    const MCHAR* Ext(int) override { return _T("json"); }
    const MCHAR* LongDesc() override { return _T("Cyberpunk 2077 entity, WolvenKit JSON (.ent.json)"); }
    const MCHAR* ShortDesc() override { return _T("Cyberpunk 2077 entity (WolvenKit)"); }
    const MCHAR* AuthorName() override { return _T("CP2077 Import"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override { return _T(""); }
    const MCHAR* OtherMessage2() override { return _T(""); }
    unsigned int Version() override { return CP77IMPORT_VERSION; }
    void ShowAbout(HWND hWnd) override {
        MessageBox(hWnd, _T("CP2077 Import ") CP77IMPORT_VERSION_STR _T("\n\nCharacters, weapons and animations from Cyberpunk 2077 via WolvenKit.\n")
                         _T("Menu: CP2077 -> Import / Animations."), _T("CP2077 Import"), MB_ICONINFORMATION);
    }
    int DoImport(const MCHAR* name, ImpInterface*, Interface*, BOOL suppressPrompts) override {
        return cp77max::ImportFromFile(name, suppressPrompts);
    }
};

class CP77SceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    // new on every call: Max deletes what Create returns (a static object here corrupts the heap)
    void* Create(BOOL) override { return new CP77SceneImport(); }
    const MCHAR* ClassName() override { return _T("CP2077 Import"); }
    const MCHAR* NonLocalizedClassName() override { return _T("CP2077 Import"); }
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID ClassID() override { return CP77IMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("CP2077SceneImport"); }
    HINSTANCE HInstance() override { return hInstance; }
};

static CP77SceneImportClassDesc theSceneImportDesc;

// ------------------------------------------------------------
//  MAXScript: CP77Cpp.<function>
//  The enum, FUNCTION_MAP and the descriptor below must match.
//  Strings come back through ret_ - MAXScript copies them at once.
// ------------------------------------------------------------
class CP77FP : public FPStaticInterface {
public:
    enum {
        fn_version, fn_getSetting, fn_setSetting, fn_settingsPath, fn_logPath,
        fn_buildIndex, fn_indexCount, fn_search, fn_appearances,
        fn_importCharacter, fn_importJson,
        fn_listRigs, fn_clips, fn_applyClip, fn_applyAll, fn_resetPose, fn_listLoaded, fn_deleteLoaded
    };

    const MCHAR* version() { return keep(cp77max::Version()); }
    const MCHAR* getSetting(const MCHAR* k) { return keep(cp77max::GetSetting(s(k))); }
    BOOL setSetting(const MCHAR* k, const MCHAR* v) { return cp77max::SetSetting(s(k), s(v)) ? TRUE : FALSE; }
    const MCHAR* settingsPath() { return keep(cp77max::SettingsPath()); }
    const MCHAR* logPath() { return keep(cp77max::LogPath()); }
    const MCHAR* buildIndex() { return keep(cp77max::BuildIndex()); }
    int indexCount() { return cp77max::IndexCount(); }
    const MCHAR* search(const MCHAR* kind, const MCHAR* filter, int max) { return keep(cp77max::Search(s(kind), s(filter), max)); }
    const MCHAR* appearances(const MCHAR* ent) { return keep(cp77max::Appearances(s(ent))); }
    const MCHAR* importCharacter(const MCHAR* ent, const MCHAR* app, const MCHAR* wEnt, const MCHAR* wApp, const MCHAR* slot) {
        return keep(cp77max::ImportCharacter(s(ent), s(app), s(wEnt), s(wApp), s(slot)));
    }
    const MCHAR* importJson(const MCHAR* p) { return keep(cp77max::ImportJson(s(p))); }
    const MCHAR* listRigs() { return keep(cp77max::ListRigs()); }
    const MCHAR* clips(const MCHAR* a) { return keep(cp77max::Clips(s(a))); }
    const MCHAR* applyClip(const MCHAR* a, const MCHAR* c, int rig) { return keep(cp77max::ApplyClip(s(a), s(c), rig)); }
    const MCHAR* applyAll(const MCHAR* a, const MCHAR* f, int rig) { return keep(cp77max::ApplyAll(s(a), s(f), rig)); }
    const MCHAR* resetPose(int rig) { return keep(cp77max::ResetPose(rig)); }
    const MCHAR* listLoaded(int rig) { return keep(cp77max::ListLoaded(rig)); }
    const MCHAR* deleteLoaded(int rig, int index) { return keep(cp77max::DeleteLoaded(rig, index)); }

    DECLARE_DESCRIPTOR(CP77FP)
    BEGIN_FUNCTION_MAP
        FN_0(fn_version, TYPE_STRING, version)
        FN_1(fn_getSetting, TYPE_STRING, getSetting, TYPE_STRING)
        FN_2(fn_setSetting, TYPE_BOOL, setSetting, TYPE_STRING, TYPE_STRING)
        FN_0(fn_settingsPath, TYPE_STRING, settingsPath)
        FN_0(fn_logPath, TYPE_STRING, logPath)
        FN_0(fn_buildIndex, TYPE_STRING, buildIndex)
        FN_0(fn_indexCount, TYPE_INT, indexCount)
        FN_3(fn_search, TYPE_STRING, search, TYPE_STRING, TYPE_STRING, TYPE_INT)
        FN_1(fn_appearances, TYPE_STRING, appearances, TYPE_STRING)
        FN_5(fn_importCharacter, TYPE_STRING, importCharacter, TYPE_STRING, TYPE_STRING, TYPE_STRING, TYPE_STRING, TYPE_STRING)
        FN_1(fn_importJson, TYPE_STRING, importJson, TYPE_STRING)
        FN_0(fn_listRigs, TYPE_STRING, listRigs)
        FN_1(fn_clips, TYPE_STRING, clips, TYPE_STRING)
        FN_3(fn_applyClip, TYPE_STRING, applyClip, TYPE_STRING, TYPE_STRING, TYPE_INT)
        FN_3(fn_applyAll, TYPE_STRING, applyAll, TYPE_STRING, TYPE_STRING, TYPE_INT)
        FN_1(fn_resetPose, TYPE_STRING, resetPose, TYPE_INT)
        FN_1(fn_listLoaded, TYPE_STRING, listLoaded, TYPE_INT)
        FN_2(fn_deleteLoaded, TYPE_STRING, deleteLoaded, TYPE_INT, TYPE_INT)
    END_FUNCTION_MAP

private:
    std::wstring ret_;
    const MCHAR* keep(const std::wstring& v) { ret_ = v; return ret_.c_str(); }
    static std::wstring s(const MCHAR* p) { return p ? std::wstring(p) : std::wstring(); }
};

static CP77FP theCP77FP(
    CP77IMPORT_FP_ID, _T("CP77Cpp"), 0, &theSceneImportDesc, FP_CORE,
    CP77FP::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    CP77FP::fn_getSetting, _T("getSetting"), 0, TYPE_STRING, 0, 1,
        _T("key"), 0, TYPE_STRING,
    CP77FP::fn_setSetting, _T("setSetting"), 0, TYPE_BOOL, 0, 2,
        _T("key"), 0, TYPE_STRING,
        _T("value"), 0, TYPE_STRING,
    CP77FP::fn_settingsPath, _T("settingsPath"), 0, TYPE_STRING, 0, 0,
    CP77FP::fn_logPath, _T("logPath"), 0, TYPE_STRING, 0, 0,
    CP77FP::fn_buildIndex, _T("buildIndex"), 0, TYPE_STRING, 0, 0,
    CP77FP::fn_indexCount, _T("indexCount"), 0, TYPE_INT, 0, 0,
    CP77FP::fn_search, _T("search"), 0, TYPE_STRING, 0, 3,
        _T("kind"), 0, TYPE_STRING,
        _T("filter"), 0, TYPE_STRING,
        _T("maxResults"), 0, TYPE_INT,
    CP77FP::fn_appearances, _T("appearances"), 0, TYPE_STRING, 0, 1,
        _T("entity"), 0, TYPE_STRING,
    CP77FP::fn_importCharacter, _T("importCharacter"), 0, TYPE_STRING, 0, 5,
        _T("entity"), 0, TYPE_STRING,
        _T("appearance"), 0, TYPE_STRING,
        _T("weaponEntity"), 0, TYPE_STRING,
        _T("weaponAppearance"), 0, TYPE_STRING,
        _T("slot"), 0, TYPE_STRING,
    CP77FP::fn_importJson, _T("importJson"), 0, TYPE_STRING, 0, 1,
        _T("path"), 0, TYPE_STRING,
    CP77FP::fn_listRigs, _T("listRigs"), 0, TYPE_STRING, 0, 0,
    CP77FP::fn_clips, _T("clips"), 0, TYPE_STRING, 0, 1,
        _T("animset"), 0, TYPE_STRING,
    CP77FP::fn_applyClip, _T("applyClip"), 0, TYPE_STRING, 0, 3,
        _T("animset"), 0, TYPE_STRING,
        _T("clip"), 0, TYPE_STRING,
        _T("rig"), 0, TYPE_INT,
    CP77FP::fn_applyAll, _T("applyAll"), 0, TYPE_STRING, 0, 3,
        _T("animset"), 0, TYPE_STRING,
        _T("filter"), 0, TYPE_STRING,
        _T("rig"), 0, TYPE_INT,
    CP77FP::fn_resetPose, _T("resetPose"), 0, TYPE_STRING, 0, 1,
        _T("rig"), 0, TYPE_INT,
    CP77FP::fn_listLoaded, _T("listLoaded"), 0, TYPE_STRING, 0, 1,
        _T("rig"), 0, TYPE_INT,
    CP77FP::fn_deleteLoaded, _T("deleteLoaded"), 0, TYPE_STRING, 0, 2,
        _T("rig"), 0, TYPE_INT,
        _T("index"), 0, TYPE_INT,
    p_end);

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("CP2077 Import ") CP77IMPORT_VERSION_STR _T(" - Cyberpunk 2077 characters, weapons and animations");
}
__declspec(dllexport) int LibNumberClasses() { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) { return i == 0 ? &theSceneImportDesc : nullptr; }
__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }
__declspec(dllexport) int CanAutoDefer() { return FALSE; }

// A core interface registers itself through its descriptor; if that did
// not happen (seen with other plugins), register it here.
__declspec(dllexport) int LibInitialize() {
    static bool done = false;
    if (done) return TRUE;
    done = true;
    if (GetCOREInterface(CP77IMPORT_FP_ID) == nullptr) RegisterCOREInterface(&theCP77FP);
    return TRUE;
}
