/*
 * GETexture, GEMaterial and GELight: the objects behind SetTexture, DrawModelHierarchy and UpdateLight.
 * Engine setters and constructors write the objects, engine accessors read what the layout wrote.
 */
#include "oracle.h"

#include <string.h>

/* ---- GETexture ------------------------------------------------------------------------------- */

static void texture_getters(void)
{
    GETextureLayout t, base;
    memset(&t, 0, sizeof(t));
    memset(&base, 0, sizeof(base));

    t.stage.texture = &base;
    t.baseTexture = &base;
    t.frameUVSize = 0.5f;
    t.frameTopLeft.u = 0.25f;
    t.frameTopLeft.v = 0.75f;
    t.frameFormat = 3;
    t.frame = 4;
    t.frameCount = 5;
    t.mipLevelCount = 6;
    t.numMipLevelsToDrop = 7;
    t.screen = (void *)0x1111;
    t.surface = (void *)0x2222;
    base.userData = (void *)0x3333;
    base.surface = (void *)0x4444;
    base.isCompressed = 1;
    base.isPromoted = 1;
    t.hasChanged = 1;
    t.createMIPMaps = 1;

    CHECK(((fn_p_u)oracle_export("?GetStageTexture@GETexture@@QBEPAV1@K@Z"))(&t, 0) == (void *)&base, "GETexture.stage.texture is at 0xc");
    CHECK(((fn_p_v)oracle_export("?GetStage@GETexture@@QBEPBUGETStage@@XZ"))(&t) == (void *)&t.stage, "GETexture.stage (GETStage) starts at 0xc");
    CHECK(((fn_p_v)oracle_export("?GetFrameTopLeftCorner@GETexture@@QBEABVGETextureCoordinate@@XZ"))(&t) == (void *)&t.frameTopLeft, "GETexture.frameTopLeft is at 0x2c");
    CHECK_F(((fn_f_v)oracle_export("?GetFrameUVSize@GETexture@@QBEMXZ"))(&t), 0.5f, "GETexture.frameUVSize is at 0x4c");
    CHECK_F(((fn_u_v)oracle_export("?GetFrameFormat@GETexture@@ABEKXZ"))(&t), 3, "GETexture.frameFormat is at 0x50");
    CHECK_F(((fn_u_v)oracle_export("?GetFrame@GETexture@@QBEKXZ"))(&t), 4, "GETexture.frame is at 0x54");
    CHECK_F(((fn_u_v)oracle_export("?GetFrameCount@GETexture@@QBEKXZ"))(&t), 5, "GETexture.frameCount is at 0x58");
    CHECK_F(((fn_u_v)oracle_export("?GetMipLevelCount@GETexture@@QBEKXZ"))(&t), 6, "GETexture.mipLevelCount is at 0x60");
    CHECK_F(((fn_u_v)oracle_export("?GetNumMipLevelsToDrop@GETexture@@QBEKXZ"))(&t), 7, "GETexture.numMipLevelsToDrop is at 0x64");
    CHECK(((fn_p_v)oracle_export("?GetScreen@GETexture@@QBEPAVGEScreen@@XZ"))(&t) == (void *)0x1111, "GETexture.screen is at 0x34");
    CHECK(((fn_p_v)oracle_export("?GetSurface@GETexture@@QBEPAVGESurface@@XZ"))(&t) == (void *)0x2222, "GETexture.surface is at 0x38");
    CHECK(((fn_p_v)oracle_export("?GetData@GETexture@@QAEPAXXZ"))(&t) == (void *)0x3333, "GETexture.baseTexture (0x3c) -> userData (0x48)");
    CHECK(((fn_p_v)oracle_export("?GetFrameSurface@GETexture@@QBEPAVGESurface@@XZ"))(&t) == (void *)0x4444, "GETexture.baseTexture (0x3c) -> surface (0x38)");
    CHECK(((fn_b_v)oracle_export("?GetIsCompressed@GETexture@@QBE_NXZ"))(&t) == 1, "GETexture.baseTexture -> isCompressed (0x68)");
    CHECK(((fn_b_v)oracle_export("?GetIsPromoted@GETexture@@QBE_NXZ"))(&t) == 1, "GETexture.baseTexture -> isPromoted (0x69)");
    CHECK(((fn_b_v)oracle_export("?HasChanged@GETexture@@QBE_NXZ"))(&t) == 1, "GETexture.hasChanged is at 0x6a");
    CHECK(((fn_b_v)oracle_export("?GetCreateMIPMaps@GETexture@@QBE_NXZ"))(&t) == 1, "GETexture.createMIPMaps is at 0x6b");
}

