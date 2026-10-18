/*
 * Pixel and texture format checks (task P2.6.3).  Included by screen_checks.h after surface_checks.h, so it uses
 * the fake DirectDraw 7 and the helpers of both.
 *
 * Which parts of the rasterizer depend on a pixel format (DX7HRTnLDisplay.dll, read from its decompilation):
 *   - the 12-entry format table (28-byte entries, data at 0x1001779c).  Read by EnumVideoModeCallback (a mode gets
 *     the index of the first entry with the same bit count and four masks, 14 when none) and by CreateSurface
 *     (the whole entry becomes the DDPIXELFORMAT).  Entry 8 is DXT1, 9 to 11 are depth buffers.
 *   - DX7Screen::EnumTextureFormatCallback: the only caller of IDirect3DDevice7::EnumTextureFormats is the 32-bit
 *     probe; it accepts a format with 32 bits and a non-zero alpha, red, green and blue mask (tested against
 *     0xff000000, 0xff0000, 0xff00 and 0xff) and stops the enumeration there.
 *   - DX7Surface::Clear, SetTransparencyColor and CopyWithFilter hand the surface's format index to the ENGINE's
 *     GEPackColor / GEUnpackRed/Green/Blue/Alpha (Low-Level Engine.dll).  The bit layouts of 4444, 555, 565, 888A
 *     and the other formats are implemented there, not in the display DLL, so there is nothing to reimplement:
 *     the checks below prove that every format index reaches those functions unchanged and that the results are
 *     the engine's.
 * The texture selection for the game's textures (CreateTextureHandle, SetTexture, DemoteTexture) belongs to the
 * texture area (P2.6.5): the golden's six texture formats are what the real device OFFERS; this DLL does not
 * choose among them anywhere outside the probe above.
 *
 * Both variants are driven the same way and must give the same answers; the filter results are compared through
 * "obs" lines.
 */

/* The table, as the original holds it: flags, fourCC, bit count, red, green, blue, alpha/z masks. */
typedef struct FormatRow {
    unsigned long flags, fourcc, bits, r, g, b, a;
} FormatRow;

