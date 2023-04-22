/*
    RBVREnhanced - dllmain.cpp
    The DLL entrypoint for the mod. Applies initialisation and hooks.
*/

#include "pch.h"
#include <cstdio>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <string>
#include <filesystem>
#include <MinHook.h>
#include "Types.h"

// namespace shortcuts
namespace fs = std::filesystem;

// definitions
// folder to load rawfiles from by default
#define DEFAULT_RAWFILES_DIR "rawfiles"
// folder to load songs from by default - TODO: add songs folder
#define DEFAULT_SONGS_DIR "."
// console log message macros
#define RBVRE_MSG(x, ...) std::printf("[RBVRE:MSG] " x "\n", __VA_ARGS__)
#ifdef _DEBUG
#define RBVRE_DEBUG(x, ...) std::printf("[RBVRE:DBG] " x "\n", __VA_ARGS__)
#else
#define RBVRE_DEBUG(x, ...) 
#endif // _DEBUG

// global variables - config options
std::string RawfilesFolder(DEFAULT_RAWFILES_DIR);
std::string SongsFolder(DEFAULT_SONGS_DIR);
bool PrintRawfiles = false;
bool PrintArkfiles = false;
// global variables - song loading
std::vector<std::string> skuList;
int loadedExtraSKUs = 0;
int loadedOfficialSKUs = 0;

// ARKless file loading hook
typedef enum _FileMode {
    kRead = 0x0,
    kReadNoArk = 0x1,
    kReadNoBuffer = 0x2,
    kAppend = 0x3,
    kWrite = 0x4,
    kWriteNoBuffer = 0x5
} FileMode;
void *(*NewFileTrampoline)(const char* path, FileMode mode);
void* NewFileHook(const char* path, FileMode mode) {
    std::string rawpath(RawfilesFolder);
    if (rawpath.crbegin()[0] != '/') rawpath.append("/");
    rawpath.append(path);
    const char* newpath = rawpath.c_str();
    if (fs::exists(newpath)) {
        if (PrintRawfiles) RBVRE_MSG("Loading rawfile: %s", newpath);
        return NewFileTrampoline(newpath, kReadNoArk);
    }
    /* // TODO: songs subfolder
    rawpath.replace(0, RawfilesFolder.length(), SongsFolder);
    newpath = rawpath.c_str();
    if (fs::exists(newpath)) {
        if (PrintRawfiles) RBVRE_MSG("Loading rawfile: %s", newpath);
        return NewFileTrampoline(newpath, kReadNoArk);
    }*/
    if (PrintArkfiles || (PrintRawfiles && mode == kReadNoArk)) RBVRE_MSG("Loading file: %s", path);
    return NewFileTrampoline(path, mode);
}
bool (*FileExistsTrampoline)(const char* path, FileMode mode);
bool FileExistsHook(const char* path, FileMode mode) {
    std::string rawpath(RawfilesFolder);
    if (rawpath.crbegin()[0] != '/') rawpath.append("/");
    rawpath.append(path);
    const char* newpath = rawpath.c_str();
    if (fs::exists(newpath)) {
        return FileExistsTrampoline(newpath, kReadNoArk);
    }
    /* // TODO: songs subfolder
    rawpath.replace(0, RawfilesFolder.length(), SongsFolder);
    newpath = rawpath.c_str();
    if (fs::exists(newpath)) {
        return FileExistsTrampoline(newpath, kReadNoArk);
    }*/
    return FileExistsTrampoline(path, mode);
}

// controller hook to remove need for having an oculus controller connected
bool (*OculusControllerConnectedTrampoline)(void* OculusController, int HandType);
bool OculusControllerConnectedHook(void* OculusController, int HandType) {
    if (HandType == 1) return true; // right hand always true
    return OculusControllerConnectedTrampoline(OculusController, HandType);
}