static void texture_frames(void)
{
    GETextureLayout t, f0, f1;
    struct GETextureLayout *frames[2];
    memset(&t, 0, sizeof(t));
    frames[0] = &f0;
    frames[1] = &f1;
    t.frames = frames;
    t.frameFormat = 0; /* an animated texture: one texture per frame */
    CHECK(((fn_p_u)oracle_export("?GetFrameTexture@GETexture@@QBEPAV1@K@Z"))(&t, 1) == (void *)&f1, "GETexture.frames (0x40) indexes the frame textures when frameFormat is 0");
    t.frameFormat = 1;
    CHECK(((fn_p_u)oracle_export("?GetFrameTexture@GETexture@@QBEPAV1@K@Z"))(&t, 1) == (void *)&f0, "GETexture.frames (0x40) holds the single frame texture when frameFormat is not 0");
}

static void texture_setters(void)
{
    GETextureLayout t;
    memset(&t, 0, sizeof(t));
    t.frameFormat = 1; /* a single-frame texture keeps the stage state in itself */

    ((fn_v_uu)oracle_export("?SetStageColorMode@GETexture@@QAEXKK@Z"))(&t, 7, 0);
    CHECK(t.stage.colorMode == 7 && t.hasChanged == 1, "GETexture::SetStageColorMode writes stage.colorMode (0x10) and sets hasChanged");
    t.hasChanged = 0;
    ((fn_v_uu)oracle_export("?SetStageAlphaMode@GETexture@@QAEXKK@Z"))(&t, 8, 0);
    CHECK(t.stage.alphaMode == 8, "GETexture::SetStageAlphaMode writes stage.alphaMode (0x14)");
    ((fn_v_uu)oracle_export("?SetStageTextureAddressingMode@GETexture@@QAEXKK@Z"))(&t, 9, 0);
    CHECK(t.stage.addressingMode == 9, "GETexture::SetStageTextureAddressingMode writes stage.addressingMode (0x18)");
    ((fn_v_uu)oracle_export("?SetStageMinFilterMode@GETexture@@QAEXKK@Z"))(&t, 10, 0);
    CHECK(t.stage.minFilter == 10, "GETexture::SetStageMinFilterMode writes stage.minFilter (0x1c)");
    ((fn_v_uu)oracle_export("?SetStageMagFilterMode@GETexture@@QAEXKK@Z"))(&t, 11, 0);
    CHECK(t.stage.magFilter == 11, "GETexture::SetStageMagFilterMode writes stage.magFilter (0x20)");
    ((fn_v_uu)oracle_export("?SetStageMipFilterMode@GETexture@@QAEXKK@Z"))(&t, 12, 0);
    CHECK(t.stage.mipFilter == 12, "GETexture::SetStageMipFilterMode writes stage.mipFilter (0x24)");
    ((fn_v_fu)oracle_export("?SetStageMipMapBias@GETexture@@QAEXMK@Z"))(&t, 1.5f, 0);
    CHECK_F(t.stage.mipMapBias, 1.5f, "GETexture::SetStageMipMapBias writes stage.mipMapBias (0x28)");
    ((fn_v_pu)oracle_export("?SetStageTexture@GETexture@@QAEXPAV1@K@Z"))(&t, &t, 0);
    CHECK(t.stage.texture == &t, "GETexture::SetStageTexture writes stage.texture (0xc)");

    t.hasChanged = 1;
    ((fn_v_v)oracle_export("?ValidateChanges@GETexture@@QAEXXZ"))(&t);
    CHECK(t.hasChanged == 0, "GETexture::ValidateChanges clears hasChanged (0x6a)");
    ((fn_v_v)oracle_export("?SetHasChanged@GETexture@@QAEXXZ"))(&t);
    CHECK(t.hasChanged == 1, "GETexture::SetHasChanged sets hasChanged (0x6a)");
    t.baseTexture = &t;
    ((fn_v_p)oracle_export("?SetData@GETexture@@QAEXPAX@Z"))(&t, (void *)0x5555);
    CHECK(t.userData == (void *)0x5555, "GETexture::SetData writes userData (0x48) through baseTexture (0x3c)");
}

