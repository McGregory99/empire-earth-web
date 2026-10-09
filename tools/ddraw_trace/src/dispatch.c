/* Wrapped COM objects and the generic call dispatcher.
 *
 * Every method of the five wrapped interfaces gets a thunk with the exact stdcall arity (from methods.inc, which is
 * generated from the SDK headers).  A thunk packs its arguments and calls dispatch(), which unwraps interface
 * arguments, forwards the call to the real object, wraps interface results and, when the caller is a traced
 * module, writes one normalized trace line.  Interfaces other than the five are handed out unwrapped. */
#include "ddt.h"
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <string.h>

#define MAXARGS 8
#define MAXSLOTS 64
#define ARG_TXT 2048

enum Kind {
    K_HEX, K_DEC, K_F32, K_PTR, K_HANDLE, K_GUID, K_IFACE_IN, K_IFACE_OUT, K_VOIDPP, K_STRUCT, K_CALLBACK,
    K_RS, K_TSS, K_XFORM, K_PT, K_OUT_DWORD, K_OUT_BOOL, K_OUT_LONG, K_RAWOUT, K_OUTPTR
};
enum Cb { CB_MODES, CB_SURFACES, CB_DEVICES, CB_PIXFMT };
enum Hook {
    H_NONE, H_QI, H_ADDREF, H_RELEASE, H_GETDDINTERFACE, H_CREATESURFACE, H_GETATTACHED, H_FLIP, H_BLT, H_BLTFAST,
    H_LOCK, H_UNLOCK, H_CREATEVB
};

typedef struct ArgDef {
    const char *type, *name;
    uint8_t kind, aux, out;
} ArgDef;

typedef struct MethodDef {
    const char *name;
    uint8_t nargs, hook, timing;
    ArgDef a[MAXARGS];
} MethodDef;

typedef struct RawMethod {
    const char *name;
    uint8_t nargs;
    const char *a[2 * MAXARGS];
} RawMethod;

#define UNPACK(...) __VA_ARGS__

#define DDT_IFACE(t, n, c) [IF_##t] = c,
#define DDT_METHOD(t, s, n, na, args)
static const int slot_counts[IF_COUNT] = {
#include "methods.inc"
};
#undef DDT_IFACE
#undef DDT_METHOD

#define DDT_IFACE(t, n, c)
#define DDT_METHOD(t, s, n, na, args) {#n, na, {UNPACK args}},
static const RawMethod raw_methods[] = {
#include "methods.inc"
};
#undef DDT_IFACE
#undef DDT_METHOD

static const char *const if_short[IF_COUNT] = {"dd7", "d3d7", "dev7", "surf", "vb"};

static MethodDef g_methods[IF_COUNT][MAXSLOTS];
static const void *const *g_vtbl[IF_COUNT];

/* ---- registry of wrapped objects ----------------------------------------------------------------- */
#define NBUCKETS 1024
static CRITICAL_SECTION g_reg_cs;
static Wrap *g_by_real[NBUCKETS], *g_by_self[NBUCKETS];
static unsigned g_ids[IF_COUNT], g_families;

static unsigned hash_ptr(const void *p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) % NBUCKETS; }

void *wrap_new(int iface, void *real) {
    Wrap *w;
    unsigned h;
    if (!real)
        return NULL;
    ddt_init();
    h = hash_ptr(real);
    EnterCriticalSection(&g_reg_cs);
    for (w = g_by_real[h]; w; w = w->by_real_next)
        if (w->real == real && w->iface == iface) {
            LeaveCriticalSection(&g_reg_cs);
            return w;
        }
    w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w);
    if (!w) {
        LeaveCriticalSection(&g_reg_cs);
        return real; /* out of memory: hand out the real object, tracing degrades but nothing breaks */
    }
    w->vtbl = g_vtbl[iface];
    w->real = real;
    w->iface = iface;
    w->id = ++g_ids[iface];
    w->family = ++g_families;
    w->by_real_next = g_by_real[h];
    g_by_real[h] = w;
    h = hash_ptr(w);
    w->by_self_next = g_by_self[h];
    g_by_self[h] = w;
    LeaveCriticalSection(&g_reg_cs);
    return w;
}

static Wrap *find_wrapper(const void *p) {
    Wrap *w;
    if (!p)
        return NULL;
    EnterCriticalSection(&g_reg_cs);
    for (w = g_by_self[hash_ptr(p)]; w; w = w->by_self_next)
        if ((const void *)w == p)
            break;
    LeaveCriticalSection(&g_reg_cs);
    return w;
}

void *unwrap(void *p) {
    Wrap *w = find_wrapper(p);
    return w ? w->real : p;
}

/* The real object is gone: forget the mapping so that a new object at the same address gets a fresh id.  The
 * interfaces obtained from one another with QueryInterface share one object (and in Wine one reference count), so
 * the last Release on any of them ends all of them. */
static void retire_locked(Wrap *w) {
    Wrap **pp;
    w->dead = 1;
    for (pp = &g_by_real[hash_ptr(w->real)]; *pp; pp = &(*pp)->by_real_next)
        if (*pp == w) {
            *pp = w->by_real_next;
            break;
        }
}

static void retire(Wrap *w) {
    unsigned b;
    EnterCriticalSection(&g_reg_cs);
    for (b = 0; b < NBUCKETS; b++) {
        Wrap *x;
        for (x = g_by_self[b]; x; x = x->by_self_next)
            if (!x->dead && (x == w || x->family == w->family))
                retire_locked(x);
    }
    LeaveCriticalSection(&g_reg_cs);
}

