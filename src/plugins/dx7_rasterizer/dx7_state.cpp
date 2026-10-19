// The slots of the rasterizer that only set Direct3D 7 device state (task P2.6.4): render state, the ambient
// colour, fog, shading, culling, depth, blending, the viewport, the lights and the capability getters.
//
// Each one follows the original's DX7Rasterizer function for function:
//   - the engine's per-screen state (GERScreenStateLayout, kept by the engine's own GERasterizer::SetX, which
//     every setter calls last) is what a call is compared with, so asking for what the screen already has sends
//     nothing to the device;
//   - a few settings also keep a cache in the plugin's screen record (DX7ScreenRecordLayout) that is compared too;
//   - most setters call the engine only when the device accepted the value, so a refused state is asked for again.
// The odd things the original does are kept and pinned by tests/dx7_rasterizer/state_checks.h: SetAlphaTestingCompareState
// compares with the alpha-testing field, SetBlending's destination factor keeps the source's value when it is not one
// of the 13 known modes, the colours are packed without clamping.
//
// Where the original would dereference a device that the screen's activation (dx7_scene.cpp) has not created yet, this build
// does nothing (the original would crash).
#include "null_rasterizer.h"

#include "engine_imports.h"
#include "dx7_math.h"
#include "rasterizer_abi.h"

typedef unsigned long ulong;

using namespace dx7math;

namespace {

// IDirect3DDevice7::SetRenderState states used here (D3DRENDERSTATETYPE).
enum {
    kAntialias = 2,
    kZEnable = 7,
    kFillMode = 8,
    kShadeMode = 9,
    kZWriteEnable = 0xe,
    kAlphaTestEnable = 0xf,
    kSrcBlend = 0x13,
    kDestBlend = 0x14,
    kCullMode = 0x16,
    kZFunc = 0x17,
    kAlphaRef = 0x18,
    kAlphaFunc = 0x19,
    kDitherEnable = 0x1a,
    kAlphaBlendEnable = 0x1b,
    kFogEnable = 0x1c,
    kSpecularEnable = 0x1d,
    kFogColor = 0x22,
    kClipping = 0x88,
    kFogStart = 0x24,
    kFogEnd = 0x25,
    kColorKeyEnable = 0x29,
    kLighting = 0x89,
    kAmbient = 0x8b,
    kColorVertex = 0x8d,
    kSpecularMaterialSource = 0x91,
    kDiffuseMaterialSource = 0x93,
};

// D3DTRANSFORMSTATETYPE.
enum {
    kWorldTransform = 1,
    kViewTransform = 2,
    kProjectionTransform = 3,
};

long SetState(IDirect3DDevice7* device, ulong state, ulong value)
{
    return device->vtbl->SetRenderState(device, state, value);
}

ulong FloatBits(float value)
{
    ulong bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The engine's compare functions (0..7) as D3DCMPFUNC: the order of the original's switch.  Anything else sends 0.
ulong CompareFunction(ulong mode)
{
    static const ulong kFunctions[8] = {1, 2, 4, 3, 6, 7, 5, 8};
    return mode < 8 ? kFunctions[mode] : 0;
}

// A channel in 0..1 as the original's /QIfist code converts it: scaled by 255 in single precision and rounded to
// nearest even; nothing clamps it, so a value above 1 spills into the next channel of the packed colour.
ulong RoundedChannel(float channel)
{
    volatile float scaled = channel * 255.0f;
    return static_cast<ulong>(RoundToNearest(scaled));
}

// 0xAARRGGBB with alpha 255, three rounded channels: the packing of the ambient and the debug colour.
ulong PackRounded(float red, float green, float blue)
{
    ulong packed = static_cast<ulong>(RoundToNearest(255.0f));
    packed = (packed << 8) | RoundedChannel(red);
    packed = (packed << 8) | RoundedChannel(green);
    packed = (packed << 8) | RoundedChannel(blue);
    return packed;
}


// Eight lights shared by every screen (the original's table at 0x10017d2c): 1 = taken by AddLight.
unsigned char g_lightTaken[8];

} // namespace

// ---- simple toggles -------------------------------------------------------------------------------------------

void EEWebNullRasterizer::SetAlphaTesting(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->alphaTesting) return;
    if (SetState(device, kAlphaTestEnable, mode) == 0) {
        GERasterizer::SetAlphaTesting(mode, screen);
    }
}

