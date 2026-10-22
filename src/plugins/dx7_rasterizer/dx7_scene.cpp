// The device and scene lifecycle of the rasterizer (task P2.6.5): ActivateScreen and the TnL class's ActivateTheScreen and
// CreateVertexBuffers it calls (they create the screen's surfaces, the IDirect3D7, the Direct3D device on the render target and
// the ring vertex buffer, and give the screen record its initial caches), SetRenderTarget, BeginScene, EndScene, Clear,
// ClearZBuffer and ShowScene.  (Deactivate, which releases what the activation created, is in null_rasterizer.cpp.)
//
// Each one follows the original's DX7Rasterizer / DX7TnLRasterizer function for function; tests/dx7_rasterizer/lifecycle_checks.h
// compares both on the same scripted DirectDraw and Direct3D objects: the calls they get, in order, with their arguments,
// the status they return, the native error they record and the words they change in the engine's per-screen state and in the
// plugin's screen record.  The odd things the original does are kept:
//   - ActivateScreen's result is the status of the *last* surface call it made, so a refused primary surface goes unnoticed when
//     the off-screen render target is made after it;
//   - a screen's state is written only when the whole activation succeeded; a refused CreateDevice leaves the IDirect3D7 it
//     asked for unreleased, a refused vertex buffer leaves the device (Deactivate releases it);
//   - the activation forces the engine's cached settings to an impossible value (-16) before calling each setter, so every
//     setter sends its value to the new device;
//   - BeginScene and EndScene wait (Sleep(1)) while Direct3D answers "still drawing"; the status for a lost surface is 9, for
//     any other error 1, and the error is recorded with the engine.
//
// Where the original dereferences something that does not exist (a screen that was never added, a missing render target or depth
// buffer, a surface that is not a DX7Surface) this build does nothing or answers EEWEB_NOT_SUPPORTED / 1; the original crashes.
#include "null_rasterizer.h"

#include "dx7_math.h"
#include "dx7_surface.h"
#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
}

typedef unsigned long ulong;

namespace {

const GUID_ kIidDirect3D7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
const GUID_ kIidTnLHal = {0xf5049e78, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};

const long kLost = static_cast<long>(DDERR_SURFACELOST_);
const long kStillDrawing = static_cast<long>(DDERR_WASSTILLDRAWING_);

// The value the activation puts in the engine's cached settings so that the setter that follows sends its value.
const unsigned long kForceSend = 0xfffffff0UL;

GESurfaceDescriptionLayout MakeDescription(ulong type, ulong pixelFormat, ulong width, ulong height, ulong backBuffers, ulong depth)
{
    GESurfaceDescriptionLayout description;
    __builtin_memset(&description, 0, sizeof description);
    description.type = type;
    description.pixelFormat = pixelFormat;
    description.width = width;
    description.height = height;
    description.backBufferCount = backBuffers;
    description.pixelDepth = depth;
    description.is3DRenderable = 1;
    return description;
}

void Dereference(GESurface* surface)
{
    reinterpret_cast<UReferenceCounter*>(surface)->Dereference();
}

// What a failed activation gives back: the surfaces it made (a flip chain's back buffer belongs to the primary surface; the depth
// buffer to the target it is attached to).  The original leaves them all.
void ReleaseSurfaces(GESurface* primary, GESurface* target, bool madeTarget)
{
    if (primary) {
        Dereference(primary);
    }
    if (madeTarget && target) {
        Dereference(target);
    }
}

// The status of BeginScene / EndScene: 0, 9 for a lost surface, 1 for any other error (which the engine records).
long SceneStatus(long error)
{
    if (error == 0) {
        return 0;
    }
    ESRecordNativeError(static_cast<ulong>(error));
    return error == kLost ? 9 : 1;
}

} // namespace

// ---- activation ---------------------------------------------------------------------------------------------

