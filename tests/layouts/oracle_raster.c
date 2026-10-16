/*
 * GERasterizer (the engine base class of the plugin's rasterizer object) and the per-screen state
 * record it keeps for every screen.  The record derives from GEViewport and carries the render
 * state; the plugin reads and writes it through GERasterizer.screens (0x20).  All checks use the
 * engine's own Get/Set methods on a fake rasterizer built through the layout.
 */
#include "oracle.h"

#include <stdlib.h>
#include <string.h>

static GERasterizerLayout *make_rasterizer(GERScreenStateLayout **record)
{
    static GERasterizerLayout rasterizer;
    static struct GERScreenStateLayout *screens[1];
    *record = (GERScreenStateLayout *)calloc(1, sizeof(GERScreenStateLayout));
    screens[0] = *record;
    memset(&rasterizer, 0, sizeof(rasterizer));
    rasterizer.screens.first = screens;
    rasterizer.screens.last = screens + 1;
    rasterizer.screens.end = screens + 1;
    return &rasterizer;
}

static void rasterizer_base(void)
{
    GERScreenStateLayout *rec;
    GERasterizerLayout *r = make_rasterizer(&rec);
    CHECK_F(((fn_u_v)oracle_export("?GetScreenCount@GERasterizer@@QBEKXZ"))(r), 1, "GERasterizer.screens (a vector at 0x1c: first at 0x20, last at 0x24) counts the screens");
    CHECK(((fn_p_u)oracle_export("?GetViewport@GERasterizer@@QBEAAVGEViewport@@K@Z"))(r, 0) == (void *)rec, "GERasterizer.screens[i] is a screen record that starts with a GEViewport");
    CHECK(((fn_p_v)oracle_export("?GetAmbientColor@GERasterizer@@QBEABVGEColor@@XZ"))(r) == (void *)&r->ambientColor, "GERasterizer.ambientColor is at 0x4");
    CHECK(((fn_p_v)oracle_export("?GetComputedAmbientColor@GERasterizer@@QBEABVGEColor@@XZ"))(r) == (void *)&r->computedAmbientColor, "GERasterizer.computedAmbientColor is at 0x10");
    CHECK(((fn_p_v)oracle_export("?GetName@GERasterizer@@QBEABVUString@@XZ"))(r) == (void *)&r->name, "GERasterizer.name (a UString) is at 0x54");
    r->ambientColor.r = 0.5f;
    r->ambientColor.g = 0.25f;
    r->ambientColor.b = 0.125f;
    ((fn_v_fu)oracle_export("?SetAmbientIntensity@GERasterizer@@UAEXMK@Z"))(r, 2.0f, 0);
    CHECK_F(r->ambientIntensity, 2.0f, "GERasterizer::SetAmbientIntensity writes ambientIntensity (0x3c)");
    CHECK(FEQ(r->computedAmbientColor.r, 1.0f) && FEQ(r->computedAmbientColor.g, 0.5f) && FEQ(r->computedAmbientColor.b, 0.25f),
          "GERasterizer::SetAmbientIntensity derives computedAmbientColor (0x10) from ambientColor (0x4) * intensity");
    CHECK_F(((fn_f_v)oracle_export("?GetAmbientIntensity@GERasterizer@@QBEMXZ"))(r), 2.0f, "GERasterizer.ambientIntensity is at 0x3c");
    ((fn_v_fff)oracle_export("?SetWaterRippleEffect@GERasterizer@@QAEXMMM@Z"))(r, 1.0f, 2.0f, 3.0f);
    CHECK(FEQ(r->waterRipple[0], 1.0f) && FEQ(r->waterRipple[1], 2.0f) && FEQ(r->waterRipple[2], 3.0f), "GERasterizer::SetWaterRippleEffect writes waterRipple (0x40, 0x44, 0x48)");
    ((fn_v_u)oracle_export("?SetCulling@GERasterizer@@UAEXK@Z"))(r, 3);
    CHECK(r->culling == 3, "GERasterizer::SetCulling writes culling (0x4c)");
    ((fn_v_u)oracle_export("?SetScreenID@GERasterizer@@QAEXK@Z"))(r, 9);
    CHECK(r->screenId == 9, "GERasterizer::SetScreenID writes screenId (0x64)");
    ((fn_v_u)oracle_export("?SetPolyAreaProfiling@GERasterizer@@QAEX_N@Z"))(r, 1);
    CHECK(r->polyAreaProfiling == 1, "GERasterizer::SetPolyAreaProfiling writes polyAreaProfiling (0x50)");
    r->activated = 1;
    CHECK(((fn_b_v)oracle_export("?GetIsActivated@GERasterizer@@QBE_NXZ"))(r) == 1, "GERasterizer.activated is at 0x51");
    free(rec);
}