void EEWebNullRasterizer::SetAlphaTestingCompareState(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    // The original compares with the alpha-testing mode here, not with the compare state it is about to set.
    if (!state || !device || mode == state->alphaTesting) return;
    if (SetState(device, kAlphaFunc, CompareFunction(mode)) == 0) {
        GERasterizer::SetAlphaTestingCompareState(mode, screen);
    }
}

void EEWebNullRasterizer::SetAlphaTestingRefValue(float reference, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || reference == state->alphaTestRefValue) return;
    if (SetState(device, kAlphaRef, TruncatedChannel(reference)) == 0) {
        GERasterizer::SetAlphaTestingRefValue(reference, screen);
    }
}

void EEWebNullRasterizer::SetAntiAliasing(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->antiAliasing) return;
    if (SetState(device, kAntialias, mode == 1 ? 2 : 0) == 0) {
        GERasterizer::SetAntiAliasing(mode, screen);
    }
}

void EEWebNullRasterizer::SetColorKeying(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->colorKeying) return;
    if (SetState(device, kColorKeyEnable, mode == 1) == 0) {
        GERasterizer::SetColorKeying(mode, screen);
    }
}

void EEWebNullRasterizer::SetDithering(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->dithering) return;
    if (SetState(device, kDitherEnable, mode == 1) == 0) {
        GERasterizer::SetDithering(mode, screen);
    }
}

void EEWebNullRasterizer::SetDrawStyle(ulong style, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || style == state->drawStyle) return;
    // The engine's styles 0, 1, 2 are D3DFILL_SOLID (3), WIREFRAME (2) and POINT (1) in reverse; others send 0.
    const ulong fill = style == 0 ? 3 : style == 1 ? 2 : style == 2 ? 1 : 0;
    if (SetState(device, kFillMode, fill) == 0) {
        GERasterizer::SetDrawStyle(style, screen);
    }
}

void EEWebNullRasterizer::SetFog(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->fog) return;
    if (SetState(device, kFogEnable, mode == 1) == 0) {
        GERasterizer::SetFog(mode, screen);
    }
}

void EEWebNullRasterizer::SetLighting(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->lighting) return;
    SetState(device, kLighting, mode == 1); // the result is not looked at
    GERasterizer::SetLighting(mode, screen);
}

void EEWebNullRasterizer::SetShading(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->shading) return;
    if (SetState(device, kShadeMode, (mode == 1) + 1) == 0) { // D3DSHADE_FLAT (1) or GOURAUD (2)
        GERasterizer::SetShading(mode, screen);
    }
}

void EEWebNullRasterizer::SetSpecular(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->specular) return;
    if (SetState(device, kSpecularEnable, mode == 1) == 0) {
        GERasterizer::SetSpecular(mode, screen);
    }
}

void EEWebNullRasterizer::SetZCompare(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || mode == state->zCompare) return;
    if (SetState(device, kZFunc, CompareFunction(mode)) == 0) {
        GERasterizer::SetZCompare(mode, screen);
    }
}

// ---- settings with a cache in the screen record ---------------------------------------------------------------

void EEWebNullRasterizer::SetBlending(ulong source, ulong destination, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !record || !device || (source == state->blendSource && destination == state->blendDest)) return;
    const unsigned char enable = (source != 1 || destination != 0) ? 1 : 0;
    if (record->blendEnabledCache != enable) {
        SetState(device, kAlphaBlendEnable, enable);
        record->blendEnabledCache = enable;
    }
    // The 13 engine blend modes are D3DBLEND_ZERO.. (1..13); another mode sends what the previous factor sent (0 at first).
    ulong value = 0;
    if (record->blendSourceCache != source) {
        if (source <= 12) value = source + 1;
        SetState(device, kSrcBlend, value);
        record->blendSourceCache = source;
    }
    if (record->blendDestCache != destination) {
        if (destination <= 12) value = destination + 1;
        SetState(device, kDestBlend, value);
        record->blendDestCache = destination;
    }
    GERasterizer::SetBlending(source, destination, screen);
}