static void texture_copy_constructor(void)
{
    union {
        GETextureLayout t;
        uint8_t bytes[sizeof(GETextureLayout) + 16];
    } src, dst;
    size_t i;
    memset(&src, 0, sizeof(src));
    for (i = 4; i < sizeof(GETextureLayout); i++) {
        src.bytes[i] = (uint8_t)(i * 7 + 1);
    }
    oracle_guard_fill(&dst, sizeof(dst));
    ((fn_p_p)oracle_export("??0GETexture@@QAE@ABV0@@Z"))(&dst, &src);
    CHECK(memcmp(dst.bytes + 4, src.bytes + 4, sizeof(GETextureLayout) - 4) == 0, "GETexture's copy constructor copies every byte of 0x4..0x6b");
    CHECK(oracle_guard_intact(&dst, sizeof(GETextureLayout), sizeof(dst)), "GETexture's copy constructor stops at 0x6c: the object is 0x6c bytes");
    CHECK(dst.t.vptr == oracle_vftable("GETexture"), "GETexture.vptr (0x0) is the engine's GETexture vftable");
}

/* ---- GEMaterial ------------------------------------------------------------------------------ */

static void material(void)
{
    union {
        GEMaterialLayout m;
        uint8_t bytes[sizeof(GEMaterialLayout) + 16];
    } buf, copy;
    GEMaterialLayout *m = &buf.m;
    GEColorLayout color = {0.1f, 0.2f, 0.3f};
    oracle_guard_fill(&buf, sizeof(buf));
    ((fn_p_uu)oracle_export("??0GEMaterial@@AAE@KK@Z"))(m, 3, 2); /* three textures, two levels of detail */
    CHECK(oracle_guard_intact(&buf, sizeof(GEMaterialLayout), sizeof(buf)), "GEMaterial's constructor stays inside 0x48 bytes");
    CHECK(m->vptr == oracle_vftable("GEMaterial"), "GEMaterial.vptr (0x0) is the engine's GEMaterial vftable");
    CHECK(m->refCount == 1, "GEMaterial.refCount (0x4) starts at 1");
    CHECK(m->color.r == 1.0f && m->color.g == 1.0f && m->color.b == 1.0f, "GEMaterial.color (0x18) starts white");
    CHECK(m->textureCount == 3, "GEMaterial.textureCount (0x28) is the first constructor argument");
    CHECK(m->lodCount == 2, "GEMaterial.lodCount (0x40) is the second constructor argument");
    CHECK(m->opacity == 1.0f, "GEMaterial.opacity (0x44) starts at 1.0");
    CHECK(m->textures && m->polygonStart && m->polygonLength, "GEMaterial's constructor allocates the arrays at 0x24, 0x2c and 0x30");

    ((fn_v_p)oracle_export("?SetColor@GEMaterial@@QAEXABVGEColor@@@Z"))(m, &color);
    CHECK(m->color.r == 0.1f && m->color.b == 0.3f, "GEMaterial::SetColor writes color (0x18)");
    ((fn_v_f)oracle_export("?SetOpacity@GEMaterial@@QAEXM@Z"))(m, 0.5f);
    CHECK_F(m->opacity, 0.5f, "GEMaterial::SetOpacity writes opacity (0x44)");
    ((fn_v_u)oracle_export("?SetTextureIndex@GEMaterial@@QAEXK@Z"))(m, 9);
    CHECK(m->textureIndex == 9, "GEMaterial::SetTextureIndex writes textureIndex (0x3c)");
    CHECK_F(((fn_f_v)oracle_export("?GetOpacity@GEMaterial@@QAEMXZ"))(m), 0.5f, "GEMaterial::GetOpacity reads opacity (0x44)");
    CHECK(((fn_p_v)oracle_export("?GetColor@GEMaterial@@QAEAAVGEColor@@XZ"))(m) == (void *)&m->color, "GEMaterial.color is at 0x18");
    CHECK(((fn_p_v)oracle_export("?GetName@GEMaterial@@QAEAAVUString@@XZ"))(m) == (void *)m->name, "GEMaterial.name (a UString) is at 0x8");

    m->textures[1] = (struct GETextureLayout *)0x7777;
    m->polygonStart[1] = 70;
    m->polygonLength[1] = 71;
    m->vertexStart = 72;
    m->vertexLength = 73;
    CHECK(((fn_p_u)oracle_export("?GetTexture@GEMaterial@@QBEPAVGETexture@@K@Z"))(m, 1) == (void *)0x7777, "GEMaterial.textures (0x24) is indexed by texture");
    CHECK_F(((fn_u_u)oracle_export("?GetPolygonStartingIndex@GEMaterial@@QAEKK@Z"))(m, 1), 70, "GEMaterial.polygonStart (0x2c) is indexed by level of detail");
    CHECK_F(((fn_u_u)oracle_export("?GetPolygonRangeLength@GEMaterial@@QAEKK@Z"))(m, 1), 71, "GEMaterial.polygonLength (0x30) is indexed by level of detail");
    CHECK_F(((fn_u_v)oracle_export("?GetVertexStartingIndex@GEMaterial@@QAEKXZ"))(m), 72, "GEMaterial.vertexStart is at 0x34");
    CHECK_F(((fn_u_v)oracle_export("?GetVertexRangeLength@GEMaterial@@QAEKXZ"))(m), 73, "GEMaterial.vertexLength is at 0x38");
    CHECK_F(((fn_u_v)oracle_export("?GetLODCount@GEMaterial@@QAEKXZ"))(m), 2, "GEMaterial.lodCount is at 0x40");
    CHECK_F(((fn_u_v)oracle_export("?GetTextureIndex@GEMaterial@@QAEKXZ"))(m), 9, "GEMaterial.textureIndex is at 0x3c");

    oracle_guard_fill(&copy, sizeof(copy));
    ((fn_p_p)oracle_export("??0GEMaterial@@QAE@ABV0@@Z"))(&copy, m);
    CHECK(oracle_guard_intact(&copy, sizeof(GEMaterialLayout), sizeof(copy)), "GEMaterial's copy constructor stops at 0x48: the object is 0x48 bytes");
    CHECK(memcmp(copy.bytes + 0x18, buf.bytes + 0x18, 0x48 - 0x18) == 0, "GEMaterial's copy constructor copies every byte of 0x18..0x47");
    ((fn_v_v)oracle_export("??1GEMaterial@@MAE@XZ"))(m);
}