const char *wrap_label(void *p, char tmp[24]) {
    Wrap *w;
    if (!p)
        return "NULL";
    w = find_wrapper(p);
    if (!w)
        return "raw";
    snprintf(tmp, 24, "%s#%u", if_short[w->iface], w->id);
    return tmp;
}

/* ---- argument classification --------------------------------------------------------------------- */
static const struct { const char *type; uint8_t kind, aux; } type_map[] = {
    {"D3DVALUE", K_F32, 0}, {"D3DCOLOR", K_HEX, 0}, {"WINBOOL", K_DEC, 0}, {"LONG", K_DEC, 0},
    {"HWND", K_HANDLE, 0}, {"HDC", K_HANDLE, 0}, {"HANDLE", K_HANDLE, 0}, {"HMONITOR", K_HANDLE, 0},
    {"REFIID", K_GUID, 0}, {"REFGUID", K_GUID, 0}, {"REFCLSID", K_GUID, 0}, {"GUID*", K_GUID, 0},
    {"D3DRENDERSTATETYPE", K_RS, 0}, {"D3DTEXTURESTAGESTATETYPE", K_TSS, 0}, {"D3DTRANSFORMSTATETYPE", K_XFORM, 0},
    {"D3DPRIMITIVETYPE", K_PT, 0},
    {"LPDWORD", K_OUT_DWORD, 0}, {"DWORD*", K_OUT_DWORD, 0}, {"WINBOOL*", K_OUT_BOOL, 0}, {"LPLONG", K_OUT_LONG, 0},
    {"void**", K_VOIDPP, 0}, {"LPVOID*", K_VOIDPP, 0},
    {"LPDIRECTDRAWSURFACE7", K_IFACE_IN, IF_SURF7}, {"IDirectDrawSurface7*", K_IFACE_IN, IF_SURF7},
    {"LPDIRECTDRAWSURFACE7*", K_IFACE_OUT, IF_SURF7}, {"IDirectDrawSurface7**", K_IFACE_OUT, IF_SURF7},
    {"IDirect3DVertexBuffer7*", K_IFACE_IN, IF_VB7}, {"IDirect3DVertexBuffer7**", K_IFACE_OUT, IF_VB7},
    {"IDirect3DDevice7*", K_IFACE_IN, IF_DEV7}, {"IDirect3DDevice7**", K_IFACE_OUT, IF_DEV7},
    {"IDirect3D7*", K_IFACE_IN, IF_D3D7}, {"IDirect3D7**", K_IFACE_OUT, IF_D3D7},
    {"LPDIRECTDRAWCLIPPER*", K_RAWOUT, 0}, {"LPDIRECTDRAWPALETTE*", K_RAWOUT, 0},
    {"LPDDENUMMODESCALLBACK2", K_CALLBACK, CB_MODES}, {"LPDDENUMSURFACESCALLBACK7", K_CALLBACK, CB_SURFACES},
    {"LPD3DENUMDEVICESCALLBACK7", K_CALLBACK, CB_DEVICES}, {"LPD3DENUMPIXELFORMATSCALLBACK", K_CALLBACK, CB_PIXFMT},
    {"LPDDSURFACEDESC2", K_STRUCT, ST_DDSD2}, {"LPDDCAPS", K_STRUCT, ST_DDCAPS}, {"LPDDSCAPS2", K_STRUCT, ST_DDSCAPS2},
    {"LPRECT", K_STRUCT, ST_RECT}, {"RECT*", K_STRUCT, ST_RECT}, {"LPPOINT", K_STRUCT, ST_POINT},
    {"POINT*", K_STRUCT, ST_POINT}, {"LPDDBLTFX", K_STRUCT, ST_BLTFX}, {"LPDDCOLORKEY", K_STRUCT, ST_COLORKEY},
    {"LPDDPIXELFORMAT", K_STRUCT, ST_DDPF}, {"D3DDEVICEDESC7*", K_STRUCT, ST_DEVDESC7},
    {"D3DVIEWPORT7*", K_STRUCT, ST_VIEWPORT}, {"D3DMATERIAL7*", K_STRUCT, ST_MATERIAL},
    {"D3DLIGHT7*", K_STRUCT, ST_LIGHT}, {"D3DMATRIX*", K_STRUCT, ST_MATRIX}, {"D3DRECT*", K_STRUCT, ST_D3DRECT},
    {"D3DVERTEXBUFFERDESC*", K_STRUCT, ST_VBDESC}, {"D3DCLIPSTATUS*", K_STRUCT, ST_CLIPSTATUS},
    {"LPDDDEVICEIDENTIFIER2", K_STRUCT, ST_DEVIDENT2},
};

static int ci_contains(const char *hay, const char *needle) {
    size_t n = strlen(needle), i, j;
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < n; j++) {
            char a = hay[i + j], b = needle[j];
            if (!a)
                return 0;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b)
                break;
        }
        if (j == n)
            return 1;
    }
    return 0;
}

static int name_is_decimal(const char *n) {
    static const char *const dec[] = {"count", "num", "start", "stage", "index", "idx", "width", "height", "bpp", "refresh",
                                      "size", "stencil", "lod", "prio", "dwx", "dwy"};
    unsigned i;
    for (i = 0; i < sizeof dec / sizeof *dec; i++)
        if (ci_contains(n, dec[i]))
            return 1;
    return 0;
}

