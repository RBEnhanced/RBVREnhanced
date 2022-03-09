/*
    RBVREnhanced - Hooking.h
    Header file for Hooking.cpp.
*/

#pragma once
void InstallHook(void* func2hook, void* payloadFunc, void** trampolinePtr);
