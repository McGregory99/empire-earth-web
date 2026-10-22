/*
 * Screen checks (task P2.6.1): the 20-slot GEScreen object and the rasterizer slots that expose it.
 * Included by harness.c after CHECK, INFO, counter(), slot(), THISCALL and proc_address().
 *
 * Both variants are driven the same way and must give the same answers:
 *
 *   - "fake": a scripted IDirectDraw7 / IDirect3D7 (plain C vtables below) is handed to the screen's
 *     private constructor, which both DLLs export under the original's decorated name.  The fake logs
 *     every call it gets and returns scripted modes and capabilities, so the screen's observable
 *     behaviour (getters, the calls it makes, the arguments of those calls) is deterministic, and no
 *     display mode is ever really changed.  The expected values below were first observed on the
 *     original DX7HRTnLDisplay.dll (variant "original"); "ours" must reproduce them.
 *   - "real": the rasterizer's ActivateRasterizer slot enumerates the real DirectDraw devices of the
 *     Wine prefix.  Their values are not known in advance, so they are printed as "obs" lines, and
 *     `make test-rasterizer` requires the two variants to print identical lines (INFO lines
 *     are not compared: they hold what is known to differ).
 */
#include "ge_layouts.h"

#include <stdarg.h>

#define FAKE_DD_STDCALL __attribute__((stdcall))

typedef struct FakeMode {
    unsigned w, h, hz, bpp, r, g, b, a;
} FakeMode;

typedef struct FakeDD FakeDD;
struct FakeDD {
    const void *const *vtbl; /* must stay first: this is the COM object */
    char log[32768];
    unsigned long refs;
    long fail_coop_calls; /* the next n SetCooperativeLevel calls fail */
    long test_coop_result; /* what TestCooperativeLevel returns */
    int hw_tnl;            /* what the fake IDirect3D7 reports: a hardware T&L device or not */
    const void *const *d3d_vtbl;
    const void *const *device_vtbl;
    const void *const *surface_vtbl;
    const void *const *vb_vtbl;
    /* surfaces (surface_checks.h) */
    int surface_count;                  /* ids handed out so far */
    void *by_id[64];                    /* the fake surfaces by id */
    long create_results[8];             /* scripted results of the next CreateSurface calls (0 = success) */
    int create_result_count, create_result_pos;
    long create_device_result;          /* what IDirect3D7::CreateDevice returns */
    unsigned long device_depth_mask;    /* dwDeviceRenderBitDepth of the fake device */
    unsigned long texture_formats[8][5]; /* {bit count, r, g, b, a} of the formats EnumTextureFormats offers */
    int texture_format_count;
    int texture_formats_delivered;       /* how many of them the callback saw (it stops the enumeration when it has enough) */
    const FakeMode *modes;               /* the modes EnumDisplayModes offers; null: kFakeModes (P2.6.3) */
    unsigned mode_count;
    /* the device's state methods (state_checks.h, P2.6.4) */
    long rs_fail;                        /* the next n SetRenderState calls fail (and are still logged) */
    long set_light_result;               /* what SetLight returns */
    long enable_result;                  /* what LightEnable returns */
    /* the device and scene lifecycle (lifecycle_checks.h, P2.6.5) */
    long qi_d3d_result;                  /* what QueryInterface(IDirect3D7) returns (non-zero: no interface) */
    long add_attached_code;              /* the next `add_attached_times` AddAttachedSurface calls (of any surface) return this */
    int add_attached_times;
    long create_vb_result;               /* what IDirect3D7::CreateVertexBuffer returns */
    struct {
        long code;
        int times;
    } dev_script[8];                     /* scripted results of BeginScene/EndScene/Clear/SetRenderTarget: `code` is returned `times` times */
    void *devices[8];                    /* what the fake IDirect3D7 handed out, in order (named by the tests) */
    int device_count;
    void *d3ds[8];
    int d3d_count;
    void *vbs[8];
    int vb_count;
    int d3d_released, device_released, vb_released; /* Release calls seen, to tell what an activation left unreleased */
    /* textures and the vertex-buffer ring (texture_checks.h, P2.6.6) */
    long tex_fail;                       /* the next n SetTexture / SetTextureStageState calls of any device fail (and are still logged) */
    long validate_result;                /* what ValidateDevice returns */
    unsigned long validate_passes;       /* what it stores in the pass count */
    struct {
        long code;
        int times;
    } vb_script[2];                      /* scripted results of the vertex buffer's Lock (0) and Unlock (1): `code` is returned `times` times */
    int vb_null_lock_times;              /* the next n Lock calls succeed without storing a pointer */
    void *text_dc;                       /* what a surface's GetDC hands out when set (text_checks.h, P2.6.7e): a real memory device context */
};

enum { FD_BEGIN, FD_END, FD_CLEAR, FD_SRT, FD_DRAW_PRIMITIVE, FD_DRAW_INDEXED };
enum { FV_LOCK, FV_UNLOCK };

typedef struct FakeD3D {
    const void *const *vtbl;
    FakeDD *dd;
} FakeD3D;

typedef struct FakeDevice {
    const void *const *vtbl;
    FakeDD *dd;
    int id;                              /* 0 for the first screen's device; the log of the others carries "d<id>." */
} FakeDevice;

static void fake_log(FakeDD *dd, const char *fmt, ...)
{
    size_t used = strlen(dd->log);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dd->log + used, sizeof dd->log - used, fmt, ap);
    va_end(ap);
    strncat(dd->log, "\n", sizeof dd->log - strlen(dd->log) - 1);
}

static const FakeMode kFakeModes[] = {
    {640, 480, 60, 16, 0xf800, 0x07e0, 0x001f, 0},       /* 565 -> format 7 */
    {640, 480, 60, 32, 0xff0000, 0xff00, 0xff, 0},       /* X8R8G8B8 -> format 1 */
    {800, 600, 60, 16, 0xf800, 0x07e0, 0x001f, 0},
    {800, 600, 75, 32, 0xff0000, 0xff00, 0xff, 0},
    {1024, 768, 60, 32, 0xff0000, 0xff00, 0xff, 0},      /* the current mode of the fake */
    {1024, 768, 60, 16, 0x7c00, 0x03e0, 0x001f, 0},      /* 555 -> format 5 */
    {1024, 768, 60, 8, 0, 0, 0, 0},                      /* palettized: no format in the table -> 14 */
    {1280, 1024, 60, 32, 0xff0000, 0xff00, 0xff, 0xff000000u}, /* A8R8G8B8 -> format 0 */
};
#define FAKE_MODE_COUNT ((unsigned)(sizeof kFakeModes / sizeof kFakeModes[0]))
#define FAKE_CURRENT_MODE 4u
static const unsigned kFakeFormat[] = {7, 1, 7, 1, 1, 5, 14, 0};

static void fake_fill_mode(DWORD *desc, const FakeMode *m)
{
    memset(desc, 0, 0x7c);
    desc[0] = 0x7c;
    desc[2] = m->h;
    desc[3] = m->w;
    desc[6] = m->hz;
    desc[0x48 / 4] = 0x20;
    desc[0x54 / 4] = m->bpp;
    desc[0x58 / 4] = m->r;
    desc[0x5c / 4] = m->g;
    desc[0x60 / 4] = m->b;
    desc[0x64 / 4] = m->a;
}

static const unsigned char kIidDirect3D7[16] = {0x77, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11,
                                                0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};

static long FAKE_DD_STDCALL fake_qi(FakeDD *dd, const void *iid, void **out)
{
    int is_d3d = memcmp(iid, kIidDirect3D7, 16) == 0;
    fake_log(dd, "dd.QueryInterface(%s)", is_d3d ? "IDirect3D7" : "other");
    if (!is_d3d) {
        *out = NULL;
        return (long)0x80004002; /* E_NOINTERFACE */
    }
    if (dd->qi_d3d_result != 0) {
        *out = NULL;
        return dd->qi_d3d_result;
    }
    FakeD3D *d3d = (FakeD3D *)calloc(1, sizeof *d3d);
    d3d->vtbl = dd->d3d_vtbl;
    d3d->dd = dd;
    if (dd->d3d_count < 8) {
        dd->d3ds[dd->d3d_count++] = d3d;
    }
    *out = d3d;
    return 0;
}

static unsigned long FAKE_DD_STDCALL fake_add_ref(FakeDD *dd)
{
    return ++dd->refs;
}