// ActivateRenderingSurface(screen, textureManager, window, width, height) (slot 1, 0x10009238): the screen is activated on an
// off-screen video-memory surface (description type 1, 3D renderable, the size asked for, the pixel format and depth of the first
// video mode that has as many bits per pixel as the current one), which is its primary surface, its secondary surface and, through
// SetRenderTarget, its render target.  When the surface cannot be made, or ActivateTheScreen fails (the surface is given back
// first), the first 16-bit mode is used instead; the answer is then that of the second attempt.  tests/dx7_rasterizer/target_checks.h
// compares both builds.  Differs from the original, deliberately: a screen that has no 16-bit mode, or no mode with the current mode's
// depth, is refused (the original asks the engine to verify, and goes on with a mode index that does not exist); the surfaces and the
// device of a screen that is active already are not given back (the original leaks them too).
long EEWebNullRasterizer::ActivateRenderingSurface(ulong screen, GETextureManager* textureManager, GEWindow* window, ulong width, ulong height)
{
    GERScreenStateLayout* state = State(screen);
    if (!state || !state->screen) {
        return EEWEB_NOT_SUPPORTED;
    }
    GEScreen* owner = reinterpret_cast<GEScreen*>(state->screen);
    const ulong currentDepth = reinterpret_cast<const EEWebVideoMode&>(owner->GetCurrentMode()).bitsPerPixel;
    const ulong modes = owner->GetModeCount();
    GESurface* surface = 0;
    long result = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        const ulong depth = attempt == 0 ? currentDepth : 0x10;
        ulong index = 0;
        while (index < modes && reinterpret_cast<const EEWebVideoMode&>(owner->GetMode(index)).bitsPerPixel != depth) {
            index++;
        }
        if (index >= modes) {
            return EEWEB_NOT_SUPPORTED;
        }
        const EEWebVideoMode mode = reinterpret_cast<const EEWebVideoMode&>(owner->GetMode(index));
        GESurfaceDescriptionLayout description = MakeDescription(1, mode.pixelFormat, width, height, 0, mode.bitsPerPixel);
        result = owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &surface);
        if (result == 0) {
            result = ActivateTheScreen(screen, surface, surface, textureManager, window);
            if (result == 0) {
                break;
            }
            Dereference(surface);
        }
        if (attempt == 1) {
            return result;
        }
    }
    GEScreenLayout* screenFields = reinterpret_cast<GEScreenLayout*>(owner);
    screenFields->textureManager = reinterpret_cast<GETextureManagerLayout*>(textureManager);
    reinterpret_cast<GETextureManagerLayout*>(textureManager)->screen = screenFields;
    state->primarySurface = reinterpret_cast<GESurfaceLayout*>(surface);
    state->currentTexture = 0;
    state->renderTarget = 0;
    state->secondarySurface = reinterpret_cast<GESurfaceLayout*>(surface);
    state->window = reinterpret_cast<GEWindowLayout*>(window);
    state->activated = 1;
    SetRenderTarget(surface, screen);
    reinterpret_cast<GESurfaceLayout*>(surface)->refCount++;
    return result;
}