void EEWebNullRasterizer::SetZBuffer(ulong mode, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !record || !device || mode == state->zBuffer) return;
    const bool write = (mode & 1) != 0;
    const ulong enable = ((mode & 2) == 0 && !write) ? 0 : 1;
    if (record->zEnableCache != enable) {
        SetState(device, kZEnable, enable);
        record->zEnableCache = enable;
    }
    if (record->zWriteCache != (write ? 1 : 0)) {
        SetState(device, kZWriteEnable, write);
        record->zWriteCache = write ? 1 : 0;
    }
    GERasterizer::SetZBuffer(mode, screen);
}

// Culling is one setting for the rasterizer, not per screen: every screen that has a device gets it.
void EEWebNullRasterizer::SetCulling(ulong mode)
{
    if (mode == Base()->culling) return;
    const ulong value = mode == 1 ? 2 : mode == 2 ? 3 : 1; // D3DCULL_CW (2), CCW (3) or NONE (1)
    for (ulong i = 0; i < RecordCount(); ++i) {
        IDirect3DDevice7* device = Device(i);
        if (device) SetState(device, kCullMode, value);
    }
    GERasterizer::SetCulling(mode);
}

// ---- colours: ambient, debug, fog ------------------------------------------------------------------------------

void EEWebNullRasterizer::SetAmbientColor(GEColor const& color, ulong screen)
{
    GERasterizerLayout* base = Base();
    const GEColorLayout& c = reinterpret_cast<const GEColorLayout&>(color);
    base->ambientColor = c;
    base->computedAmbientColor.r = c.r * base->ambientIntensity;
    base->computedAmbientColor.g = c.g * base->ambientIntensity;
    base->computedAmbientColor.b = c.b * base->ambientIntensity;
    IDirect3DDevice7* device = Device(screen);
    if (device) {
        SetState(device, kAmbient, PackRounded(base->computedAmbientColor.r, base->computedAmbientColor.g, base->computedAmbientColor.b));
    }
}

void EEWebNullRasterizer::SetAmbientIntensity(float intensity, ulong screen)
{
    GERasterizerLayout* base = Base();
    base->ambientIntensity = intensity;
    base->computedAmbientColor.r = base->ambientColor.r * intensity;
    base->computedAmbientColor.g = base->ambientColor.g * intensity;
    base->computedAmbientColor.b = base->ambientColor.b * intensity;
    IDirect3DDevice7* device = Device(screen);
    if (device) {
        SetState(device, kAmbient, PackRounded(base->computedAmbientColor.r, base->computedAmbientColor.g, base->computedAmbientColor.b));
    }
}

void EEWebNullRasterizer::SetDebugColor(GEColor const& color)
{
    const GEColorLayout& c = reinterpret_cast<const GEColorLayout&>(color);
    Own()->debugColor = static_cast<uint32_t>(PackRounded(c.r, c.g, c.b));
}

void EEWebNullRasterizer::SetFogColor(GEColor const& color, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    const GEColorLayout& c = reinterpret_cast<const GEColorLayout&>(color);
    if (!state || !device || (state->fogColor.r == c.r && state->fogColor.g == c.g && state->fogColor.b == c.b)) return;
    // Alpha 255 and three truncated channels; the first channel's mask is ORed in before it is shifted, as in the original.
    ulong packed = (TruncatedChannel(c.r) | 0xffffff00UL) << 8;
    packed = (packed | TruncatedChannel(c.g)) << 8;
    packed |= TruncatedChannel(c.b);
    if (SetState(device, kFogColor, packed) == 0) {
        GERasterizer::SetFogColor(color, screen);
    }
}

void EEWebNullRasterizer::SetFogRange(float start, float end, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device) return;
    if (state->fogStart != start) SetState(device, kFogStart, FloatBits(start));
    if (state->fogEnd != end) SetState(device, kFogEnd, FloatBits(end));
    GERasterizer::SetFogRange(start, end, screen);
}

// Shading with vertex colours: a white material and the vertex colour as diffuse and specular source, or the material alone.
void EEWebNullRasterizer::SetShadingColorSource(ulong source, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !device || source == state->shadingColorSource) return;
    if (source != 0) {
        // D3DMATERIAL7: diffuse and ambient white, specular and emissive black, power 0.
        float material[17] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        device->vtbl->SetMaterial(device, material);
    }
    SetState(device, kColorVertex, source != 0);
    SetState(device, kDiffuseMaterialSource, source != 0);
    if (SetState(device, kSpecularMaterialSource, source != 0) == 0) {
        GERasterizer::SetShadingColorSource(source, screen);
    }
}