static void classify(ArgDef *d, const char *method) {
    unsigned i;
    d->kind = K_HEX;
    d->aux = 0;
    d->out = 0;
    for (i = 0; i < sizeof type_map / sizeof *type_map; i++)
        if (!strcmp(type_map[i].type, d->type)) {
            d->kind = type_map[i].kind;
            d->aux = type_map[i].aux;
            if (d->kind == K_STRUCT)
                d->out = (!strncmp(method, "Get", 3) || !strcmp(method, "Lock")) && d->aux != ST_RECT && d->aux != ST_POINT;
            if (d->kind == K_VOIDPP && !strcmp(method, "Lock"))
                d->kind = K_OUTPTR; /* IDirect3DVertexBuffer7::Lock hands back a data pointer, not an interface */
            return;
        }
    if (strchr(d->type, '*') || !strncmp(d->type, "LP", 2))
        d->kind = K_PTR;
    else if (name_is_decimal(d->name))
        d->kind = K_DEC;
}

static const struct { const char *name; uint8_t hook; } hook_map[] = {
    {"QueryInterface", H_QI}, {"AddRef", H_ADDREF}, {"Release", H_RELEASE}, {"GetDDInterface", H_GETDDINTERFACE},
    {"CreateSurface", H_CREATESURFACE}, {"GetAttachedSurface", H_GETATTACHED}, {"Flip", H_FLIP}, {"Blt", H_BLT},
    {"BltFast", H_BLTFAST}, {"Lock", H_LOCK}, {"Unlock", H_UNLOCK}, {"CreateVertexBuffer", H_CREATEVB},
};

static const char *const timing_methods[] = {"GetFlipStatus", "GetBltStatus", "WaitForVerticalBlank", "GetScanLine",
                                             "GetVerticalBlankStatus"};

static uint32_t dispatch(int iface, int slot, void *ra, const uint32_t *a) __attribute__((noinline));

#define THUNK_HEAD(t, s) static uint32_t WINAPI thunk_##t##_##s
#define THUNK_RET(t, s) return dispatch(IF_##t, s, __builtin_return_address(0), a)
#define THUNK_0(t, s) THUNK_HEAD(t, s)(uint32_t a0) { uint32_t a[1] = {a0}; THUNK_RET(t, s); }
#define THUNK_1(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1) { uint32_t a[2] = {a0, a1}; THUNK_RET(t, s); }
#define THUNK_2(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2) { uint32_t a[3] = {a0, a1, a2}; THUNK_RET(t, s); }
#define THUNK_3(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { uint32_t a[4] = {a0, a1, a2, a3}; THUNK_RET(t, s); }
#define THUNK_4(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) { uint32_t a[5] = {a0, a1, a2, a3, a4}; THUNK_RET(t, s); }
#define THUNK_5(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { uint32_t a[6] = {a0, a1, a2, a3, a4, a5}; THUNK_RET(t, s); }
#define THUNK_6(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) { uint32_t a[7] = {a0, a1, a2, a3, a4, a5, a6}; THUNK_RET(t, s); }
#define THUNK_7(t, s) THUNK_HEAD(t, s)(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7}; THUNK_RET(t, s); }

#define DDT_IFACE(t, n, c)
#define DDT_METHOD(t, s, n, na, args) THUNK_##na(t, s)
#include "methods.inc"
#undef DDT_IFACE
#undef DDT_METHOD

#define DDT_IFACE(t, n, c)
#define DDT_METHOD(t, s, n, na, args) (const void *)thunk_##t##_##s,
static const void *const all_thunks[] = {
#include "methods.inc"
};
#undef DDT_IFACE
#undef DDT_METHOD

void dispatch_init(void) {
    int i, s, k = 0, base = 0;
    InitializeCriticalSection(&g_reg_cs);
    for (i = 0; i < IF_COUNT; i++) {
        g_vtbl[i] = &all_thunks[base];
        base += slot_counts[i];
        for (s = 0; s < slot_counts[i]; s++, k++) {
            const RawMethod *r = &raw_methods[k];
            MethodDef *m = &g_methods[i][s];
            unsigned j;
            m->name = r->name;
            m->nargs = r->nargs;
            for (j = 0; j < r->nargs; j++) {
                m->a[j].type = r->a[2 * j];
                m->a[j].name = r->a[2 * j + 1];
                classify(&m->a[j], m->name);
            }
            for (j = 0; j < sizeof hook_map / sizeof *hook_map; j++)
                if (!strcmp(hook_map[j].name, m->name))
                    m->hook = hook_map[j].hook;
            for (j = 0; j < sizeof timing_methods / sizeof *timing_methods; j++)
                if (!strcmp(timing_methods[j], m->name))
                    m->timing = 1;
        }
    }
}