static const FormatRow kFormatTable[12] = {
    {0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, /* 0  A8R8G8B8 */
    {0x40, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000}, /* 1  X8R8G8B8 */
    {0x41, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000}, /* 2  A4R4G4B4 */
    {0x41, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000}, /* 3  A1R5G5B5 */
    {0x41, 0, 16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000001}, /* 4  R5G5B5A1 */
    {0x40, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00000000}, /* 5  X1R5G5B5 */
    {0x40, 0, 16, 0x0000f800, 0x000007c0, 0x0000003e, 0x00000000}, /* 6  */
    {0x40, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000}, /* 7  R5G6B5 */
    {0x04, 0x31545844, 0, 0, 0, 0, 0},                             /* 8  DXT1 */
    {0x400, 0, 32, 0, 0xffffffff, 0, 0},                           /* 9  32-bit depth */
    {0x400, 0, 24, 0, 0x00ffffff, 0, 0},                           /* 10 24-bit depth */
    {0x400, 0, 16, 0, 0x0000ffff, 0, 0},                           /* 11 16-bit depth */
};

/* ---- mode enumeration: every table entry, and what matches none ---------------------------------------- */

static void check_mode_format_indices(const struct Expect *e, const ScreenApi *api)
{
    /* The first mode is the one the fake reports as current (1024x768x32). */
    static const FakeMode kModes[] = {
        {1024, 768, 60, 32, 0xff0000, 0xff00, 0xff, 0},
        {1, 1, 60, 32, 0xff0000, 0xff00, 0xff, 0xff000000u}, /* 0  */
        {2, 1, 60, 16, 0xf00, 0xf0, 0xf, 0xf000},            /* 2  */
        {3, 1, 60, 16, 0x7c00, 0x3e0, 0x1f, 0x8000},         /* 3  */
        {4, 1, 60, 16, 0xf800, 0x7c0, 0x3e, 0x1},            /* 4  */
        {5, 1, 60, 16, 0x7c00, 0x3e0, 0x1f, 0},              /* 5  */
        {6, 1, 60, 16, 0xf800, 0x7c0, 0x3e, 0},              /* 6  */
        {7, 1, 60, 16, 0xf800, 0x7e0, 0x1f, 0},              /* 7  */
        {8, 1, 60, 0, 0, 0, 0, 0},                           /* 8: a mode with no bits and no masks matches the DXT1 row */
        {9, 1, 60, 24, 0xff0000, 0xff00, 0xff, 0},           /* 14: no 24-bit colour row */
        {10, 1, 60, 16, 0xf800, 0x7e0, 0x1f, 0x1},           /* 14: alpha mask differs from 565 */
        {11, 1, 60, 32, 0xff0000, 0xff00, 0xff, 0x80000000u}, /* 14: alpha mask differs from A8R8G8B8 */
        {12, 1, 60, 8, 0, 0, 0, 0},                          /* 14: palettized */
        {13, 1, 60, 32, 0xff, 0xff00, 0xff0000, 0},          /* 14: B8G8R8 */
        {14, 1, 60, 16, 0xf800, 0x7e0, 0x1f, 0xf000},        /* 14 */
        {15, 1, 60, 32, 0xff0000, 0xff00, 0xff, 0x1},        /* 14 */
    };
    static const unsigned kIndex[] = {1, 0, 2, 3, 4, 5, 6, 7, 8, 14, 14, 14, 14, 14, 14, 14};
    FakeDD dd;
    fake_dd_init(&dd);
    dd.modes = kModes;
    dd.mode_count = (unsigned)(sizeof kModes / sizeof kModes[0]);
    void *screen = make_screen(api, &dd, "Format Modes Device", 1, 0);
    CHECK(screen != NULL, "%s: formats: a screen over modes with every pixel format of the table", e->label);
    if (!screen) {
        return;
    }
    CHECK(((ScreenULong)screen_slot(screen, EEWEB_SLOT_GEScreen_GetModeCount))(screen) == dd.mode_count, "%s: formats: GetModeCount() is %u", e->label, dd.mode_count);
    for (unsigned i = 0; i < dd.mode_count; i++) {
        const uint32_t *m = (const uint32_t *)((ScreenPtrArg)screen_slot(screen, EEWEB_SLOT_GEScreen_GetMode))(screen, i);
        CHECK(m[3] == kModes[i].bpp && m[4] == kIndex[i], "%s: formats: mode %u (%u bits, masks %x/%x/%x/%x) has bit count %u and format index %u (got %u, %u)", e->label,
              i, kModes[i].bpp, kModes[i].r, kModes[i].g, kModes[i].b, kModes[i].a, kModes[i].bpp, kIndex[i], (unsigned)m[3], (unsigned)m[4]);
    }
    destroy_screen(screen);
}

/* ---- CreateSurface: the DDSURFACEDESC2 of every table entry ------------------------------------------- */

static void append_field(char *text, size_t size, unsigned offset, unsigned long value)
{
    if (value) {
        char item[48];
        snprintf(item, sizeof item, "%s+0x%x=0x%lx", text[0] ? "," : "", offset, value);
        strncat(text, item, size - strlen(text) - 1);
    }
}

static void check_create_surface_formats(const struct Expect *e, const ScreenApi *api)
{
    static const struct {
        unsigned type;
        unsigned long flags, caps0, caps1;
    } kKinds[] = {
        {1, 0x1007, 0x10004040, 0},   /* offscreen video memory */
        {5, 0x1007, 0x1000, 0x14},    /* texture, not 3D, not volatile */
        {3, 0x1007, 0x10024000, 0},   /* depth buffer */
    };
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Format Surface Device", 1, 0);
    CHECK(screen != NULL, "%s: formats: a screen for CreateSurface", e->label);
    if (!screen) {
        return;
    }
    for (unsigned format = 0; format < 12; format++) {
        for (unsigned k = 0; k < 3; k++) {
            if ((k == 2) != (format >= 9)) {
                continue; /* depth buffers use type 3 only; colour formats use types 1 and 5 */
            }
            const FormatRow *row = &kFormatTable[format];
            GESurfaceDescriptionLayout d = make_desc(kKinds[k].type, format, 0, 24, 8, (unsigned)row->bits, 0, 0);
            char expected[512] = "dd.CreateSurface(";
            char fields[400] = "";
            append_field(fields, sizeof fields, 0x0, 0x7c);
            append_field(fields, sizeof fields, 0x4, kKinds[k].flags);
            append_field(fields, sizeof fields, 0x8, 8);
            append_field(fields, sizeof fields, 0xc, 24);
            append_field(fields, sizeof fields, 0x48, 0x20);
            append_field(fields, sizeof fields, 0x4c, row->flags);
            append_field(fields, sizeof fields, 0x50, row->fourcc);
            append_field(fields, sizeof fields, 0x54, row->bits);
            append_field(fields, sizeof fields, 0x58, row->r);
            append_field(fields, sizeof fields, 0x5c, row->g);
            append_field(fields, sizeof fields, 0x60, row->b);
            append_field(fields, sizeof fields, 0x64, row->a);
            append_field(fields, sizeof fields, 0x68, kKinds[k].caps0);
            append_field(fields, sizeof fields, 0x6c, kKinds[k].caps1);
            strncat(expected, fields, sizeof expected - strlen(expected) - 1);
            char tail[32];
            snprintf(tail, sizeof tail, ") -> s%d\n", dd.surface_count + 1);
            strncat(expected, tail, sizeof expected - strlen(expected) - 1);

            log_clear(&dd);
            long status = -1;
            void *surface = new_surface(screen, NULL, &dd, &d, &status);
            log_normalize(&dd);
            CHECK(status == 0 && surface != NULL && strcmp(dd.log, expected) == 0,
                  "%s: formats: CreateSurface(type %u, format %u) sends the table entry as the DDPIXELFORMAT (expected \"%s\", got \"%s\")", e->label, kKinds[k].type, format,
                  expected, dd.log);
            if (surface) {
                CHECK(S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelFormat) == format && S_ULONG(surface, EEWEB_SLOT_GESurface_GetPixelDepth) == row->bits,
                      "%s: formats: ... GetPixelFormat() %u and GetPixelDepth() %lu", e->label, format, row->bits);
                engine_dereference(surface);
            }
        }
    }
    destroy_screen(screen);
}