// ---- handed on to the engine without a device call -------------------------------------------------------------

void EEWebNullRasterizer::SetDisplayNoTextureMode(bool on, ulong screen)
{
    if (State(screen)) GERasterizer::SetDisplayNoTextureMode(on, screen);
}

void EEWebNullRasterizer::SetDisplayOverdrawMode(bool on, ulong screen)
{
    if (State(screen)) GERasterizer::SetDisplayOverdrawMode(on, screen);
}

void EEWebNullRasterizer::SetFogType(ulong type, ulong screen)
{
    if (State(screen)) GERasterizer::SetFogType(type, screen);
}

void EEWebNullRasterizer::SetFogValue(float value, ulong screen)
{
    if (State(screen)) GERasterizer::SetFogValue(value, screen);
}

void EEWebNullRasterizer::SetProjectionMode(ulong mode, ulong screen)
{
    if (State(screen)) GERasterizer::SetProjectionMode(mode, screen);
}

void EEWebNullRasterizer::SetWaitForVSync(bool wait, ulong screen)
{
    if (State(screen)) GERasterizer::SetWaitForVSync(wait, screen);
}

// ---- the viewport ----------------------------------------------------------------------------------------------

// The engine keeps the viewport; the device gets its rectangle (whole pixels, rounded down) and depth range.
void EEWebNullRasterizer::SetViewport(GEViewport const& viewport, ulong screen)
{
    if (Note(EEWebCheckViewport(&viewport)) != EEWEB_LAYOUT_OK) return;
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !record || !device) return;
    GERasterizer::SetViewport(viewport, screen);
    const GEViewportLayout* v = reinterpret_cast<const GEViewportLayout*>(&viewport);
    float rectangle[4] = {v->viewRect.bottom, v->viewRect.left, v->viewRect.right, v->viewRect.top};
    EEWebRoundRectangle(rectangle);
    volatile float width = rectangle[2] - rectangle[1];
    volatile float height = rectangle[0] - rectangle[3];
    record->viewport[0] = static_cast<uint32_t>(FloorLikeOriginal(rectangle[1]));
    record->viewport[1] = static_cast<uint32_t>(FloorLikeOriginal(rectangle[3]));
    record->viewport[2] = static_cast<uint32_t>(FloorLikeOriginal(width));
    record->viewport[3] = static_cast<uint32_t>(FloorLikeOriginal(height));
    __builtin_memcpy(&record->viewport[4], &v->depthNear, sizeof(float));
    __builtin_memcpy(&record->viewport[5], &v->depthFar, sizeof(float));
    device->vtbl->SetViewport(device, record);
    record->sceneDrawn = 1;
}

// ---- the transformations the draws set up ------------------------------------------------------------------------

// The engine keeps a transformation as 4x4 floats with the translation in the last column (m[3], m[7], m[11]); Direct3D wants the
// transpose.
void EEWebNullRasterizer::ConvertToD3DMatrix(const GETransformationLayout& transformation, float* matrix)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            matrix[row * 4 + column] = transformation.m[column * 4 + row];
        }
    }
}

// Pixel coordinates: no clipping by Direct3D, an identity world and view, and a projection that maps the screen's
// width x height to -1..1 with y down.  Runs when either flag of the record says the last set-up was not this one.
long EEWebNullRasterizer::Set2DMode(ulong screen)
{
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !device) return 0;
    if (record->unknown6c || record->sceneDrawn) {
        SetState(device, kClipping, 1);
        float matrix[16];
        ConvertToD3DMatrix(transformation, matrix);
        device->vtbl->SetTransform(device, kWorldTransform, matrix);
        device->vtbl->SetTransform(device, kViewTransform, matrix);
        transformation.m[3] = -1.0f;
        transformation.m[0] = static_cast<float>(2.0L / static_cast<long double>(record->viewport[2]));
        transformation.m[5] = static_cast<float>(-2.0L / static_cast<long double>(record->viewport[3]));
        transformation.m[7] = 1.0f;
        ConvertToD3DMatrix(transformation, matrix);
        device->vtbl->SetTransform(device, kProjectionTransform, matrix);
        record->unknown6c = 0;
        record->sceneDrawn = 0;
    }
    return 0;
}

