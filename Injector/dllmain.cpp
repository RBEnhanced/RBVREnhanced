// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <Windows.h>

static FARPROC OriginalFunctions[10] = { 0 };

#define ADD_ORIGINAL(i, name) OriginalFunctions[i] = GetProcAddress(dll, #name)

#define PROXY(i, name) \
	__declspec(dllexport) INT_PTR __stdcall name() \
	{ \
		return OriginalFunctions[i](); \
	}


extern "C" {
	PROXY(0, HidD_SetFeature);
	PROXY(1, HidD_GetFeature);
	PROXY(2, HidD_FlushQueue);
	PROXY(3, HidD_GetPreparsedData);
	PROXY(4, HidD_GetHidGuid);
	PROXY(5, HidD_GetAttributes);
	PROXY(6, HidP_GetCaps);
	PROXY(7, HidD_SetOutputReport);
	PROXY(8, HidD_GetProductString);
}
static void LoadFunctions(HMODULE dll)
{
	ADD_ORIGINAL(0, HidD_SetFeature);
	ADD_ORIGINAL(1, HidD_GetFeature);
	ADD_ORIGINAL(2, HidD_FlushQueue);
	ADD_ORIGINAL(3, HidD_GetPreparsedData);
	ADD_ORIGINAL(4, HidD_GetHidGuid);
	ADD_ORIGINAL(5, HidD_GetAttributes);
	ADD_ORIGINAL(6, HidP_GetCaps);
	ADD_ORIGINAL(7, HidD_SetOutputReport);
	ADD_ORIGINAL(8, HidD_GetProductString);
}

static void LoadOriginalDLL()
{
	char buffer[MAX_PATH];
	// load the original hid.dll
	GetSystemDirectoryA(buffer, MAX_PATH);
	strcat_s(buffer, "\\hid.dll");
	HMODULE dll = LoadLibraryA(buffer);
	if (!dll) //oopsie daisy
		ExitProcess(0);
	LoadFunctions(dll);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		LoadOriginalDLL();
		LoadLibraryA("RBVREnhanced.dll");
	}
    return TRUE;
}