/* ---- Clear and SetTransparencyColor: the engine packs the colour for the surface's own format ---------- */

static void check_pack_formats(const struct Expect *e, const ScreenApi *api)
{
    GEColorLayout color = {1.0f, 0.5f, 0.25f};
    GEColorLayout key = {0.2f, 0.4f, 0.6f};
    unsigned long fills[8] = {0}, keys[8] = {0};
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Format Pack Device", 1, 0);
    CHECK(screen != NULL, "%s: formats: a screen for packing", e->label);
    if (!screen) {
        return;
    }
    for (unsigned format = 0; format < 8; format++) {
        GESurfaceDescriptionLayout d = make_desc(1, format, 0, 4, 4, (unsigned)kFormatTable[format].bits, 0, 0);
        long status;
        void *surface = new_surface(screen, NULL, &dd, &d, &status);
        CHECK(surface != NULL, "%s: formats: a surface of format %u", e->label, format);
        if (surface) {
            const int id = dd.surface_count;
            char expected[200];
            fills[format] = engine_pack_color(format, &color, 0.75f);
            keys[format] = engine_pack_color(format, &key, 1.0f);
            snprintf(expected, sizeof expected, "s%d.Blt(dst=null,src=null,srcrect=null,flags=0x400,fx={size=100,fill=0x%lx})\n", id, fills[format]);
            log_clear(&dd);
            CHECK(((SurfClearAll)SURF(surface, EEWEB_SLOT_GESurface_Clear_2))(surface, &color, 0.75f) == 0 && strcmp(dd.log, expected) == 0,
                  "%s: formats: Clear(color, alpha) on format %u fills with the colour the engine packs for it (expected \"%s\", got \"%s\")", e->label, format, expected, dd.log);
            GERectangleLayout rect = {3.0f, 1.0f, 3.0f, 1.0f};
            snprintf(expected, sizeof expected, "s%d.Blt(dst=[1,1,3,3],src=null,srcrect=null,flags=0x400,fx={size=100,fill=0x%lx})\n", id, fills[format]);
            log_clear(&dd);
            CHECK(((SurfClearRect)SURF(surface, EEWEB_SLOT_GESurface_Clear_1))(surface, &rect, &color, 0.75f) == 0 && strcmp(dd.log, expected) == 0,
                  "%s: formats: Clear(rect, color, alpha) on format %u uses the same packed colour (got \"%s\")", e->label, format, dd.log);
            snprintf(expected, sizeof expected, "s%d.SetColorKey(flags=0x8,low=0x%lx,high=0x%lx)\n", id, keys[format], keys[format]);
            log_clear(&dd);
            ((SurfSetColor)SURF(surface, EEWEB_SLOT_GESurface_SetTransparencyColor))(surface, &key);
            CHECK(strcmp(dd.log, expected) == 0, "%s: formats: SetTransparencyColor on format %u packs the key for it (expected \"%s\", got \"%s\")", e->label, format, expected,
                  dd.log);
            OBS("fake: format %u: fill 0x%08lx key 0x%08lx", format, fills[format], keys[format]);
            engine_dereference(surface);
        }
    }
    destroy_screen(screen);
    /* A mutation that sends one format index for all of them must be visible: the formats pack differently. */
    int distinct = 1;
    for (unsigned a = 0; a < 8; a++) {
        for (unsigned b = a + 1; b < 8; b++) {
            if (fills[a] == fills[b] && keys[a] == keys[b]) {
                distinct = 0;
                INFO("%s: formats %u and %u pack both test colours alike", e->label, a, b);
            }
        }
    }
    CHECK(distinct, "%s: formats: the eight colour formats pack the two test colours differently (a wrong index cannot hide)", e->label);
}