// The engine's camera: the view is the camera's inverse with Y and Z swapped (the engine's up axis is Z), the projection
// the viewport's perspective or orthographic matrix by the screen's projection mode.  Runs when the 3D set-up is not the
// current one or a scene was drawn since.
long EEWebNullRasterizer::Set3DMode(ulong screen)
{
    GETransformationLayout swap;
    EEWebTransformationConstruct(&swap);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device) return 0;
    if (!record->unknown6c || record->sceneDrawn) {
        swap.m[5] = 0.0f;
        swap.m[6] = 1.0f;
        swap.m[9] = 1.0f;
        swap.m[10] = 0.0f;
        GETransformationLayout view;
        EEWebTransformationApply(&swap, &state->viewport.cameraInverse, &view);
        float matrix[16];
        ConvertToD3DMatrix(view, matrix);
        device->vtbl->SetTransform(device, kViewTransform, matrix);
        ConvertToD3DMatrix(state->projectionMode == 1 ? state->viewport.perspectiveProjection : state->viewport.orthographicProjection, matrix);
        device->vtbl->SetTransform(device, kProjectionTransform, matrix);
        record->unknown6c = 1;
        record->sceneDrawn = 0;
    }
    return 0;
}

// ---- lights ----------------------------------------------------------------------------------------------------

long EEWebNullRasterizer::AddLight(GELight& light, ulong screen, ulong& id)
{
    if (Note(EEWebCheckLight(&light)) != EEWEB_LAYOUT_OK) return EEWEB_LAYOUT_VIOLATION;
    IDirect3DDevice7* device = Device(screen);
    if (!device) return EEWEB_NOT_SUPPORTED;
    id = 0;
    while (id < 8 && g_lightTaken[id] != 0) ++id;
    if (id >= 8) return 7;
    g_lightTaken[id] = 1;
    const long result = UpdateLight(id, light, screen);
    if (result != 0) return result; // the light stays taken, as in the original
    return device->vtbl->LightEnable(device, id, 1) != 0 ? 1 : 0;
}

long EEWebNullRasterizer::RemoveAllLights(ulong screen)
{
    for (ulong id = 0; id < 8; ++id) {
        RemoveLight(id, screen);
    }
    return 0;
}

long EEWebNullRasterizer::RemoveLight(ulong id, ulong screen)
{
    if (id >= 8 || g_lightTaken[id] != 1) return 9;
    g_lightTaken[id] = 0;
    IDirect3DDevice7* device = Device(screen);
    return device && device->vtbl->LightEnable(device, id, 0) == 0 ? 0 : 1;
}

// The engine's light as a D3DLIGHT7: point (1) or directional (3), the computed colour as diffuse, the position, the
// negated direction, the range and the three attenuations.
long EEWebNullRasterizer::UpdateLight(ulong id, GELight& light, ulong screen)
{
    if (Note(EEWebCheckLight(&light)) != EEWEB_LAYOUT_OK) return EEWEB_LAYOUT_VIOLATION;
    if (id >= 8 || g_lightTaken[id] != 1) return 9;
    IDirect3DDevice7* device = Device(screen);
    if (!device) return 1;
    const GELightLayout* l = reinterpret_cast<const GELightLayout*>(&light);
    float words[26];
    __builtin_memset(words, 0, sizeof words);
    ulong* type = reinterpret_cast<ulong*>(&words[0]);
    *type = l->style != 0 ? 3 : 1;
    words[1] = l->computedColor.r;
    words[2] = l->computedColor.g;
    words[3] = l->computedColor.b;
    words[13] = l->location.x;
    words[14] = l->location.y;
    words[15] = l->location.z;
    words[16] = -l->direction.x;
    words[17] = -l->direction.y;
    words[18] = -l->direction.z;
    words[19] = l->range;
    words[21] = l->attenuation0;
    words[22] = l->attenuation1;
    words[23] = l->attenuation2;
    return device->vtbl->SetLight(device, id, words) != 0 ? 1 : 0;
}

// ---- capability getters ----------------------------------------------------------------------------------------

ulong EEWebNullRasterizer::GetCaps(ulong, ulong)
{
    return 0;
}

bool EEWebNullRasterizer::GetCanModulateTextureAlpha(ulong screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    return record && record->canModulateTextureAlpha;
}

bool EEWebNullRasterizer::GetDirtyRectangleSupport(ulong screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    return record && record->dirtyRectangleSupport;
}