/* ---- calling the real object --------------------------------------------------------------------- */
typedef uint32_t(WINAPI *Fn0)(uint32_t);
typedef uint32_t(WINAPI *Fn1)(uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn2)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn3)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn4)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(WINAPI *Fn7)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static uint32_t call_real(void *fn, const uint32_t *r, unsigned nargs) {
    switch (nargs) {
    case 0: return ((Fn0)fn)(r[0]);
    case 1: return ((Fn1)fn)(r[0], r[1]);
    case 2: return ((Fn2)fn)(r[0], r[1], r[2]);
    case 3: return ((Fn3)fn)(r[0], r[1], r[2], r[3]);
    case 4: return ((Fn4)fn)(r[0], r[1], r[2], r[3], r[4]);
    case 5: return ((Fn5)fn)(r[0], r[1], r[2], r[3], r[4], r[5]);
    case 6: return ((Fn6)fn)(r[0], r[1], r[2], r[3], r[4], r[5], r[6]);
    default: return ((Fn7)fn)(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    }
}

/* ---- callbacks ------------------------------------------------------------------------------------ */
typedef struct CbCtx {
    void *orig_cb, *orig_ctx;
    int traced, depth;
    const char *method;
} CbCtx;

static void cb_line(const CbCtx *c, const char *args, uint32_t result) {
    char text[4096];
    snprintf(text, sizeof text, "cb %s(%s) = %d", c->method, args, (int)result);
    trace_emit(c->depth, text, 0);
}

static HRESULT WINAPI cb_modes(DDSURFACEDESC2 *d, void *ctx) {
    CbCtx *c = ctx;
    HRESULT hr = ((HRESULT(WINAPI *)(DDSURFACEDESC2 *, void *))c->orig_cb)(d, c->orig_ctx);
    if (c->traced) {
        char txt[2048], args[2200];
        Buf b;
        buf_init(&b, txt, sizeof txt);
        fmt_struct(&b, ST_DDSD2, d);
        snprintf(args, sizeof args, "lpDDSurfaceDesc=%s, lpContext=P", txt);
        cb_line(c, args, (uint32_t)hr);
    }
    return hr;
}

static HRESULT WINAPI cb_surfaces(IDirectDrawSurface7 *s, DDSURFACEDESC2 *d, void *ctx) {
    CbCtx *c = ctx;
    void *w = wrap_new(IF_SURF7, s);
    HRESULT hr = ((HRESULT(WINAPI *)(void *, DDSURFACEDESC2 *, void *))c->orig_cb)(w, d, c->orig_ctx);
    if (c->traced) {
        char txt[2048], args[2300], lbl[24];
        Buf b;
        buf_init(&b, txt, sizeof txt);
        fmt_struct(&b, ST_DDSD2, d);
        snprintf(args, sizeof args, "lpDDSurface=%s, lpDDSurfaceDesc=%s, lpContext=P", wrap_label(w, lbl), txt);
        cb_line(c, args, (uint32_t)hr);
    }
    return hr;
}

static HRESULT WINAPI cb_devices(LPSTR desc, LPSTR name, D3DDEVICEDESC7 *dd, void *ctx) {
    CbCtx *c = ctx;
    HRESULT hr = ((HRESULT(WINAPI *)(LPSTR, LPSTR, D3DDEVICEDESC7 *, void *))c->orig_cb)(desc, name, dd, c->orig_ctx);
    if (c->traced) {
        char txt[3072], args[3600];
        Buf b;
        buf_init(&b, txt, sizeof txt);
        fmt_struct(&b, ST_DEVDESC7, dd);
        snprintf(args, sizeof args, "lpDeviceDescription=\"%.200s\", lpDeviceName=\"%.200s\", lpD3DDeviceDesc=%s, lpContext=P",
                 desc ? desc : "", name ? name : "", txt);
        cb_line(c, args, (uint32_t)hr);
    }
    return hr;
}

static HRESULT WINAPI cb_pixfmt(DDPIXELFORMAT *pf, void *ctx) {
    CbCtx *c = ctx;
    HRESULT hr = ((HRESULT(WINAPI *)(DDPIXELFORMAT *, void *))c->orig_cb)(pf, c->orig_ctx);
    if (c->traced) {
        char txt[512], args[600];
        Buf b;
        buf_init(&b, txt, sizeof txt);
        fmt_struct(&b, ST_DDPF, pf);
        snprintf(args, sizeof args, "lpDDPixFmt=%s, lpContext=P", txt);
        cb_line(c, args, (uint32_t)hr);
    }
    return hr;
}

static void *const cb_shims[] = {(void *)cb_modes, (void *)cb_surfaces, (void *)cb_devices, (void *)cb_pixfmt};

/* ---- the dispatcher -------------------------------------------------------------------------------- */
typedef struct CallRec {
    Wrap *self;
    const MethodDef *m;
    int traced;
    uint32_t a[MAXARGS], r[MAXARGS];
    Wrap *outw[MAXARGS];
    CbCtx cb;
    GUID iid;
    char suffix[200];
    char txt[MAXARGS][ARG_TXT];
} CallRec;

static void fmt_guid_arg(Buf *b, const void *g) {
    char tmp[48];
    if ((uintptr_t)g < 0x10000 && g)
        buf_printf(b, "GUID(%u)", (unsigned)(uintptr_t)g);
    else if (IsBadReadPtr(g, sizeof(GUID)))
        buf_puts(b, g ? "BAD" : "NULL");
    else
        buf_puts(b, guid_name(g, tmp));
}

static void enum_name(Buf *b, const char *name, DWORD v, const char *what) {
    if (name)
        buf_printf(b, "%s(%u)", name, (unsigned)v);
    else
        buf_printf(b, "%u", (unsigned)v);
    (void)what;
}

/* Render argument i.  `post` is 0 before the real call (inputs) and 1 after it (outputs). */
static void render_arg(CallRec *c, unsigned i, int post, HRESULT hr) {
    const ArgDef *d = &c->m->a[i];
    uint32_t v = c->a[i + 1];
    Buf b;
    char lbl[24];
    int ok = SUCCEEDED(hr);

    buf_init(&b, c->txt[i], ARG_TXT);
    switch (d->kind) {
    case K_HEX: buf_printf(&b, "%s=0x%x", d->name, (unsigned)v); break;
    case K_DEC: buf_printf(&b, "%s=%d", d->name, (int)v); break;
    case K_F32: buf_printf(&b, "%s=f32:%08x", d->name, (unsigned)v); break;
    case K_PTR:
    case K_HANDLE:
    case K_CALLBACK: buf_printf(&b, "%s=%s", d->name, v ? "P" : "NULL"); break;
    case K_GUID:
        buf_printf(&b, "%s=", d->name);
        fmt_guid_arg(&b, (const void *)(uintptr_t)v);
        break;
    case K_IFACE_IN: buf_printf(&b, "%s=%s", d->name, wrap_label((void *)(uintptr_t)v, lbl)); break;
    case K_RS:
        buf_printf(&b, "%s=", d->name);
        enum_name(&b, rs_name(v), v, "rs");
        break;
    case K_TSS:
        buf_printf(&b, "%s=", d->name);
        enum_name(&b, tss_name(v), v, "tss");
        break;
    case K_XFORM:
        buf_printf(&b, "%s=", d->name);
        enum_name(&b, xform_name(v), v, "xform");
        break;
    case K_PT:
        buf_printf(&b, "%s=", d->name);
        enum_name(&b, pt_name(v), v, "pt");
        break;
    case K_STRUCT:
        buf_printf(&b, "%s%s", d->name, d->out ? "=>" : "=");
        if (d->out && !ok) {
            buf_puts(&b, "-");
        } else if (d->aux == ST_D3DRECT && i > 0 && v) {
            unsigned n = c->a[i], k;
            const D3DRECT *r = (const D3DRECT *)(uintptr_t)v;
            buf_puts(&b, "[");
            for (k = 0; k < n && k < 16; k++) {
                buf_printf(&b, "%s", k ? "," : "");
                fmt_struct(&b, ST_D3DRECT, r + k);
            }
            buf_puts(&b, "]");
        } else {
            fmt_struct(&b, d->aux, (const void *)(uintptr_t)v);
        }
        break;
    case K_OUT_DWORD:
    case K_OUT_BOOL:
    case K_OUT_LONG:
        buf_printf(&b, "%s=>", d->name);
        if (!v)
            buf_puts(&b, "NULL");
        else if (c->m->timing)
            buf_puts(&b, "?");
        else if (!ok || IsBadReadPtr((void *)(uintptr_t)v, 4))
            buf_puts(&b, "-");
        else if (d->kind == K_OUT_DWORD)
            buf_printf(&b, "0x%x", (unsigned)*(DWORD *)(uintptr_t)v);
        else
            buf_printf(&b, "%d", (int)*(LONG *)(uintptr_t)v);
        break;
    case K_OUTPTR:
        buf_printf(&b, "%s=>%s", d->name, (v && ok && !IsBadReadPtr((void *)(uintptr_t)v, 4) && *(void **)(uintptr_t)v) ? "P" : "NULL");
        break;
    case K_IFACE_OUT:
    case K_VOIDPP:
    case K_RAWOUT:
        buf_printf(&b, "%s->", d->name);
        if (!v || !ok || IsBadReadPtr((void *)(uintptr_t)v, 4) || !*(void **)(uintptr_t)v)
            buf_puts(&b, "NULL");
        else
            buf_puts(&b, wrap_label(*(void **)(uintptr_t)v, lbl));
        break;
    }
    (void)post;
}

static int is_out_kind(const ArgDef *d) {
    switch (d->kind) {
    case K_IFACE_OUT: case K_VOIDPP: case K_RAWOUT: case K_OUT_DWORD: case K_OUT_BOOL: case K_OUT_LONG: case K_OUTPTR: return 1;
    case K_STRUCT: return d->out;
    default: return 0;
    }
}

static int iface_for_iid(const GUID *iid) {
    if (IsBadReadPtr(iid, sizeof(GUID)))
        return -1;
    if (IsEqualGUID(iid, &IID_IDirectDraw7)) return IF_DD7;
    if (IsEqualGUID(iid, &IID_IDirect3D7)) return IF_D3D7;
    if (IsEqualGUID(iid, &IID_IDirect3DDevice7)) return IF_DEV7;
    if (IsEqualGUID(iid, &IID_IDirectDrawSurface7)) return IF_SURF7;
    if (IsEqualGUID(iid, &IID_IDirect3DVertexBuffer7)) return IF_VB7;
    return -1;
}

static int find_arg(const MethodDef *m, int kind) {
    unsigned i;
    for (i = 0; i < m->nargs; i++)
        if (m->a[i].kind == kind)
            return (int)i;
    return -1;
}

static int find_context(const MethodDef *m) {
    unsigned i;
    for (i = 0; i < m->nargs; i++)
        if (m->a[i].kind == K_PTR && (ci_contains(m->a[i].name, "ctx") || ci_contains(m->a[i].name, "context")))
            return (int)i;
    return -1;
}


/* ---- Lock / Unlock: the contents written between them are hashed at Unlock ------------------------------------ */
static unsigned fvf_vertex_size(DWORD fvf) {
    unsigned n = 0, tex, i;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ: n = 12; break;
    case D3DFVF_XYZRHW: n = 16; break;
    case D3DFVF_XYZB1: n = 16; break;
    case D3DFVF_XYZB2: n = 20; break;
    case D3DFVF_XYZB3: n = 24; break;
    case D3DFVF_XYZB4: n = 28; break;
    case D3DFVF_XYZB5: n = 32; break;
    default: break;
    }
    if (fvf & D3DFVF_NORMAL) n += 12;
    if (fvf & 0x20) n += 4; /* point size (D3DFVF_RESERVED1 in the D3D7 headers) */
    if (fvf & D3DFVF_DIFFUSE) n += 4;
    if (fvf & D3DFVF_SPECULAR) n += 4;
    tex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (i = 0; i < tex && i < 8; i++) {
        static const unsigned sizes[4] = {8, 12, 16, 4}; /* texcoord size bits: 2D, 3D, 4D, 1D */
        n += sizes[(fvf >> (16 + 2 * i)) & 3];
    }
    return n;
}

