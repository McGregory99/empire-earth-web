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

// A pixel format, a caps block, a colour key and the blit effects block, as DirectDraw 7 lays them out.
struct DDCOLORKEY_ {
    DWORD_ low, high;
};
struct DDBLTFX_ {
    DWORD_ words[25]; // 100 bytes; word 0 is dwSize, word 20 (offset 0x50) is dwFillColor
};
static_assert(sizeof(DDBLTFX_) == 100, "DDBLTFX is 100 bytes");
struct RECT_ {
    long left, top, right, bottom;
};

// Results the surface methods tell apart (the SDK's values).
#define DDERR_SURFACEBUSY_ 0x887601AEUL
#define DDERR_SURFACELOST_ 0x887601C2UL
#define DDERR_WASSTILLDRAWING_ 0x8876021CUL
#define DDERR_NOTLOCKED_ 0x88760248UL

// Cooperative levels used by DX7Screen::Capture / Release (the values are the SDK's).
#define DDSCL_NORMAL 0x00000008UL
#define DDSCL_SETFOCUSWINDOW 0x00000080UL

struct IDirectDraw7;
struct IDirectDrawSurface7;
struct IDirect3DDevice7;
typedef int(__attribute__((stdcall)) * EnumModesCallback)(DDSURFACEDESC2_*, void*);