static unsigned long FAKE_DD_STDCALL fake_release(FakeDD *dd)
{
    fake_log(dd, "dd.Release");
    return --dd->refs;
}

static long FAKE_DD_STDCALL fake_enum_display_modes(FakeDD *dd, DWORD flags, void *desc, void *ctx,
                                                    int(FAKE_DD_STDCALL *cb)(void *, void *))
{
    fake_log(dd, "dd.EnumDisplayModes(flags=%lu,desc=%s)", (unsigned long)flags, desc ? "set" : "null");
    const FakeMode *modes = dd->modes ? dd->modes : kFakeModes;
    const unsigned count = dd->modes ? dd->mode_count : FAKE_MODE_COUNT;
    for (unsigned i = 0; i < count; i++) {
        DWORD mode[0x7c / 4];
        fake_fill_mode(mode, &modes[i]);
        if (!cb(mode, ctx)) {
            break;
        }
    }
    return 0;
}

static long FAKE_DD_STDCALL fake_get_caps(FakeDD *dd, DWORD *hw, DWORD *hel)
{
    fake_log(dd, "dd.GetCaps(size=%lu,hel=%s)", (unsigned long)hw[0], hel ? "set" : "null");
    hw[1] = 0x1 | 0x8000;
    return 0;
}

static long FAKE_DD_STDCALL fake_get_display_mode(FakeDD *dd, DWORD *desc)
{
    fake_log(dd, "dd.GetDisplayMode(size=%lu)", (unsigned long)desc[0]);
    fake_fill_mode(desc, &kFakeModes[FAKE_CURRENT_MODE]);
    return 0;
}

static long FAKE_DD_STDCALL fake_set_coop(FakeDD *dd, void *hwnd, DWORD flags)
{
    fake_log(dd, "dd.SetCooperativeLevel(hwnd=0x%lx,flags=0x%lx)", (unsigned long)hwnd, (unsigned long)flags);
    if (dd->fail_coop_calls > 0) {
        dd->fail_coop_calls--;
        return (long)0x88760082; /* DDERR_EXCLUSIVEMODEALREADYSET */
    }
    return 0;
}

static long FAKE_DD_STDCALL fake_set_display_mode(FakeDD *dd, DWORD w, DWORD h, DWORD bpp, DWORD hz, DWORD flags)
{
    fake_log(dd, "dd.SetDisplayMode(%lux%lux%lu,hz=%lu,flags=%lu)", (unsigned long)w, (unsigned long)h,
             (unsigned long)bpp, (unsigned long)hz, (unsigned long)flags);
    return 0;
}

/* ---- surfaces ------------------------------------------------------------------------------------
 * A scripted IDirectDrawSurface7.  Every call is logged with its arguments ("s<id>.<Method>(...)"), results
 * can be scripted per method (a result is returned `times` times, then the call succeeds again), and Lock
 * hands out a buffer the tests fill and read. */
enum { FS_ADD, FS_BLT, FS_DEL, FS_FLIP, FS_GETATT, FS_GETDC, FS_RELDC, FS_LOCK, FS_UNLOCK, FS_RESTORE, FS_SETCK, FS_METHODS };

typedef struct FakeSurface {
    const void *const *vtbl; /* must stay first */
    FakeDD *dd;
    int id;
    unsigned long refs;
    unsigned char *mem;
    unsigned long pitch;
    struct {
        long code;
        int times;
    } script[FS_METHODS];
} FakeSurface;

static void fake_dump_rect(char *out, size_t size, const long *r)
{
    if (!r) {
        snprintf(out, size, "null");
    } else {
        snprintf(out, size, "[%ld,%ld,%ld,%ld]", r[0], r[1], r[2], r[3]);
    }
}

static long fake_surface_result(FakeSurface *s, int method)
{
    if (s->script[method].times > 0) {
        s->script[method].times--;
        return s->script[method].code;
    }
    return 0;
}

static FakeSurface *fake_surface_new(FakeDD *dd, const DWORD *desc)
{
    FakeSurface *s = (FakeSurface *)calloc(1, sizeof *s);
    s->vtbl = dd->surface_vtbl;
    s->dd = dd;
    s->id = ++dd->surface_count;
    if (s->id < 64) {
        dd->by_id[s->id] = s;
    }
    s->refs = 1;
    unsigned long bits = desc[0x54 / 4] ? desc[0x54 / 4] : 32;
    unsigned long width = desc[3] ? desc[3] : 16;
    unsigned long height = desc[2] ? desc[2] : 16;
    s->pitch = (width * bits / 8 + 3) & ~3UL;
    s->mem = (unsigned char *)calloc(1, s->pitch * (height + 2) + 64);
    return s;
}

static long FAKE_DD_STDCALL fake_create_surface(FakeDD *dd, DWORD *desc, void **out, void *outer)
{
    (void)outer;
    char text[1024] = "";
    for (unsigned i = 0; i < 0x7c / 4; i++) {
        if (desc[i]) {
            char item[40];
            snprintf(item, sizeof item, "%s+0x%x=0x%lx", text[0] ? "," : "", i * 4, (unsigned long)desc[i]);
            strncat(text, item, sizeof text - strlen(text) - 1);
        }
    }
    long result = 0;
    if (dd->create_result_pos < dd->create_result_count) {
        result = dd->create_results[dd->create_result_pos++];
    }
    if (result != 0) {
        fake_log(dd, "dd.CreateSurface(%s) -> error 0x%lx", text, (unsigned long)result);
        *out = NULL;
        return result;
    }
    FakeSurface *s = fake_surface_new(dd, desc);
    fake_log(dd, "dd.CreateSurface(%s) -> s%d", text, s->id);
    *out = s;
    return 0;
}

static unsigned long FAKE_DD_STDCALL fake_surf_release(FakeSurface *s)
{
    fake_log(s->dd, "s%d.Release", s->id);
    return --s->refs;
}

static long FAKE_DD_STDCALL fake_surf_add_attached(FakeSurface *s, FakeSurface *other)
{
    fake_log(s->dd, "s%d.AddAttachedSurface(s%d)", s->id, other->id);
    if (s->dd->add_attached_times > 0) {
        s->dd->add_attached_times--;
        return s->dd->add_attached_code;
    }
    return fake_surface_result(s, FS_ADD);
}

static long FAKE_DD_STDCALL fake_surf_blt(FakeSurface *s, long *dst, FakeSurface *src, long *src_rect, DWORD flags, DWORD *fx)
{
    char d[64], r[64], f[64] = "null", sn[16] = "null";
    fake_dump_rect(d, sizeof d, dst);
    fake_dump_rect(r, sizeof r, src_rect);
    if (fx) {
        snprintf(f, sizeof f, "{size=%lu,fill=0x%lx}", (unsigned long)fx[0], (unsigned long)fx[0x50 / 4]);
    }
    if (src) {
        snprintf(sn, sizeof sn, "s%d", src->id);
    }
    fake_log(s->dd, "s%d.Blt(dst=%s,src=%s,srcrect=%s,flags=0x%lx,fx=%s)", s->id, d, sn, r, (unsigned long)flags, f);
    return fake_surface_result(s, FS_BLT);
}

static long FAKE_DD_STDCALL fake_surf_delete_attached(FakeSurface *s, DWORD flags, FakeSurface *other)
{
    fake_log(s->dd, "s%d.DeleteAttachedSurface(flags=%lu,s%d)", s->id, (unsigned long)flags, other ? other->id : 0);
    return fake_surface_result(s, FS_DEL);
}

static long FAKE_DD_STDCALL fake_surf_flip(FakeSurface *s, FakeSurface *target, DWORD flags)
{
    fake_log(s->dd, "s%d.Flip(target=%s,flags=0x%lx)", s->id, target ? "set" : "null", (unsigned long)flags);
    return fake_surface_result(s, FS_FLIP);
}

static long FAKE_DD_STDCALL fake_surf_get_attached(FakeSurface *s, DWORD *caps, FakeSurface **out)
{
    long result = fake_surface_result(s, FS_GETATT);
    if (result != 0) {
        fake_log(s->dd, "s%d.GetAttachedSurface(caps=0x%lx) -> error 0x%lx", s->id, (unsigned long)caps[0], (unsigned long)result);
        return result;
    }
    DWORD desc[0x7c / 4];
    memset(desc, 0, sizeof desc);
    desc[3] = 16;
    desc[2] = 16;
    FakeSurface *child = fake_surface_new(s->dd, desc);
    fake_log(s->dd, "s%d.GetAttachedSurface(caps=0x%lx) -> s%d", s->id, (unsigned long)caps[0], child->id);
    *out = child;
    return 0;
}

