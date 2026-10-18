/*
 * Surface checks (task P2.6.2): the 32-slot GESurface object, DX7Screen::CreateSurface / CreateAWindow and the
 * 32-bit probe the screen's constructor runs.  Included by screen_checks.h (it uses its fake DirectDraw 7, now
 * able to create surfaces) and so by harness.c.
 *
 * Both variants are driven the same way and must give the same answers.  The expected values were derived from
 * the original DX7HRTnLDisplay.dll and then observed on it (variant "original"); "ours" must reproduce them:
 * the DirectDraw calls and their arguments (the fake logs every call), the results, the fields the engine can
 * reach.  The pixel-exact filters are compared through a checksum of the destination memory.
 */

/* ---- helpers --------------------------------------------------------------------------------------- */

typedef long (THISCALL *ScreenCreateSurface)(void *self, void *desc, void **out);
typedef long (THISCALL *SurfNoArg)(void *self);
typedef unsigned long (THISCALL *SurfULong)(void *self);
typedef unsigned char (THISCALL *SurfBool)(void *self);
typedef void *(THISCALL *SurfPtr)(void *self);
typedef void *(THISCALL *SurfPtrArg)(void *self, unsigned long index);
typedef long (THISCALL *SurfLongArg)(void *self, unsigned long arg);
typedef long (THISCALL *SurfPtrOp)(void *self, void *other);
typedef long (THISCALL *SurfFlip)(void *self, unsigned char wait);
typedef long (THISCALL *SurfClearAll)(void *self, void *color, float alpha);
typedef long (THISCALL *SurfClearRect)(void *self, void *rect, void *color, float alpha);
typedef long (THISCALL *SurfCopyTo)(void *self, void *dest, void *src_rect, void *dst_rect, unsigned char color_key);
typedef long (THISCALL *SurfCopyFilter)(void *self, void *dest, void *src_rect, void *dst_rect);
typedef void (THISCALL *SurfSetColor)(void *self, void *color);
typedef long (THISCALL *ScreenCreateWindow)(void *self, const void *point, unsigned long w, unsigned long h, const void *cls,
                                           const void *title, void **out, unsigned long style, void *parent);

static int in_module_of_screen(void *screen, const void *p)
{
    /* the plugin's module is the one that holds the screen's vtable */
    MEMORY_BASIC_INFORMATION a, b;
    if (!VirtualQuery(*(void **)screen, &a, sizeof a) || !VirtualQuery(p, &b, sizeof b)) {
        return 0;
    }
    return a.AllocationBase == b.AllocationBase;
}

#define SURF(s, slot_index) surf_slot((s), (slot_index))
static void *surf_slot(void *surface, int index)
{
    return (*(void *const **)surface)[index];
}

#define S_ULONG(s, idx) (((SurfULong)SURF(s, idx))(s))
#define S_BOOL(s, idx) (((SurfBool)SURF(s, idx))(s) != 0)

static unsigned long surf_refcount(void *surface)
{
    return ((const unsigned long *)surface)[1];
}

static void *engine_proc(const char *decorated)
{
    return proc_address(GetModuleHandleA("Low-Level Engine.dll"), decorated);
}

/* UReferenceCounter::Dereference of the real engine: the way the engine lets go of a surface. */
static void engine_dereference(void *object)
{
    typedef void (THISCALL * Deref)(void *self);
    Deref fn;
    void *raw = engine_proc("?Dereference@UReferenceCounter@@QAEXXZ");
    memcpy(&fn, &raw, sizeof fn);
    fn(object);
}

static unsigned long engine_pack_color(unsigned long format, const void *color, float alpha)
{
    typedef unsigned long (__cdecl * Pack)(unsigned long, const void *, float);
    Pack fn;
    void *raw = engine_proc("?GEPackColor@@YAKKABVGEColor@@M@Z");
    memcpy(&fn, &raw, sizeof fn);
    return fn(format, color, alpha);
}

static unsigned long engine_native_error(void)
{
    typedef unsigned long (__cdecl * Get)(void);
    Get fn;
    void *raw = engine_proc("?ESGetNativeError@@YAKXZ");
    memcpy(&fn, &raw, sizeof fn);
    return fn();
}

static void engine_record_error(unsigned long code)
{
    typedef unsigned long (__cdecl * Rec)(unsigned long);
    Rec fn;
    void *raw = engine_proc("?ESRecordNativeError@@YAKK@Z");
    memcpy(&fn, &raw, sizeof fn);
    fn(code);
}

/* The window handles of the 32-bit probe differ from run to run; the tests' own handle is 0x1234. */
static void log_normalize(FakeDD *dd)
{
    char out[sizeof dd->log];
    size_t n = 0;
    const char *p = dd->log;
    while (*p && n + 16 < sizeof out) {
        if (strncmp(p, "hwnd=0x", 7) == 0 && strncmp(p, "hwnd=0x1234,", 12) != 0) {
            memcpy(out + n, "hwnd=WIN", 8);
            n += 8;
            p += 7;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
                p++;
            }
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    memcpy(dd->log, out, n + 1);
}

static unsigned long fnv(const unsigned char *data, size_t n)
{
    unsigned long h = 2166136261UL;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ data[i]) * 16777619UL;
    }
    return h;
}

static void destroy_screen(void *screen)
{
    typedef void(__cdecl * OperatorDelete)(void *);
    OperatorDelete op_delete;
    void *raw = proc_address(GetModuleHandleA("msvcrt.dll"), "??3@YAXPAX@Z");
    memcpy(&op_delete, &raw, sizeof op_delete);
    ((Destroy)screen_slot(screen, EEWEB_SLOT_GEScreen_Destructor))(screen, 0);
    op_delete(screen);
}

static void *new_surface(void *screen, FakeSurface **fake, FakeDD *dd, GESurfaceDescriptionLayout *desc, long *status)
{
    void *surface = NULL;
    int before = dd->surface_count;
    long r = ((ScreenCreateSurface)screen_slot(screen, EEWEB_SLOT_GEScreen_CreateSurface))(screen, desc, &surface);
    if (status) {
        *status = r;
    }
    (void)fake;
    (void)before;
    return surface;
}

static GESurfaceDescriptionLayout make_desc(unsigned type, unsigned format, unsigned volatility, unsigned w, unsigned h, unsigned depth, int is3d,
                                            int transparency)
{
    GESurfaceDescriptionLayout d;
    memset(&d, 0, sizeof d);
    d.type = type;
    d.pixelFormat = format;
    d.volatility = volatility;
    d.width = w;
    d.height = h;
    d.pixelDepth = depth;
    d.is3DRenderable = (uint8_t)is3d;
    d.isTransparencyEnabled = (uint8_t)transparency;
    return d;
}

