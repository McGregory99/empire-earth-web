// Textures of the rasterizer (task P2.6.6): SetTexture, with the per-screen stage cache in the screen record (0x2c..0x5f), and
// CanDoCurrentState, which binds a texture and asks the device whether it can draw with it.  (The texture-handle slots 72, 73, 76
// and DestroyTextureHandle 74 are one-liners in null_rasterizer.h: in the original they are `return 0` and an empty function.)
//
// SetTexture follows the original's DX7Rasterizer::SetTexture step for step; tests/dx7_rasterizer/texture_checks.h compares both on
// the same scripted Direct3D device: the calls the device gets, in order, with their arguments, and the words that change in the
// engine's per-screen state (current texture 0x2b8, change counter 0x338, flag 0x33c), the screen record (stage cache) and the
// texture (reference count, changed flag, promoted flag).  What the original does and this build keeps:
//   - binding what is bound already does nothing, unless the texture says it changed (then the stage states are checked again);
//   - the engine's bookkeeping happens before anything is sent: the flag, the old texture's Dereference, the new one's reference
//     count and Promote, the change counter;
//   - the device is told only what differs from the cache of the screen record (the surface handle, then the colour operation with
//     its arguments, the alpha operation with its arguments, the addressing, the three filters and the mip map bias); an engine mode
//     outside 0..3 maps to zero;
//   - after a texture is bound, stage 1 is switched off again every time; unbinding (null) switches stage 0 off and resets three
//     cache words only;
//   - the results of the device calls are ignored.
//
// Where the original dereferences something that does not exist (a screen without a device, a texture whose surface is not a
// DX7Surface) this build does nothing at all (no call, no change).
#include "null_rasterizer.h"

#include "dx7_surface.h"
#include "engine_imports.h"
#include "rasterizer_abi.h"

typedef unsigned long ulong;

namespace {

// What the engine's colour or alpha mode (0..3) means to Direct3D: the operation (D3DTSS_COLOROP / ALPHAOP value), the value of the
// first argument's companion and the second.  A mode outside 0..3 maps to all zero.
struct Operation {
    ulong operation, firstState, secondState;
};

Operation OperationFor(ulong mode)
{
    switch (mode) {
    case 0: return {3, 2, 0};
    case 1: return {2, 2, 0};
    case 2: return {4, 2, 0};
    case 3: return {4, 2, 1};
    default: return {0, 0, 0};
    }
}

ulong AddressFor(ulong mode)
{
    return mode == 0 ? 3 : mode == 1 ? 2 : mode == 2 ? 1 : 0;
}

ulong FilterFor(ulong mode)
{
    return mode == 0 ? 1 : mode == 1 ? 2 : mode == 2 ? 3 : 0;
}

// The DirectDraw surface of the texture's stage: stage texture -> base texture -> engine surface (a DX7Surface) -> its surface.
IDirectDrawSurface7* DirectDrawSurfaceOf(const GETextureLayout* texture)
{
    const GETextureLayout* stage = texture->stage.texture;
    const GETextureLayout* base = stage ? stage->baseTexture : 0;
    GESurface* surface = base ? reinterpret_cast<GESurface*>(base->surface) : 0;
    if (!surface || !EEWebObjectIsClass(surface, ".?AVDX7Surface@@")) {
        return 0;
    }
    DX7Surface* dx7 = dynamic_cast<DX7Surface*>(surface);
    return dx7 ? dx7->GetDirectDrawSurface() : 0;
}

// D3DTSS_COLOROP 1, COLORARG1 2, COLORARG2 3; ALPHAOP 4, ALPHAARG1 5, ALPHAARG2 6; ADDRESS 0xc; MAGFILTER 0x10, MINFILTER 0x11,
// MIPFILTER 0x12; MIPMAPLODBIAS 0x13.
struct StageSender {
    IDirect3DDevice7* device;
    void Send(ulong type, ulong value) { device->vtbl->SetTextureStageState(device, 0, type, value); }
};

} // namespace

void EEWebNullRasterizer::SetTexture(GETexture* texture, ulong screen)
{
    BindTexture(texture, screen);
}