static long FAKE_DD_STDCALL fake_surf_get_dc(FakeSurface *s, void **out)
{
    long result = fake_surface_result(s, FS_GETDC);
    fake_log(s->dd, "s%d.GetDC", s->id);
    if (result == 0) {
        *out = s->dd->text_dc ? s->dd->text_dc : (void *)(0xDC0000UL + (unsigned long)s->id);
    }
    return result;
}

static long FAKE_DD_STDCALL fake_surf_release_dc(FakeSurface *s, void *dc)
{
    fake_log(s->dd, "s%d.ReleaseDC(0x%lx)", s->id, (unsigned long)dc);
    return fake_surface_result(s, FS_RELDC);
}

static long FAKE_DD_STDCALL fake_surf_lock(FakeSurface *s, long *rect, DWORD *desc, DWORD flags, void *event)
{
    char r[64];
    fake_dump_rect(r, sizeof r, rect);
    fake_log(s->dd, "s%d.Lock(rect=%s,desc.size=%lu,flags=0x%lx,event=0x%lx)", s->id, r, (unsigned long)desc[0], (unsigned long)flags,
             (unsigned long)event);
    long result = fake_surface_result(s, FS_LOCK);
    if (result == 0) {
        desc[0x10 / 4] = s->pitch;
        desc[0x24 / 4] = (DWORD)(uintptr_t)s->mem;
    }
    return result;
}

static long FAKE_DD_STDCALL fake_surf_unlock(FakeSurface *s, long *rect)
{
    char r[64];
    fake_dump_rect(r, sizeof r, rect);
    fake_log(s->dd, "s%d.Unlock(rect=%s)", s->id, r);
    return fake_surface_result(s, FS_UNLOCK);
}

static long FAKE_DD_STDCALL fake_surf_restore(FakeSurface *s)
{
    fake_log(s->dd, "s%d.Restore", s->id);
    return fake_surface_result(s, FS_RESTORE);
}

static long FAKE_DD_STDCALL fake_surf_set_color_key(FakeSurface *s, DWORD flags, DWORD *key)
{
    fake_log(s->dd, "s%d.SetColorKey(flags=0x%lx,low=0x%lx,high=0x%lx)", s->id, (unsigned long)flags, (unsigned long)key[0], (unsigned long)key[1]);
    return fake_surface_result(s, FS_SETCK);
}

static long FAKE_DD_STDCALL fake_get_avail_vid_mem(FakeDD *dd, DWORD *caps, DWORD *total, DWORD *avail)
{
    fake_log(dd, "dd.GetAvailableVidMem(caps=0x%lx)", (unsigned long)caps[0]);
    *total = 134217728;
    *avail = 100663296;
    return 0;
}

static long FAKE_DD_STDCALL fake_restore_all(FakeDD *dd)
{
    fake_log(dd, "dd.RestoreAllSurfaces");
    return 0;
}

static long FAKE_DD_STDCALL fake_test_coop(FakeDD *dd)
{
    fake_log(dd, "dd.TestCooperativeLevel");
    return dd->test_coop_result;
}

static void FAKE_DD_STDCALL fake_unexpected(void)
{
    printf("FAIL the fake IDirectDraw7 got a call it does not implement\n");
    fflush(stdout);
    ExitProcess(97);
}

static long FAKE_DD_STDCALL fake_enum_devices(FakeD3D *d3d, long(FAKE_DD_STDCALL *cb)(char *, char *, void *, void *), void *ctx)
{
    fake_log(d3d->dd, "d3d.EnumDevices");
    DWORD desc7[0xe0 / 4];
    memset(desc7, 0, sizeof desc7);
    char name[] = "fake device";
    /* A software-style device first, then (optionally) the hardware T&L one; the callback stops the enumeration at the T&L one. */
    desc7[0] = 0x00000000;
    if (cb(name, name, desc7, ctx) == 0) {
        return 0;
    }
    desc7[0] = d3d->dd->hw_tnl ? 0x00090000u : 0x00080000u;
    cb(name, name, desc7, ctx);
    return 0;
}

static unsigned long FAKE_DD_STDCALL fake_d3d_release(FakeD3D *d3d)
{
    fake_log(d3d->dd, "d3d.Release");
    d3d->dd->d3d_released++;
    return 0;
}

static long FAKE_DD_STDCALL fake_d3d_create_device(FakeD3D *d3d, const unsigned char *iid, FakeSurface *surface, FakeDevice **out)
{
    static const unsigned char kIidHal[16] = {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11,
                                              0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e};
    static const unsigned char kIidTnLHal[16] = {0x78, 0x9e, 0x04, 0xf5, 0x61, 0x48, 0xd2, 0x11,
                                                 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8};
    fake_log(d3d->dd, "d3d.CreateDevice(%s,s%d)", memcmp(iid, kIidHal, 16) == 0 ? "HAL" : memcmp(iid, kIidTnLHal, 16) == 0 ? "TnLHAL" : "other",
             surface ? surface->id : 0);
    if (d3d->dd->create_device_result != 0) {
        *out = NULL;
        return d3d->dd->create_device_result;
    }
    FakeDevice *device = (FakeDevice *)calloc(1, sizeof *device);
    device->vtbl = d3d->dd->device_vtbl;
    device->dd = d3d->dd;
    device->id = d3d->dd->device_count; /* the logs of the devices after the first carry "d<id>." */
    if (d3d->dd->device_count < 8) {
        d3d->dd->devices[d3d->dd->device_count++] = device;
    }
    *out = device;
    return 0;
}

/* IDirect3D7::CreateVertexBuffer (P2.6.5): logs the D3DVERTEXBUFFERDESC (size, caps, FVF, vertex count) and the flags. */
typedef struct FakeVB {
    const void *const *vtbl;
    FakeDD *dd;
    int id;
    unsigned char *memory;               /* what Lock hands out: 0x98a vertices of 0x24 bytes */
} FakeVB;

static long FAKE_DD_STDCALL fake_d3d_create_vb(FakeD3D *d3d, const DWORD *desc, FakeVB **out, DWORD flags)
{
    fake_log(d3d->dd, "d3d.CreateVertexBuffer(%lx,%lx,%lx,%lx,flags=%lx)", (unsigned long)desc[0], (unsigned long)desc[1], (unsigned long)desc[2],
             (unsigned long)desc[3], (unsigned long)flags);
    if (d3d->dd->create_vb_result != 0) {
        return d3d->dd->create_vb_result;
    }
    FakeVB *vb = (FakeVB *)calloc(1, sizeof *vb);
    vb->vtbl = d3d->dd->vb_vtbl;
    vb->dd = d3d->dd;
    vb->id = d3d->dd->vb_count;
    vb->memory = (unsigned char *)calloc(0x98a, 0x24);
    if (d3d->dd->vb_count < 8) {
        d3d->dd->vbs[d3d->dd->vb_count++] = vb;
    }
    *out = vb;
    return 0;
}

static unsigned long FAKE_DD_STDCALL fake_vb_release(FakeVB *vb)
{
    char prefix[16] = "";
    if (vb->id) {
        snprintf(prefix, sizeof prefix, "v%d.", vb->id);
    }
    fake_log(vb->dd, "%svb.Release", prefix);
    vb->dd->vb_released++;
    return 0;
}

/* IDirect3DVertexBuffer7::Lock / Unlock (P2.6.6): the log shows the flags and the size argument; the pointer is the buffer's memory. */
static long FAKE_DD_STDCALL fake_vb_lock(FakeVB *vb, DWORD flags, void **data, DWORD *size)
{
    char prefix[16] = "";
    if (vb->id) {
        snprintf(prefix, sizeof prefix, "v%d.", vb->id);
    }
    fake_log(vb->dd, "%svb.Lock(%lx,%lx)", prefix, (unsigned long)flags, (unsigned long)(uintptr_t)size);
    if (vb->dd->vb_script[FV_LOCK].times > 0) {
        vb->dd->vb_script[FV_LOCK].times--;
        return vb->dd->vb_script[FV_LOCK].code; /* a refused call stores nothing */
    }
    if (vb->dd->vb_null_lock_times > 0) {
        vb->dd->vb_null_lock_times--;
        return 0;
    }
    *data = vb->memory;
    return 0;
}

