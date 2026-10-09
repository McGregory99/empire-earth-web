/* A scripted stand-in for the next ddraw in the chain: just enough of IDirectDraw7 and IDirectDrawSurface7 to make the
 * proxy see results that Wine's ddraw cannot be made to give on demand (DDERR_WASSTILLDRAWING, a flip chain without full
 * screen), and to prove that interface arguments reach the real object unwrapped: every method that takes a surface
 * checks that the pointer is one of the mock's own objects. */
#define CINTERFACE
#define COBJMACROS
#include <ddraw.h>
#include <string.h>

typedef struct Mock {
    const void *vtbl;
    int id; /* 1 = primary, 2 = back buffer */
} Mock;

static Mock g_primary, g_back;
static LONG g_dd_refs = 1;
static int g_flip_calls, g_blt_calls;
static IDirectDraw7 *g_dd;

static int is_mine(const void *p) { return p == (const void *)&g_primary || p == (const void *)&g_back; }

static HRESULT WINAPI s_qi(IDirectDrawSurface7 *This, REFIID riid, void **out) { (void)This; (void)riid; *out = NULL; return E_NOINTERFACE; }
static ULONG WINAPI s_addref(IDirectDrawSurface7 *This) { (void)This; return 2; }
static ULONG WINAPI s_release(IDirectDrawSurface7 *This) { (void)This; return 1; }

/* The first two flips, and the first Blt with DDBLT_DONOTWAIT, report DDERR_WASSTILLDRAWING; a surface argument that
 * is not one of ours (for example one of the proxy's wrappers) is an invalid parameter. */
static HRESULT WINAPI s_flip(IDirectDrawSurface7 *This, IDirectDrawSurface7 *override, DWORD flags) {
    (void)This; (void)flags;
    if (override && !is_mine(override))
        return DDERR_INVALIDPARAMS;
    return (g_flip_calls++ < 2) ? DDERR_WASSTILLDRAWING : DD_OK;
}

static HRESULT WINAPI s_blt(IDirectDrawSurface7 *This, LPRECT dr, IDirectDrawSurface7 *src, LPRECT sr, DWORD flags, LPDDBLTFX fx) {
    (void)This; (void)dr; (void)sr; (void)fx;
    if (src && !is_mine(src))
        return DDERR_INVALIDPARAMS;
    ++g_blt_calls;
    if ((flags & DDBLT_DONOTWAIT) && g_blt_calls == 1)
        return DDERR_WASSTILLDRAWING;
    return DD_OK;
}

static HRESULT WINAPI s_bltfast(IDirectDrawSurface7 *This, DWORD x, DWORD y, IDirectDrawSurface7 *src, LPRECT r, DWORD t) {
    (void)This; (void)x; (void)y; (void)r; (void)t;
    return (src && !is_mine(src)) ? DDERR_INVALIDPARAMS : DD_OK;
}

static HRESULT WINAPI s_getattached(IDirectDrawSurface7 *This, LPDDSCAPS2 caps, IDirectDrawSurface7 **out) {
    *out = NULL;
    if (This == (IDirectDrawSurface7 *)&g_primary && caps && (caps->dwCaps & DDSCAPS_BACKBUFFER)) {
        *out = (IDirectDrawSurface7 *)&g_back;
        return DD_OK;
    }
    return DDERR_NOTFOUND;
}

static HRESULT WINAPI s_flipstatus(IDirectDrawSurface7 *This, DWORD flags) { (void)This; (void)flags; return DD_OK; }

static IDirectDrawSurface7Vtbl g_surface_vtbl = {
    .QueryInterface = s_qi, .AddRef = s_addref, .Release = s_release, .Blt = s_blt, .BltFast = s_bltfast,
    .Flip = s_flip, .GetAttachedSurface = s_getattached, .GetFlipStatus = s_flipstatus,
};

static HRESULT WINAPI d_qi(IDirectDraw7 *This, REFIID riid, void **out) { (void)This; (void)riid; *out = NULL; return E_NOINTERFACE; }
static ULONG WINAPI d_addref(IDirectDraw7 *This) { (void)This; return (ULONG)InterlockedIncrement(&g_dd_refs); }
static ULONG WINAPI d_release(IDirectDraw7 *This) { (void)This; return (ULONG)InterlockedDecrement(&g_dd_refs); }
static HRESULT WINAPI d_setcoop(IDirectDraw7 *This, HWND h, DWORD f) { (void)This; (void)h; (void)f; return DD_OK; }

static HRESULT WINAPI d_createsurface(IDirectDraw7 *This, LPDDSURFACEDESC2 desc, IDirectDrawSurface7 **out, IUnknown *outer) {
    (void)This; (void)outer;
    *out = NULL;
    if (!(desc->dwFlags & DDSD_CAPS))
        return DDERR_INVALIDPARAMS;
    *out = (IDirectDrawSurface7 *)((desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) ? &g_primary : &g_back);
    return DD_OK;
}

static IDirectDraw7Vtbl g_dd_vtbl = {
    .QueryInterface = d_qi, .AddRef = d_addref, .Release = d_release, .SetCooperativeLevel = d_setcoop,
    .CreateSurface = d_createsurface,
};

__declspec(dllexport) HRESULT WINAPI DirectDrawCreateEx(GUID *guid, void **out, REFIID iid, IUnknown *outer) {
    static IDirectDraw7 dd;
    (void)guid; (void)outer;
    g_primary.vtbl = &g_surface_vtbl;
    g_primary.id = 1;
    g_back.vtbl = &g_surface_vtbl;
    g_back.id = 2;
    dd.lpVtbl = &g_dd_vtbl;
    g_dd = &dd;
    if (!IsEqualGUID(iid, &IID_IDirectDraw7))
        return DDERR_INVALIDPARAMS;
    *out = g_dd;
    return DD_OK;
}
