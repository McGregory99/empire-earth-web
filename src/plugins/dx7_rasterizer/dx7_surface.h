// DX7Surface: one DirectDraw 7 surface, as the engine sees it through the 32-slot GESurface interface
// (task P2.6.2).  A surface is created by DX7Screen::CreateSurface from a GESurfaceDescription; it keeps the
// description it was created from, the IDirectDrawSurface7, the surfaces attached to it (a flip chain's back
// buffer, a texture's next mipmap level), and the memory/DC of the last lock.  The observable behaviour of the
// original (tests/dx7_rasterizer/surface_checks.h compares both on the same scripted DirectDraw objects) is kept;
// only the object layout is our own, except what the engine reaches:
//
//   offset 0x0  vptr       (the 32-slot GESurface vtable, destructor in slot 0)
//   offset 0x4  refCount   (GESurfaceLayout.refCount: the engine's UReferenceCounter::Dereference counts it down
//                           and deletes the surface through the destructor slot at zero)
//
// Result codes of the operations, as in the original: 0 success, 1 failure (the DirectDraw error is recorded with
// the engine's ESRecordNativeError), 0xd the surface is lost or busy (the caller restores and tries again),
// 2 TextLock/TextUnlock in the wrong state.  Operations that DirectDraw may refuse with DDERR_WASSTILLDRAWING
// repeat the call (with Sleep(0)) until it is accepted.
#ifndef EEWEB_DX7_SURFACE_H
#define EEWEB_DX7_SURFACE_H

#include "ddraw7_min.h"
#include "ge_interfaces.h"
#include "ge_layouts.h"

class DX7Screen;

class DX7Surface : public GESurface {
public:
    long AttachSurface(GESurface*) override;                                          // 1
    long Clear(GEColor&, float) override;                                             // 3
    long Clear(GERectangle&, GEColor&, float) override;                               // 2
    long CopyTo(GESurface*, GERectangle&, GERectangle&, bool) override;               // 4
    long CopyWithFilter(GESurface*, GERectangle&, GERectangle&) override;             // 5
    long DetachSurface(unsigned long) override;                                       // 6
    long Flip(bool) override;                                                         // 7
    unsigned long GetAllocatedSize() override;                                        // 8
    GESurface* GetAttachedSurface(unsigned long) override;                            // 9
    unsigned long GetAttachedSurfaceCount() override;                                 // 10
    unsigned long GetBytesPerRow() const override { return m_pitch; }                 // 11
    unsigned long GetHeight() const override { return m_description.height; }         // 12
    bool GetIs3DRenderable() const override { return m_description.is3DRenderable != 0; }  // 13
    bool GetIsTextLocked() const override { return m_textLocked; }                    // 14
    bool GetIsTransparencyEnabled() const override { return m_description.isTransparencyEnabled != 0; }  // 15
    unsigned char* GetMemoryAddress() const override { return m_memory; }             // 16
    GEScreen* GetParentScreen() const override { return m_screen; }                   // 17
    unsigned long GetPixelFormat() const override { return m_description.pixelFormat; }  // 18
    unsigned long GetPixelDepth() const override { return m_description.pixelDepth; }    // 19
    unsigned long GetSurfaceVolatility() const override { return m_description.volatility; }  // 20
    GESurfaceDescription const& GetSurfaceDescription() const override;               // 21
    HDC__* GetTextDC() const override { return reinterpret_cast<HDC__*>(m_textDC); }  // 22
    GEColor const& GetTransparencyColor() const override;                             // 23
    unsigned long GetType() const override { return m_description.type; }             // 24
    unsigned long GetWidth() const override { return m_description.width; }          // 25
    long Lock() override;                                                             // 26
    long RestoreSurface() override;                                                   // 27
    void SetTransparencyColor(GEColor&) override;                                     // 28
    long TextLock() override;                                                         // 29
    long TextUnlock() override;                                                       // 30
    long Unlock() override;                                                           // 31

    // The DirectDraw surface behind it: what the rasterizer hands to Direct3D (CreateDevice, SetRenderTarget) and blits.
    IDirectDrawSurface7* GetDirectDrawSurface() const { return m_surface; }

private:
    friend class DX7Screen;

    // DX7Screen::CreateSurface and the 32-bit probe are the only makers.
    DX7Surface(IDirectDrawSurface7* surface, const GESurfaceDescriptionLayout& description, GEScreen* screen);
    ~DX7Surface() override;                                                           // 0

    void AddAttached(DX7Surface*);
    template <class Pixel>
    long CopyWithFilterOfSize(DX7Surface* destination, const GERectangleLayout& source, const GERectangleLayout& target);

    // Offsets 0x4 and 0x8 are the engine's (see the file comment); the description is the engine's struct.
    unsigned long m_refCount;
    GESurfaceDescriptionLayout m_description;
    // Plugin-owned from here.
    DX7Surface** m_attached;
    unsigned long m_attachedCount;
    unsigned long m_attachedCapacity;
    float m_transparencyColor[3]; // GEColor: starts white
    IDirectDrawSurface7* m_surface;
    GEScreen* m_screen;
    unsigned char* m_memory;
    void* m_textDC;
    unsigned long m_pitch;
    bool m_locked;
    bool m_textLocked;
};

#endif