static long FAKE_DD_STDCALL fake_vb_unlock(FakeVB *vb)
{
    char prefix[16] = "";
    if (vb->id) {
        snprintf(prefix, sizeof prefix, "v%d.", vb->id);
    }
    fake_log(vb->dd, "%svb.Unlock", prefix);
    if (vb->dd->vb_script[FV_UNLOCK].times > 0) {
        vb->dd->vb_script[FV_UNLOCK].times--;
        return vb->dd->vb_script[FV_UNLOCK].code;
    }
    return 0;
}

static unsigned long FAKE_DD_STDCALL fake_device_release(FakeDevice *device)
{
    char prefix[16] = "";
    if (device->id) {
        snprintf(prefix, sizeof prefix, "d%d.", device->id);
    }
    fake_log(device->dd, "%sdev.Release", prefix);
    device->dd->device_released++;
    return 0;
}

static long FAKE_DD_STDCALL fake_device_get_caps(FakeDevice *device, DWORD *desc7)
{
    fake_log(device->dd, "dev.GetCaps");
    memset(desc7, 0, 0xec);
    desc7[0x74 / 4] = (DWORD)device->dd->device_depth_mask;
    return 0;
}

static long FAKE_DD_STDCALL fake_device_enum_texture_formats(FakeDevice *device, long(FAKE_DD_STDCALL *cb)(void *, void *), void *ctx)
{
    fake_log(device->dd, "dev.EnumTextureFormats");
    for (int i = 0; i < device->dd->texture_format_count; i++) {
        DWORD pf[8];
        memset(pf, 0, sizeof pf);
        pf[0] = 32;
        pf[1] = 0x40;
        pf[3] = (DWORD)device->dd->texture_formats[i][0];
        pf[4] = (DWORD)device->dd->texture_formats[i][1];
        pf[5] = (DWORD)device->dd->texture_formats[i][2];
        pf[6] = (DWORD)device->dd->texture_formats[i][3];
        pf[7] = (DWORD)device->dd->texture_formats[i][4];
        device->dd->texture_formats_delivered++;
        if (cb(pf, ctx) == 0) {
            break;
        }
    }
    return 0;
}


/* The state methods of IDirect3DDevice7 (P2.6.4): each one logs its arguments as hex words; the logs of the
 * devices of later screens carry the prefix "d<id>.". */
static void fake_dev_log(FakeDevice *device, const char *fmt, ...)
{
    char text[1024];
    char prefix[16] = "";
    va_list ap;
    if (device->id) {
        snprintf(prefix, sizeof prefix, "d%d.", device->id);
    }
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fake_log(device->dd, "%s%s", prefix, text);
}

static void fake_dev_words(char *out, size_t size, const DWORD *words, unsigned count)
{
    out[0] = 0;
    for (unsigned i = 0; i < count; i++) {
        size_t used = strlen(out);
        snprintf(out + used, size - used, i ? ",%lx" : "%lx", (unsigned long)words[i]);
    }
}