long EEWebNullRasterizer::ActivateScreen(ulong screen, GETextureManager* textureManager, GEWindow* window, ulong backBuffers, bool zBuffer,
                                         ulong zFormat)
{
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    if (!state || !record || !state->screen) {
        return EEWEB_NOT_SUPPORTED;
    }
    GEScreen* owner = reinterpret_cast<GEScreen*>(state->screen);
    const EEWebVideoMode mode = reinterpret_cast<const EEWebVideoMode&>(owner->GetCurrentMode());
    const GEWindowLayout* frame = reinterpret_cast<const GEWindowLayout*>(window);
    const ulong depthBits = zFormat == 9 ? 0x20 : 0x10;
    if (!owner->GetIsFullScreen() && backBuffers == 1 && !frame) {
        return EEWEB_NOT_SUPPORTED; // checked before anything is created, so there is nothing to give back
    }

    if (state->activated) {
        // Activating a screen that is active: what the previous activation created is given back first (the original overwrites the
        // record and the state and leaks it all).
        ReleaseDeviceObjects(record, screen);
        if (state->primarySurface) {
            Dereference(reinterpret_cast<GESurface*>(state->primarySurface));
        }
        if (state->secondarySurface) {
            Dereference(reinterpret_cast<GESurface*>(state->secondarySurface));
        }
        if (state->renderTarget) {
            Dereference(reinterpret_cast<GESurface*>(state->renderTarget));
        }
        state->primarySurface = 0;
        state->secondarySurface = 0;
        state->renderTarget = 0;
        state->activated = 0;
    }

    GESurface* primary = 0;
    GESurface* target = 0; // the surface the scene is rendered to; becomes the secondary surface of the screen's state
    GESurface* depth = 0;
    bool madeTarget = false;
    long result = 0;
    GESurfaceDescriptionLayout description;

    if (!owner->GetIsFullScreen()) {
        // Windowed: the primary surface is the desktop; with one "back buffer" the scene goes to an off-screen target the size of the
        // window.
        description = MakeDescription(0, mode.pixelFormat, mode.width, mode.height, 0, mode.bitsPerPixel);
        result = owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &primary);
        if (backBuffers == 1) {
            description = MakeDescription(1, mode.pixelFormat, frame->width, frame->height, 0, mode.bitsPerPixel);
            result = owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &target);
            madeTarget = true;
        }
        if (zBuffer) {
            const bool windowSize = target != 0 && frame != 0;
            description = MakeDescription(3, zFormat, windowSize ? frame->width : mode.width, windowSize ? frame->height : mode.height, 0, depthBits);
            owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &depth);
            GESurface* host = target ? target : primary;
            if (depth && host) {
                result = host->AttachSurface(depth);
                Dereference(depth);
            }
        }
    } else {
        // Full screen: a flip chain of `backBuffers` back buffers; the scene is rendered to the first of them.
        description = MakeDescription(backBuffers != 0 ? 2 : 0, mode.pixelFormat, mode.width, mode.height, backBuffers, mode.bitsPerPixel);
        result = owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &primary);
        if (backBuffers != 0 && primary) {
            target = primary->GetAttachedSurface(0);
        }
        if (zBuffer) {
            description = MakeDescription(3, zFormat, mode.width, mode.height, 0, depthBits);
            owner->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &depth);
            GESurface* host = target ? target : primary;
            if (depth && host) {
                result = host->AttachSurface(depth);
                Dereference(depth);
            }
        }
    }
    if (result != 0) {
        ReleaseSurfaces(primary, target, madeTarget);
        return result;
    }
    result = ActivateTheScreen(screen, primary, target, textureManager, window);
    if (result != 0) {
        ReleaseSurfaces(primary, target, madeTarget);
        return result;
    }

    GEScreenLayout* screenFields = reinterpret_cast<GEScreenLayout*>(owner);
    screenFields->textureManager = reinterpret_cast<GETextureManagerLayout*>(textureManager);
    reinterpret_cast<GETextureManagerLayout*>(textureManager)->screen = screenFields;
    state->primarySurface = reinterpret_cast<GESurfaceLayout*>(primary);
    state->currentTexture = 0;
    state->renderTarget = 0;
    state->secondarySurface = reinterpret_cast<GESurfaceLayout*>(target);
    state->window = const_cast<GEWindowLayout*>(frame);
    state->activated = 1;
    SetRenderTarget(target, screen);
    reinterpret_cast<GESurfaceLayout*>(target)->refCount++;
    if (madeTarget) {
        Dereference(target);
    }

    // Every setting the screen caches in the engine's state starts from "unknown" and is set, so the device gets all of them.
    state->alphaTesting = kForceSend;
    SetAlphaTesting(0, screen);
    state->alphaTestCompare = kForceSend;
    SetAlphaTestingCompareState(0, screen);
    state->alphaTestRefValue = 4294967296.0f; // 0x4f800000
    SetAlphaTestingRefValue(0.0f, screen);
    state->antiAliasing = kForceSend;
    SetAntiAliasing(0, screen);
    state->blendSource = kForceSend;
    state->blendDest = kForceSend;
    SetBlending(1, 0, screen);
    state->colorKeying = kForceSend;
    SetColorKeying(0, screen);
    Base()->culling = kForceSend;
    SetCulling(0);
    state->dithering = kForceSend;
    SetDithering(0, screen);
    state->drawStyle = kForceSend;
    SetDrawStyle(0, screen);
    state->fog = kForceSend;
    SetFog(0, screen);
    state->lighting = kForceSend;
    SetLighting(0, screen);
    state->projectionMode = kForceSend;
    SetProjectionMode(1, screen);
    state->shading = kForceSend;
    SetShading(1, screen);
    state->shadingColorSource = kForceSend;
    SetShadingColorSource(1, screen);
    state->specular = kForceSend;
    SetSpecular(0, screen);
    state->zBuffer = kForceSend;
    SetZBuffer(0, screen);
    state->zCompare = kForceSend;
    SetZCompare(0, screen);
    record->unknown6c = 0;
    Set3DMode(screen);
    return result;
}