/* The CreateSurface calls of the DX7Screen API: one description in, the DirectDraw call out. */
static void check_create_surface(const struct Expect *e, void *screen, FakeDD *dd)
{
    GESurfaceDescriptionLayout d;
    void *surface;
    long status;

    /* type 1: video memory, 565 */
    d = make_desc(1, 7, 0, 64, 32, 16, 0, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    log_normalize(dd);
    CHECK(status == 0 && surface != NULL, "%s: CreateSurface(type 1) succeeds", e->label);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x20,+0xc=0x40,+0x48=0x20,+0x4c=0x40,+0x54=0x10,+0x58=0xf800,+0x5c=0x7e0,"
                 "+0x60=0x1f,+0x68=0x10004040) -> s3\n") == 0,
          "%s: type 1, format 565: DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT, caps LOCALVIDMEM|VIDEOMEMORY|OFFSCREENPLAIN (got \"%s\")", e->label, dd->log);
    if (!surface) {
        return;
    }
    CHECK(in_module_of_screen(screen, *(void **)surface), "%s: the surface's vtable lies inside the plugin", e->label);
    CHECK(surf_refcount(surface) == 1, "%s: a new surface has reference count 1 (the engine's UReferenceCounter field at 0x4)", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetWidth) == 64 && S_ULONG(surface, EEWEB_SLOT_GESurface_GetHeight) == 32,
          "%s: GetWidth() 64 and GetHeight() 32", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelFormat) == 7 && S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelDepth) == 16 &&
              S_ULONG(surface, EEWEB_SLOT_GESurface_GetType) == 1 && S_ULONG(surface, EEWEB_SLOT_GESurface_GetSurfaceVolatility) == 0,
          "%s: GetPixelFormat() 7, GetPixelDepth() 16, GetType() 1, GetSurfaceVolatility() 0", e->label);
    CHECK(!S_BOOL(surface, EEWEB_SLOT_GESurface_GetIs3DRenderable) && !S_BOOL(surface, EEWEB_SLOT_GESurface_GetIsTransparencyEnabled) &&
              !S_BOOL(surface, EEWEB_SLOT_GESurface_GetIsTextLocked),
          "%s: not 3D renderable, no transparency, not text locked", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetBytesPerRow) == 0 && S_ULONG(surface, EEWEB_SLOT_GESurface_GetMemoryAddress) == 0 &&
              S_ULONG(surface, EEWEB_SLOT_GESurface_GetTextDC) == 0,
          "%s: no memory address, row size or text DC before locking", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetParentScreen) == (unsigned long)screen, "%s: GetParentScreen() is the screen", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 0, "%s: no attached surface", e->label);
    CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetAllocatedSize) == 4096, "%s: GetAllocatedSize() is depth/8 * height * width (4096)", e->label);
    {
        const float *c = (const float *)((SurfPtr)SURF(surface, EEWEB_SLOT_GESurface_GetTransparencyColor))(surface);
        CHECK(c[0] == 1.0f && c[1] == 1.0f && c[2] == 1.0f, "%s: the transparency colour starts white", e->label);
        const GESurfaceDescriptionLayout *got = (const GESurfaceDescriptionLayout *)((SurfPtr)SURF(surface, EEWEB_SLOT_GESurface_GetSurfaceDescription))(surface);
        CHECK(memcmp(got, &d, sizeof d) == 0, "%s: GetSurfaceDescription() is the description the surface was created from", e->label);
    }
    log_clear(dd);
    engine_dereference(surface);
    CHECK(strcmp(dd->log, "s3.Release\n") == 0, "%s: the engine's Dereference destroys the surface, which releases its DirectDraw surface (got \"%s\")", e->label, dd->log);

    /* type 1 with transparency and 3D: CKSRCBLT and 3DDEVICE, format 0 (A8R8G8B8) */
    d = make_desc(1, 0, 0, 16, 16, 32, 1, 1);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x11007,+0x8=0x10,+0xc=0x10,+0x48=0x20,+0x4c=0x41,+0x54=0x20,+0x58=0xff0000,+0x5c=0xff00,"
                 "+0x60=0xff,+0x64=0xff000000,+0x68=0x10006040) -> s4\n") == 0,
          "%s: type 1 with transparency and 3D adds DDSD_CKSRCBLT (0x10000) and DDSCAPS_3DDEVICE (got \"%s\")", e->label, dd->log);
    CHECK(surface && S_BOOL(surface, EEWEB_SLOT_GESurface_GetIs3DRenderable) && S_BOOL(surface, EEWEB_SLOT_GESurface_GetIsTransparencyEnabled),
          "%s: ... and the surface reports both", e->label);
    if (surface) {
        engine_dereference(surface);
    }

    /* type 0: the primary surface */
    d = make_desc(0, 3, 0, 0, 0, 0, 0, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x1,+0x48=0x20,+0x4c=0x41,+0x54=0x10,+0x58=0x7c00,+0x5c=0x3e0,+0x60=0x1f,+0x64=0x8000,"
                 "+0x68=0x10004200) -> s5\n") == 0,
          "%s: type 0 (primary): DDSD_CAPS only, caps LOCALVIDMEM|VIDEOMEMORY|PRIMARYSURFACE (got \"%s\")", e->label, dd->log);
    if (surface) {
        engine_dereference(surface);
    }

    /* type 2: a flipping primary with two back buffers; the chain is walked with GetAttachedSurface(DDSCAPS_FLIP) */
    d = make_desc(2, 3, 0, 0, 0, 0, 1, 0);
    d.backBufferCount = 2;
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 0 && strcmp(dd->log,
                                "dd.CreateSurface(+0x0=0x7c,+0x4=0x21,+0x14=0x2,+0x48=0x20,+0x4c=0x41,+0x54=0x10,+0x58=0x7c00,+0x5c=0x3e0,+0x60=0x1f,"
                                "+0x64=0x8000,+0x68=0x10006218) -> s6\n"
                                "s6.GetAttachedSurface(caps=0x10) -> s7\n"
                                "s7.GetAttachedSurface(caps=0x10) -> s8\n") == 0,
          "%s: type 2 with two back buffers: CAPS|BACKBUFFERCOUNT, PRIMARY|FLIP|COMPLEX|3DDEVICE, then two GetAttachedSurface(0x10) (got \"%s\")", e->label,
          dd->log);
    CHECK(d.type == 7, "%s: the caller's description is changed to type 7 (the mark of a back buffer)", e->label);
    if (surface) {
        void *first = ((SurfPtrArg)SURF(surface, EEWEB_SLOT_GESurface_GetAttachedSurface))(surface, 0);
        CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 1 && first != NULL &&
                  S_ULONG(first, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 1,
              "%s: the primary has the first back buffer attached, and that one the second", e->label);
        if (first) {
            void *second = ((SurfPtrArg)SURF(first, EEWEB_SLOT_GESurface_GetAttachedSurface))(first, 0);
            CHECK(second != NULL && S_ULONG(second, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 0 &&
                      S_ULONG(second, EEWEB_SLOT_GESurface_GetType) == 7,
                  "%s: the last back buffer has none and is of type 7", e->label);
        }
        log_clear(dd);
        engine_dereference(surface);
        CHECK(strcmp(dd->log, "s6.Release\n") == 0, "%s: destroying the chain releases only the primary's DirectDraw surface (back buffers are type 7) (got \"%s\")",
              e->label, dd->log);
    }

    /* type 3: depth buffer; format 12 tries 16, 24, then 32 bits, 13 the other way round */
    d = make_desc(3, 12, 0, 64, 64, 0, 0, 0);
    dd->create_results[0] = (long)0x8876000a;
    dd->create_results[1] = (long)0x8876000a;
    dd->create_result_count = 2;
    dd->create_result_pos = 0;
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 0 && strcmp(dd->log,
                                "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x40,+0xc=0x40,+0x48=0x20,+0x4c=0x400,+0x54=0x10,+0x5c=0xffff,"
                                "+0x68=0x10024000) -> error 0x8876000a\n"
                                "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x40,+0xc=0x40,+0x48=0x20,+0x4c=0x400,+0x54=0x18,+0x5c=0xffffff,"
                                "+0x68=0x10024000) -> error 0x8876000a\n"
                                "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x40,+0xc=0x40,+0x48=0x20,+0x4c=0x400,+0x54=0x20,+0x5c=0xffffffff,"
                                "+0x68=0x10024000) -> s9\n") == 0,
          "%s: type 3 with format 12 tries the 16-bit, 24-bit and 32-bit depth formats (got \"%s\")", e->label, dd->log);
    CHECK(d.pixelFormat == 9 && d.pixelDepth == 0x20, "%s: ... the caller's description ends with format 9 and depth 32 (got %u, %u)", e->label,
          (unsigned)d.pixelFormat, (unsigned)d.pixelDepth);
    CHECK(surface && S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelFormat) == 9 && S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelDepth) == 0,
          "%s: ... and the surface kept the description it was created from (format 9, depth 0)", e->label);
    if (surface) {
        engine_dereference(surface);
    }
    d = make_desc(3, 13, 0, 8, 8, 0, 0, 0);
    dd->create_results[0] = (long)0x8876000a;
    dd->create_result_count = 1;
    dd->create_result_pos = 0;
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 0 && d.pixelFormat == 10 && d.pixelDepth == 0x18, "%s: type 3 with format 13 tries 32 bits, then 24 (format 10, depth 24)", e->label);
    if (surface) {
        engine_dereference(surface);
    }
    d = make_desc(3, 12, 0, 8, 8, 0, 0, 0);
    dd->create_results[0] = (long)0x8876000a;
    dd->create_results[1] = (long)0x8876000a;
    dd->create_results[2] = (long)0x8876000a;
    dd->create_result_count = 3;
    dd->create_result_pos = 0;
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 1 && surface == NULL && d.pixelFormat == 9 && d.pixelDepth == 0x20,
          "%s: when all three depth formats fail, CreateSurface returns 1 and no surface (status %ld)", e->label, status);

    /* type 4: system memory */
    d = make_desc(4, 1, 0, 32, 32, 32, 1, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x20,+0xc=0x20,+0x48=0x20,+0x4c=0x40,+0x54=0x20,+0x58=0xff0000,+0x5c=0xff00,"
                 "+0x60=0xff,+0x68=0x2840) -> s11\n") == 0,
          "%s: type 4: SYSTEMMEMORY|OFFSCREENPLAIN|3DDEVICE (got \"%s\")", e->label, dd->log);
    if (surface) {
        engine_dereference(surface);
    }

    /* type 5: textures, with and without 3D rendering and the dynamic hint */
    d = make_desc(5, 2, 0, 128, 128, 16, 0, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x80,+0xc=0x80,+0x48=0x20,+0x4c=0x41,+0x54=0x10,+0x58=0xf00,+0x5c=0xf0,+0x60=0xf,"
                 "+0x64=0xf000,+0x68=0x1000,+0x6c=0x14) -> s12\n") == 0,
          "%s: type 5: TEXTURE with the texture-manage hint (0x14) (got \"%s\")", e->label, dd->log);
    if (surface) {
        engine_dereference(surface);
    }
    d = make_desc(5, 2, 1, 128, 128, 16, 1, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log,
                 "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x80,+0xc=0x80,+0x48=0x20,+0x4c=0x41,+0x54=0x10,+0x58=0xf00,+0x5c=0xf0,+0x60=0xf,"
                 "+0x64=0xf000,+0x68=0x10007000,+0x6c=0x8) -> s13\n") == 0,
          "%s: type 5 with 3D rendering and a non-zero volatility: LOCALVIDMEM|VIDEOMEMORY|3DDEVICE|TEXTURE and the dynamic hint (got \"%s\")", e->label,
          dd->log);
    if (surface) {
        engine_dereference(surface);
    }

    /* type 6: mipmapped textures, three levels */
    d = make_desc(6, 7, 1, 64, 32, 16, 0, 0);
    d.mipMapCount = 3;
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 0 && strcmp(dd->log,
                                "dd.CreateSurface(+0x0=0x7c,+0x4=0x21007,+0x8=0x20,+0xc=0x40,+0x18=0x3,+0x48=0x20,+0x4c=0x40,+0x54=0x10,+0x58=0xf800,"
                                "+0x5c=0x7e0,+0x60=0x1f,+0x68=0x401008,+0x6c=0x18) -> s14\n"
                                "s14.GetAttachedSurface(caps=0x400000) -> s15\n"
                                "s15.GetAttachedSurface(caps=0x400000) -> s16\n") == 0,
          "%s: type 6 with three levels: MIPMAPCOUNT, MIPMAP|TEXTURE|COMPLEX, then GetAttachedSurface(0x400000) twice (got \"%s\")", e->label, dd->log);
    if (surface) {
        void *level1 = ((SurfPtrArg)SURF(surface, EEWEB_SLOT_GESurface_GetAttachedSurface))(surface, 0);
        void *level2 = level1 ? ((SurfPtrArg)SURF(level1, EEWEB_SLOT_GESurface_GetAttachedSurface))(level1, 0) : NULL;
        CHECK(level1 && level2 && S_ULONG(level1, EEWEB_SLOT_GESurface_GetWidth) == 32 && S_ULONG(level1, EEWEB_SLOT_GESurface_GetHeight) == 16 &&
                  S_ULONG(level2, EEWEB_SLOT_GESurface_GetWidth) == 16 && S_ULONG(level2, EEWEB_SLOT_GESurface_GetHeight) == 8 &&
                  S_ULONG(level2, EEWEB_SLOT_GESurface_GetType) == 6,
              "%s: the levels are half the size of the previous one (32x16, 16x8) and of type 6", e->label);
        CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetAllocatedSize) == 2 * 32 * 64 + 2 * 16 * 32 + 2 * 8 * 16,
              "%s: GetAllocatedSize() adds the levels' sizes (4096 + 1024 + 256)", e->label);
        log_clear(dd);
        engine_dereference(surface);
        CHECK(strcmp(dd->log, "s16.Release\ns15.Release\ns14.Release\n") == 0,
              "%s: destroying the mipmap chain releases the deepest level first (got \"%s\")", e->label, dd->log);
    }

    /* an unknown type: no flags and no caps */
    d = make_desc(9, 7, 0, 4, 4, 16, 0, 0);
    log_clear(dd);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(strcmp(dd->log, "dd.CreateSurface(+0x0=0x7c,+0x8=0x4,+0xc=0x4,+0x48=0x20,+0x4c=0x40,+0x54=0x10,+0x58=0xf800,+0x5c=0x7e0,+0x60=0x1f) -> s17\n") == 0,
          "%s: an unknown type sends no flags and no caps (got \"%s\")", e->label, dd->log);
    if (surface) {
        engine_dereference(surface);
    }

    /* DirectDraw fails: 1 and the error is recorded */
    d = make_desc(1, 7, 0, 4, 4, 16, 0, 0);
    dd->create_results[0] = (long)0x88760017;
    dd->create_result_count = 1;
    dd->create_result_pos = 0;
    engine_record_error(0);
    surface = new_surface(screen, NULL, dd, &d, &status);
    CHECK(status == 1 && surface == NULL && engine_native_error() == 0x88760017UL,
          "%s: a failing DirectDraw CreateSurface gives 1, no surface, and the error in the engine's log (got %ld, 0x%lx)", e->label, status,
          engine_native_error());
}