static void post_create_vb(CallRec *c) {
    const D3DVERTEXBUFFERDESC *d = (const D3DVERTEXBUFFERDESC *)(uintptr_t)c->a[1];
    if (c->outw[1] && !IsBadReadPtr(d, sizeof *d))
        c->outw[1]->vb_bytes = d->dwNumVertices * fvf_vertex_size(d->dwFVF);
}

static void post_lock(CallRec *c) {
    Wrap *w = c->self;
    w->locked = 0;
    if (w->iface == IF_VB7) {
        void **data = (void **)(uintptr_t)c->a[2];
        const DWORD *size = (const DWORD *)(uintptr_t)c->a[3];
        if (!data || IsBadReadPtr(data, sizeof *data) || !*data)
            return;
        w->lock_ptr = *data;
        w->lock_bytes = (size && !IsBadReadPtr(size, sizeof *size) && *size) ? *size : w->vb_bytes;
        w->lock_has_rect = 0;
        w->locked = 1;
    } else if (w->iface == IF_SURF7) {
        const RECT *rc = (const RECT *)(uintptr_t)c->a[1];
        const DWORD *d = (const DWORD *)(uintptr_t)c->a[2]; /* DDSURFACEDESC2 words */
        DWORD width, height, bpp, rowbytes, rows;
        if (!d || IsBadReadPtr(d, sizeof(DDSURFACEDESC2)) || !d[9]) /* Wine does not set DDSD_LPSURFACE; the pointer is enough */
            return;
        width = d[3];
        height = d[2];
        w->lock_has_rect = rc && !IsBadReadPtr(rc, sizeof *rc);
        if (w->lock_has_rect) {
            w->lock_rect = *rc;
            width = (DWORD)(rc->right - rc->left);
            height = (DWORD)(rc->bottom - rc->top);
        }
        w->lock_ptr = (void *)(uintptr_t)d[9];
        w->lock_pitch = (d[1] & DDSD_PITCH) ? d[4] : 0;
        if (d[19] & DDPF_FOURCC) { /* block compressed: one contiguous run */
            rowbytes = ((width + 3) / 4) * ((height + 3) / 4) * (d[20] == 0x31545844u /* 'DXT1' */ ? 8 : 16);
            rows = 1;
        } else {
            bpp = d[21];
            if (!bpp && d[3] && w->lock_pitch)
                bpp = w->lock_pitch * 8 / d[3];
            rowbytes = width * bpp / 8;
            rows = height;
            if (!w->lock_pitch)
                w->lock_pitch = rowbytes;
        }
        w->lock_rows = rows;
        w->lock_rowbytes = rowbytes;
        w->lock_bytes = rows * rowbytes;
        w->locked = 1;
    }
}

