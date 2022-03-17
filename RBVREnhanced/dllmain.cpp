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

// initialisation function
void InitMod() {
    // install game hooks
    InstallHook((void*)0x1406fa9a0, &NewFileHook, (void**)&NewFileTrampoline);
    InstallHook((void*)0x1406f9380, &FileExistsHook, (void**)&FileExistsTrampoline);
    InstallHook((void*)0x1402e0560, &SkuToSongNameHook, (void**)&SkuToSongNameTrampoline);
    InstallHook((void*)0x140bb98d0, &OculusControllerConnectedHook, (void**)&OculusControllerConnectedTrampoline);
    // install oculus API hooks for song loading purposes
    HMODULE OVRPlatform = GetModuleHandleA("LibOVRPlatform64_1.dll");
    if (OVRPlatform != NULL) {
        InstallHook((void*)GetProcAddress(OVRPlatform, "ovr_PurchaseArray_GetSize"), &PurchaseArrayGetSizeHook, (void**)&PurchaseArrayGetSizeTrampoline);
        InstallHook((void*)GetProcAddress(OVRPlatform, "ovr_PurchaseArray_GetElement"), &PurchaseArrayGetElementHook, (void**)&PurchaseArrayGetElementTrampoline);
        InstallHook((void*)GetProcAddress(OVRPlatform, "ovr_Purchase_GetSKU"), &PurchaseGetSKUHook, (void**)&PurchaseGetSKUTrampoline);
    }
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
    // scan for songs in the specified folder
    ScanSongs(SongsFolder);
    loadedExtraSKUs = skuList.size();
    RBVRE_MSG("Loaded %i songs!", loadedExtraSKUs);
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