/* ---- the operations of a surface ---------------------------------------------------------------------- */

#define FS(dd, id) ((FakeSurface *)(dd)->by_id[id])
#define SCRIPT(dd, id, method, error, count)         \
    do {                                             \
        FS(dd, id)->script[method].code = (long)(error); \
        FS(dd, id)->script[method].times = (count);  \
    } while (0)

static void check_surface_operations(const struct Expect *e, const ScreenApi *api)
{
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Surface Device", 1, 0);
    if (!screen) {
        return;
    }
    GESurfaceDescriptionLayout da = make_desc(1, 7, 0, 64, 32, 16, 0, 0);
    GESurfaceDescriptionLayout db = make_desc(1, 7, 0, 32, 16, 16, 0, 0);
    long status;
    log_clear(&dd);
    void *a = new_surface(screen, NULL, &dd, &da, &status); /* s3 */
    void *b = new_surface(screen, NULL, &dd, &db, &status); /* s4 */
    CHECK(a && b, "%s: two surfaces for the operations", e->label);
    if (!a || !b) {
        return;
    }
    FakeSurface *fa = FS(&dd, 3);
    FakeSurface *fb = FS(&dd, 4);
    CHECK(fa && fb && fa->id == 3 && fb->id == 4, "%s: the surfaces are s3 and s4", e->label);

    /* Lock / Unlock */
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Lock))(a) == 0, "%s: Lock() succeeds", e->label);
    CHECK(strcmp(dd.log, "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n") == 0, "%s: Lock() locks the whole surface, waiting (got \"%s\")", e->label, dd.log);
    CHECK(S_ULONG(a, EEWEB_SLOT_GESurface_GetMemoryAddress) == (unsigned long)(uintptr_t)fa->mem && S_ULONG(a, EEWEB_SLOT_GESurface_GetBytesPerRow) == fa->pitch,
          "%s: ... GetMemoryAddress() and GetBytesPerRow() are what DirectDraw returned (pitch %lu)", e->label, fa->pitch);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Unlock))(a) == 0 && strcmp(dd.log, "s3.Unlock(rect=null)\n") == 0, "%s: Unlock() (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_LOCK, 0x8876021c, 2);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Lock))(a) == 0 &&
              strcmp(dd.log,
                     "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\ns3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n"
                     "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n") == 0,
          "%s: Lock() retries while DirectDraw is still drawing (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_LOCK, 0x887601c2, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Lock))(a) == 0xd, "%s: Lock() gives 0xd when the surface is lost", e->label);
    SCRIPT(&dd, 3, FS_LOCK, 0x88760091, 1);
    engine_record_error(0);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Lock))(a) == 1 && engine_native_error() == 0x88760091UL,
          "%s: Lock() gives 1 for another error, which is recorded in the engine's log", e->label);
    SCRIPT(&dd, 3, FS_UNLOCK, 0x88760248, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Unlock))(a) == 0, "%s: Unlock() of a surface that was not locked is not an error", e->label);
    SCRIPT(&dd, 3, FS_UNLOCK, 0x88760091, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_Unlock))(a) == 1, "%s: Unlock() gives 1 for another error", e->label);

    /* TextLock / TextUnlock: GetDC / ReleaseDC */
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextLock))(a) == 0 && strcmp(dd.log, "s3.GetDC\n") == 0, "%s: TextLock() asks GetDC (got \"%s\")", e->label, dd.log);
    CHECK(S_BOOL(a, EEWEB_SLOT_GESurface_GetIsTextLocked) && S_ULONG(a, EEWEB_SLOT_GESurface_GetTextDC) == 0xDC0003UL,
          "%s: ... the surface is text locked and GetTextDC() is the DC", e->label);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextLock))(a) == 2 && dd.log[0] == 0, "%s: TextLock() twice gives 2 without a DirectDraw call", e->label);
    SCRIPT(&dd, 3, FS_RELDC, 0x88760091, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextUnlock))(a) == 1 && S_BOOL(a, EEWEB_SLOT_GESurface_GetIsTextLocked),
          "%s: a failing ReleaseDC gives 1 and leaves the surface text locked", e->label);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextUnlock))(a) == 0 && strcmp(dd.log, "s3.ReleaseDC(0xdc0003)\n") == 0 &&
              !S_BOOL(a, EEWEB_SLOT_GESurface_GetIsTextLocked),
          "%s: TextUnlock() releases the DC (got \"%s\")", e->label, dd.log);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextUnlock))(a) == 2 && dd.log[0] == 0, "%s: TextUnlock() when not locked gives 2", e->label);
    SCRIPT(&dd, 3, FS_GETDC, 0x887601c2, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextLock))(a) == 0xd, "%s: TextLock() gives 0xd when the surface is lost", e->label);
    SCRIPT(&dd, 3, FS_GETDC, 0x88760091, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextLock))(a) == 1, "%s: TextLock() gives 1 for another error", e->label);

    /* Clear */
    GEColorLayout color = {1.0f, 0.5f, 0.25f};
    unsigned long fill = engine_pack_color(7, &color, 0.75f);
    char expected[256];
    snprintf(expected, sizeof expected, "s3.Blt(dst=null,src=null,srcrect=null,flags=0x400,fx={size=100,fill=0x%lx})\n", fill);
    log_clear(&dd);
    CHECK(((SurfClearAll)SURF(a, EEWEB_SLOT_GESurface_Clear_2))(a, &color, 0.75f) == 0 && strcmp(dd.log, expected) == 0,
          "%s: Clear(color, alpha) colour-fills the whole surface with the colour packed for its format (got \"%s\")", e->label, dd.log);
    GERectangleLayout rect = {20.9f, 2.5f, 30.7f, 4.2f}; /* bottom, left, right, top */
    snprintf(expected, sizeof expected, "s3.Blt(dst=[2,4,30,20],src=null,srcrect=null,flags=0x400,fx={size=100,fill=0x%lx})\n", fill);
    log_clear(&dd);
    CHECK(((SurfClearRect)SURF(a, EEWEB_SLOT_GESurface_Clear_1))(a, &rect, &color, 0.75f) == 0 && strcmp(dd.log, expected) == 0,
          "%s: Clear(rect, color, alpha) fills the rectangle, truncated to integers (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_BLT, 0x8876021c, 3);
    log_clear(&dd);
    CHECK(((SurfClearAll)SURF(a, EEWEB_SLOT_GESurface_Clear_2))(a, &color, 0.75f) == 0, "%s: Clear() retries while DirectDraw is still drawing", e->label);
    {
        int blts = 0;
        for (const char *p = dd.log; (p = strstr(p, "s3.Blt(")) != NULL; p++) {
            blts++;
        }
        CHECK(blts == 4, "%s: ... asking Blt four times (still drawing three times, then done; got %d)", e->label, blts);
    }
    SCRIPT(&dd, 3, FS_BLT, 0x887601c2, 1);
    CHECK(((SurfClearAll)SURF(a, EEWEB_SLOT_GESurface_Clear_2))(a, &color, 0.75f) == 0xd, "%s: Clear() gives 0xd when the surface is lost", e->label);
    SCRIPT(&dd, 3, FS_BLT, 0x88760091, 1);
    CHECK(((SurfClearRect)SURF(a, EEWEB_SLOT_GESurface_Clear_1))(a, &rect, &color, 0.75f) == 1, "%s: Clear(rect) gives 1 for another error", e->label);

    /* CopyTo: this surface is the source, the argument the destination */
    GERectangleLayout src_rect = {16.0f, 1.0f, 17.9f, 2.0f};
    GERectangleLayout dst_rect = {8.0f, 0.0f, 9.5f, 0.5f};
    log_clear(&dd);
    CHECK(((SurfCopyTo)SURF(a, EEWEB_SLOT_GESurface_CopyTo))(a, b, &src_rect, &dst_rect, 0) == 0 &&
              strcmp(dd.log, "s4.Blt(dst=[0,0,9,8],src=s3,srcrect=[1,2,17,16],flags=0x8000000,fx=null)\n") == 0,
          "%s: CopyTo blits from this surface into the destination, waiting (got \"%s\")", e->label, dd.log);
    log_clear(&dd);
    CHECK(((SurfCopyTo)SURF(a, EEWEB_SLOT_GESurface_CopyTo))(a, b, &src_rect, &dst_rect, 1) == 0 &&
              strcmp(dd.log, "s4.Blt(dst=[0,0,9,8],src=s3,srcrect=[1,2,17,16],flags=0x8008000,fx=null)\n") == 0,
          "%s: CopyTo with the colour key flag adds DDBLT_KEYSRC (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 4, FS_BLT, 0x887601ae, 1);
    CHECK(((SurfCopyTo)SURF(a, EEWEB_SLOT_GESurface_CopyTo))(a, b, &src_rect, &dst_rect, 0) == 0xd, "%s: CopyTo gives 0xd when the surface is busy", e->label);
    SCRIPT(&dd, 4, FS_BLT, 0x887601c2, 1);
    CHECK(((SurfCopyTo)SURF(a, EEWEB_SLOT_GESurface_CopyTo))(a, b, &src_rect, &dst_rect, 0) == 0xd, "%s: CopyTo gives 0xd when the surface is lost", e->label);
    SCRIPT(&dd, 4, FS_BLT, 0x88760091, 1);
    CHECK(((SurfCopyTo)SURF(a, EEWEB_SLOT_GESurface_CopyTo))(a, b, &src_rect, &dst_rect, 0) == 1, "%s: CopyTo gives 1 for another error", e->label);

    /* Flip, RestoreSurface */
    log_clear(&dd);
    CHECK(((SurfFlip)SURF(a, EEWEB_SLOT_GESurface_Flip))(a, 1) == 0 && strcmp(dd.log, "s3.Flip(target=null,flags=0x20)\n") == 0, "%s: Flip(true) (got \"%s\")", e->label, dd.log);
    log_clear(&dd);
    CHECK(((SurfFlip)SURF(a, EEWEB_SLOT_GESurface_Flip))(a, 0) == 0 && strcmp(dd.log, "s3.Flip(target=null,flags=0x28)\n") == 0, "%s: Flip(false) (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_FLIP, 0x8876021c, 1);
    log_clear(&dd);
    CHECK(((SurfFlip)SURF(a, EEWEB_SLOT_GESurface_Flip))(a, 1) == 0 && strcmp(dd.log, "s3.Flip(target=null,flags=0x20)\ns3.Flip(target=null,flags=0x20)\n") == 0,
          "%s: Flip retries while DirectDraw is still drawing (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_FLIP, 0x887601c2, 1);
    CHECK(((SurfFlip)SURF(a, EEWEB_SLOT_GESurface_Flip))(a, 1) == 0xd, "%s: Flip gives 0xd when the surface is lost", e->label);
    SCRIPT(&dd, 3, FS_FLIP, 0x88760091, 1);
    CHECK(((SurfFlip)SURF(a, EEWEB_SLOT_GESurface_Flip))(a, 1) == 1, "%s: Flip gives 1 for another error", e->label);
    log_clear(&dd);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_RestoreSurface))(a) == 0 && strcmp(dd.log, "s3.Restore\n") == 0, "%s: RestoreSurface() (got \"%s\")", e->label, dd.log);
    SCRIPT(&dd, 3, FS_RESTORE, 0x88760091, 1);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_RestoreSurface))(a) == 1, "%s: RestoreSurface() gives 1 on an error", e->label);

    /* SetTransparencyColor */
    GEColorLayout key = {0.2f, 0.4f, 0.6f};
    unsigned long packed = engine_pack_color(7, &key, 1.0f);
    snprintf(expected, sizeof expected, "s3.SetColorKey(flags=0x8,low=0x%lx,high=0x%lx)\n", packed, packed);
    log_clear(&dd);
    ((SurfSetColor)SURF(a, EEWEB_SLOT_GESurface_SetTransparencyColor))(a, &key);
    CHECK(strcmp(dd.log, expected) == 0, "%s: SetTransparencyColor sets the source colour key (DDCKEY_SRCBLT) to the colour packed for the format (got \"%s\")", e->label, dd.log);
    {
        const float *c = (const float *)((SurfPtr)SURF(a, EEWEB_SLOT_GESurface_GetTransparencyColor))(a);
        CHECK(memcmp(c, &key, sizeof key) == 0, "%s: ... and GetTransparencyColor() returns it (got %.9g %.9g %.9g)", e->label, c[0], c[1], c[2]);
        CHECK(!S_BOOL(a, EEWEB_SLOT_GESurface_GetIsTransparencyEnabled), "%s: ... without changing GetIsTransparencyEnabled() (original behaviour)", e->label);
    }

    /* AttachSurface / DetachSurface */
    log_clear(&dd);
    CHECK(((SurfPtrOp)SURF(a, EEWEB_SLOT_GESurface_AttachSurface))(a, b) == 0 && strcmp(dd.log, "s3.AddAttachedSurface(s4)\n") == 0,
          "%s: AttachSurface asks AddAttachedSurface (got \"%s\")", e->label, dd.log);
    CHECK(surf_refcount(b) == 2 && S_ULONG(a, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 1 &&
              ((SurfPtrArg)SURF(a, EEWEB_SLOT_GESurface_GetAttachedSurface))(a, 0) == b,
          "%s: ... the attached surface is referenced (count 2) and listed", e->label);
    CHECK(S_ULONG(a, EEWEB_SLOT_GESurface_GetAllocatedSize) == 4096 + 1024, "%s: GetAllocatedSize() includes the attached surface (4096 + 1024)", e->label);
    SCRIPT(&dd, 3, FS_DEL, 0x88760091, 1);
    CHECK(((SurfLongArg)SURF(a, EEWEB_SLOT_GESurface_DetachSurface))(a, 0) == 1 && S_ULONG(a, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 1 &&
              surf_refcount(b) == 2,
          "%s: a failing DetachSurface gives 1 and keeps the surface attached", e->label);
    log_clear(&dd);
    CHECK(((SurfLongArg)SURF(a, EEWEB_SLOT_GESurface_DetachSurface))(a, 0) == 0 && strcmp(dd.log, "s3.DeleteAttachedSurface(flags=0,s4)\n") == 0,
          "%s: DetachSurface(0) asks DeleteAttachedSurface (got \"%s\")", e->label, dd.log);
    CHECK(surf_refcount(b) == 1 && S_ULONG(a, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 0, "%s: ... and drops the reference and the list entry", e->label);
    SCRIPT(&dd, 3, FS_ADD, 0x88760091, 1);
    CHECK(((SurfPtrOp)SURF(a, EEWEB_SLOT_GESurface_AttachSurface))(a, b) == 1 && surf_refcount(b) == 1 && S_ULONG(a, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) == 0,
          "%s: a failing AttachSurface gives 1 and changes nothing", e->label);

    /* destruction: the text lock is released first, then the attached surfaces, then the DirectDraw surface */
    CHECK(((SurfPtrOp)SURF(a, EEWEB_SLOT_GESurface_AttachSurface))(a, b) == 0, "%s: attach again", e->label);
    CHECK(((SurfNoArg)SURF(a, EEWEB_SLOT_GESurface_TextLock))(a) == 0, "%s: text lock before destruction", e->label);
    log_clear(&dd);
    engine_dereference(a);
    CHECK(strcmp(dd.log, "s3.ReleaseDC(0xdc0003)\ns3.Release\n") == 0, "%s: destroying a surface releases its DC, lets go of the attached surface, then releases itself (got \"%s\")", e->label, dd.log);
    CHECK(surf_refcount(b) == 1, "%s: ... the attached surface survives with the reference the test holds", e->label);
    log_clear(&dd);
    engine_dereference(b);
    CHECK(strcmp(dd.log, "s4.Release\n") == 0, "%s: the last reference destroys the other surface (got \"%s\")", e->label, dd.log);
    destroy_screen(screen);
}

/* ---- the 32-bit probe of the screen's constructor ---------------------------------------------------- */

#define PROBE_PREFIX                                                                                   \
    "dd.GetCaps(size=380,hel=null)\n"                                                                  \
    "dd.EnumDisplayModes(flags=0,desc=null)\n"                                                         \
    "dd.GetDisplayMode(size=124)\n"                                                                    \
    "dd.SetCooperativeLevel(hwnd=WIN,flags=0x408)\n"
#define PROBE_SURFACE1                                                                                 \
    "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x2,+0xc=0x2,+0x48=0x20,+0x4c=0x40,+0x54=0x10,"      \
    "+0x58=0xf800,+0x5c=0x7e0,+0x60=0x1f,+0x68=0x10006040) -> s1\n"
#define PROBE_SURFACE2                                                                                 \
    "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x10,+0xc=0x10,+0x48=0x20,+0x4c=0x41,+0x54=0x20,"    \
    "+0x58=0xff0000,+0x5c=0xff00,+0x60=0xff,+0x64=0xff000000,+0x68=0x1000,+0x6c=0x18) -> s2\n"
#define PROBE_RELEASE_WINDOW "dd.SetCooperativeLevel(hwnd=WIN,flags=0x8)\n"

static void probe_case(const struct Expect *e, const ScreenApi *api, FakeDD *dd, const char *what, const char *expected_log, int device32, int texture32)
{
    void *screen = make_screen(api, dd, "Probe Device", 1, 0);
    log_normalize(dd);
    CHECK(screen != NULL, "%s: probe (%s): the screen is built", e->label, what);
    if (!screen) {
        return;
    }
    CHECK(strcmp(dd->log, expected_log) == 0, "%s: probe (%s): the constructor's DirectDraw calls (got \"%s\")", e->label, what, dd->log);
    CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitDeviceSupport) == device32 &&
              SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitTextureSupport) == texture32,
          "%s: probe (%s): Get32BitDeviceSupport() %d and Get32BitTextureSupport() %d", e->label, what, device32, texture32);
    destroy_screen(screen);
}