/* ---- the filtered copy in every colour format ---------------------------------------------------------- */

static void fill_raw(unsigned char *mem, unsigned width, unsigned height, unsigned pitch, unsigned bits, unsigned seed)
{
    unsigned long state = 0x9e3779b9UL * (seed + 1);
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            state = state * 1664525UL + 1013904223UL;
            unsigned long v = (state >> 8) & 0xffffffUL;
            v = v * 257UL + (state >> 3);
            if (bits == 16) {
                *(uint16_t *)(mem + y * pitch + x * 2) = (uint16_t)v;
            } else {
                *(uint32_t *)(mem + y * pitch + x * 4) = (uint32_t)v;
            }
        }
    }
}

static void check_filter_formats(const struct Expect *e, const ScreenApi *api)
{
    static const float kRects[][8] = {
        /* source: bottom, left, right, top  destination: bottom, left, right, top */
        {6.5f, 0.5f, 6.5f, 0.5f, 4.0f, 0.0f, 4.0f, 0.0f},
        {7.25f, 1.25f, 6.0f, 0.25f, 3.0f, 1.0f, 4.0f, 1.0f},
        {3.5f, 2.5f, 3.5f, 2.5f, 2.0f, 1.0f, 3.0f, 0.0f},
        {7.9f, 0.1f, 7.9f, 0.1f, 4.0f, 0.0f, 4.0f, 0.0f},
    };
    FakeDD dd;
    fake_dd_init(&dd);
    void *screen = make_screen(api, &dd, "Format Filter Device", 1, 0);
    CHECK(screen != NULL, "%s: formats: a screen for filtering", e->label);
    if (!screen) {
        return;
    }
    for (unsigned format = 0; format < 8; format++) {
        const unsigned bits = (unsigned)kFormatTable[format].bits;
        GESurfaceDescriptionLayout ds = make_desc(1, format, 0, 8, 8, bits, 0, 0);
        GESurfaceDescriptionLayout dt = make_desc(1, format, 0, 4, 4, bits, 0, 0);
        long status;
        void *src = new_surface(screen, NULL, &dd, &ds, &status);
        const int src_id = dd.surface_count;
        void *dst = new_surface(screen, NULL, &dd, &dt, &status);
        const int dst_id = dd.surface_count;
        if (!src || !dst) {
            CHECK(0, "%s: formats: filter surfaces of format %u", e->label, format);
            continue;
        }
        FakeSurface *fs = FS(&dd, src_id);
        FakeSurface *fd = FS(&dd, dst_id);
        fill_raw(fs->mem, 8, 8, (unsigned)fs->pitch, bits, format);
        char expected[256];
        snprintf(expected, sizeof expected, "s%d.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\ns%d.Lock(rect=null,desc.size=124,flags=0x0,event=0x0)\ns%d.Unlock(rect=null)\ns%d.Unlock(rect=null)\n",
                 src_id, dst_id, dst_id, src_id);
        for (unsigned k = 0; k < sizeof kRects / sizeof kRects[0]; k++) {
            GERectangleLayout from = {kRects[k][0], kRects[k][1], kRects[k][2], kRects[k][3]};
            GERectangleLayout to = {kRects[k][4], kRects[k][5], kRects[k][6], kRects[k][7]};
            memset(fd->mem, 0xCD, fd->pitch * 4);
            log_clear(&dd);
            long r = ((SurfCopyFilter)SURF(src, EEWEB_SLOT_GESurface_CopyWithFilter))(src, dst, &from, &to);
            CHECK(r == 0 && strcmp(dd.log, expected) == 0, "%s: formats: filter format %u rectangle %u locks, unlocks and succeeds (got %ld, \"%s\")", e->label, format, k, r, dd.log);
            OBS("fake: format %u filter rectangle %u destination checksum 0x%08lx", format, k, fnv(fd->mem, fd->pitch * 4));
        }
        engine_dereference(src);
        engine_dereference(dst);
    }
    destroy_screen(screen);
}