static long FAKE_DD_STDCALL fake_dev_set_render_state(FakeDevice *device, DWORD state, DWORD value)
{
    fake_dev_log(device, "RS(%lx,%lx)", (unsigned long)state, (unsigned long)value);
    if (device->dd->rs_fail > 0) {
        device->dd->rs_fail--;
        return (long)0x80004005;
    }
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_set_transform(FakeDevice *device, DWORD state, const DWORD *matrix)
{
    char words[512];
    fake_dev_words(words, sizeof words, matrix, 16);
    fake_dev_log(device, "TR(%lx,%s)", (unsigned long)state, words);
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_set_viewport(FakeDevice *device, const DWORD *viewport)
{
    char words[128];
    fake_dev_words(words, sizeof words, viewport, 6);
    fake_dev_log(device, "VP(%s)", words);
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_set_material(FakeDevice *device, const DWORD *material)
{
    char words[256];
    fake_dev_words(words, sizeof words, material, 17);
    fake_dev_log(device, "MAT(%s)", words);
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_set_light(FakeDevice *device, DWORD index, const DWORD *light)
{
    char words[512];
    fake_dev_words(words, sizeof words, light, 26);
    fake_dev_log(device, "LIGHT(%lx,%s)", (unsigned long)index, words);
    return device->dd->set_light_result;
}

static long FAKE_DD_STDCALL fake_dev_light_enable(FakeDevice *device, DWORD index, DWORD enable)
{
    fake_dev_log(device, "ENABLE(%lx,%lx)", (unsigned long)index, (unsigned long)enable);
    return device->dd->enable_result;
}

/* The scene methods of IDirect3DDevice7 (P2.6.5). */
static long fake_dev_scripted(FakeDevice *device, int method)
{
    if (device->dd->dev_script[method].times > 0) {
        device->dd->dev_script[method].times--;
        return device->dd->dev_script[method].code;
    }
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_begin_scene(FakeDevice *device)
{
    fake_dev_log(device, "BEGIN");
    return fake_dev_scripted(device, FD_BEGIN);
}

static long FAKE_DD_STDCALL fake_dev_end_scene(FakeDevice *device)
{
    fake_dev_log(device, "END");
    return fake_dev_scripted(device, FD_END);
}

static long FAKE_DD_STDCALL fake_dev_set_render_target(FakeDevice *device, FakeSurface *surface, DWORD flags)
{
    fake_dev_log(device, "SRT(%s%d,%lx)", surface ? "s" : "null", surface ? surface->id : 0, (unsigned long)flags);
    return fake_dev_scripted(device, FD_SRT);
}

static long FAKE_DD_STDCALL fake_dev_clear(FakeDevice *device, DWORD count, const long *rects, DWORD flags, DWORD color, DWORD z, DWORD stencil)
{
    char text[256] = "";
    for (DWORD i = 0; i < count && i < 4; i++) {
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%s[%ld,%ld,%ld,%ld]", i ? "," : "", rects[4 * i], rects[4 * i + 1], rects[4 * i + 2], rects[4 * i + 3]);
    }
    fake_dev_log(device, "CLEAR(%lu,%s,%lx,%lx,%lx,%lx)", (unsigned long)count, text, (unsigned long)flags, (unsigned long)color, (unsigned long)z,
                 (unsigned long)stencil);
    return fake_dev_scripted(device, FD_CLEAR);
}

static long FAKE_DD_STDCALL fake_dev_set_texture_stage_state(FakeDevice *device, DWORD stage, DWORD type, DWORD value)
{
    fake_dev_log(device, "TSS(%lx,%lx,%lx)", (unsigned long)stage, (unsigned long)type, (unsigned long)value);
    if (device->dd->tex_fail > 0) {
        device->dd->tex_fail--;
        return (long)0x80004005;
    }
    return 0;
}

/* IDirect3DDevice7::SetTexture and ValidateDevice (P2.6.6). */
static long FAKE_DD_STDCALL fake_dev_set_texture(FakeDevice *device, DWORD stage, FakeSurface *surface)
{
    if (surface) {
        fake_dev_log(device, "TEX(%lx,s%d)", (unsigned long)stage, surface->id);
    } else {
        fake_dev_log(device, "TEX(%lx,null)", (unsigned long)stage);
    }
    if (device->dd->tex_fail > 0) {
        device->dd->tex_fail--;
        return (long)0x80004005;
    }
    return 0;
}

static long FAKE_DD_STDCALL fake_dev_validate(FakeDevice *device, DWORD *passes)
{
    fake_dev_log(device, "VALIDATE(in=%lx)", (unsigned long)*passes);
    *passes = (DWORD)device->dd->validate_passes;
    return device->dd->validate_result;
}

/* IDirect3DDevice7::DrawPrimitiveVB and DrawIndexedPrimitiveVB (P2.6.7b): the log shows the primitive type, the buffer, the first vertex, the
 * vertex count, the indices (and their count) and the flags; the result can be scripted. */
static long FAKE_DD_STDCALL fake_dev_draw_primitive_vb(FakeDevice *device, DWORD type, FakeVB *vb, DWORD start, DWORD count, DWORD flags)
{
    fake_dev_log(device, "DPVB(%lx,v%d,%lu,%lu,%lx)", (unsigned long)type, vb ? vb->id : -1, (unsigned long)start, (unsigned long)count, (unsigned long)flags);
    return fake_dev_scripted(device, FD_DRAW_PRIMITIVE);
}

static long FAKE_DD_STDCALL fake_dev_draw_indexed_vb(FakeDevice *device, DWORD type, FakeVB *vb, DWORD start, DWORD count, const unsigned short *indices,
                                                     DWORD index_count, DWORD flags)
{
    char text[256] = "";
    for (DWORD i = 0; i < index_count && i < 24; i++) {
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%s%u", i ? "," : "", (unsigned)indices[i]);
    }
    fake_dev_log(device, "DIPVB(%lx,v%d,%lu,%lu,[%s],%lu,%lx)", (unsigned long)type, vb ? vb->id : -1, (unsigned long)start, (unsigned long)count, text,
                 (unsigned long)index_count, (unsigned long)flags);
    return fake_dev_scripted(device, FD_DRAW_INDEXED);
}

static void fake_dd_init(FakeDD *dd)
{
    static const void *vtbl[30];
    static const void *d3d_vtbl[8];
    static const void *vb_vtbl[8];
    static const void *device_vtbl[48];
    static const void *surface_vtbl[49];
    for (int i = 0; i < 30; i++) {
        vtbl[i] = (const void *)fake_unexpected;
    }
    vtbl[0] = (const void *)fake_qi;
    vtbl[1] = (const void *)fake_add_ref;
    vtbl[2] = (const void *)fake_release;
    vtbl[6] = (const void *)fake_create_surface;
    vtbl[8] = (const void *)fake_enum_display_modes;
    vtbl[11] = (const void *)fake_get_caps;
    vtbl[12] = (const void *)fake_get_display_mode;
    vtbl[20] = (const void *)fake_set_coop;
    vtbl[21] = (const void *)fake_set_display_mode;
    vtbl[23] = (const void *)fake_get_avail_vid_mem;
    vtbl[25] = (const void *)fake_restore_all;
    vtbl[26] = (const void *)fake_test_coop;
    for (int i = 0; i < 8; i++) {
        d3d_vtbl[i] = (const void *)fake_unexpected;
        vb_vtbl[i] = (const void *)fake_unexpected;
    }
    d3d_vtbl[2] = (const void *)fake_d3d_release;
    d3d_vtbl[3] = (const void *)fake_enum_devices;
    d3d_vtbl[4] = (const void *)fake_d3d_create_device;
    d3d_vtbl[5] = (const void *)fake_d3d_create_vb;
    vb_vtbl[2] = (const void *)fake_vb_release;
    vb_vtbl[3] = (const void *)fake_vb_lock;
    vb_vtbl[4] = (const void *)fake_vb_unlock;
    for (int i = 0; i < 48; i++) {
        device_vtbl[i] = (const void *)fake_unexpected;
    }
    device_vtbl[2] = (const void *)fake_device_release;
    device_vtbl[3] = (const void *)fake_device_get_caps;
    device_vtbl[4] = (const void *)fake_device_enum_texture_formats;
    device_vtbl[5] = (const void *)fake_dev_begin_scene;
    device_vtbl[6] = (const void *)fake_dev_end_scene;
    device_vtbl[8] = (const void *)fake_dev_set_render_target;
    device_vtbl[10] = (const void *)fake_dev_clear;
    device_vtbl[31] = (const void *)fake_dev_draw_primitive_vb;
    device_vtbl[32] = (const void *)fake_dev_draw_indexed_vb;
    device_vtbl[35] = (const void *)fake_dev_set_texture;
    device_vtbl[37] = (const void *)fake_dev_set_texture_stage_state;
    device_vtbl[38] = (const void *)fake_dev_validate;
    device_vtbl[11] = (const void *)fake_dev_set_transform;
    device_vtbl[13] = (const void *)fake_dev_set_viewport;
    device_vtbl[16] = (const void *)fake_dev_set_material;
    device_vtbl[18] = (const void *)fake_dev_set_light;
    device_vtbl[20] = (const void *)fake_dev_set_render_state;
    device_vtbl[44] = (const void *)fake_dev_light_enable;
    for (int i = 0; i < 49; i++) {
        surface_vtbl[i] = (const void *)fake_unexpected;
    }
    surface_vtbl[2] = (const void *)fake_surf_release;
    surface_vtbl[3] = (const void *)fake_surf_add_attached;
    surface_vtbl[5] = (const void *)fake_surf_blt;
    surface_vtbl[8] = (const void *)fake_surf_delete_attached;
    surface_vtbl[11] = (const void *)fake_surf_flip;
    surface_vtbl[12] = (const void *)fake_surf_get_attached;
    surface_vtbl[17] = (const void *)fake_surf_get_dc;
    surface_vtbl[25] = (const void *)fake_surf_lock;
    surface_vtbl[26] = (const void *)fake_surf_release_dc;
    surface_vtbl[27] = (const void *)fake_surf_restore;
    surface_vtbl[29] = (const void *)fake_surf_set_color_key;
    surface_vtbl[32] = (const void *)fake_surf_unlock;
    memset(dd, 0, sizeof *dd);
    dd->vtbl = vtbl;
    dd->d3d_vtbl = d3d_vtbl;
    dd->device_vtbl = device_vtbl;
    dd->surface_vtbl = surface_vtbl;
    dd->vb_vtbl = vb_vtbl;
    /* A device with 16- and 32-bit render targets and an ARGB texture format: the probe finds 32-bit support. */
    dd->device_depth_mask = 0x400 | 0x100 | 0x800;
    dd->texture_format_count = 2;
    dd->texture_formats[0][0] = 16;
    dd->texture_formats[0][1] = 0xf800;
    dd->texture_formats[0][2] = 0x07e0;
    dd->texture_formats[0][3] = 0x001f;
    dd->texture_formats[1][0] = 32;
    dd->texture_formats[1][1] = 0x00ff0000;
    dd->texture_formats[1][2] = 0x0000ff00;
    dd->texture_formats[1][3] = 0x000000ff;
    dd->texture_formats[1][4] = 0xff000000u;
    dd->refs = 1;
    dd->test_coop_result = 0;
    dd->hw_tnl = 1;
}

/* ---- calling the screens ------------------------------------------------------------------- */

typedef void *(THISCALL *ScreenCtor)(void *self, void *monitor, void *dd, const UString *name, unsigned char attached,
                                     unsigned char shares_primary);
typedef unsigned long (__cdecl *StaticCount)(void);
typedef void *(__cdecl *StaticGet)(unsigned long index);
typedef unsigned char (THISCALL *ScreenBool)(void *self);
typedef unsigned long (THISCALL *ScreenULong)(void *self);
typedef void *(THISCALL *ScreenPtr)(void *self);
typedef void *(THISCALL *ScreenPtrArg)(void *self, unsigned long index);
typedef long (THISCALL *ScreenCapture)(void *self, const void *window, unsigned char fullscreen);
typedef long (THISCALL *ScreenPtrOp)(void *self, const void *window);
typedef long (THISCALL *ScreenSetMode)(void *self, unsigned long index);
typedef void *(THISCALL *Destroy)(void *self, unsigned flags);

typedef struct ScreenApi {
    ScreenCtor construct;
    StaticCount screen_count;
    StaticGet get_screen;
} ScreenApi;

static void *screen_slot(void *screen, int index)
{
    return (*(void *const **)screen)[index];
}

#define SCREEN_BOOL(screen, idx) (((ScreenBool)screen_slot(screen, idx))(screen) != 0)

/* operator new / delete of msvcrt.dll, the heap both plugins use for screens (the rasterizer deletes the screens it holds). */
static void *engine_new(size_t size)
{
    typedef void *(__cdecl *OperatorNew)(size_t);
    OperatorNew op_new;
    void *raw = proc_address(GetModuleHandleA("msvcrt.dll"), "??2@YAPAXI@Z");
    memcpy(&op_new, &raw, sizeof op_new);
    void *memory = op_new(size);
    memset(memory, 0, size);
    return memory;
}

/* Build a screen over `dd` with the engine's own UString for the name. */
static void *make_screen(const ScreenApi *api, FakeDD *dd, const char *name, int attached, int shares)
{
    UString *text = (UString *)calloc(1, sizeof *text);
    ustring_construct_from_chars(text, name);
    void *memory = engine_new(0x80); /* from msvcrt.dll, like the plugins' own operator new: Deactivate deletes it */
    void *screen = api->construct(memory, NULL, dd, text, (unsigned char)attached, (unsigned char)shares);
    ustring_destruct(text);
    free(text);
    return screen;
}

static void log_clear(FakeDD *dd)
{
    dd->log[0] = 0;
}

/* The calls P2.6.1 owns, in the order the constructor makes them (the original's Test32BitSupport adds
 * window and surface calls that belong to later tasks; they are filtered out). */
static void log_filter_constructor(const FakeDD *dd, char *out, size_t size)
{
    out[0] = 0;
    const char *p = dd->log;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (strncmp(p, "dd.GetCaps", 10) == 0 || strncmp(p, "dd.EnumDisplayModes", 19) == 0 ||
            strncmp(p, "dd.GetDisplayMode", 17) == 0) {
            strncat(out, p, n < size - strlen(out) - 2 ? n : size - strlen(out) - 2);
            strncat(out, "\n", size - strlen(out) - 1);
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
}

static void check_screen_object(const struct Expect *e, HMODULE h, const ScreenApi *api)
{
    FakeDD dd;
    fake_dd_init(&dd);

    unsigned long count_before = api->screen_count();
    void *screen = make_screen(api, &dd, "Fake DirectDraw Device", 1, 0);
    CHECK(screen != NULL, "%s: the screen's private constructor builds a screen over a DirectDraw object", e->label);
    if (!screen) {
        return;
    }
    CHECK(api->screen_count() == count_before + 1, "%s: DX7Screen::GetScreenCount() counts the new screen", e->label);
    CHECK(api->get_screen(count_before) == screen, "%s: DX7Screen::GetScreen(%lu) is the new screen", e->label, count_before);

    char filtered[1024];
    log_filter_constructor(&dd, filtered, sizeof filtered);
    CHECK(strcmp(filtered,
                 "dd.GetCaps(size=380,hel=null)\n"
                 "dd.EnumDisplayModes(flags=0,desc=null)\n"
                 "dd.GetDisplayMode(size=124)\n") == 0,
          "%s: the constructor asks GetCaps, EnumDisplayModes and GetDisplayMode, in that order (got \"%s\")", e->label,
          filtered);

    /* RTTI: DX7Screen derives from GEScreen, like the original */
    void *const *vtable = *(void *const **)screen;
    CHECK(in_module(h, vtable), "%s: the screen's vtable lies inside the plugin", e->label);
    if (in_module(h, vtable)) {
        const struct RttiLocator *col = locator_of(vtable);
        CHECK(in_module(h, col) && strcmp(col->type->name, ".?AVDX7Screen@@") == 0, "%s: the screen's RTTI names DX7Screen", e->label);
        CHECK(col->hierarchy->count == 2 && strcmp(col->hierarchy->bases[1]->type->name, ".?AVGEScreen@@") == 0,
              "%s: DX7Screen derives from GEScreen", e->label);
        CHECK(vtable_slot_count(h, vtable) == EEWEB_SLOTS_GEScreen, "%s: the screen's vtable has %d slots (GEScreen has 20)", e->label,
              (int)vtable_slot_count(h, vtable));
    }

    /* getters */
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetModeCount))(screen) == FAKE_MODE_COUNT,
          "%s: GetModeCount() is %u", e->label, FAKE_MODE_COUNT);
    int modes_ok = 1;
    for (unsigned i = 0; i < FAKE_MODE_COUNT; i++) {
        const uint32_t *m = (const uint32_t *)((ScreenPtrArg)screen_slot(screen, EEWEB_SLOT_GEScreen_GetMode))(screen, i);
        if (m[0] != kFakeModes[i].w || m[1] != kFakeModes[i].h || m[2] != kFakeModes[i].hz || m[3] != kFakeModes[i].bpp ||
            m[4] != kFakeFormat[i]) {
            modes_ok = 0;
            INFO("%s: GetMode(%u) = %ux%u hz %u bpp %u format %u", e->label, i, m[0], m[1], m[2], m[3], m[4]);
        }
    }
    CHECK(modes_ok, "%s: GetMode(i) lists the modes in enumeration order with width, height, refresh, bpp and format index", e->label);
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetCurrentModeIndex))(screen) == FAKE_CURRENT_MODE,
          "%s: GetCurrentModeIndex() is the mode GetDisplayMode reported (%u)", e->label, FAKE_CURRENT_MODE);
    CHECK(((ScreenPtr)screen_slot(screen, EEWEB_SLOT_GEScreen_GetCurrentMode))(screen) ==
              ((ScreenPtrArg)screen_slot(screen, EEWEB_SLOT_GEScreen_GetMode))(screen, FAKE_CURRENT_MODE),
          "%s: GetCurrentMode() is GetMode(GetCurrentModeIndex())", e->label);
    const UString *name = (const UString *)((ScreenPtr)screen_slot(screen, EEWEB_SLOT_GEScreen_GetName))(screen);
    CHECK(name != NULL && name->chars != NULL && strcmp(name->chars, "Fake DirectDraw Device") == 0, "%s: GetName() is the device description", e->label);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetAttachedToDesktop) == 1, "%s: GetAttachedToDesktop() is what the constructor got", e->label);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetSharesPrimary) == 0, "%s: GetSharesPrimary() is what the constructor got", e->label);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetIsFullScreen) == 0, "%s: a new screen is not full screen", e->label);
    /* Known defect of the original, kept: the capability tests OR the bits instead of ANDing them, so both are always true. */
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetCanMagFilterSurfaces) == 1 && SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetCanMinFilterSurfaces) == 1,
          "%s: GetCanMagFilterSurfaces() and GetCanMinFilterSurfaces() are true whatever the caps (original defect kept)", e->label);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitDeviceSupport) == 1 && SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitTextureSupport) == 1,
          "%s: Get32BitDeviceSupport() and Get32BitTextureSupport() are true when the fake device renders in 32 bits and offers an ARGB texture format", e->label);
    const float *rect = (const float *)((ScreenPtr)screen_slot(screen, EEWEB_SLOT_GEScreen_GetDesktopRectangle))(screen);
    CHECK(rect[0] == 768.0f && rect[1] == 0.0f && rect[2] == 1024.0f && rect[3] == 0.0f,
          "%s: GetDesktopRectangle() without a monitor is the current mode (bottom 768, left 0, right 1024, top 0; got %g %g %g %g)", e->label,
          rect[0], rect[1], rect[2], rect[3]);

    /* Capture / SetMode / Release.  The window is a struct whose first field is the HWND. */
    struct {
        void *hwnd;
    } window = {(void *)0x1234};
    ScreenCapture capture = (ScreenCapture)screen_slot(screen, EEWEB_SLOT_GEScreen_Capture);
    ScreenPtrOp release = (ScreenPtrOp)screen_slot(screen, EEWEB_SLOT_GEScreen_Release);
    ScreenSetMode set_mode = (ScreenSetMode)screen_slot(screen, EEWEB_SLOT_GEScreen_SetMode);

    log_clear(&dd);
    CHECK(capture(screen, &window, 1) == 0, "%s: Capture(window, true) succeeds", e->label);
    CHECK(strcmp(dd.log, "dd.SetCooperativeLevel(hwnd=0x1234,flags=0x80)\ndd.SetCooperativeLevel(hwnd=0x1234,flags=0x513)\n") == 0,
          "%s: the first full-screen capture of a desktop-attached screen sets focus (0x80), then exclusive (0x513) (got \"%s\")", e->label, dd.log);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetIsFullScreen) == 1, "%s: the screen is full screen after Capture(window, true)", e->label);

    log_clear(&dd);
    CHECK(set_mode(screen, 5) == 0, "%s: SetMode(5) succeeds while full screen", e->label);
    CHECK(strcmp(dd.log, "dd.SetDisplayMode(1024x768x16,hz=60,flags=0)\n") == 0, "%s: SetMode(5) asks SetDisplayMode(1024, 768, 16, 60, 0) (got \"%s\")", e->label, dd.log);
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetCurrentModeIndex))(screen) == 5, "%s: GetCurrentModeIndex() follows SetMode", e->label);
    log_clear(&dd);
    CHECK(release(screen, &window) == 0, "%s: Release(window) succeeds", e->label);
    CHECK(strcmp(dd.log, "dd.SetCooperativeLevel(hwnd=0x1234,flags=0x8)\n") == 0, "%s: Release(window) sets the normal cooperative level (got \"%s\")", e->label, dd.log);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetIsFullScreen) == 0, "%s: the screen is windowed after Release", e->label);
    log_clear(&dd);
    CHECK(set_mode(screen, 2) == 0 && dd.log[0] == 0, "%s: SetMode while windowed does nothing and succeeds", e->label);

    log_clear(&dd);
    CHECK(capture(screen, &window, 0) == 0 && strcmp(dd.log, "dd.SetCooperativeLevel(hwnd=0x1234,flags=0x408)\n") == 0,
          "%s: Capture(window, false) sets the multithreaded normal level 0x408 (got \"%s\")", e->label, dd.log);
    log_clear(&dd);
    CHECK(capture(screen, &window, 1) == 0 && strcmp(dd.log, "dd.SetCooperativeLevel(hwnd=0x1234,flags=0x691)\n") == 0,
          "%s: a full-screen capture after a windowed one of a desktop-attached screen uses 0x691 (got \"%s\")", e->label, dd.log);
    CHECK(release(screen, &window) == 0 && release(screen, &window) == 0, "%s: the two captures are released", e->label);

    /* a failing cooperative level */
    dd.fail_coop_calls = 1;
    log_clear(&dd);
    CHECK(capture(screen, &window, 1) == 1, "%s: Capture(window, true) fails with 1 when SetCooperativeLevel fails", e->label);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetIsFullScreen) == 0, "%s: a failed capture leaves the screen windowed", e->label);

    /* RestoreAllSurfaces: TestCooperativeLevel gates the restore */
    log_clear(&dd);
    dd.test_coop_result = (long)0x88760082;
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_RestoreAllSurfaces))(screen) == 1,
          "%s: RestoreAllSurfaces() fails (1) while the cooperative level is lost", e->label);
    CHECK(strcmp(dd.log, "dd.TestCooperativeLevel\n") == 0, "%s: ... without trying to restore (got \"%s\")", e->label, dd.log);
    log_clear(&dd);
    dd.test_coop_result = 0;
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_RestoreAllSurfaces))(screen) == 0 &&
              strcmp(dd.log, "dd.TestCooperativeLevel\ndd.RestoreAllSurfaces\n") == 0,
          "%s: RestoreAllSurfaces() restores once the level is back (got \"%s\")", e->label, dd.log);

    /* a second screen: not attached to the desktop, shares the primary */
    FakeDD dd2;
    fake_dd_init(&dd2);
    void *second = make_screen(api, &dd2, "Second Device", 0, 1);
    CHECK(second != NULL && SCREEN_BOOL(second, EEWEB_SLOT_GEScreen_GetAttachedToDesktop) == 0 &&
              SCREEN_BOOL(second, EEWEB_SLOT_GEScreen_GetSharesPrimary) == 1,
          "%s: a second screen keeps its own flags", e->label);
    CHECK(api->screen_count() == count_before + 2, "%s: two screens are registered", e->label);
    log_clear(&dd2);
    CHECK(((ScreenCapture)screen_slot(second, EEWEB_SLOT_GEScreen_Capture))(second, &window, 1) == 0 &&
              strcmp(dd2.log, "dd.SetCooperativeLevel(hwnd=0x1234,flags=0x413)\n") == 0,
          "%s: a full-screen capture of a screen that is not attached to the desktop uses 0x413 (got \"%s\")", e->label, dd2.log);
    ((ScreenPtrOp)screen_slot(second, EEWEB_SLOT_GEScreen_Release))(second, &window);

    /* destruction through the deleting destructor: releases the DirectDraw object once and unregisters the screen */
    log_clear(&dd2);
    ((Destroy)screen_slot(second, EEWEB_SLOT_GEScreen_Destructor))(second, 0);
    CHECK(strcmp(dd2.log, "dd.Release\n") == 0, "%s: destroying a screen releases its DirectDraw object once (got \"%s\")", e->label, dd2.log);
    CHECK(api->screen_count() == count_before + 1, "%s: a destroyed screen is no longer registered", e->label);
    {
        typedef void(__cdecl * OperatorDelete)(void *);
        OperatorDelete op_delete;
        void *raw_delete = proc_address(GetModuleHandleA("msvcrt.dll"), "??3@YAXPAX@Z");
        memcpy(&op_delete, &raw_delete, sizeof op_delete);
        op_delete(second);
    }

    /* The rasterizer slots that expose screens: GetIsScreenValid, AddValidScreen, the video memory queries, Deactivate. */
    void *obj;
    {
        PimGetRasterizer get_rasterizer;
        void *raw = proc_address(h, "RPIMGetRasterizer");
        memcpy(&get_rasterizer, &raw, sizeof get_rasterizer);
        obj = get_rasterizer();
    }
    void *const *rvtable = *(void *const **)obj;
    typedef unsigned char (THISCALL *IsScreenValid)(void *self, void *screen);
    typedef long (THISCALL *AddScreen)(void *self, void *screen);
    typedef long (THISCALL *VideoMemory)(void *self, unsigned long screen, unsigned long *out);
    typedef long (THISCALL *VideoMemory2)(void *self, unsigned long screen, unsigned long *total, unsigned long *avail);
    IsScreenValid is_valid_screen = (IsScreenValid)slot(rvtable, EEWEB_SLOT_DX7RasterizerAbi_GetIsScreenValid);

    log_clear(&dd);
    CHECK(is_valid_screen(obj, screen) == 1, "%s: GetIsScreenValid() is true for a screen whose Direct3D 7 offers a hardware T&L device", e->label);
    CHECK(strcmp(dd.log, "dd.QueryInterface(IDirect3D7)\nd3d.EnumDevices\nd3d.Release\n") == 0,
          "%s: ... by asking IDirect3D7 to enumerate its devices, and releasing it (got \"%s\")", e->label, dd.log);
    dd.hw_tnl = 0;
    CHECK(is_valid_screen(obj, screen) == 0, "%s: GetIsScreenValid() is false when no device has hardware T&L", e->label);
    dd.hw_tnl = 1;

    long add_status = ((AddScreen)slot(rvtable, EEWEB_SLOT_GERasterizer_AddValidScreen))(obj, screen);
    CHECK(add_status == 0, "%s: AddValidScreen() adds the screen", e->label);
    CHECK(rasterizer_screen_count(e, obj) == 1, "%s: the rasterizer's screen count is 1 (got %lu)", e->label, rasterizer_screen_count(e, obj));

    unsigned long out_total = 0, out_free = 0, out = 0;
    log_clear(&dd);
    CHECK(((VideoMemory2)slot(rvtable, EEWEB_SLOT_DX7RasterizerAbi_GetVideoMemory))(obj, 0, &out_total, &out_free) == 0 &&
              out_total == 134217728 && out_free == 100663296,
          "%s: GetVideoMemory(0) returns the total and free video memory of the screen's DirectDraw", e->label);
    CHECK(strcmp(dd.log, "dd.GetAvailableVidMem(caps=0x4000)\n") == 0, "%s: ... asking for DDSCAPS_VIDEOMEMORY (0x4000) (got \"%s\")", e->label, dd.log);
    CHECK(((VideoMemory)slot(rvtable, EEWEB_SLOT_GERasterizer_GetTotalVideoMemory))(obj, 0, &out) == 0 && out == 134217728,
          "%s: GetTotalVideoMemory(0) is the total", e->label);
    CHECK(((VideoMemory)slot(rvtable, EEWEB_SLOT_GERasterizer_GetAvailableVideoMemory))(obj, 0, &out) == 0 && out == 100663296,
          "%s: GetAvailableVideoMemory(0) is the free memory", e->label);

    log_clear(&dd);
    typedef long (THISCALL *Deactivate)(void *self);
    CHECK(((Deactivate)slot(rvtable, EEWEB_SLOT_GERasterizer_Deactivate))(obj) == 0, "%s: Deactivate() succeeds", e->label);
    CHECK(strcmp(dd.log, "dd.Release\n") == 0, "%s: Deactivate() destroys the screens it holds, releasing their DirectDraw objects (got \"%s\")", e->label, dd.log);
    CHECK(api->screen_count() == count_before, "%s: ... and unregisters them", e->label);
    CHECK(rasterizer_screen_count(e, obj) == 0, "%s: the rasterizer holds no screens after Deactivate", e->label);
}