static void check_32bit_probe(const struct Expect *e, const ScreenApi *api)
{
    FakeDD dd;

    fake_dd_init(&dd);
    probe_case(e, api, &dd, "everything works",
               PROBE_PREFIX PROBE_SURFACE1
               "dd.QueryInterface(IDirect3D7)\nd3d.CreateDevice(HAL,s1)\ndev.GetCaps\ndev.EnumTextureFormats\n" PROBE_SURFACE2
               "s2.Release\ndev.Release\nd3d.Release\ns1.Release\n" PROBE_RELEASE_WINDOW,
               1, 1);

    fake_dd_init(&dd);
    dd.create_device_result = (long)0x88760091;
    probe_case(e, api, &dd, "CreateDevice fails",
               PROBE_PREFIX PROBE_SURFACE1 "dd.QueryInterface(IDirect3D7)\nd3d.CreateDevice(HAL,s1)\nd3d.Release\ns1.Release\n" PROBE_RELEASE_WINDOW, 0, 0);

    fake_dd_init(&dd);
    dd.device_depth_mask = 0x400 | 0x800; /* no 32-bit render target */
    probe_case(e, api, &dd, "no 32-bit render target",
               PROBE_PREFIX PROBE_SURFACE1
               "dd.QueryInterface(IDirect3D7)\nd3d.CreateDevice(HAL,s1)\ndev.GetCaps\ndev.EnumTextureFormats\n" PROBE_SURFACE2
               "s2.Release\ndev.Release\nd3d.Release\ns1.Release\n" PROBE_RELEASE_WINDOW,
               0, 1);

    fake_dd_init(&dd);
    dd.texture_format_count = 1; /* only 565 */
    probe_case(e, api, &dd, "no ARGB texture format",
               PROBE_PREFIX PROBE_SURFACE1
               "dd.QueryInterface(IDirect3D7)\nd3d.CreateDevice(HAL,s1)\ndev.GetCaps\ndev.EnumTextureFormats\n"
               "dev.Release\nd3d.Release\ns1.Release\n" PROBE_RELEASE_WINDOW,
               1, 0);

    fake_dd_init(&dd);
    dd.create_results[0] = 0;
    dd.create_results[1] = (long)0x88760091;
    dd.create_result_count = 2;
    probe_case(e, api, &dd, "the 16x16 texture cannot be created",
               PROBE_PREFIX PROBE_SURFACE1
               "dd.QueryInterface(IDirect3D7)\nd3d.CreateDevice(HAL,s1)\ndev.GetCaps\ndev.EnumTextureFormats\n"
               "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x10,+0xc=0x10,+0x48=0x20,+0x4c=0x41,+0x54=0x20,+0x58=0xff0000,+0x5c=0xff00,"
               "+0x60=0xff,+0x64=0xff000000,+0x68=0x1000,+0x6c=0x18) -> error 0x88760091\n"
               "dev.Release\nd3d.Release\ns1.Release\n" PROBE_RELEASE_WINDOW,
               1, 0);

    fake_dd_init(&dd);
    dd.create_results[0] = (long)0x88760091;
    dd.create_result_count = 1;
    probe_case(e, api, &dd, "the 2x2 surface cannot be created",
               PROBE_PREFIX
               "dd.CreateSurface(+0x0=0x7c,+0x4=0x1007,+0x8=0x2,+0xc=0x2,+0x48=0x20,+0x4c=0x40,+0x54=0x10,+0x58=0xf800,+0x5c=0x7e0,"
               "+0x60=0x1f,+0x68=0x10006040) -> error 0x88760091\n" PROBE_RELEASE_WINDOW,
               0, 0);

    fake_dd_init(&dd);
    dd.fail_coop_calls = 1;
    probe_case(e, api, &dd, "the window cannot be captured",
               "dd.GetCaps(size=380,hel=null)\ndd.EnumDisplayModes(flags=0,desc=null)\ndd.GetDisplayMode(size=124)\n"
               "dd.SetCooperativeLevel(hwnd=WIN,flags=0x408)\n",
               0, 0);
}