/* ---- texture formats: what the 32-bit probe accepts ---------------------------------------------------- */

typedef struct TexCase {
    const char *what;
    int count;
    unsigned long formats[8][5]; /* bit count, r, g, b, a */
    int texture32;
    int delivered;
} TexCase;

static void check_texture_format_enumeration(const struct Expect *e, const ScreenApi *api)
{
    /* The six formats the golden's device offers, in its order (tests/ddraw_trace/golden/startup-menu-n60.summary.txt). */
#define F555 {16, 0x7c00, 0x3e0, 0x1f, 0}
#define F565 {16, 0xf800, 0x7e0, 0x1f, 0}
#define F888 {32, 0xff0000, 0xff00, 0xff, 0}
#define F1555 {16, 0x7c00, 0x3e0, 0x1f, 0x8000}
#define F4444 {16, 0xf00, 0xf0, 0xf, 0xf000}
#define F8888 {32, 0xff0000, 0xff00, 0xff, 0xff000000UL}
    static const TexCase kCases[] = {
        {"the golden's six formats", 6, {F555, F565, F888, F1555, F4444, F8888}, 1, 6},
        {"the golden's formats without 32-bit ARGB", 5, {F555, F565, F888, F1555, F4444}, 0, 5},
        {"ARGB first: the enumeration stops there", 3, {F8888, F565, F555}, 1, 1},
        {"only 32-bit RGB", 1, {F888}, 0, 1},
        {"only 16-bit formats", 3, {F555, F565, F4444}, 0, 3},
        {"BGRA masks (red is not in 0xff0000)", 1, {{32, 0xff, 0xff00, 0xff0000, 0xff000000UL}}, 0, 1},
        {"32 bits with a lax test: every mask only needs one bit in range", 1, {{32, 0x10000, 0x100, 0x1, 0x1000000}}, 1, 1},
        {"32 bits, alpha mask outside 0xff000000", 1, {{32, 0xff0000, 0xff00, 0xff, 0xff0000}}, 0, 1},
        {"32 bits, no green mask", 1, {{32, 0xff0000, 0, 0xff, 0xff000000UL}}, 0, 1},
        {"24 bits", 1, {{24, 0xff0000, 0xff00, 0xff, 0xff000000UL}}, 0, 1},
        {"no formats at all", 0, {{0}}, 0, 0},
    };
    for (unsigned c = 0; c < sizeof kCases / sizeof kCases[0]; c++) {
        const TexCase *tc = &kCases[c];
        FakeDD dd;
        fake_dd_init(&dd);
        dd.texture_format_count = tc->count;
        for (int i = 0; i < tc->count; i++) {
            for (int j = 0; j < 5; j++) {
                dd.texture_formats[i][j] = tc->formats[i][j];
            }
        }
        void *screen = make_screen(api, &dd, "Texture Format Device", 1, 0);
        log_normalize(&dd);
        CHECK(screen != NULL, "%s: texture formats (%s): the screen is built", e->label, tc->what);
        if (!screen) {
            continue;
        }
        CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitDeviceSupport) == 1 && SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitTextureSupport) == tc->texture32,
              "%s: texture formats (%s): Get32BitTextureSupport() is %d", e->label, tc->what, tc->texture32);
        CHECK(dd.texture_formats_delivered == tc->delivered, "%s: texture formats (%s): the callback saw %d formats before the enumeration ended (got %d)", e->label, tc->what,
              tc->delivered, dd.texture_formats_delivered);
        CHECK((strstr(dd.log, "+0x8=0x10,+0xc=0x10,+0x48=0x20,+0x4c=0x41,+0x54=0x20") != NULL) == (tc->texture32 != 0),
              "%s: texture formats (%s): the 16x16 ARGB probe texture is created exactly when a format was accepted", e->label, tc->what);
        destroy_screen(screen);
    }