/* Before the real Unlock: what the plugin wrote while the buffer was locked. */
static void pre_unlock(CallRec *c) {
    Wrap *w = c->self;
    uint64_t h = FNV1A64_INIT;
    DWORD r;

    c->suffix[0] = 0;
    if (!w->locked)
        return;
    w->locked = 0;
    if (IsBadReadPtr(w->lock_ptr, w->iface == IF_VB7 ? w->lock_bytes : (w->lock_rows - 1) * w->lock_pitch + w->lock_rowbytes)) {
        snprintf(c->suffix, sizeof c->suffix, " locked={unreadable}");
        return;
    }
    if (w->iface == IF_VB7) {
        h = fnv1a64(w->lock_ptr, w->lock_bytes);
        snprintf(c->suffix, sizeof c->suffix, " locked={bytes=%u, hash=fnv1a64:%08x%08x}", (unsigned)w->lock_bytes,
                 (unsigned)(h >> 32), (unsigned)h);
    } else {
        char rect[64] = "NULL";
        for (r = 0; r < w->lock_rows; r++)
            h = fnv1a64_update(h, (const char *)w->lock_ptr + (size_t)r * w->lock_pitch, w->lock_rowbytes);
        if (w->lock_has_rect)
            snprintf(rect, sizeof rect, "{%d,%d,%d,%d}", (int)w->lock_rect.left, (int)w->lock_rect.top,
                     (int)w->lock_rect.right, (int)w->lock_rect.bottom);
        snprintf(c->suffix, sizeof c->suffix, " locked={rect=%s, bytes=%u, hash=fnv1a64:%08x%08x}", rect,
                 (unsigned)w->lock_bytes, (unsigned)(h >> 32), (unsigned)h);
    }
}