/* ---- the filtered copy -------------------------------------------------------------------------------- */

static void fill_pattern(unsigned char *mem, unsigned width, unsigned height, unsigned pitch, int bits)
{
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            unsigned r = (x * 37 + y * 11 + 5) & 255, g = (x * 13 + y * 29 + 70) & 255, b = (x * 7 + y * 53 + 140) & 255, a = (x * 31 + y * 17 + 9) & 255;
            if (bits == 16) {
                *(uint16_t *)(mem + y * pitch + x * 2) = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                *(uint32_t *)(mem + y * pitch + x * 4) = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

static void filter_case(const struct Expect *e, const ScreenApi *api, int bits)
{
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Filter Device", 1, 0);
    if (!screen) {
        return;
    }
    unsigned format = bits == 16 ? 7 : 0;
    GESurfaceDescriptionLayout ds = make_desc(1, format, 0, 8, 8, (unsigned)bits, 0, 0);
    GESurfaceDescriptionLayout dt = make_desc(1, format, 0, 4, 4, (unsigned)bits, 0, 0);
    long status;
    void *src = new_surface(screen, NULL, &dd, &ds, &status); /* s3 */
    void *dst = new_surface(screen, NULL, &dd, &dt, &status); /* s4 */
    if (!src || !dst) {
        CHECK(0, "%s: filter %d: surfaces", e->label, bits);
        return;
    }
    FakeSurface *fs = FS(&dd, 3);
    FakeSurface *fd = FS(&dd, 4);
    fill_pattern(fs->mem, 8, 8, (unsigned)fs->pitch, bits);
    memset(fd->mem, 0xCD, fd->pitch * 4);
    GERectangleLayout src_rect = {6.5f, 0.5f, 6.5f, 0.5f};
    GERectangleLayout dst_rect = {4.0f, 0.0f, 4.0f, 0.0f};
    log_clear(&dd);
    long r = ((SurfCopyFilter)SURF(src, EEWEB_SLOT_GESurface_CopyWithFilter))(src, dst, &src_rect, &dst_rect);
    CHECK(r == 0, "%s: filter %d: CopyWithFilter succeeds", e->label, bits);
    CHECK(strcmp(dd.log,
                 "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\ns4.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n"
                 "s4.Unlock(rect=null)\ns3.Unlock(rect=null)\n") == 0,
          "%s: filter %d: it locks the source, then the destination, and unlocks them in the opposite order (got \"%s\")", e->label, bits, dd.log);
    unsigned long crc = fnv(fd->mem, fd->pitch * 4);
    OBS("fake: filter %d destination checksum 0x%08lx", bits, crc);

    /* more rectangles: scaling up and down, sample positions on and off the pixel grid, rounding ties, an offset
     * destination; each result is compared with the original's through its checksum */
    static const float kRects[][8] = {
        /* source: bottom, left, right, top  destination: bottom, left, right, top */
        {7.25f, 1.25f, 6.0f, 0.25f, 3.0f, 1.0f, 4.0f, 1.0f},
        {6.0f, 0.0f, 4.0f, 2.0f, 4.0f, 0.0f, 4.0f, 0.0f},
        {7.0f, 1.0f, 7.0f, 1.0f, 4.0f, 0.0f, 4.0f, 0.0f},
        {3.5f, 2.5f, 3.5f, 2.5f, 2.0f, 1.0f, 3.0f, 0.0f},
        {7.9f, 0.1f, 7.9f, 0.1f, 4.0f, 0.0f, 4.0f, 0.0f},
        {5.0f, 3.0f, 6.0f, 3.0f, 3.0f, 0.0f, 3.0f, 1.0f},
        {7.5f, 0.5f, 7.5f, 0.5f, 4.0f, 0.0f, 4.0f, 0.0f},
    };
    for (unsigned k = 0; k < sizeof kRects / sizeof kRects[0]; k++) {
        GERectangleLayout from = {kRects[k][0], kRects[k][1], kRects[k][2], kRects[k][3]};
        GERectangleLayout to = {kRects[k][4], kRects[k][5], kRects[k][6], kRects[k][7]};
        memset(fd->mem, 0xCD, fd->pitch * 4);
        r = ((SurfCopyFilter)SURF(src, EEWEB_SLOT_GESurface_CopyWithFilter))(src, dst, &from, &to);
        OBS("fake: filter %d rectangle %u destination checksum 0x%08lx (result %ld)", bits, k, fnv(fd->mem, fd->pitch * 4), r);
    }

    /* the source cannot be locked: nothing else happens */
    memset(fd->mem, 0xCD, fd->pitch * 4);
    SCRIPT(&dd, 3, FS_LOCK, 0x887601c2, 1);
    log_clear(&dd);
    r = ((SurfCopyFilter)SURF(src, EEWEB_SLOT_GESurface_CopyWithFilter))(src, dst, &src_rect, &dst_rect);
    CHECK(r == 0xd && strcmp(dd.log, "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n") == 0,
          "%s: filter %d: when the source is lost the result is 0xd and nothing else is called (got %ld, \"%s\")", e->label, bits, r, dd.log);
    /* the destination cannot be locked: the source is unlocked and the result is that of Unlock */
    unsigned long untouched = fnv(fd->mem, fd->pitch * 4);
    SCRIPT(&dd, 4, FS_LOCK, 0x887601c2, 1);
    log_clear(&dd);
    r = ((SurfCopyFilter)SURF(src, EEWEB_SLOT_GESurface_CopyWithFilter))(src, dst, &src_rect, &dst_rect);
    CHECK(r == 0 && strcmp(dd.log,
                           "s3.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\ns4.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\n"
                           "s3.Unlock(rect=null)\n") == 0 &&
              fnv(fd->mem, fd->pitch * 4) == untouched,
          "%s: filter %d: when the destination is lost the source is unlocked and the result is that of its Unlock (got %ld, \"%s\")", e->label, bits, r, dd.log);
    engine_dereference(src);
    engine_dereference(dst);
    destroy_screen(screen);
}

static void check_filtered_copy(const struct Expect *e, const ScreenApi *api)
{
    filter_case(e, api, 16);
    filter_case(e, api, 32);
}

/* ---- windows --------------------------------------------------------------------------------------------- */

static void window_case(const struct Expect *e, void *screen, const char *what, float x, float y, unsigned w, unsigned h, unsigned style, void *parent,
                        const char *class_name)
{
    UString cls, title;
    ustring_construct_from_chars(&cls, class_name);
    ustring_construct_from_chars(&title, "EEWeb test window");
    GE2DPointLayout point = {x, y};
    void *window = NULL;
    long r = ((ScreenCreateWindow)screen_slot(screen, EEWEB_SLOT_GEScreen_CreateAWindow))(screen, &point, w, h, &cls, &title, &window, style, parent);
    ustring_destruct(&cls);
    ustring_destruct(&title);
    if (strcmp(class_name, "STATIC") != 0) {
        CHECK(r == 1 && window == NULL && engine_native_error() == 1411UL,
              "%s: window (%s): an unknown window class gives 1, no window and ERROR_CLASS_DOES_NOT_EXIST in the engine's log (got %ld, 0x%lx)", e->label, what, r,
              engine_native_error());
        return;
    }
    CHECK(r == 0 && window != NULL, "%s: window (%s): CreateAWindow succeeds", e->label, what);
    if (!window) {
        return;
    }
    const GEWindowLayout *gw = (const GEWindowLayout *)window;
    CHECK(gw->hwnd != NULL && gw->width == w && gw->height == h && gw->style == style && gw->ownsWindow == 1 && gw->clientOrigin.x == 0.0f && gw->clientOrigin.y == 0.0f,
          "%s: window (%s): the GEWindow holds the handle, the size, the style, ownsWindow 1 and a zero client origin", e->label, what);
    HWND hwnd = (HWND)gw->hwnd;
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    OBS("fake: window %s: style 0x%08lx exstyle 0x%08lx rect %ld,%ld %ldx%ld client %ldx%ld parent %s", what, (unsigned long)GetWindowLongA(hwnd, GWL_STYLE),
        (unsigned long)GetWindowLongA(hwnd, GWL_EXSTYLE), wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, cr.right - cr.left, cr.bottom - cr.top,
        GetParent(hwnd) == (HWND)parent && parent ? "given" : (GetParent(hwnd) ? "other" : "none"));
    if (style != 1) {
        CHECK(wr.right - wr.left == (long)w && wr.bottom - wr.top == (long)h, "%s: window (%s): the window is the size asked for", e->label, what);
    } /* an overlapped window is sized by AdjustWindowRect; its exact outer size is compared through the observation line */
    typedef void(THISCALL * WindowDtor)(void *self);
    typedef void(__cdecl * OperatorDelete)(void *);
    WindowDtor dtor;
    OperatorDelete op_delete;
    void *raw = engine_proc("??1GEWindow@@QAE@XZ");
    memcpy(&dtor, &raw, sizeof dtor);
    raw = proc_address(GetModuleHandleA("msvcrt.dll"), "??3@YAXPAX@Z");
    memcpy(&op_delete, &raw, sizeof op_delete);
    dtor(window);
    op_delete(window);
}

static void check_windows(const struct Expect *e, const ScreenApi *api)
{
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Window Device", 1, 0);
    if (!screen) {
        return;
    }
    window_case(e, screen, "popup", 10.4f, 20.6f, 100, 50, 0, NULL, "STATIC");
    window_case(e, screen, "overlapped", 10.4f, 20.6f, 100, 50, 1, NULL, "STATIC");
    {
        /* with a parent: the engine passes its GEWindow, whose first field is the handle */
        struct {
            void *hwnd;
        } parent = {CreateWindowExA(0, "STATIC", "parent", 0x90000000, 0, 0, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL)};
        CHECK(parent.hwnd != NULL, "%s: a parent window for the child window case", e->label);
        window_case(e, screen, "child", 5.0f, 6.0f, 40, 30, 0, &parent, "STATIC");
        if (parent.hwnd) {
            DestroyWindow((HWND)parent.hwnd);
        }
    }
    window_case(e, screen, "unknown class", 0.0f, 0.0f, 16, 16, 0, NULL, "EEWebNoSuchClass");
    destroy_screen(screen);
}

/* ---- all of it ----------------------------------------------------------------------------------------- */

static void check_surfaces(const struct Expect *e, HMODULE h, const ScreenApi *api)
{
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Surface Device", 1, 0);
    CHECK(screen != NULL, "%s: a screen for the surface checks", e->label);
    if (screen) {
        check_create_surface(e, screen, &dd);
        /* RTTI: DX7Surface derives from GESurface (and, in the original, through it from the engine's UReferenceCounter) */
        FakeDD dd2;
        fake_dd_init(&dd2);
        void *screen2 = make_screen(api, &dd2, "RTTI Device", 1, 0);
        GESurfaceDescriptionLayout d = make_desc(1, 7, 0, 4, 4, 16, 0, 0);
        long status;
        void *surface = screen2 ? new_surface(screen2, NULL, &dd2, &d, &status) : NULL;
        if (surface) {
            void *const *vtable = *(void *const **)surface;
            CHECK(in_module(h, vtable), "%s: the surface's vtable lies inside the plugin", e->label);
            if (in_module(h, vtable)) {
                const struct RttiLocator *col = locator_of(vtable);
                CHECK(in_module(h, col) && strcmp(col->type->name, ".?AVDX7Surface@@") == 0, "%s: the surface's RTTI names DX7Surface", e->label);
                CHECK(col->hierarchy->count >= 2 && strcmp(col->hierarchy->bases[1]->type->name, ".?AVGESurface@@") == 0, "%s: DX7Surface derives from GESurface", e->label);
                INFO("%s: DX7Surface's class hierarchy has %u entries (the original's has 3: its GESurface derives from the engine's UReferenceCounter, which ours does not model)",
                     e->label, (unsigned)col->hierarchy->count);
                CHECK(vtable_slot_count(h, vtable) == EEWEB_SLOTS_GESurface, "%s: the surface's vtable has %d slots (GESurface has 32)", e->label,
                      (int)vtable_slot_count(h, vtable));
            }
            engine_dereference(surface);
        }
        if (screen2) {
            destroy_screen(screen2);
        }
        destroy_screen(screen);
    }
    check_32bit_probe(e, api);
    check_surface_operations(e, api);
    check_filtered_copy(e, api);
    check_windows(e, api);
}