#undef F555
#undef F565
#undef F888
#undef F1555
#undef F4444
#undef F8888
}

/* The 2x2 surface of the 32-bit probe takes the format of the FIRST 800x600x16 mode, whatever its masks. */
static void check_probe_surface_format(const struct Expect *e, const ScreenApi *api)
{
    static const FakeMode kFirst4444[] = {
        {1024, 768, 60, 32, 0xff0000, 0xff00, 0xff, 0},
        {800, 600, 60, 16, 0xf00, 0xf0, 0xf, 0xf000},
        {800, 600, 60, 16, 0xf800, 0x7e0, 0x1f, 0},
    };
    static const FakeMode kAfter32[] = {
        {1024, 768, 60, 32, 0xff0000, 0xff00, 0xff, 0},
        {800, 600, 60, 32, 0xff0000, 0xff00, 0xff, 0xff000000u},
        {800, 600, 60, 16, 0x7c00, 0x3e0, 0x1f, 0},
        {800, 600, 75, 16, 0xf800, 0x7e0, 0x1f, 0},
    };
    static const FakeMode kFirst5551[] = {
        {1024, 768, 60, 32, 0xff0000, 0xff00, 0xff, 0},
        {640, 480, 60, 16, 0xf800, 0x7e0, 0x1f, 0},
        {800, 600, 60, 16, 0xf800, 0x7c0, 0x3e, 0x1},
    };
    static const struct {
        const char *what;
        const FakeMode *modes;
        unsigned count;
        unsigned format;
    } kCases[] = {
        {"4444 before 565", kFirst4444, 3, 2},
        {"800x600x32 does not count, 555 before 565", kAfter32, 4, 5},
        {"5551 after other resolutions", kFirst5551, 3, 4},
    };
    for (unsigned c = 0; c < sizeof kCases / sizeof kCases[0]; c++) {
        FakeDD dd;
        fake_dd_init(&dd);
        dd.modes = kCases[c].modes;
        dd.mode_count = kCases[c].count;
        void *screen = make_screen(api, &dd, "Probe Format Device", 1, 0);
        log_normalize(&dd);
        CHECK(screen != NULL, "%s: probe format (%s): the screen is built", e->label, kCases[c].what);
        if (!screen) {
            continue;
        }
        const FormatRow *row = &kFormatTable[kCases[c].format];
        char fields[300] = "";
        append_field(fields, sizeof fields, 0x0, 0x7c);
        append_field(fields, sizeof fields, 0x4, 0x1007);
        append_field(fields, sizeof fields, 0x8, 2);
        append_field(fields, sizeof fields, 0xc, 2);
        append_field(fields, sizeof fields, 0x48, 0x20);
        append_field(fields, sizeof fields, 0x4c, row->flags);
        append_field(fields, sizeof fields, 0x54, row->bits);
        append_field(fields, sizeof fields, 0x58, row->r);
        append_field(fields, sizeof fields, 0x5c, row->g);
        append_field(fields, sizeof fields, 0x60, row->b);
        append_field(fields, sizeof fields, 0x64, row->a);
        append_field(fields, sizeof fields, 0x68, 0x10006040);
        char expected[400];
        snprintf(expected, sizeof expected, "dd.CreateSurface(%s) -> s1\n", fields);
        CHECK(strstr(dd.log, expected) != NULL, "%s: probe format (%s): the 2x2 probe surface has the DDPIXELFORMAT of table entry %u (expected \"%s\" in \"%s\")", e->label,
              kCases[c].what, kCases[c].format, expected, dd.log);
        CHECK(SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitDeviceSupport) == 1 && SCREEN_BOOL(screen, EEWEB_SLOT_GEScreen_Get32BitTextureSupport) == 1,
              "%s: probe format (%s): the probe still finds 32-bit support", e->label, kCases[c].what);
        destroy_screen(screen);
    }
}

static void check_pixel_formats(const struct Expect *e, const ScreenApi *api)
{
    check_mode_format_indices(e, api);
    check_create_surface_formats(e, api);
    check_pack_formats(e, api);
    check_filter_formats(e, api);
    check_texture_format_enumeration(e, api);
    check_probe_surface_format(e, api);
}