/* ---- the real DirectDraw devices ------------------------------------------------------------ */

#define OBS(...)                        \
    do {                                \
        printf("obs  ");                \
        printf(__VA_ARGS__);            \
        printf("\n");                   \
        fflush(stdout);                 \
    } while (0)

static void check_real_screens(const struct Expect *e, void *obj, const ScreenApi *api)
{
    void *const *rvtable = *(void *const **)obj;
    typedef long (THISCALL *Op)(void *self);
    long status = ((Op)slot(rvtable, EEWEB_SLOT_GERasterizer_ActivateRasterizer))(obj);
    CHECK(status == 0, "%s: ActivateRasterizer() enumerates the DirectDraw devices and succeeds (got 0x%lx)", e->label, status);
    unsigned long screens = api->screen_count();
    CHECK(screens > 0, "%s: ... and creates at least one screen (got %lu)", e->label, screens);
    CHECK(rasterizer_screen_count(e, obj) == screens, "%s: every created screen is valid and was added to the rasterizer", e->label);
    OBS("real: rasterizer screens %lu, DX7Screen::GetScreenCount %lu", rasterizer_screen_count(e, obj), screens);
    for (unsigned long s = 0; s < screens && s < 4; s++) {
        void *screen = api->get_screen(s);
        const UString *name = (const UString *)((ScreenPtr)screen_slot(screen, EEWEB_SLOT_GEScreen_GetName))(screen);
        unsigned long modes = ((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetModeCount))(screen);
        unsigned long cur = ((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetCurrentModeIndex))(screen);
        OBS("real: screen %lu name \"%s\" modes %lu current %lu attached %d shares %d fullscreen %d mag %d min %d", s,
            name && name->chars ? name->chars : "(null)", modes, cur, SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetAttachedToDesktop),
            SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetSharesPrimary), SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetIsFullScreen),
            SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetCanMagFilterSurfaces), SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_GetCanMinFilterSurfaces));
        for (unsigned long m = 0; m < modes; m++) {
            const uint32_t *mode = (const uint32_t *)((ScreenPtrArg)screen_slot(screen, EEWEB_SLOT_GEScreen_GetMode))(screen, m);
            OBS("real: screen %lu mode %lu: %ux%u hz %u bpp %u format %u", s, m, mode[0], mode[1], mode[2], mode[3], mode[4]);
        }
        const float *rect = (const float *)((ScreenPtr)screen_slot(screen, EEWEB_SLOT_GEScreen_GetDesktopRectangle))(screen);
        OBS("real: screen %lu desktop rectangle bottom %g left %g right %g top %g", s, rect[0], rect[1], rect[2], rect[3]);
        OBS("real: screen %lu Get32BitDeviceSupport %d Get32BitTextureSupport %d", s, SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitDeviceSupport),
            SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitTextureSupport));
    }
    typedef long (THISCALL *Deactivate)(void *self);
    CHECK(((Deactivate)slot(rvtable, EEWEB_SLOT_GERasterizer_Deactivate))(obj) == 0, "%s: Deactivate() releases the real screens", e->label);
    CHECK(api->screen_count() == 0, "%s: no screen is left registered", e->label);
}

