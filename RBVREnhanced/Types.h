#pragma once

typedef struct _Color {
    float r;
    float g;
    float b;
    float a;
} Color;

typedef struct _Symbol {
    char* sym;
} Symbol;

typedef struct _PropInfo {
    int mOffset;
    int mDynamicStorageOffset;
    int mType;
    ULONG64 mSize;
    void* mMetadata;
} PropInfo;

typedef struct _RBGemSmasherCom {
    char pad[32];
    Symbol mColorName;
    Color mColor;
} RBGemSmasherCom;