// The hardware T&L class's activation: Direct3D 7 from the screen's DirectDraw object, a T&L HAL device on the render target
// (`back`), the viewport over the whole target, the device's fixed render states, the vertex buffer, and the record's caches.
long EEWebNullRasterizer::ActivateTheScreen(ulong screen, GESurface*, GESurface* back, GETextureManager*, GEWindow*)
{
    DX7ScreenRecordLayout* record = Record(screen);
    DX7Screen* owner = ScreenOf(screen);
    DX7Surface* target = dynamic_cast<DX7Surface*>(back);
    if (!record || !owner || !target) {
        return 1;
    }
    IDirectDraw7* directDraw = owner->GetDirectDraw();
    IDirect3D7* direct3d = 0;
    IDirect3DDevice7* device = 0;
    long error = directDraw->vtbl->QueryInterface(directDraw, &kIidDirect3D7, reinterpret_cast<void**>(&direct3d));
    if (error == 0) {
        error = direct3d->vtbl->CreateDevice(direct3d, &kIidTnLHal, target->GetDirectDrawSurface(), &device);
    }
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
        if (direct3d) {
            direct3d->vtbl->Release(direct3d); // the original keeps asking for it and never gives it back
        }
        return 1;
    }
    record->direct3d = direct3d;
    record->device = device;
    const float one = 1.0f;
    record->viewport[0] = 0;
    record->viewport[1] = 0;
    record->viewport[2] = target->GetWidth();
    record->viewport[3] = target->GetHeight();
    record->viewport[4] = 0;
    __builtin_memcpy(&record->viewport[5], &one, sizeof one);
    device->vtbl->SetViewport(device, record);
    // D3DRENDERSTATE_SPECULARENABLE, then the fixed-function states 0x8a, 0x88 (clipping), 0x8f, 0x8e, 0x8c (ambient material source), 0x92, 0x94.
    static const ulong kStates[8][2] = {{0x1d, 0}, {0x8a, 0}, {0x88, 0}, {0x8f, 1}, {0x8e, 0}, {0x8c, 3}, {0x92, 0}, {0x94, 0}};
    for (int i = 0; i < 8; ++i) {
        device->vtbl->SetRenderState(device, kStates[i][0], kStates[i][1]);
    }
    const long failed = CreateVertexBuffers(screen);
    if (failed != 0) {
        return failed;
    }
    record->canModulateTextureAlpha = 1;
    record->textureStateDirty = 1;
    record->zEnableCache = 1;
    record->zWriteCache = 1;
    record->blendEnabledCache = 1;
    record->blendSourceCache = kForceSend;
    record->blendDestCache = kForceSend;
    // The texture stage cache of a stage with no texture: filters point (4), addressing wrap (3)...
    static const unsigned long kStageCache[12] = {0, 4, 4, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 3, 3, 3, 3, 0x7f7fffffUL};
    for (int i = 0; i < 12; ++i) {
        record->stageCache[i] = kStageCache[i];
    }
    device->vtbl->SetTextureStageState(device, 0, 0xb, 0);
    return 0;
}

// The ring vertex buffer of the T&L class: write-only, FVF 0x152, 0x98a vertices.
long EEWebNullRasterizer::CreateVertexBuffers(ulong screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    if (!record || !record->direct3d) {
        return 1;
    }
    IDirect3D7* direct3d = static_cast<IDirect3D7*>(record->direct3d);
    const ulong description[4] = {0x10, 0x10000, 0x152, 0x98a}; // D3DVERTEXBUFFERDESC: size, caps (write only), FVF, vertex count
    const long error = direct3d->vtbl->CreateVertexBuffer(direct3d, description, reinterpret_cast<IDirect3DVertexBuffer7**>(&record->vertexBuffer), 0);
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
    }
    return error != 0 ? 1 : 0;
}