static uint32_t dispatch(int iface, int slot, void *ra, const uint32_t *a) {
    CallRec c;
    const MethodDef *m = &g_methods[iface][slot];
    Wrap *self = (Wrap *)(uintptr_t)a[0];
    uint32_t result;
    unsigned i;
    int cbi, ctxi;
    void **vt;
    int is_present = 0;

    c.self = self;
    c.m = m;
    c.traced = trace_wants(ra);
    c.suffix[0] = 0;
    for (i = 0; i <= m->nargs; i++) {
        c.a[i] = a[i];
        c.r[i] = a[i];
        c.outw[i] = NULL;
    }
    c.r[0] = (uint32_t)(uintptr_t)self->real;
    if (m->hook == H_QI && !IsBadReadPtr((void *)(uintptr_t)a[1], sizeof(GUID)))
        c.iid = *(const GUID *)(uintptr_t)a[1];

    for (i = 0; i < m->nargs; i++)
        if (m->a[i].kind == K_IFACE_IN)
            c.r[i + 1] = (uint32_t)(uintptr_t)unwrap((void *)(uintptr_t)a[i + 1]);

    /* enumeration callbacks: interface-carrying ones are always wrapped, the others only when tracing */
    cbi = find_arg(m, K_CALLBACK);
    ctxi = find_context(m);
    if (cbi >= 0 && ctxi >= 0 && a[cbi + 1] && (c.traced || m->a[cbi].aux == CB_SURFACES)) {
        c.cb.orig_cb = (void *)(uintptr_t)a[cbi + 1];
        c.cb.orig_ctx = (void *)(uintptr_t)a[ctxi + 1];
        c.cb.traced = c.traced;
        c.cb.depth = 1;
        c.cb.method = m->name;
        c.r[cbi + 1] = (uint32_t)(uintptr_t)cb_shims[m->a[cbi].aux];
        c.r[ctxi + 1] = (uint32_t)(uintptr_t)&c.cb;
    }

    if (c.traced)
        for (i = 0; i < m->nargs; i++)
            if (!is_out_kind(&m->a[i]))
                render_arg(&c, i, 0, S_OK);

    if (m->hook == H_UNLOCK && c.traced)
        pre_unlock(&c);

    vt = *(void ***)self->real;
    result = call_real(vt[slot], c.r, m->nargs);

    /* wrap interface results */
    for (i = 0; i < m->nargs; i++) {
        const ArgDef *d = &m->a[i];
        void **out;
        int target = -1;
        if (d->kind != K_IFACE_OUT && d->kind != K_VOIDPP)
            continue;
        if (FAILED((HRESULT)result) && m->hook != H_RELEASE)
            continue;
        out = (void **)(uintptr_t)a[i + 1];
        if (!out || IsBadWritePtr(out, sizeof *out) || !*out)
            continue;
        if (d->kind == K_IFACE_OUT)
            target = d->aux;
        else if (m->hook == H_QI)
            target = iface_for_iid(&c.iid);
        else if (m->hook == H_GETDDINTERFACE)
            target = IF_DD7;
        if (m->hook == H_QI && IsEqualGUID(&c.iid, &IID_IUnknown) && *out == self->real) {
            *out = self;
        } else if (target >= 0) {
            c.outw[i] = wrap_new(target, *out);
            if (m->hook == H_QI && c.outw[i] != self)
                c.outw[i]->family = self->family;
            *out = c.outw[i];
        }
    }

    switch (m->hook) {
    case H_RELEASE:
        if (result == 0)
            retire(self);
        break;
    case H_CREATESURFACE: {
        const DDSURFACEDESC2 *d = (const DDSURFACEDESC2 *)(uintptr_t)a[1];
        if (c.outw[1] && !IsBadReadPtr(d, sizeof *d) && (d->dwFlags & DDSD_CAPS))
            c.outw[1]->caps = d->ddsCaps.dwCaps;
        break;
    }
    case H_GETATTACHED: {
        const DDSCAPS2 *k = (const DDSCAPS2 *)(uintptr_t)a[1];
        if (c.outw[1] && !IsBadReadPtr(k, sizeof *k))
            c.outw[1]->caps |= k->dwCaps;
        break;
    }
    case H_LOCK:
        if (c.traced && SUCCEEDED((HRESULT)result))
            post_lock(&c);
        break;
    case H_CREATEVB: post_create_vb(&c); break;
    case H_FLIP: is_present = 1; break;
    case H_BLT:
    case H_BLTFAST: is_present = (self->caps & DDSCAPS_PRIMARYSURFACE) != 0; break;
    default: break;
    }

    if (c.traced) {
        char text[ARG_TXT * MAXARGS + 256], res[16], lbl[24];
        Buf b;
        buf_init(&b, text, sizeof text);
        for (i = 0; i < m->nargs; i++)
            if (is_out_kind(&m->a[i]))
                render_arg(&c, i, 1, (HRESULT)result);
        buf_printf(&b, "%s.%s(", wrap_label(self, lbl), m->name);
        for (i = 0; i < m->nargs; i++)
            buf_printf(&b, "%s%s", i ? ", " : "", c.txt[i]);
        if (m->hook == H_ADDREF || m->hook == H_RELEASE)
            buf_printf(&b, ") = %u", (unsigned)result);
        else if (m->timing)
            buf_puts(&b, ") = ? [timing]");
        else
            buf_printf(&b, ") = %s", hr_name((HRESULT)result, res));
        buf_puts(&b, c.suffix);
        trace_emit(0, b.p, m->timing);
        if (is_present)
            trace_present();
    }
    return result;
}

/* ---- exports ---------------------------------------------------------------------------------------- */
HRESULT WINAPI ddt_x_DirectDrawCreateEx(GUID *lpGuid, void **lplpDD, REFIID iid, IUnknown *pUnkOuter)
    __asm__("_ddt_x_DirectDrawCreateEx");
HRESULT WINAPI ddt_x_DirectDrawEnumerateA(void *cb, void *ctx) __asm__("_ddt_x_DirectDrawEnumerateA");
HRESULT WINAPI ddt_x_DirectDrawEnumerateExA(void *cb, void *ctx, DWORD flags) __asm__("_ddt_x_DirectDrawEnumerateExA");

