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

bool EEWebTnLRasterizer::GetIsScreenValid(DX7Screen* screen)
{
    return EEWebNullRasterizer::GetIsScreenValid(screen);
}

long EEWebTnLRasterizer::ActivateTheScreen(unsigned long screen, GESurface* primary, GESurface* back, GETextureManager* manager, GEWindow* window)
{
    return EEWebNullRasterizer::ActivateTheScreen(screen, primary, back, manager, window);
}

long EEWebTnLRasterizer::CreateVertexBuffers(unsigned long screen)
{
    return EEWebNullRasterizer::CreateVertexBuffers(screen);
}

#endif