// ---- the render target, the scene and its clears -----------------------------------------------------------------

void EEWebNullRasterizer::SetRenderTarget(GESurface* surface, ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    DX7Surface* target = dynamic_cast<DX7Surface*>(surface);
    if (!state || !record || !device || !target) {
        return;
    }
    if (reinterpret_cast<GESurfaceLayout*>(surface) == state->renderTarget) {
        return;
    }
    if (state->renderTarget) {
        Dereference(reinterpret_cast<GESurface*>(state->renderTarget));
    }
    state->renderTarget = reinterpret_cast<GESurfaceLayout*>(surface);
    reinterpret_cast<GESurfaceLayout*>(surface)->refCount++;
    device->vtbl->SetRenderTarget(device, target->GetDirectDrawSurface(), 0);
    record->viewport[2] = target->GetWidth();
    record->viewport[3] = target->GetHeight();
    device->vtbl->SetViewport(device, record);
}

long EEWebNullRasterizer::BeginScene(GESurface* surface, ulong screen)
{
    IDirect3DDevice7* device = Device(screen);
    if (!device) {
        return EEWEB_NOT_SUPPORTED;
    }
    SetRenderTarget(surface, screen);
    long error;
    while ((error = device->vtbl->BeginScene(device)) == kStillDrawing) {
        Sleep(1);
    }
    return SceneStatus(error);
}

long EEWebNullRasterizer::EndScene(ulong screen)
{
    IDirect3DDevice7* device = Device(screen);
    if (!device) {
        return EEWEB_NOT_SUPPORTED;
    }
    long error;
    while ((error = device->vtbl->EndScene(device)) == kStillDrawing) {
        Sleep(1);
    }
    return SceneStatus(error);
}

namespace {

// The D3DRECT (left, top, right, bottom) of a GERectangle (bottom, left, right, top): rounded to whole pixels by the engine,
// then floored the way the original does.
void ToPixelRectangle(const GERectangleLayout& rectangle, long* pixels)
{
    float rounded[4] = {rectangle.bottom, rectangle.left, rectangle.right, rectangle.top};
    EEWebRoundRectangle(rounded);
    pixels[0] = dx7math::FloorLikeOriginal(rounded[1]);
    pixels[1] = dx7math::FloorLikeOriginal(rounded[3]);
    pixels[2] = dx7math::FloorLikeOriginal(rounded[2]);
    pixels[3] = dx7math::FloorLikeOriginal(rounded[0]);
}

// IDirect3DDevice7::Clear of the whole rectangle, repeated while Direct3D is still drawing; any error is 1 (and is recorded).
long ClearRectangle(IDirect3DDevice7* device, const long* pixels, ulong flags, ulong color, float depth)
{
    ulong depthBits;
    __builtin_memcpy(&depthBits, &depth, sizeof depthBits);
    long error;
    while ((error = device->vtbl->Clear(device, 1, pixels, flags, color, depthBits, 0)) == kStillDrawing) {
        Sleep(0);
    }
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
        return 1;
    }
    return 0;
}

} // namespace

long EEWebNullRasterizer::Clear(GEColor const& color, GERectangle const& rectangle, ulong screen)
{
    IDirect3DDevice7* device = Device(screen);
    if (!device) {
        return EEWEB_NOT_SUPPORTED;
    }
    long pixels[4];
    ToPixelRectangle(reinterpret_cast<const GERectangleLayout&>(rectangle), pixels);
    const GEColorLayout& c = reinterpret_cast<const GEColorLayout&>(color);
    // Alpha 255 and three truncated channels; the first channel's mask is ORed in before it is shifted, as in the original.
    ulong packed = (dx7math::TruncatedChannel(c.r) | 0xffffff00UL) << 8;
    packed = (packed | dx7math::TruncatedChannel(c.g)) << 8;
    packed |= dx7math::TruncatedChannel(c.b);
    return ClearRectangle(device, pixels, 1 /* D3DCLEAR_TARGET */, packed, 0.0f);
}

