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

// definitions
#define DEFAULT_RAWFILES_DIR "rawfiles"
#define RBVRE_MSG(x, ...) std::printf("[RBVRE:MSG] " x "\n", __VA_ARGS__)
#ifdef _DEBUG
#define RBVRE_DEBUG(x, ...) std::printf("[RBVRE:DBG] " x "\n", __VA_ARGS__)
#else
#define RBVRE_DEBUG(x, ...) 
#endif // _DEBUG

// global variables
std::string RawfilesFolder(DEFAULT_RAWFILES_DIR);
bool PrintRawfiles = false;
bool PrintArkfiles = false;

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
    if (std::filesystem::exists(newpath)) {
        if (PrintRawfiles) RBVRE_MSG("Loading rawfile: %s", newpath);
        return NewFileTrampoline(newpath, kReadNoArk);
    }
    if (PrintArkfiles) RBVRE_MSG("Loading ARK file: %s", path);
    return NewFileTrampoline(path, mode);
}

// SKU song name hook, for debug purposes
void* (*SkuToSongNameTrampoline)(void* unk, const char* SKU);
void* SkuToSongNameHook(void * unk, const char* SKU) {
    RBVRE_DEBUG("SKU: %s", SKU);
    return SkuToSongNameTrampoline(unk, SKU);
}

// initialisation function
void InitMod() {
    // install hooks
    InstallHook((void*)0x1406fa9a0, &NewFileHook, (void**)&NewFileTrampoline);
    InstallHook((void*)0x1402e0560, &SkuToSongNameHook, (void**)&SkuToSongNameTrampoline);
    // load config
    INIReader reader("RBVREnhanced.ini");
    RawfilesFolder = reader.Get("Arkless", "RawfilesFolder", DEFAULT_RAWFILES_DIR);
    PrintRawfiles = reader.GetBoolean("Arkless", "PrintRawfiles", false);
    PrintArkfiles = reader.GetBoolean("Arkless", "PrintArkfiles", false);
    if (reader.GetBoolean("Settings", "DebugConsole", true)) {
        // allocate console
        AllocConsole();
        SetConsoleTitleA("RBVREnhanced");
        // set up stdout/stderr
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
    }
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