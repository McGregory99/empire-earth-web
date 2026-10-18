// The slice of DirectDraw 7 / Direct3D 7 that the plugin calls, declared by hand: the plugin is built
// without a Windows SDK (RAST_CXXFLAGS, -ffreestanding), and the interfaces are plain COM, so a vtable
// struct in the documented order is all a call needs.  Only the methods the code calls have a type;
// every other slot is an opaque pointer that keeps the order right.  All methods are __stdcall.
#ifndef EEWEB_DDRAW7_MIN_H
#define EEWEB_DDRAW7_MIN_H

typedef long HRESULT_;
typedef unsigned long DWORD_;
typedef void* HWND_;

struct GUID_ {
    unsigned long d1;
    unsigned short d2, d3;
    unsigned char d4[8];
};

// DDSURFACEDESC2 (124 bytes), only the fields EnumDisplayModes / GetDisplayMode fill in for a mode.
struct DDPIXELFORMAT_ {
    DWORD_ size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
};
struct DDSURFACEDESC2_ {
    DWORD_ size, flags, height, width, pitch, backBufferCount, refreshRate, alphaBitDepth, reserved;
    void* surface;
    DWORD_ colorKeys[8];
    DDPIXELFORMAT_ pixelFormat;
    DWORD_ caps[4];
    DWORD_ textureStage;
};
static_assert(sizeof(DDSURFACEDESC2_) == 0x7c, "DDSURFACEDESC2 is 124 bytes");
static_assert(__builtin_offsetof(DDSURFACEDESC2_, pixelFormat) == 0x48, "ddpfPixelFormat is at 0x48");

// DDCAPS_DX7 is 380 bytes; the plugin reads dwCaps (offset 4) and nothing else.
struct DDCAPS_ {
    DWORD_ size, caps, rest[93];
};
static_assert(sizeof(DDCAPS_) == 0x17c, "DDCAPS_DX7 is 380 bytes");
#define DDCAPS_3D 0x00000001UL

struct DDSCAPS2_ {
    DWORD_ caps, caps2, caps3, caps4;
};
#define DDSCAPS_VIDEOMEMORY 0x00004000UL

// Cooperative levels used by DX7Screen::Capture / Release (the values are the SDK's).
#define DDSCL_NORMAL 0x00000008UL
#define DDSCL_SETFOCUSWINDOW 0x00000080UL

struct IDirectDraw7;
typedef int(__attribute__((stdcall)) * EnumModesCallback)(DDSURFACEDESC2_*, void*);

struct IDirectDraw7Vtbl {
    HRESULT_(__attribute__((stdcall)) * QueryInterface)(IDirectDraw7*, const GUID_*, void**);                   // 0
    DWORD_(__attribute__((stdcall)) * AddRef)(IDirectDraw7*);                                                   // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirectDraw7*);                                                  // 2
    void* compact;                                                                                              // 3
    void* createClipper;                                                                                        // 4
    void* createPalette;                                                                                        // 5
    void* createSurface;                                                                                        // 6
    void* duplicateSurface;                                                                                     // 7
    HRESULT_(__attribute__((stdcall)) * EnumDisplayModes)(IDirectDraw7*, DWORD_, DDSURFACEDESC2_*, void*, EnumModesCallback);  // 8
    void* enumSurfaces;                                                                                         // 9
    void* flipToGDISurface;                                                                                     // 10
    HRESULT_(__attribute__((stdcall)) * GetCaps)(IDirectDraw7*, DDCAPS_*, DDCAPS_*);                            // 11
    HRESULT_(__attribute__((stdcall)) * GetDisplayMode)(IDirectDraw7*, DDSURFACEDESC2_*);                       // 12
    void* getFourCCCodes;                                                                                       // 13
    void* getGDISurface;                                                                                        // 14
    void* getMonitorFrequency;                                                                                  // 15
    void* getScanLine;                                                                                          // 16
    void* getVerticalBlankStatus;                                                                               // 17
    void* initialize;                                                                                           // 18
    void* restoreDisplayMode;                                                                                   // 19
    HRESULT_(__attribute__((stdcall)) * SetCooperativeLevel)(IDirectDraw7*, HWND_, DWORD_);                     // 20
    HRESULT_(__attribute__((stdcall)) * SetDisplayMode)(IDirectDraw7*, DWORD_, DWORD_, DWORD_, DWORD_, DWORD_); // 21
    void* waitForVerticalBlank;                                                                                 // 22
    HRESULT_(__attribute__((stdcall)) * GetAvailableVidMem)(IDirectDraw7*, DDSCAPS2_*, DWORD_*, DWORD_*);       // 23
    void* getSurfaceFromDC;                                                                                     // 24
    HRESULT_(__attribute__((stdcall)) * RestoreAllSurfaces)(IDirectDraw7*);                                     // 25
    HRESULT_(__attribute__((stdcall)) * TestCooperativeLevel)(IDirectDraw7*);                                   // 26
    void* getDeviceIdentifier;                                                                                  // 27
    void* startModeTest;                                                                                        // 28
    void* evaluateMode;                                                                                         // 29
};
static_assert(sizeof(IDirectDraw7Vtbl) == 30 * 4, "IDirectDraw7 has 30 methods");

struct IDirectDraw7 {
    const IDirectDraw7Vtbl* vtbl;
};

// IDirect3D7: only EnumDevices (slot 3) and Release (slot 2).
struct IDirect3D7;
typedef long(__attribute__((stdcall)) * EnumDevicesCallback)(char*, char*, void* deviceDesc7, void*);
struct IDirect3D7Vtbl {
    void* queryInterface;                                                                    // 0
    void* addRef;                                                                            // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirect3D7*);                                 // 2
    HRESULT_(__attribute__((stdcall)) * EnumDevices)(IDirect3D7*, EnumDevicesCallback, void*);  // 3
};
struct IDirect3D7 {
    const IDirect3D7Vtbl* vtbl;
};

// D3DDEVICEDESC7 starts with dwDevCaps.
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000UL
#define D3DDEVCAPS_HWRASTERIZATION 0x00080000UL

typedef int(__attribute__((stdcall)) * DDEnumCallbackExA)(GUID_*, char* description, char* name, void* context, void* monitor);

extern "C" {
__declspec(dllimport) HRESULT_ __stdcall DirectDrawEnumerateExA(DDEnumCallbackExA, void*, DWORD_);
__declspec(dllimport) HRESULT_ __stdcall DirectDrawCreateEx(GUID_*, void**, const GUID_*, void*);
}

#endif