struct IDirectDraw7Vtbl {
    HRESULT_(__attribute__((stdcall)) * QueryInterface)(IDirectDraw7*, const GUID_*, void**);                   // 0
    DWORD_(__attribute__((stdcall)) * AddRef)(IDirectDraw7*);                                                   // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirectDraw7*);                                                  // 2
    void* compact;                                                                                              // 3
    void* createClipper;                                                                                        // 4
    void* createPalette;                                                                                        // 5
    HRESULT_(__attribute__((stdcall)) * CreateSurface)(IDirectDraw7*, DDSURFACEDESC2_*, IDirectDrawSurface7**, void*);  // 6
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

// IDirect3D7: EnumDevices (slot 3), CreateDevice (slot 4), CreateVertexBuffer (slot 5) and Release (slot 2).
struct IDirect3D7;
struct IDirect3DVertexBuffer7;
typedef long(__attribute__((stdcall)) * EnumDevicesCallback)(char*, char*, void* deviceDesc7, void*);
struct IDirect3D7Vtbl {
    void* queryInterface;                                                                    // 0
    void* addRef;                                                                            // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirect3D7*);                                 // 2
    HRESULT_(__attribute__((stdcall)) * EnumDevices)(IDirect3D7*, EnumDevicesCallback, void*);  // 3
    HRESULT_(__attribute__((stdcall)) * CreateDevice)(IDirect3D7*, const GUID_*, IDirectDrawSurface7*, IDirect3DDevice7**);  // 4
    HRESULT_(__attribute__((stdcall)) * CreateVertexBuffer)(IDirect3D7*, const void* vertexBufferDesc, IDirect3DVertexBuffer7**, DWORD_ flags);  // 5
};
struct IDirect3D7 {
    const IDirect3D7Vtbl* vtbl;
};

// IDirect3DVertexBuffer7: Release (slot 2), Lock (3) and Unlock (4) are called.
struct IDirect3DVertexBuffer7Vtbl {
    void* queryInterface;                                                      // 0
    void* addRef;                                                              // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirect3DVertexBuffer7*);       // 2
    HRESULT_(__attribute__((stdcall)) * Lock)(IDirect3DVertexBuffer7*, DWORD_ flags, void** data, DWORD_* size);  // 3
    HRESULT_(__attribute__((stdcall)) * Unlock)(IDirect3DVertexBuffer7*);      // 4
};
static_assert(__builtin_offsetof(IDirect3DVertexBuffer7Vtbl, Lock) == 0xc, "Lock is slot 3");
static_assert(__builtin_offsetof(IDirect3DVertexBuffer7Vtbl, Unlock) == 0x10, "Unlock is slot 4");
struct IDirect3DVertexBuffer7 {
    const IDirect3DVertexBuffer7Vtbl* vtbl;
};

// D3DDEVICEDESC7 starts with dwDevCaps.
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000UL
#define D3DDEVCAPS_HWRASTERIZATION 0x00080000UL

// IDirectDrawSurface7: the methods DX7Surface calls are typed, every other slot only keeps the order.
struct IDirectDrawSurface7;
struct IDirectDrawSurface7Vtbl {
    void* queryInterface;                                                                                       // 0
    void* addRef;                                                                                               // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirectDrawSurface7*);                                           // 2
    HRESULT_(__attribute__((stdcall)) * AddAttachedSurface)(IDirectDrawSurface7*, IDirectDrawSurface7*);        // 3
    void* addOverlayDirtyRect;                                                                                  // 4
    HRESULT_(__attribute__((stdcall)) * Blt)(IDirectDrawSurface7*, RECT_*, IDirectDrawSurface7*, RECT_*, DWORD_, DDBLTFX_*);  // 5
    void* bltBatch;                                                                                             // 6
    void* bltFast;                                                                                              // 7
    HRESULT_(__attribute__((stdcall)) * DeleteAttachedSurface)(IDirectDrawSurface7*, DWORD_, IDirectDrawSurface7*);  // 8
    void* enumAttachedSurfaces;                                                                                 // 9
    void* enumOverlayZOrders;                                                                                   // 10
    HRESULT_(__attribute__((stdcall)) * Flip)(IDirectDrawSurface7*, IDirectDrawSurface7*, DWORD_);              // 11
    HRESULT_(__attribute__((stdcall)) * GetAttachedSurface)(IDirectDrawSurface7*, DDSCAPS2_*, IDirectDrawSurface7**);  // 12
    void* slots13to16[4];                                                                                       // 13 GetBltStatus, 14 GetCaps, 15 GetClipper, 16 GetColorKey
    HRESULT_(__attribute__((stdcall)) * GetDC)(IDirectDrawSurface7*, void**);                                   // 17
    void* slots18to24[7];                                                                                       // 18 GetFlipStatus .. 24 IsLost
    HRESULT_(__attribute__((stdcall)) * Lock)(IDirectDrawSurface7*, RECT_*, DDSURFACEDESC2_*, DWORD_, void*);   // 25
    HRESULT_(__attribute__((stdcall)) * ReleaseDC)(IDirectDrawSurface7*, void*);                                // 26
    HRESULT_(__attribute__((stdcall)) * Restore)(IDirectDrawSurface7*);                                         // 27
    void* setClipper;                                                                                           // 28
    HRESULT_(__attribute__((stdcall)) * SetColorKey)(IDirectDrawSurface7*, DWORD_, DDCOLORKEY_*);               // 29
    void* slots30to31[2];                                                                                       // 30 SetOverlayPosition, 31 SetPalette
    HRESULT_(__attribute__((stdcall)) * Unlock)(IDirectDrawSurface7*, RECT_*);                                  // 32
    void* slots33to48[16];                                                                                      // 33 UpdateOverlay .. 48 GetLOD
};
static_assert(sizeof(IDirectDrawSurface7Vtbl) == 49 * 4, "IDirectDrawSurface7 has 49 methods");
static_assert(__builtin_offsetof(IDirectDrawSurface7Vtbl, Blt) == 0x14, "Blt is slot 5");
static_assert(__builtin_offsetof(IDirectDrawSurface7Vtbl, Lock) == 0x64, "Lock is slot 25");
static_assert(__builtin_offsetof(IDirectDrawSurface7Vtbl, Unlock) == 0x80, "Unlock is slot 32");

struct IDirectDrawSurface7 {
    const IDirectDrawSurface7Vtbl* vtbl;
};

// IDirect3DDevice7: the methods the plugin calls are typed (the 32-bit probe, the state setters), every other slot only keeps the order.
typedef long(__attribute__((stdcall)) * EnumTextureFormatsCallback)(DDPIXELFORMAT_*, void*);
struct IDirect3DDevice7Vtbl {
    void* queryInterface;                                                                                        // 0
    void* addRef;                                                                                                // 1
    DWORD_(__attribute__((stdcall)) * Release)(IDirect3DDevice7*);                                               // 2
    HRESULT_(__attribute__((stdcall)) * GetCaps)(IDirect3DDevice7*, void* deviceDesc7);                          // 3
    HRESULT_(__attribute__((stdcall)) * EnumTextureFormats)(IDirect3DDevice7*, EnumTextureFormatsCallback, void*);  // 4
    HRESULT_(__attribute__((stdcall)) * BeginScene)(IDirect3DDevice7*);                                          // 5
    HRESULT_(__attribute__((stdcall)) * EndScene)(IDirect3DDevice7*);                                            // 6
    void* getDirect3D;                                                                                           // 7
    HRESULT_(__attribute__((stdcall)) * SetRenderTarget)(IDirect3DDevice7*, IDirectDrawSurface7*, DWORD_ flags);  // 8
    void* getRenderTarget;                                                                                       // 9
    HRESULT_(__attribute__((stdcall)) * Clear)(IDirect3DDevice7*, DWORD_ count, const void* rects, DWORD_ flags, DWORD_ color, DWORD_ z, DWORD_ stencil);  // 10
    HRESULT_(__attribute__((stdcall)) * SetTransform)(IDirect3DDevice7*, DWORD_ state, const void* matrix);      // 11
    void* getTransform;                                                                                          // 12
    HRESULT_(__attribute__((stdcall)) * SetViewport)(IDirect3DDevice7*, const void* viewport7);                  // 13
    void* slots14to15[2];                                                                                        // 14 MultiplyTransform, 15 GetViewport
    HRESULT_(__attribute__((stdcall)) * SetMaterial)(IDirect3DDevice7*, const void* material7);                  // 16
    void* getMaterial;                                                                                           // 17
    HRESULT_(__attribute__((stdcall)) * SetLight)(IDirect3DDevice7*, DWORD_ index, const void* light7);          // 18
    void* getLight;                                                                                              // 19
    HRESULT_(__attribute__((stdcall)) * SetRenderState)(IDirect3DDevice7*, DWORD_ state, DWORD_ value);          // 20
    void* slots21to34[14];                                                                                       // 21 GetRenderState .. 34 GetTexture
    HRESULT_(__attribute__((stdcall)) * SetTexture)(IDirect3DDevice7*, DWORD_ stage, IDirectDrawSurface7* surface);  // 35
    void* getTextureStageState;                                                                                  // 36
    HRESULT_(__attribute__((stdcall)) * SetTextureStageState)(IDirect3DDevice7*, DWORD_ stage, DWORD_ type, DWORD_ value);  // 37
    HRESULT_(__attribute__((stdcall)) * ValidateDevice)(IDirect3DDevice7*, DWORD_* passes);                      // 38
    void* slots39to43[5];                                                                                        // 39 ApplyStateBlock .. 43 Load
    HRESULT_(__attribute__((stdcall)) * LightEnable)(IDirect3DDevice7*, DWORD_ index, DWORD_ enable);            // 44
};
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, BeginScene) == 0x14, "BeginScene is slot 5");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetRenderTarget) == 0x20, "SetRenderTarget is slot 8");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, Clear) == 0x28, "Clear is slot 10");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetTexture) == 0x8c, "SetTexture is slot 35");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetTextureStageState) == 0x94, "SetTextureStageState is slot 37");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, ValidateDevice) == 0x98, "ValidateDevice is slot 38");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetTransform) == 0x2c, "SetTransform is slot 11");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetViewport) == 0x34, "SetViewport is slot 13");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetMaterial) == 0x40, "SetMaterial is slot 16");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetLight) == 0x48, "SetLight is slot 18");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, SetRenderState) == 0x50, "SetRenderState is slot 20");
static_assert(__builtin_offsetof(IDirect3DDevice7Vtbl, LightEnable) == 0xb0, "LightEnable is slot 44");
struct IDirect3DDevice7 {
    const IDirect3DDevice7Vtbl* vtbl;
};

typedef int(__attribute__((stdcall)) * DDEnumCallbackExA)(GUID_*, char* description, char* name, void* context, void* monitor);

extern "C" {
__declspec(dllimport) HRESULT_ __stdcall DirectDrawEnumerateExA(DDEnumCallbackExA, void*, DWORD_);
__declspec(dllimport) HRESULT_ __stdcall DirectDrawCreateEx(GUID_*, void**, const GUID_*, void*);
}

#endif