/* The exports of the plugin the screens are reached through. */
static int load_screen_api(const struct Expect *e, HMODULE h, ScreenApi *api)
{
    void *raw = proc_address(h, "??0DX7Screen@@AAE@PAUHMONITOR__@@PAUIDirectDraw7@@ABVUString@@_N3@Z");
    CHECK(raw != NULL, "%s: exports DX7Screen's private constructor", e->label);
    memcpy(&api->construct, &raw, sizeof raw);
    raw = proc_address(h, "?GetScreenCount@DX7Screen@@SAKXZ");
    CHECK(raw != NULL, "%s: exports DX7Screen::GetScreenCount", e->label);
    memcpy(&api->screen_count, &raw, sizeof raw);
    raw = proc_address(h, "?GetScreen@DX7Screen@@SAPAV1@K@Z");
    CHECK(raw != NULL, "%s: exports DX7Screen::GetScreen", e->label);
    memcpy(&api->get_screen, &raw, sizeof raw);
    return api->construct && api->screen_count && api->get_screen;
}

#include "surface_checks.h" /* the surface object, CreateSurface, CreateAWindow and the 32-bit probe (P2.6.2) */
#include "format_checks.h"  /* the pixel format table in every consumer, texture format enumeration (P2.6.3) */
#include "state_checks.h"   /* the render state, light and viewport setters on a fake Direct3D device (P2.6.4) */
#include "lifecycle_checks.h" /* ActivateScreen, Deactivate and the scene calls on the fake DirectDraw and Direct3D (P2.6.5) */
#include "texture_checks.h"   /* SetTexture, CanDoCurrentState and the texture handle slots (P2.6.6) */
#include "ring_checks.h"      /* the ring vertex buffer's LockVB and UnlockVB (P2.6.6) */
#include "array_checks.h"     /* CreatePolygonArray, CreateVertexArray and the arrays they build (P2.6.7a) */
#include "draw2d_checks.h"    /* Draw(GEBitmap), Draw(GELine), Draw(GEPoint) and Draw(GERhombus) (P2.6.7b) */
#include "poly_checks.h"      /* Draw(GEPolygonArray, GEVertexArray, ...) (P2.6.7c) */
#include "line3d_checks.h"    /* Draw3DLine (P2.6.7d) */
#include "text_checks.h"      /* Draw(GEText, ...) (P2.6.7e) */
#include "terrain_checks.h"   /* TransformToCamera (P2.6.7g) */
#include "model_checks.h"     /* the model draws (P2.6.7h) */

