// The two builds of the plugin, from one source: EEWEB_TNL=1 builds EEWebRasterizer.dll, the counterpart of the original's DX7HRTnLDisplay.dll
// (hardware transform and lighting: the class EEWebTnLRasterizer, which overrides the activation, the vertex buffer and the validity test of
// the base class), EEWEB_TNL=0 builds EEWebRasterizerNonTnL.dll, the counterpart of DX7HRDisplay.dll (the base class EEWebNullRasterizer, the
// original's DX7Rasterizer, is the rasterizer).  The original's TnL DLL holds both classes; the non-TnL one only the base.  The Makefile
// compiles every source twice, with and without -DEEWEB_TNL=0 (tnl_rasterizer.cpp is empty in the non-TnL build).
#ifndef EEWEB_RASTERIZER_VARIANT_H
#define EEWEB_RASTERIZER_VARIANT_H

#include "null_rasterizer.h"

#ifndef EEWEB_TNL
#define EEWEB_TNL 1
#endif

#if EEWEB_TNL
// The name the engine's configuration selects the rasterizer by ("Rasterizer Name"), and PIMGetName's text.
#define EEWEB_RASTERIZER_NAME "EEWeb Null Rasterizer"

class EEWebTnLRasterizer : public EEWebNullRasterizer {
public:
    EEWebTnLRasterizer();
    ~EEWebTnLRasterizer() override;  // 69

    bool GetIsScreenValid(DX7Screen*) override;  // 78
protected:
    long ActivateTheScreen(unsigned long, GESurface*, GESurface*, GETextureManager*, GEWindow*) override;  // 71
    long CreateVertexBuffers(unsigned long) override;  // 79
};

typedef EEWebTnLRasterizer EEWebVariantRasterizer;
#else
#define EEWEB_RASTERIZER_NAME "EEWeb Null Rasterizer (no T&L)"
typedef EEWebNullRasterizer EEWebVariantRasterizer;
#endif

static const char kEEWebRasterizerName[] = EEWEB_RASTERIZER_NAME;

#endif
