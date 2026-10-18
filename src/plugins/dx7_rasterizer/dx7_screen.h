// DX7Screen: one DirectDraw 7 device, as the engine sees it through the 20-slot GEScreen interface
// (task P2.6.1).  The original's DX7Screen is created by the rasterizer while it enumerates the
// DirectDraw devices (ActivateRasterizer), kept in a static list until it is destroyed, and handed to
// the engine through AddValidScreen when it can render.  This class reproduces the observable behaviour
// of the original (tests/dx7_rasterizer/screen_checks.h compares both on the same scripted DirectDraw
// object) with its own object layout; only what the engine reaches is fixed:
//
//   offset 0x0  vptr            (the 20-slot GEScreen vtable, destructor in slot 19)
//   offset 0x4  textureManager  (GEScreenLayout.textureManager: the engine's GEScreen::SetTextureManager writes it)
//   offset 0x8  rasterizerIndex (GEScreenLayout.rasterizerIndex: GEScreen::SetRasterizerIndex writes it)
//
// CreateSurface, CreateAWindow and the Test32BitSupport probe the constructor ends with (task P2.6.2) build
// DX7Surface objects (dx7_surface.h); the pixel and texture formats beyond the table's entries are P2.6.3.
#ifndef EEWEB_DX7_SCREEN_H
#define EEWEB_DX7_SCREEN_H

#include "ddraw7_min.h"
#include "engine_ustring.h"
#include "ge_interfaces.h"
#include "ge_layouts.h"

struct HMONITOR__;

// One entry of GEScreen::GetMode / GetCurrentMode: struct GEVideoMode of the engine, 0x14 bytes.
struct EEWebVideoMode {
    unsigned long width;
    unsigned long height;
    unsigned long refreshRate;
    unsigned long bitsPerPixel;
    unsigned long pixelFormat; // index into the pixel format table of dx7_screen.cpp; 14 when none matches
};
static_assert(sizeof(EEWebVideoMode) == sizeof(GEVideoModeLayout), "GEVideoMode is 20 bytes (ge_layouts.h)");

class EEWebNullRasterizer;

// user32's GetMonitorInfoA for a monitor handle (resolved at run time, like the original): false when there is
// no such monitor.  `flags` is MONITORINFO.dwFlags (bit 0: the primary monitor).
bool EEWebQueryMonitorFlags(void* monitor, unsigned long& flags);

class DX7Screen : public GEScreen {
public:
    // The registry of live screens: every constructed screen, until it is destroyed.
    static unsigned long GetScreenCount();
    static DX7Screen* GetScreen(unsigned long index);

    IDirectDraw7* GetDirectDraw() const { return m_directDraw; }

    long Capture(GEWindow const*, bool) override;                                                       // 0
    long CreateSurface(GESurfaceDescription&, GESurface**) override;                                    // 1
    long CreateAWindow(GE2DPoint const&, unsigned long, unsigned long, UString&, UString&, GEWindow*&, unsigned long, GEWindow*) override;  // 2
    bool Get32BitDeviceSupport() override { return m_device32; }                                        // 3
    bool Get32BitTextureSupport() override { return m_texture32; }                                      // 4
    bool GetAttachedToDesktop() const override { return m_attachedToDesktop; }                          // 5
    bool GetCanMagFilterSurfaces() const override { return m_canMagFilter; }                            // 6
    bool GetCanMinFilterSurfaces() const override { return m_canMinFilter; }                            // 7
    GEVideoMode const& GetCurrentMode() const override;                                                 // 8
    unsigned long GetCurrentModeIndex() const override { return m_currentMode; }                        // 9
    GERectangle const& GetDesktopRectangle() override;                                                  // 10
    bool GetIsFullScreen() const override { return m_fullScreen; }                                      // 11
    GEVideoMode const& GetMode(unsigned long) const override;                                           // 12
    unsigned long GetModeCount() const override { return m_modeCount; }                                 // 13
    UString const& GetName() const override { return m_name; }                                          // 14
    bool GetSharesPrimary() override { return m_sharesPrimary; }                                        // 15
    long Release(GEWindow const*) override;                                                             // 16
    long RestoreAllSurfaces() override;                                                                 // 17
    long SetMode(unsigned long) override;                                                               // 18

private:
    friend class EEWebNullRasterizer; // creates and destroys screens while it activates and deactivates

    // Same decorated name as the original's private constructor, which the test harness calls on both.
    DX7Screen(HMONITOR__* monitor, IDirectDraw7* directDraw, UString const& name, bool attachedToDesktop, bool sharesPrimary);
    ~DX7Screen() override;                                                                              // 19

    static int __attribute__((stdcall)) EnumVideoModeCallback(DDSURFACEDESC2_*, void*);
    static long __attribute__((stdcall)) TestWindowProc(void*, unsigned int, unsigned int, long);
    static long __attribute__((stdcall)) EnumTextureFormatCallback(DDPIXELFORMAT_*, void*);
    void AddMode(const EEWebVideoMode&);
    void Test32BitSupport();

    // Offsets 0x4 and 0x8 are the engine's (see the file comment).
    void* m_textureManager;
    unsigned long m_rasterizerIndex;
    // Plugin-owned from here.
    EEWebVideoMode* m_modes;
    unsigned long m_modeCount;
    unsigned long m_modeCapacity;
    float m_desktopRectangle[4]; // GERectangle: bottom, left, right, top
    UString m_name;
    IDirectDraw7* m_directDraw;
    HMONITOR__* m_monitor;
    unsigned long m_currentMode;
    bool m_attachedToDesktop;
    bool m_fullScreen;
    bool m_sharesPrimary;
    bool m_canMagFilter;
    bool m_canMinFilter;
    bool m_device32;
    bool m_texture32;
};

#endif
