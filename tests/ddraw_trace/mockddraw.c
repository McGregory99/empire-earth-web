/* A scripted stand-in for the next ddraw in the chain: just enough of IDirectDraw7 and IDirectDrawSurface7 to make the
 * proxy see results that Wine's ddraw cannot be made to give on demand (DDERR_WASSTILLDRAWING, a flip chain without full
 * screen), and to prove that interface arguments reach the real object unwrapped: every method that takes a surface
 * checks that the pointer is one of the mock's own objects. */
#define CINTERFACE
#define COBJMACROS
#include <ddraw.h>
#include <d3d.h>
#include <stdlib.h>
#include <string.h>

typedef struct Mock {
    const void *vtbl;
    int id; /* 1 = primary, 2 = back buffer, 3.. = surfaces with memory */
    DWORD w, h, bpp, pitch, pf_flags, rmask, gmask, bmask, amask;
    unsigned char *mem;
    int dc_held;
} Mock;

static Mock g_primary, g_back, g_pool[8];
static int g_npool;
static LONG g_dd_refs = 1;
static int g_flip_calls, g_blt_calls;
static IDirectDraw7 *g_dd;

static int is_mine(const void *p) {
    return p == (const void *)&g_primary || p == (const void *)&g_back || (p >= (const void *)g_pool && p < (const void *)(g_pool + 8));
}

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
    {   /* surfaces with memory: a colour fill or a whole-surface copy, so that the tests can predict the pixels */
        Mock *d = (Mock *)This, *s = (Mock *)src;
        DWORD y, x;
        if (d->mem && (flags & DDBLT_COLORFILL) && fx) {
            for (y = 0; y < d->h; y++)
                for (x = 0; x < d->w; x++)
                    if (d->bpp == 32)
                        ((DWORD *)(d->mem + y * d->pitch))[x] = fx->dwFillColor;
                    else
                        ((WORD *)(d->mem + y * d->pitch))[x] = (WORD)fx->dwFillColor;
        } else if (d->mem && s && s->mem && s->w == d->w && s->h == d->h && s->bpp == d->bpp) {
            for (y = 0; y < d->h; y++)
                memcpy(d->mem + y * d->pitch, s->mem + y * s->pitch, d->w * d->bpp / 8);
        }
    }
    return DD_OK;
}