static void screen_state(void)
{
    GERScreenStateLayout *rec;
    GERasterizerLayout *r = make_rasterizer(&rec);
    GEColorLayout fog = {0.1f, 0.2f, 0.3f};
    unsigned long src = 0, dst = 0;
    float fog_attributes[8];

    ((fn_v_uu)oracle_export("?SetAlphaTesting@GERasterizer@@UAEXKK@Z"))(r, 11, 0);
    CHECK(rec->alphaTesting == 11, "GERasterizer::SetAlphaTesting writes alphaTesting (0x2e8)");
    ((fn_v_uu)oracle_export("?SetAlphaTestingCompareState@GERasterizer@@UAEXKK@Z"))(r, 12, 0);
    CHECK(rec->alphaTestCompare == 12, "GERasterizer::SetAlphaTestingCompareState writes alphaTestCompare (0x2ec)");
    ((fn_v_fu)oracle_export("?SetAlphaTestingRefValue@GERasterizer@@UAEXMK@Z"))(r, 0.5f, 0);
    CHECK_F(rec->alphaTestRefValue, 0.5f, "GERasterizer::SetAlphaTestingRefValue writes alphaTestRefValue (0x2c0)");
    ((fn_v_uu)oracle_export("?SetAntiAliasing@GERasterizer@@UAEXKK@Z"))(r, 13, 0);
    CHECK(rec->antiAliasing == 13, "GERasterizer::SetAntiAliasing writes antiAliasing (0x2f0)");
    ((fn_v_uuu)oracle_export("?SetBlending@GERasterizer@@UAEXKKK@Z"))(r, 14, 15, 0);
    CHECK(rec->blendSource == 14 && rec->blendDest == 15, "GERasterizer::SetBlending writes blendSource (0x2f4) and blendDest (0x2f8)");
    ((fn_v_ppu)oracle_export("?GetBlending@GERasterizer@@QBEXAAK0K@Z"))(r, &src, &dst, 0);
    CHECK(src == 14 && dst == 15, "GERasterizer::GetBlending reads 0x2f4 and 0x2f8");
    ((fn_v_uu)oracle_export("?SetColorKeying@GERasterizer@@UAEXKK@Z"))(r, 16, 0);
    CHECK(rec->colorKeying == 16, "GERasterizer::SetColorKeying writes colorKeying (0x2fc)");
    ((fn_v_uu)oracle_export("?SetDithering@GERasterizer@@UAEXKK@Z"))(r, 17, 0);
    CHECK(rec->dithering == 17, "GERasterizer::SetDithering writes dithering (0x300)");
    ((fn_v_uu)oracle_export("?SetDrawStyle@GERasterizer@@UAEXKK@Z"))(r, 18, 0);
    CHECK(rec->drawStyle == 18, "GERasterizer::SetDrawStyle writes drawStyle (0x304)");
    ((fn_v_uu)oracle_export("?SetFog@GERasterizer@@UAEXKK@Z"))(r, 19, 0);
    CHECK(rec->fog == 19, "GERasterizer::SetFog writes fog (0x308)");
    ((fn_v_uu)oracle_export("?SetLighting@GERasterizer@@UAEXKK@Z"))(r, 20, 0);
    CHECK(rec->lighting == 20, "GERasterizer::SetLighting writes lighting (0x30c)");
    ((fn_v_uu)oracle_export("?SetProjectionMode@GERasterizer@@UAEXKK@Z"))(r, 21, 0);
    CHECK(rec->projectionMode == 21, "GERasterizer::SetProjectionMode writes projectionMode (0x310)");
    ((fn_v_uu)oracle_export("?SetShading@GERasterizer@@UAEXKK@Z"))(r, 22, 0);
    CHECK(rec->shading == 22, "GERasterizer::SetShading writes shading (0x314)");
    ((fn_v_uu)oracle_export("?SetShadingColorSource@GERasterizer@@UAEXKK@Z"))(r, 23, 0);
    CHECK(rec->shadingColorSource == 23, "GERasterizer::SetShadingColorSource writes shadingColorSource (0x318)");
    ((fn_v_uu)oracle_export("?SetSpecular@GERasterizer@@UAEXKK@Z"))(r, 24, 0);
    CHECK(rec->specular == 24, "GERasterizer::SetSpecular writes specular (0x31c)");
    ((fn_v_uu)oracle_export("?SetZBuffer@GERasterizer@@UAEXKK@Z"))(r, 25, 0);
    CHECK(rec->zBuffer == 25, "GERasterizer::SetZBuffer writes zBuffer (0x320)");
    ((fn_v_uu)oracle_export("?SetZCompare@GERasterizer@@UAEXKK@Z"))(r, 26, 0);
    CHECK(rec->zCompare == 26, "GERasterizer::SetZCompare writes zCompare (0x324)");
    ((fn_v_pu)oracle_export("?SetFogColor@GERasterizer@@UAEXABVGEColor@@K@Z"))(r, &fog, 0);
    CHECK(FEQ(rec->fogColor.r, 0.1f) && FEQ(rec->fogColor.b, 0.3f), "GERasterizer::SetFogColor writes fogColor (0x288)");
    ((fn_v_ffu)oracle_export("?SetFogRange@GERasterizer@@UAEXMMK@Z"))(r, 10.0f, 20.0f, 0);
    CHECK(FEQ(rec->fogStart, 10.0f) && FEQ(rec->fogEnd, 20.0f), "GERasterizer::SetFogRange writes fogStart (0x29c) and fogEnd (0x2a0)");
    ((fn_v_uu)oracle_export("?SetFogType@GERasterizer@@UAEXKK@Z"))(r, 5, 0);
    ((fn_v_fu)oracle_export("?SetFogValue@GERasterizer@@UAEXMK@Z"))(r, 0.75f, 0);
    memset(fog_attributes, 0, sizeof(fog_attributes));
    ((fn_v_pu)oracle_export("?GetFogAttributes@GERasterizer@@QBEXAAUGERFogAttributes@@K@Z"))(r, fog_attributes, 0);
    {
        uint32_t t;
        memcpy(&t, &fog_attributes[3], 4);
        CHECK(t == 5 && rec->fogType == 5, "GERasterizer::SetFogType writes fogType (0x294)");
        CHECK(FEQ(fog_attributes[4], 0.75f) && FEQ(rec->fogValue, 0.75f), "GERasterizer::SetFogValue writes fogValue (0x298)");
    }

    rec->screen = (struct GEScreenLayout *)0x9100;
    rec->primarySurface = (struct GESurfaceLayout *)0x9200;
    rec->secondarySurface = (struct GESurfaceLayout *)0x9300;
    rec->renderTarget = (struct GESurfaceLayout *)0x9400;
    rec->window = (struct GEWindowLayout *)0x9500;
    rec->activated = 1;
    CHECK(((fn_p_u)oracle_export("?GetScreen@GERasterizer@@QBEPAVGEScreen@@K@Z"))(r, 0) == (void *)0x9100, "GERScreenState.screen is at 0x2a8");
    CHECK(((fn_p_u)oracle_export("?GetPrimarySurface@GERasterizer@@QBEPAVGESurface@@K@Z"))(r, 0) == (void *)0x9200, "GERScreenState.primarySurface is at 0x2ac");
    CHECK(((fn_p_u)oracle_export("?GetSecondarySurface@GERasterizer@@QBEPAVGESurface@@K@Z"))(r, 0) == (void *)0x9300, "GERScreenState.secondarySurface is at 0x2b0");
    CHECK(((fn_p_u)oracle_export("?GetRenderTarget@GERasterizer@@QBEPAVGESurface@@K@Z"))(r, 0) == (void *)0x9400, "GERScreenState.renderTarget is at 0x2b4");
    CHECK(((fn_p_u)oracle_export("?GetWindow@GERasterizer@@QBEPAVGEWindow@@K@Z"))(r, 0) == (void *)0x9500, "GERScreenState.window is at 0x2bc");
    CHECK(((fn_b_u)oracle_export("?GetIsScreenActivated@GERasterizer@@QBE_NK@Z"))(r, 0) == 1, "GERScreenState.activated is at 0x33d");
    rec->polysPerFrame = 1.0f;
    rec->polysPerSecond = 2.0f;
    rec->framesPerSecond = 3.0f;
    rec->scenePolysPerFrame = 4.0f;
    rec->scenePolysPerSecond = 5.0f;
    rec->textureChangesPerFrame = 6.0f;
    rec->polyAreaPerFrame = 7.0f;
    rec->polyAreaPerSecond = 8.0f;
    CHECK_F(((fn_f_u)oracle_export("?GetDrawnPolysPerFrame@GERasterizer@@QBEMK@Z"))(r, 0), 1.0f, "GERScreenState.polysPerFrame is at 0x2d0");
    CHECK_F(((fn_f_u)oracle_export("?GetDrawnPolysPerSecond@GERasterizer@@QBEMK@Z"))(r, 0), 2.0f, "GERScreenState.polysPerSecond is at 0x2d4");
    CHECK_F(((fn_f_u)oracle_export("?GetFramesPerSecond@GERasterizer@@QBEMK@Z"))(r, 0), 3.0f, "GERScreenState.framesPerSecond is at 0x2d8");
    CHECK_F(((fn_f_u)oracle_export("?GetScenePolysPerFrame@GERasterizer@@QBEMK@Z"))(r, 0), 4.0f, "GERScreenState.scenePolysPerFrame is at 0x2dc");
    CHECK_F(((fn_f_u)oracle_export("?GetScenePolysPerSecond@GERasterizer@@QBEMK@Z"))(r, 0), 5.0f, "GERScreenState.scenePolysPerSecond is at 0x2e0");
    CHECK_F(((fn_f_u)oracle_export("?GetTotalTextureChangesPerFrame@GERasterizer@@QBEMK@Z"))(r, 0), 6.0f, "GERScreenState.textureChangesPerFrame is at 0x2e4");
    CHECK_F(((fn_f_u)oracle_export("?GetDrawnPolyAreaPerFrame@GERasterizer@@QBEMK@Z"))(r, 0), 7.0f, "GERScreenState.polyAreaPerFrame is at 0x2c8");
    CHECK_F(((fn_f_u)oracle_export("?GetDrawnPolyAreaPerSecond@GERasterizer@@QBEMK@Z"))(r, 0), 8.0f, "GERScreenState.polyAreaPerSecond is at 0x2cc");
    free(rec);
}

void oracle_rasterizer_terrain(void)
{
    rasterizer_base();
    screen_state();
    oracle_terrain();
}