// DLC loading hooks to add custom songs into the array
#define CustomElemMask 0xFF000000
void* (*SkuToSongNameTrampoline)(void* unk, const char* SKU);
void* SkuToSongNameHook(void * unk, const char* SKU) {
    RBVRE_DEBUG("SKU: %s", SKU);
    return SkuToSongNameTrampoline(unk, SKU);
}
int (*PurchaseArrayGetSizeTrampoline)(void* parr);
int PurchaseArrayGetSizeHook(void* parr) {
    loadedOfficialSKUs = PurchaseArrayGetSizeTrampoline(parr);
    return loadedOfficialSKUs + loadedExtraSKUs;
}
void *(*PurchaseArrayGetElementTrampoline)(void* parr, int elemNum);
void* PurchaseArrayGetElementHook(void* parr, int elemNum) {
    if (elemNum < loadedOfficialSKUs) return PurchaseArrayGetElementTrampoline(parr, elemNum);
    return (void*)(CustomElemMask | elemNum);
}
const char* (*PurchaseGetSKUTrampoline)(void* pelem);
const char* PurchaseGetSKUHook(void* pelem) {
    if (((int)pelem & CustomElemMask) == CustomElemMask) {
        int realSKU = ((int)pelem & ~CustomElemMask) - loadedOfficialSKUs;
        return skuList[realSKU].c_str();
    }
    return PurchaseGetSKUTrampoline(pelem);
}

// scan through a directory to get the list of ARKs
void ScanSongs(fs::path directory) {
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            // ignore subdirectories for now, TODO: scan through subdirectories and handle them
            //ScanSongs(entry.path());
        } else if (entry.is_regular_file() && // is a regular file
            entry.path().extension().compare(".hdr") == 0 && // is a HDR file
            entry.path().stem().compare("main_pc") != 0) { // is *not* the main_pc file
            // create SKU and add to the list
            std::string SKU("song_");
            SKU.append(entry.path().stem().string());
            SKU.resize(SKU.size() - 3); // remove _pc suffix
            skuList.push_back(SKU);
        }
    }
}


// Your super gamegem hack gets demolished by a compiler :uncanny:
Color newgemcolor;
Color* colorptr;

BOOL(*RBGemSmasherComUpdateColorsTrampoline)(RBGemSmasherCom* thiscom);
BOOL RBGemSmasherComUpdateColorsHook(RBGemSmasherCom* thiscom) {
    // back up the colour pointer to be used in the next hook to change the colour
    colorptr = &(thiscom->mColor);
    BOOL r = RBGemSmasherComUpdateColorsTrampoline(thiscom);
    // after this has been called, update the colors afterwards to fix particle effects
    (thiscom->mColor).r = newgemcolor.r;
    (thiscom->mColor).g = newgemcolor.g;
    (thiscom->mColor).b = newgemcolor.b;
    (thiscom->mColor).a = newgemcolor.a;
    return r;
}

BOOL(*DoSetColorTrampoline)(void* component, void* proppath, void* propinfo, Color* color, Color* toset, int param_6, bool param_7);
BOOL DoSetColorHook(void* component, void* proppath, PropInfo* propinfo, Color* color, Color* toset, int param_6, bool param_7) {
    // normally, gems have their colour variable set to null, so if it's not then its not a gem
    // alternatively if the colour to set is the pointer we saved before, its the strikeline, so we update that too
    if (color != NULL && toset != colorptr) return DoSetColorTrampoline(component, proppath, propinfo, color, toset, param_6, param_7);
    return DoSetColorTrampoline(component, proppath, propinfo, color, &newgemcolor, param_6, param_7);
}

// VRless stuff
typedef struct _Vector3 {
    float x;
    float y;
    float z;
} Vector3;

typedef struct _Matrix3 {
    Vector3 x;
    Vector3 y;
    Vector3 z;
} Matrix3;

typedef struct _Transform {
    Matrix3 m;
    Vector3 v;
} Transform;

void (*MetaStateGotoTrampoline)(void* metastate, int layout);
void MetaStateGoto(void* metastate, int layout) {
    if (layout == 0) layout = 1;
    MetaStateGotoTrampoline(metastate, layout);
    return;
}

// TODO: How do i rotate head to hit quickplay lol
Transform* (*GetEyeXfmImplTrampoline)(void* vrmgr, Transform* return_storage, int eye);
Transform* GetEyeXfmImpl(void* vrmgr, Transform* return_storage, int eye) {
    Transform* r = GetEyeXfmImplTrampoline(vrmgr, return_storage, 0);
    // eye height is hardcoded to 1.8288 in StubVrMgr so use that
    return_storage->v.z += 1.8288;
    return return_storage;
}

void UpdateEyeTextureResolution(void* rbprofile) {
    return;
}

