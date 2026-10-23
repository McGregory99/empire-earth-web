// The hardware transform-and-lighting class of the rasterizer (task P2.7), the original's DX7TnLRasterizer: it derives from the base class
// (DX7Rasterizer) and replaces three of its virtual functions, the screen's activation (ActivateTheScreen), the ring vertex buffer
// (CreateVertexBuffers) and the validity test (GetIsScreenValid).  Compiled only in the TnL build (rasterizer_variant.h).
#include "rasterizer_variant.h"

#if EEWEB_TNL

EEWebTnLRasterizer::EEWebTnLRasterizer()
{
}

EEWebTnLRasterizer::~EEWebTnLRasterizer()
{
}

namespace {

const GUID_ kIidTnLHal = {0xf5049e78, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};

// A screen is valid for the T&L class when its Direct3D 7 offers a hardware-rasterizing device with hardware transform and lighting.
long __stdcall FindTnLDevice(char*, char*, void* deviceDesc7, void* found)
{
    const unsigned long devCaps = *static_cast<unsigned long*>(deviceDesc7);
    const unsigned long wanted = D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT;
    if ((devCaps & wanted) == wanted) {
        *static_cast<bool*>(found) = true;
        return 0;
    }
    return 1;
}

} // namespace

bool EEWebTnLRasterizer::GetIsScreenValid(DX7Screen* screen)
{
    return HasDevice(screen, &FindTnLDevice);
}

// The T&L activation asks for the T&L HAL device and keeps its flags at "can modulate": no adapter is looked at.
long EEWebTnLRasterizer::ActivateTheScreen(unsigned long screen, GESurface*, GESurface* back, GETextureManager*, GEWindow*)
{
    return ActivateDevice(screen, back, kIidTnLHal, false);
}

// ... and its ring buffer is write-only, in the card's memory.
long EEWebTnLRasterizer::CreateVertexBuffers(unsigned long screen)
{
    return CreateRingBuffer(screen, 0x10000);
}

#endif