/* ---- GELight --------------------------------------------------------------------------------- */

static void light(void)
{
    union {
        GELightLayout l;
        uint8_t bytes[sizeof(GELightLayout) + 16];
    } buf, copy;
    GELightLayout *l = &buf.l;
    GEColorLayout color = {1.0f, 0.5f, 0.25f};
    GEVectorLayout direction = {0.0f, 0.0f, 1.0f};
    GE3DPointLayout location = {1.0f, 2.0f, 3.0f};
    oracle_guard_fill(&buf, sizeof(buf));
    ((fn_v_v)oracle_export("??0GELight@@QAE@XZ"))(l);
    CHECK(oracle_guard_intact(&buf, sizeof(GELightLayout), sizeof(buf)), "GELight's constructor stays inside 0x74 bytes (it has no vptr)");
    CHECK(l->color.r == 1.0f && l->computedColor.r == 1.0f && l->intensity == 1.0f && l->attenuation0 == 1.0f,
          "GELight starts white at intensity 1 with attenuation 1,0,0");

    ((fn_v_f)oracle_export("?SetIntensity@GELight@@QAEXM@Z"))(l, 0.5f);
    ((fn_v_p)oracle_export("?SetColor@GELight@@QAEXABVGEColor@@@Z"))(l, &color);
    CHECK(l->intensity == 0.5f, "GELight::SetIntensity writes intensity (0x54)");
    CHECK(l->color.r > l->color.b && l->color.b > 0.0f, "GELight::SetColor writes the normalized color (0x0)");
    CHECK(l->computedColor.r == 0.5f && l->computedColor.g == 0.25f && l->computedColor.b == 0.125f,
          "GELight::SetColor writes computedColor = the given color * intensity (0xc)");
    CHECK(((fn_p_v)oracle_export("?GetColor@GELight@@QBEABVGEColor@@XZ"))(l) == (void *)&l->color, "GELight.color is at 0x0");
    CHECK(((fn_p_v)oracle_export("?GetComputedColor@GELight@@QBEABVGEColor@@XZ"))(l) == (void *)&l->computedColor, "GELight.computedColor is at 0xc");

    ((fn_v_p)oracle_export("?SetLocation@GELight@@QAEXABVGE3DPoint@@@Z"))(l, &location);
    CHECK(l->location.x == 1.0f && l->location.z == 3.0f, "GELight::SetLocation writes location (0x18)");
    CHECK(((fn_p_v)oracle_export("?GetLocation@GELight@@QBEABVGE3DPoint@@XZ"))(l) == (void *)&l->location, "GELight.location is at 0x18");
    CHECK(((fn_p_v)oracle_export("?GetModelSpaceLocation@GELight@@QAEAAVGE3DPoint@@XZ"))(l) == (void *)&l->modelSpaceLocation, "GELight.modelSpaceLocation is at 0x24");
    ((fn_v_p)oracle_export("?SetDirection@GELight@@QAEXABVGEVector@@@Z"))(l, &direction);
    CHECK(l->direction.z == 1.0f, "GELight::SetDirection writes direction (0x30)");
    CHECK(((fn_p_v)oracle_export("?GetDirection@GELight@@QBEABVGEVector@@XZ"))(l) == (void *)&l->direction, "GELight.direction is at 0x30");
    CHECK(((fn_p_v)oracle_export("?GetModelSpaceDirection@GELight@@QAEAAVGEVector@@XZ"))(l) == (void *)&l->modelSpaceDirection, "GELight.modelSpaceDirection is at 0x3c");

    ((fn_v_f)oracle_export("?SetAttenuation1@GELight@@QAEXM@Z"))(l, 0.125f);
    ((fn_v_f)oracle_export("?SetAttenuation2@GELight@@QAEXM@Z"))(l, 0.0625f);
    CHECK(l->attenuation1 == 0.125f && l->attenuation2 == 0.0625f, "GELight::SetAttenuation1/2 write attenuation1 (0x4c) and attenuation2 (0x50)");
    CHECK_F(((fn_f_v)oracle_export("?GetAttenuation0@GELight@@QBEMXZ"))(l), l->attenuation0, "GELight.attenuation0 is at 0x48");
    ((fn_v_f)oracle_export("?SetRange@GELight@@QAEXM@Z"))(l, 4.0f);
    CHECK(l->range == 4.0f && l->rangeInverse == 0.25f && l->rangeSquared == 16.0f, "GELight::SetRange writes range (0x58), rangeInverse (0x5c), rangeSquared (0x60)");
    ((fn_v_f)oracle_export("?SetModelSpaceRange@GELight@@QAEXM@Z"))(l, 2.0f);
    CHECK(l->modelSpaceRange == 2.0f && l->modelSpaceRangeInverse == 0.5f && l->modelSpaceRangeSquared == 4.0f,
          "GELight::SetModelSpaceRange writes 0x64, 0x68, 0x6c");
    ((fn_v_u)oracle_export("?SetStyle@GELight@@QAEXK@Z"))(l, 2);
    CHECK(l->style == 2, "GELight::SetStyle writes style (0x70)");
    CHECK_F(((fn_u_v)oracle_export("?GetStyle@GELight@@QBEKXZ"))(l), 2, "GELight.style is at 0x70");
    CHECK_F(((fn_f_v)oracle_export("?GetRange@GELight@@QBEMXZ"))(l), 4.0f, "GELight.range is at 0x58");
    CHECK_F(((fn_f_v)oracle_export("?GetIntensity@GELight@@QBEMXZ"))(l), 0.5f, "GELight.intensity is at 0x54");

    oracle_guard_fill(&copy, sizeof(copy));
    ((fn_p_p)oracle_export("??0GELight@@QAE@ABV0@@Z"))(&copy, l);
    CHECK(oracle_guard_intact(&copy, sizeof(GELightLayout), sizeof(copy)), "GELight's copy constructor stops at 0x74: the object is 0x74 bytes");
    CHECK(memcmp(&copy, &buf, sizeof(GELightLayout)) == 0, "GELight's copy constructor copies all of 0x0..0x73");
}

void oracle_texture_material_light(void)
{
    texture_getters();
    texture_frames();
    texture_setters();
    texture_copy_constructor();
    material();
    light();
}