bool IsHandConnected(void* vrmgr, int hand_type) {
    return true;
}

// initialisation function
void InitMod() {
    MH_Initialize();
    // install game hooks
    MH_CreateHook((void*)0x1406fa9a0, &NewFileHook, (void**)&NewFileTrampoline);
    MH_CreateHook((void*)0x1406f9380, &FileExistsHook, (void**)&FileExistsTrampoline);
    MH_CreateHook((void*)0x1402e0560, &SkuToSongNameHook, (void**)&SkuToSongNameTrampoline);
    MH_CreateHook((void*)0x140bb98d0, &OculusControllerConnectedHook, (void**)&OculusControllerConnectedTrampoline);
    // load config
    INIReader reader("RBVREnhanced.ini");
    RawfilesFolder = reader.Get("Arkless", "RawfilesFolder", DEFAULT_RAWFILES_DIR);
    PrintRawfiles = reader.GetBoolean("Arkless", "PrintRawfiles", false);
    PrintArkfiles = reader.GetBoolean("Arkless", "PrintArkfiles", false);
    if (reader.GetBoolean("Settings", "DebugConsole", false)) {
        // allocate console
        AllocConsole();
        SetConsoleTitleA("RBVREnhanced");
        // set up stdout/stderr
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
    }
    // patching the oculus API in memory is not that great, so gate it behind an on-by-default config option
    if (reader.GetBoolean("Settings", "LoadCustomSongs", true)) {
        // install oculus API hooks for song loading purposes
        MH_CreateHookApi(L"LibOVRPlatform64_1.dll", "ovr_PurchaseArray_GetSize", &PurchaseArrayGetSizeHook, (void**)&PurchaseArrayGetSizeTrampoline);
        MH_CreateHookApi(L"LibOVRPlatform64_1.dll", "ovr_PurchaseArray_GetElement", &PurchaseArrayGetElementHook, (void**)&PurchaseArrayGetElementTrampoline);
        MH_CreateHookApi(L"LibOVRPlatform64_1.dll", "ovr_Purchase_GetSKU", &PurchaseGetSKUHook, (void**)&PurchaseGetSKUTrampoline);
        // scan for songs in the specified folder
        ScanSongs(SongsFolder);
        loadedExtraSKUs = skuList.size();
        RBVRE_MSG("Loaded %i songs!", loadedExtraSKUs);
    }
    // and, VRless, partial
    if (reader.GetBoolean("Settings", "NonVRMode", false)) {
        // replaces OculusVrMgr with StubVrMgr initialisation
        // and fixes up some StubVrMgr functions to actually work and not crash
        MH_CreateHook((void*)0x140bb0b90, (void*)0x140bc5c40, NULL);
        MH_CreateHook((void*)0x1402afc20, &MetaStateGoto, (void**)&MetaStateGotoTrampoline);
        MH_CreateHook((void*)0x140bc5d40, &GetEyeXfmImpl, (void**)&GetEyeXfmImplTrampoline);
        MH_CreateHook((void*)0x1403119f0, &UpdateEyeTextureResolution, NULL);
        MH_CreateHook((void*)0x140bb09e0, &IsHandConnected, NULL);
    }
    // since the game gem hooks are really hacky, lock them behind a boolean option
    if (reader.GetBoolean("Cosmetic", "EnableGemColours", false)) {
        MH_CreateHook((void*)0x14033b860, &RBGemSmasherComUpdateColorsHook, (void**)&RBGemSmasherComUpdateColorsTrampoline);
        MH_CreateHook((void*)0x14053c9a0, &DoSetColorHook, (void**)&DoSetColorTrampoline);
        newgemcolor.a = 1;
        newgemcolor.r = reader.GetReal("Cosmetic", "GemR", 1.0);
        newgemcolor.g = reader.GetReal("Cosmetic", "GemG", 1.0);
        newgemcolor.b = reader.GetReal("Cosmetic", "GemB", 1.0);
    }

    // apply the hooks
    MH_EnableHook(MH_ALL_HOOKS);
    // ready!
    RBVRE_MSG("Ready!");
}

// DllMain 
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
        InitMod();
    return TRUE;
}

// export for adding DLL to EXE's import table
extern "C" __declspec(dllexport) void RBVRE_EXPORT() {}