// SetTexture's body; false when the texture or the screen cannot be used (nothing was sent or changed).
bool EEWebNullRasterizer::BindTexture(GETexture* texture, ulong screen)
{
    if (texture && Note(EEWebCheckTexture(texture)) != EEWEB_LAYOUT_OK) {
        return false;
    }
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !record || !device) {
        return false;
    }
    GETextureLayout* bound = reinterpret_cast<GETextureLayout*>(texture);
    GETextureLayout* current = state->currentTexture;
    IDirectDrawSurface7* handle = 0;
    if (bound) {
        handle = DirectDrawSurfaceOf(bound);
        if (!handle) {
            return false;
        }
    }
    if (bound == current && (!bound || !bound->hasChanged)) {
        return true;
    }

    // The engine's bookkeeping comes first.
    state->hasTexture = bound != 0;
    if (current) {
        EEWebTextureDereference(current);
    }
    state->currentTexture = bound;
    if (bound) {
        bound->refCount++;
        EEWebTexturePromote(bound);
    }
    state->textureChangeCount++;

    uint32_t* cache = record->stageCache;
    StageSender stage0 = {device};
    if (!bound) {
        device->vtbl->SetTexture(device, 0, 0);
        stage0.Send(1, 1);
        stage0.Send(4, 1);
        record->textureStateDirty = 1;
        cache[0] = 0;
        cache[1] = 4;
        cache[2] = 4;
        return true;
    }

    const GETStageLayout& wanted = bound->stage;
    uint32_t old[12];
    __builtin_memcpy(old, cache, sizeof old);
    const uint32_t handleWord = reinterpret_cast<uint32_t>(handle);
    if (handleWord != old[0]) {
        device->vtbl->SetTexture(device, 0, handle);
        cache[0] = handleWord;
    }
    if (wanted.colorMode != old[1]) {
        const Operation color = OperationFor(wanted.colorMode);
        stage0.Send(1, color.operation);
        cache[1] = wanted.colorMode;
        if (color.firstState != old[3]) {
            stage0.Send(2, color.firstState);
            cache[3] = color.firstState;
        }
        if (color.secondState != old[4]) {
            stage0.Send(3, color.secondState);
            cache[4] = color.secondState;
        }
    }
    if (wanted.alphaMode != old[2]) {
        const Operation alpha = OperationFor(wanted.alphaMode);
        stage0.Send(4, alpha.operation);
        cache[2] = wanted.alphaMode;
        if (alpha.firstState != old[5]) {
            stage0.Send(5, alpha.firstState);
            cache[5] = alpha.firstState;
        }
        if (alpha.secondState != old[6]) {
            stage0.Send(6, alpha.secondState);
            cache[6] = alpha.secondState;
        }
    }
    if (wanted.addressingMode != old[7]) {
        stage0.Send(0xc, AddressFor(wanted.addressingMode));
        cache[7] = wanted.addressingMode;
    }
    if (wanted.minFilter != old[8]) {
        stage0.Send(0x11, FilterFor(wanted.minFilter));
        cache[8] = wanted.minFilter;
    }
    if (wanted.magFilter != old[9]) {
        stage0.Send(0x10, FilterFor(wanted.magFilter));
        cache[9] = wanted.magFilter;
    }
    if (wanted.mipFilter != old[10]) {
        stage0.Send(0x12, FilterFor(wanted.mipFilter));
        cache[10] = wanted.mipFilter;
    }
    float oldBias;
    __builtin_memcpy(&oldBias, &old[11], sizeof oldBias);
    if (wanted.mipMapBias != oldBias) {
        uint32_t bits;
        __builtin_memcpy(&bits, &wanted.mipMapBias, sizeof bits);
        stage0.Send(0x13, bits);
        cache[11] = bits;
    }

    // The second stage is switched off again every time.
    device->vtbl->SetTexture(device, 1, 0);
    device->vtbl->SetTextureStageState(device, 1, 1, 1);
    device->vtbl->SetTextureStageState(device, 1, 4, 1);
    record->textureStateDirty = 1;
    bound->hasChanged = 0;
    return true;
}

// Binds the texture, then asks the device whether it can draw with the stage states now set.  The pass count the device is given to
// fill starts as the screen index (the original reuses its argument), and its value is not looked at.
bool EEWebNullRasterizer::CanDoCurrentState(GETexture* texture, ulong screen)
{
    IDirect3DDevice7* device = Device(screen);
    if (!BindTexture(texture, screen) || !device) {
        return false;
    }
    DWORD_ passes = screen;
    long error = device->vtbl->ValidateDevice(device, &passes);
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
    }
    return error == 0;
}