static HRESULT WINAPI s_bltfast(IDirectDrawSurface7 *This, DWORD x, DWORD y, IDirectDrawSurface7 *src, LPRECT r, DWORD t) {
    Mock *d = (Mock *)This, *s = (Mock *)src;
    (void)t;
    if (src && !is_mine(src))
        return DDERR_INVALIDPARAMS;
    if (d->mem && s && s->mem && s->bpp == d->bpp) { /* copies the source rectangle (default: all of it) to (x, y) */
        RECT rc = r ? *r : (RECT){0, 0, (LONG)s->w, (LONG)s->h};
        LONG row;
        for (row = 0; row < rc.bottom - rc.top && y + (DWORD)row < d->h; row++)
            memcpy(d->mem + (y + (DWORD)row) * d->pitch + x * d->bpp / 8, s->mem + (rc.top + row) * s->pitch + rc.left * s->bpp / 8,
                   (size_t)(rc.right - rc.left) * s->bpp / 8);
    }
    return DD_OK;
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

/* Lock fills the whole description like Wine does (pitch, size, pixel format) and hands out the rectangle's first pixel; a surface
 * whose DC is out cannot be locked, so a proxy that reads the pixels before ReleaseDC returned would fail here. */
static int g_ro_locks;
static HRESULT WINAPI s_lock(IDirectDrawSurface7 *This, LPRECT rc, LPDDSURFACEDESC2 sd, DWORD flags, HANDLE ev) {
    Mock *m = (Mock *)This;
    (void)ev;
    if (!m->mem)
        return DDERR_GENERIC;
    if (flags & DDLOCK_READONLY) { /* MOCK_FAIL_RO_LOCK=N: the Nth read-only lock fails once (a busy surface) */
        char v[16];
        DWORD n = GetEnvironmentVariableA("MOCK_FAIL_RO_LOCK", v, sizeof v);
        if (n > 0 && n < sizeof v && ++g_ro_locks == atoi(v))
            return DDERR_SURFACEBUSY;
    }
    if (m->dc_held)
        return DDERR_SURFACEBUSY;
    sd->dwFlags |= DDSD_PITCH | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_LPSURFACE;
    sd->dwWidth = m->w;
    sd->dwHeight = m->h;
    sd->lPitch = (LONG)m->pitch;
    sd->ddpfPixelFormat.dwSize = sizeof sd->ddpfPixelFormat;
    sd->ddpfPixelFormat.dwFlags = m->pf_flags;
    sd->ddpfPixelFormat.dwRGBBitCount = m->bpp;
    sd->ddpfPixelFormat.dwRBitMask = m->rmask;
    sd->ddpfPixelFormat.dwGBitMask = m->gmask;
    sd->ddpfPixelFormat.dwBBitMask = m->bmask;
    sd->ddpfPixelFormat.dwRGBAlphaBitMask = m->amask;
    sd->lpSurface = m->mem + (rc ? (size_t)rc->top * m->pitch + (size_t)rc->left * m->bpp / 8 : 0);
    return DD_OK;
}
static HRESULT WINAPI s_unlock(IDirectDrawSurface7 *This, LPRECT rc) { (void)This; (void)rc; return DD_OK; }
static HRESULT WINAPI s_getdc(IDirectDrawSurface7 *This, HDC *dc) {
    Mock *m = (Mock *)This;
    if (!m->mem || m->dc_held)
        return DDERR_GENERIC;
    m->dc_held = 1;
    *dc = (HDC)(uintptr_t)(0x1000 + m->id);
    return DD_OK;
}
/* What GDI drew while the DC was out shows up when it is released: pixel (x, y) of a 32-bit surface becomes 0xffc0de00 | (y*8+x). */
static HRESULT WINAPI s_releasedc(IDirectDrawSurface7 *This, HDC dc) {
    Mock *m = (Mock *)This;
    DWORD x, y;
    (void)dc;
    if (!m->dc_held)
        return DDERR_GENERIC;
    for (y = 0; y < m->h; y++)
        for (x = 0; x < m->w; x++)
            ((DWORD *)(m->mem + y * m->pitch))[x] = 0xffc0de00u | (y * 8 + x);
    m->dc_held = 0;
    return DD_OK;
}

static IDirectDrawSurface7Vtbl g_surface_vtbl = {
    .Lock = s_lock, .Unlock = s_unlock, .GetDC = s_getdc, .ReleaseDC = s_releasedc,
    .QueryInterface = s_qi, .AddRef = s_addref, .Release = s_release, .Blt = s_blt, .BltFast = s_bltfast,
    .Flip = s_flip, .GetAttachedSurface = s_getattached, .GetFlipStatus = s_flipstatus,
};

/* ---- Direct3D 7: an interface, a device and one vertex buffer, enough for the draw calls ---------------------------- */
typedef struct MockObj {
    const void *vtbl;
} MockObj;
static MockObj g_d3d, g_dev, g_vb;
static unsigned char *g_vb_mem;
static DWORD g_vb_size;

static ULONG WINAPI o_addref(void *This) { (void)This; return 2; }
static ULONG WINAPI o_release(void *This) { (void)This; return 1; }
static HRESULT WINAPI o_qi(void *This, REFIID riid, void **out) { (void)This; (void)riid; *out = NULL; return E_NOINTERFACE; }

/* DISCARD hands out a fresh, zeroed generation and poisons the old one (a driver may reuse or unmap it): data read from a
 * pointer after Unlock would show 0xEE.  NOOVERWRITE keeps the contents. */
static HRESULT WINAPI vb_lock(IDirect3DVertexBuffer7 *This, DWORD flags, LPVOID *data, LPDWORD size) {
    (void)This;
    if (!g_vb_mem || (flags & DDLOCK_DISCARDCONTENTS)) {
        unsigned char *fresh = calloc(1, g_vb_size);
        if (g_vb_mem)
            memset(g_vb_mem, 0xEE, g_vb_size);
        g_vb_mem = fresh;
    }
    *data = g_vb_mem;
    if (size)
        *size = g_vb_size;
    return DD_OK;
}
static HRESULT WINAPI vb_unlock(IDirect3DVertexBuffer7 *This) { (void)This; return DD_OK; }

static IDirect3DVertexBuffer7Vtbl g_vb_vtbl = {
    .QueryInterface = (void *)o_qi, .AddRef = (void *)o_addref, .Release = (void *)o_release, .Lock = vb_lock, .Unlock = vb_unlock,
};

static HRESULT WINAPI dev_drawvb(IDirect3DDevice7 *This, D3DPRIMITIVETYPE t, IDirect3DVertexBuffer7 *vb, DWORD start, DWORD count, DWORD flags) {
    (void)This; (void)t; (void)start; (void)count; (void)flags;
    return vb == (IDirect3DVertexBuffer7 *)&g_vb ? DD_OK : DDERR_INVALIDPARAMS;
}
static HRESULT WINAPI dev_drawivb(IDirect3DDevice7 *This, D3DPRIMITIVETYPE t, IDirect3DVertexBuffer7 *vb, DWORD start, DWORD count, LPWORD idx, DWORD icount, DWORD flags) {
    (void)This; (void)t; (void)start; (void)count; (void)icount; (void)flags;
    return (vb == (IDirect3DVertexBuffer7 *)&g_vb && idx) ? DD_OK : DDERR_INVALIDPARAMS;
}
static HRESULT WINAPI dev_draw(IDirect3DDevice7 *This, D3DPRIMITIVETYPE t, DWORD fvf, LPVOID v, DWORD count, DWORD flags) {
    (void)This; (void)t; (void)fvf; (void)count; (void)flags;
    return v ? DD_OK : DDERR_INVALIDPARAMS;
}
static HRESULT WINAPI dev_drawi(IDirect3DDevice7 *This, D3DPRIMITIVETYPE t, DWORD fvf, LPVOID v, DWORD count, LPWORD idx, DWORD icount, DWORD flags) {
    (void)This; (void)t; (void)fvf; (void)count; (void)icount; (void)flags;
    return (v && idx) ? DD_OK : DDERR_INVALIDPARAMS;
}

static HRESULT WINAPI dev_settexture(IDirect3DDevice7 *This, DWORD stage, IDirectDrawSurface7 *tex) {
    (void)This; (void)stage;
    return (!tex || is_mine(tex)) ? DD_OK : DDERR_INVALIDPARAMS;
}

static IDirect3DDevice7Vtbl g_dev_vtbl = {
    .SetTexture = dev_settexture,
    .QueryInterface = (void *)o_qi, .AddRef = (void *)o_addref, .Release = (void *)o_release,
    .DrawPrimitive = dev_draw, .DrawIndexedPrimitive = dev_drawi,
    .DrawPrimitiveVB = dev_drawvb, .DrawIndexedPrimitiveVB = dev_drawivb,
};

static HRESULT WINAPI d3d_createdevice(IDirect3D7 *This, REFCLSID id, IDirectDrawSurface7 *target, IDirect3DDevice7 **out) {
    (void)This; (void)id;
    if (target && !is_mine(target))
        return DDERR_INVALIDPARAMS;
    *out = (IDirect3DDevice7 *)&g_dev;
    return D3D_OK;
}
static HRESULT WINAPI d3d_createvb(IDirect3D7 *This, D3DVERTEXBUFFERDESC *desc, IDirect3DVertexBuffer7 **out, DWORD flags) {
    (void)This; (void)flags;
    g_vb_size = desc->dwNumVertices * 36; /* the tests use FVF 0x152 */
    *out = (IDirect3DVertexBuffer7 *)&g_vb;
    return D3D_OK;
}
static IDirect3D7Vtbl g_d3d_vtbl = {
    .QueryInterface = (void *)o_qi, .AddRef = (void *)o_addref, .Release = (void *)o_release,
    .CreateDevice = d3d_createdevice, .CreateVertexBuffer = d3d_createvb,
};

static HRESULT WINAPI d_qi(IDirectDraw7 *This, REFIID riid, void **out) {
    (void)This;
    *out = NULL;
    if (IsEqualGUID(riid, &IID_IDirect3D7)) {
        *out = &g_d3d;
        return DD_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI d_addref(IDirectDraw7 *This) { (void)This; return (ULONG)InterlockedIncrement(&g_dd_refs); }
static ULONG WINAPI d_release(IDirectDraw7 *This) { (void)This; return (ULONG)InterlockedDecrement(&g_dd_refs); }
static HRESULT WINAPI d_setcoop(IDirectDraw7 *This, HWND h, DWORD f) { (void)This; (void)h; (void)f; return DD_OK; }

static HRESULT WINAPI d_createsurface(IDirectDraw7 *This, LPDDSURFACEDESC2 desc, IDirectDrawSurface7 **out, IUnknown *outer) {
    (void)This; (void)outer;
    *out = NULL;
    if (!(desc->dwFlags & DDSD_CAPS))
        return DDERR_INVALIDPARAMS;
    if ((desc->dwFlags & DDSD_WIDTH) && (desc->dwFlags & DDSD_HEIGHT) && !(desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) && g_npool < 8) {
        Mock *m = &g_pool[g_npool];
        const DDPIXELFORMAT *pf = &desc->ddpfPixelFormat;
        int own = (desc->dwFlags & DDSD_PIXELFORMAT) != 0;
        m->vtbl = &g_surface_vtbl;
        m->id = 3 + g_npool++;
        m->w = desc->dwWidth;
        m->h = desc->dwHeight;
        m->pf_flags = own ? pf->dwFlags : DDPF_RGB | DDPF_ALPHAPIXELS;
        m->bpp = own ? pf->dwRGBBitCount : 32;
        m->rmask = own ? pf->dwRBitMask : 0x00ff0000;
        m->gmask = own ? pf->dwGBitMask : 0x0000ff00;
        m->bmask = own ? pf->dwBBitMask : 0x000000ff;
        m->amask = own ? pf->dwRGBAlphaBitMask : 0xff000000;
        m->pitch = m->w * m->bpp / 8 + 8; /* padded rows, as real drivers do */
        m->mem = calloc(1, (size_t)m->pitch * m->h);
        *out = (IDirectDrawSurface7 *)m;
        return DD_OK;
    }
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
    g_d3d.vtbl = &g_d3d_vtbl;
    g_dev.vtbl = &g_dev_vtbl;
    g_vb.vtbl = &g_vb_vtbl;
    dd.lpVtbl = &g_dd_vtbl;
    g_dd = &dd;
    if (!IsEqualGUID(iid, &IID_IDirectDraw7))
        return DDERR_INVALIDPARAMS;
    *out = g_dd;
    return DD_OK;
}