typedef struct EnumCb {
    void *cb, *ctx;
    int traced;
    const char *method;
} EnumCb;

static BOOL WINAPI enum_cb(GUID *g, LPSTR desc, LPSTR name, LPVOID ctx) {
    EnumCb *e = ctx;
    BOOL r = ((BOOL(WINAPI *)(GUID *, LPSTR, LPSTR, LPVOID))e->cb)(g, desc, name, e->ctx);
    if (e->traced) {
        char t[1024], g_txt[256];
        Buf b;
        CbCtx c = {NULL, NULL, 1, 1, e->method};
        buf_init(&b, g_txt, sizeof g_txt);
        fmt_guid_arg(&b, g);
        snprintf(t, sizeof t, "lpGUID=%s, lpDriverDescription=\"%.200s\", lpDriverName=\"%.200s\", lpContext=P", g_txt,
                 desc ? desc : "", name ? name : "");
        cb_line(&c, t, (uint32_t)r);
    }
    return r;
}

static BOOL WINAPI enum_cb_ex(GUID *g, LPSTR desc, LPSTR name, LPVOID ctx, HMONITOR mon) {
    EnumCb *e = ctx;
    BOOL r = ((BOOL(WINAPI *)(GUID *, LPSTR, LPSTR, LPVOID, HMONITOR))e->cb)(g, desc, name, e->ctx, mon);
    if (e->traced) {
        char t[1024], g_txt[256];
        Buf b;
        CbCtx c = {NULL, NULL, 1, 1, e->method};
        buf_init(&b, g_txt, sizeof g_txt);
        fmt_guid_arg(&b, g);
        snprintf(t, sizeof t, "lpGUID=%s, lpDriverDescription=\"%.200s\", lpDriverName=\"%.200s\", lpContext=P, hm=%s",
                 g_txt, desc ? desc : "", name ? name : "", mon ? "P" : "NULL");
        cb_line(&c, t, (uint32_t)r);
    }
    return r;
}

HRESULT WINAPI ddt_x_DirectDrawCreateEx(GUID *lpGuid, void **lplpDD, REFIID iid, IUnknown *pUnkOuter) {
    typedef HRESULT(WINAPI * Fn)(GUID *, void **, REFIID, IUnknown *);
    Fn real = (Fn)ddt_next_proc("DirectDrawCreateEx");
    int traced = trace_wants(__builtin_return_address(0));
    HRESULT hr;
    char text[1024], g_txt[256], i_txt[256], res[16], lbl[24];
    Buf b;

    if (!real)
        return DDERR_GENERIC;
    hr = real(lpGuid, lplpDD, iid, pUnkOuter);
    if (SUCCEEDED(hr) && lplpDD && *lplpDD && iid && IsEqualGUID(iid, &IID_IDirectDraw7))
        *lplpDD = wrap_new(IF_DD7, *lplpDD);
    if (traced) {
        buf_init(&b, g_txt, sizeof g_txt);
        fmt_guid_arg(&b, lpGuid);
        buf_init(&b, i_txt, sizeof i_txt);
        fmt_guid_arg(&b, iid);
        snprintf(text, sizeof text, "DirectDrawCreateEx(lpGuid=%s, lplpDD->%s, iid=%s, pUnkOuter=%s) = %s", g_txt,
                 (SUCCEEDED(hr) && lplpDD && *lplpDD) ? wrap_label(*lplpDD, lbl) : "NULL", i_txt,
                 pUnkOuter ? "P" : "NULL", hr_name(hr, res));
        trace_emit(0, text, 0);
    }
    return hr;
}

HRESULT WINAPI ddt_x_DirectDrawEnumerateA(void *cb, void *ctx) {
    typedef HRESULT(WINAPI * Fn)(void *, void *);
    Fn real = (Fn)ddt_next_proc("DirectDrawEnumerateA");
    EnumCb e = {cb, ctx, trace_wants(__builtin_return_address(0)), "DirectDrawEnumerateA"};
    HRESULT hr;
    char text[256], res[16];

    if (!real)
        return DDERR_GENERIC;
    hr = e.traced && cb ? real((void *)enum_cb, &e) : real(cb, ctx);
    if (e.traced) {
        snprintf(text, sizeof text, "DirectDrawEnumerateA(lpCallback=%s, lpContext=%s) = %s", cb ? "P" : "NULL",
                 ctx ? "P" : "NULL", hr_name(hr, res));
        trace_emit(0, text, 0);
    }
    return hr;
}

HRESULT WINAPI ddt_x_DirectDrawEnumerateExA(void *cb, void *ctx, DWORD flags) {
    typedef HRESULT(WINAPI * Fn)(void *, void *, DWORD);
    Fn real = (Fn)ddt_next_proc("DirectDrawEnumerateExA");
    EnumCb e = {cb, ctx, trace_wants(__builtin_return_address(0)), "DirectDrawEnumerateExA"};
    HRESULT hr;
    char text[256], res[16];

    if (!real)
        return DDERR_GENERIC;
    hr = e.traced && cb ? real((void *)enum_cb_ex, &e, flags) : real(cb, ctx, flags);
    if (e.traced) {
        snprintf(text, sizeof text, "DirectDrawEnumerateExA(lpCallback=%s, lpContext=%s, dwFlags=0x%x) = %s",
                 cb ? "P" : "NULL", ctx ? "P" : "NULL", (unsigned)flags, hr_name(hr, res));
        trace_emit(0, text, 0);
    }
    return hr;
}