/*
 * Child process of the screen checks (`probe-screens <dll> original|ours`), for the fake DirectDraw and for
 * the real devices.  Creating screens and enumerating real devices leaves driver and runtime state behind
 * that the parent's load / unload heap checks would see as a leak, so it runs in its own process; the
 * parent counts its failures as one check.
 */
static int probe_screens(const struct Expect *e, const char *path)
{
    HMODULE h = load_plugin(path);
    CHECK(h != NULL, "%s: probe: LoadLibraryExA (GetLastError=%lu)", e->label, h ? 0UL : GetLastError());
    if (!h) {
        return 1;
    }
    PimGetRasterizer get_rasterizer;
    void *raw = proc_address(h, "RPIMGetRasterizer");
    memcpy(&get_rasterizer, &raw, sizeof get_rasterizer);
    ScreenApi api;
    if (raw && load_screen_api(e, h, &api)) {
        check_screen_object(e, h, &api);
        check_surfaces(e, h, &api);
        check_pixel_formats(e, &api);
        check_real_screens(e, get_rasterizer(), &api);
        check_state_setters(e, h, &api, get_rasterizer());
        check_lifecycle(e, h, &api, get_rasterizer());
        check_textures(e, h, &api, get_rasterizer());
        check_ring(e, h, &api, get_rasterizer());
        check_arrays(e, h, &api, get_rasterizer());
        check_draw2d(e, h, &api, get_rasterizer());
        check_poly(e, h, &api, get_rasterizer());
        check_line3d(e, h, &api, get_rasterizer());
        check_text(e, h, &api, get_rasterizer());
        check_terrain(e, h, &api, get_rasterizer());
        check_model(e, h, &api, get_rasterizer());
    }
    printf("%s: screens: %d checks, %d failed\n", e->label, g_checks, g_failures);
    return g_failures;
}

static void run_real_screens_child(const char *self, const char *path, const struct Expect *e)
{
    char cmd[2 * MAX_PATH + 64];
    snprintf(cmd, sizeof cmd, "\"%s\" probe-screens \"%s\" %s", self, path, e->ours ? "ours" : "original");
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    fflush(stdout);
    BOOL started = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CHECK(started, "%s: the screen probe process starts (%lu)", e->label, started ? 0UL : GetLastError());
    if (!started) {
        return;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0xDEAD;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 0xDEAD);
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CHECK(wait == WAIT_OBJECT_0 && code == 0, "%s: the screen probe finished without a failed check (exit status 0x%08lx)", e->label, code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