long EEWebNullRasterizer::ClearZBuffer(float depth, GERectangle const& rectangle, ulong screen)
{
    IDirect3DDevice7* device = Device(screen);
    if (!device) {
        return EEWEB_NOT_SUPPORTED;
    }
    long pixels[4];
    ToPixelRectangle(reinterpret_cast<const GERectangleLayout&>(rectangle), pixels);
    return ClearRectangle(device, pixels, 2 /* D3DCLEAR_ZBUFFER */, 0, depth);
}

// ---- presenting -------------------------------------------------------------------------------------------------

// Every twelfth frame the engine's statistics are recomputed: the per-frame values are the totals over twelve frames divided by
// twelve, the per-second values are the totals per elapsed millisecond times a thousand (zero when no time has passed).
static void UpdateStatistics(GERScreenStateLayout* state)
{
    const double kPerFrame = 0.0833333358168602; // 1/12 as a float
    state->polyAreaPerFrame = static_cast<float>(static_cast<double>(state->polyAreaAccumulator) * kPerFrame);
    state->polysPerFrame = static_cast<float>(static_cast<double>(state->drawnPolyCount) * kPerFrame);
    state->scenePolysPerFrame = static_cast<float>(static_cast<double>(state->scenePolyCount) * kPerFrame);
    state->textureChangesPerFrame = static_cast<float>(static_cast<double>(state->textureChangeCount) * kPerFrame);
    const unsigned long now = TMGetTime();
    const double elapsed = static_cast<double>(now - state->lastStatisticsTime);
    if (elapsed > 0.0) {
        state->polyAreaPerSecond = static_cast<float>(static_cast<double>(state->polyAreaAccumulator) * 1000.0 / elapsed);
        state->polysPerSecond = static_cast<float>(static_cast<double>(state->drawnPolyCount) * 1000.0 / elapsed);
        state->framesPerSecond = static_cast<float>(static_cast<double>(state->frameCounter) * 1000.0 / elapsed);
        state->scenePolysPerSecond = static_cast<float>(static_cast<double>(state->scenePolyCount) * 1000.0 / elapsed);
    } else {
        state->polyAreaPerSecond = 0.0f;
        state->polysPerSecond = 0.0f;
        state->framesPerSecond = 0.0f;
        state->scenePolysPerSecond = 0.0f;
    }
    state->lastStatisticsTime = now;
    state->polyAreaAccumulator = 0.0f;
    state->drawnPolyCount = 0;
    state->frameCounter = 0;
    state->scenePolyCount = 0;
    state->textureChangeCount = 0;
}

// Windowed: the render target is copied onto the window's client area of the desktop; full screen: the primary surface is flipped.
long EEWebNullRasterizer::ShowScene(ulong screen)
{
    GERScreenStateLayout* state = State(screen);
    if (!state || !state->screen || !state->primarySurface) {
        return EEWEB_NOT_SUPPORTED;
    }
    if (++state->frameCounter == 12) {
        UpdateStatistics(state);
    }
    GEScreen* owner = reinterpret_cast<GEScreen*>(state->screen);
    GESurface* primary = reinterpret_cast<GESurface*>(state->primarySurface);
    if (owner->GetIsFullScreen()) {
        return primary->Flip(state->presentFlag != 0);
    }
    DX7Surface* desktop = dynamic_cast<DX7Surface*>(primary);
    DX7Surface* source = dynamic_cast<DX7Surface*>(reinterpret_cast<GESurface*>(state->secondarySurface));
    if (!desktop || !source || !state->window) {
        return EEWEB_NOT_SUPPORTED;
    }
    const long width = static_cast<long>(source->GetWidth());
    const long height = static_cast<long>(source->GetHeight());
    const float* origin = reinterpret_cast<const float*>(&reinterpret_cast<GEWindow*>(state->window)->GetClientOrigin());
    const long left = dx7math::RoundToNearest(origin[0]);
    const long top = dx7math::RoundToNearest(origin[1]);
    RECT_ destination = {left, top, left + width, top + height};
    RECT_ area = {0, 0, width, height};
    IDirectDrawSurface7* target = desktop->GetDirectDrawSurface();
    long error;
    while ((error = target->vtbl->Blt(target, &destination, source->GetDirectDrawSurface(), &area, 0, 0)) == kStillDrawing) {
        Sleep(0);
    }
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
        return 1;
    }
    return 0;
}
