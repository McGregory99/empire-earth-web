/*
 * The text draws of DX7Rasterizer (task P2.6.7e): Draw(GEText *, GESurface *) (slot 16) and Draw(GEText *, GERectangle const &,
 * GESurface *) (slot 15).  Included by screen_checks.h after line3d_checks.h and run from the screen probe.
 *
 * Same method as draw2d_checks.h, with a real device context: the surface's GetDC (the fake DirectDraw's) hands out a memory DC with a
 * 32-bit DIB section, so the GDI the draw calls really runs.  A step shows the return value, the engine's last native error, the
 * GDI state of the DC afterwards (background mode, text colour, background colour and the font selected: all must be what the draw
 * found), the font's reference count and a hash of the DIB's pixels.  The pixels are what this machine's Wine draws with the
 * system fixed font: the literals of text_expected.inc (recorded from the original DLL) must be recorded again when the Wine
 * runtime changes.  The text is a GEText built here from the layouts of ge_layouts.h; its font is a plain object holding the stock
 * font.  Fonts with a bitmap font (GEFont 0x24) go to the engine's GEFontBitmap::Draw in the original, which is not exercised.
 */

#define TX_WIDTH 96
#define TX_HEIGHT 40

static struct {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ original_bitmap;
    uint32_t *pixels;
    HGDIOBJ initial_font;
    GEFontLayout font;
    GETextLayout text;
    char chars[64];
    void *surface[3];
    int surface_id[3];
} g_tx;

typedef long(THISCALL *TextDraw)(void *self, void *text, void *surface);
typedef long(THISCALL *TextRectDraw)(void *self, void *text, const void *rect, void *surface);

static void tx_make_surface(int i)
{
    GESurfaceDescriptionLayout desc = make_desc(1, 1, 0, 64, 64, 32, 1, 0);
    long status = 0;
    g_tx.surface[i] = new_surface(g_life.screen[0], NULL, g_life.dd, &desc, &status);
    g_tx.surface_id[i] = g_life.dd->surface_count;
}

static void tx_report(void)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < TX_WIDTH * TX_HEIGHT; i++) {
        hash = (hash ^ g_tx.pixels[i]) * 16777619u;
    }
    life_extra("bkmode=%d textcolor=%lx bkcolor=%lx font=%s refs=%u pixels=%08x", GetBkMode(g_tx.dc), (unsigned long)GetTextColor(g_tx.dc),
               (unsigned long)GetBkColor(g_tx.dc), GetCurrentObject(g_tx.dc, OBJ_FONT) == g_tx.initial_font ? "kept" : "changed", (unsigned)g_tx.font.refCount,
               (unsigned)hash);
}

/* The state every step starts from: a grey bitmap, the DC in a state the draw must give back, the font's count at 5. */
static void tx_reset(void)
{
    for (int i = 0; i < TX_WIDTH * TX_HEIGHT; i++) {
        g_tx.pixels[i] = 0x00808080u;
    }
    SetBkMode(g_tx.dc, TRANSPARENT);
    SetTextColor(g_tx.dc, 0x123456);
    SetBkColor(g_tx.dc, 0x654321);
    g_tx.font.refCount = 5;
}

static void tx_text(const char *chars, unsigned length, unsigned capacity, float x, float y, float fg, float bg, unsigned alignment, int transparent)
{
    snprintf(g_tx.chars, sizeof g_tx.chars, "%s", chars);
    g_tx.text.text.chars = (uint8_t *)g_tx.chars;
    g_tx.text.text.length = length;
    g_tx.text.text.capacity = capacity;
    g_tx.text.location.x = x;
    g_tx.text.location.y = y;
    g_tx.text.fgColor.r = fg;
    g_tx.text.fgColor.g = fg * 0.5f;
    g_tx.text.fgColor.b = 1.0f - fg;
    g_tx.text.bgColor.r = bg;
    g_tx.text.bgColor.g = 1.0f - bg;
    g_tx.text.bgColor.b = bg * 0.25f;
    g_tx.text.alignment = alignment;
    g_tx.text.isTransparent = (uint8_t)transparent;
}

#define TX_STEP(name, which, ...)                                                                                 \
    do {                                                                                                            \
        tx_reset();                                                                                                 \
        LS(name, LIFE_NODIFF, {                                                                                     \
            __VA_ARGS__;                                                                                            \
            ret = ((TextDraw)LIFE_SLOT(Draw_4))(g_life.obj, &g_tx.text, g_tx.surface[which]);                       \
            tx_report();                                                                                            \
        });                                                                                                         \
    } while (0)

#define TX_RECT_STEP(name, which, rect, ...)                                                                      \
    do {                                                                                                            \
        tx_reset();                                                                                                 \
        LS(name, LIFE_NODIFF, {                                                                                     \
            __VA_ARGS__;                                                                                            \
            ret = ((TextRectDraw)LIFE_SLOT(Draw_3))(g_life.obj, &g_tx.text, rect, g_tx.surface[which]);             \
            tx_report();                                                                                            \
        });                                                                                                         \
    } while (0)

static void tx_draws(void)
{
    /* the first draw on a surface locks its text DC (and leaves it locked) */
    TX_STEP("text: Draw(GEText) locks the text DC and draws opaque text", 0, tx_text("Empire Earth", 12, 12, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));
    TX_STEP("text: Draw(GEText) on a locked surface with transparent text", 0, tx_text("Empire Earth", 12, 12, 4.0f, 6.0f, 0.9f, 0.2f, 0, 1));
    TX_STEP("text: Draw(GEText) with fractional coordinates truncates them", 0, tx_text("Truncate", 8, 8, 10.9f, 12.7f, 0.9f, 0.2f, 0, 1));
    TX_STEP("text: Draw(GEText) with negative coordinates truncates towards zero", 0, tx_text("Negative", 8, 8, -1.9f, -0.5f, 0.9f, 0.2f, 0, 1));
    TX_STEP("text: Draw(GEText) centred", 0, tx_text("Centre", 6, 6, 48.0f, 4.0f, 0.9f, 0.2f, 1, 1));
    TX_STEP("text: Draw(GEText) right aligned", 0, tx_text("Right", 5, 5, 90.0f, 4.0f, 0.9f, 0.2f, 2, 1));
    TX_STEP("text: Draw(GEText) with another alignment is left aligned", 0, tx_text("Other", 5, 5, 20.0f, 4.0f, 0.9f, 0.2f, 3, 1));
    TX_STEP("text: Draw(GEText) measures with the capacity and draws the length", 0, tx_text("Capacity", 4, 8, 60.0f, 4.0f, 0.9f, 0.2f, 2, 1));
    TX_STEP("text: Draw(GEText) with a capacity above the length centred", 0, tx_text("Capacity", 3, 7, 60.0f, 4.0f, 0.9f, 0.2f, 1, 1));
    TX_STEP("text: Draw(GEText) with no character", 0, tx_text("", 0, 0, 4.0f, 6.0f, 0.9f, 0.2f, 1, 0));
    TX_STEP("text: Draw(GEText) with colour channels above 1 and below 0", 0, tx_text("Colours", 7, 7, 4.0f, 6.0f, 1.7f, -0.3f, 0, 0));
    TX_STEP("text: Draw(GEText) with fractional colour channels", 0, tx_text("Truncated", 9, 9, 4.0f, 6.0f, 0.4999f, 0.0079f, 0, 0));
    g_tx.font.style = 0x10;
    TX_STEP("text: Draw(GEText) with a shadow under dark text", 0, tx_text("Shadow", 6, 6, 8.0f, 8.0f, 0.02f, 0.9f, 0, 1));
    TX_STEP("text: Draw(GEText) with a shadow under bright text", 0, tx_text("Shadow", 6, 6, 8.0f, 8.0f, 0.9f, 0.9f, 0, 1));
    TX_STEP("text: Draw(GEText) with a shadow under opaque centred text", 0, tx_text("Shadow", 6, 6, 40.0f, 8.0f, 0.9f, 0.3f, 1, 0));
    g_tx.font.style = 0x20;
    TX_STEP("text: Draw(GEText) with style bits that are not the shadow", 0, tx_text("Plain", 5, 5, 8.0f, 8.0f, 0.9f, 0.3f, 0, 1));
    g_tx.font.style = 0;

    /* a surface whose text lock fails: the draw answers 0 and does nothing */
    LIFE_SCRIPT(g_tx.surface_id[1], FS_GETDC, 0x88760046L, 1);
    TX_STEP("text: Draw(GEText) when the text lock fails", 1, tx_text("Locked out", 10, 10, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));
    TX_STEP("text: Draw(GEText) on that surface once the lock works", 1, tx_text("Locked", 6, 6, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));

    static const GERectangleLayout kRect = {30.0f, 10.2f, 70.9f, 4.7f};   /* bottom, left, right, top */
    static const GERectangleLayout kNegative = {-1.5f, -10.5f, 20.5f, -4.5f};
    static const GERectangleLayout kCut = {14.6f, 12.6f, 47.4f, 9.6f}; /* sides that cut through the glyphs, none a whole number */
    TX_RECT_STEP("text: Draw(GEText, rectangle) clips to the rounded-down rectangle", 0, &kRect, tx_text("Clipped text that is long", 25, 25, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));
    TX_RECT_STEP("text: Draw(GEText, rectangle) with transparent text", 0, &kRect, tx_text("Clipped text that is long", 25, 25, 4.0f, 6.0f, 0.9f, 0.2f, 0, 1));
    TX_RECT_STEP("text: Draw(GEText, rectangle) with a negative rectangle", 0, &kNegative, tx_text("Negative rectangle", 18, 18, 0.0f, 0.0f, 0.9f, 0.2f, 0, 0));
    TX_RECT_STEP("text: Draw(GEText, rectangle) centred", 0, &kRect, tx_text("Centred", 7, 7, 40.0f, 10.0f, 0.9f, 0.2f, 1, 1));
    TX_RECT_STEP("text: Draw(GEText, rectangle) right aligned", 0, &kRect, tx_text("Right", 5, 5, 70.0f, 10.0f, 0.9f, 0.2f, 2, 1));
    TX_RECT_STEP("text: Draw(GEText, rectangle) cutting through the glyphs", 0, &kCut, tx_text("Cut through", 11, 11, 4.0f, 6.0f, 0.9f, 0.2f, 0, 1));
    TX_RECT_STEP("text: Draw(GEText, rectangle) cutting through opaque glyphs", 0, &kCut, tx_text("Cut through", 11, 11, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));
    g_tx.font.style = 0x10;
    TX_RECT_STEP("text: Draw(GEText, rectangle) with a shadow", 0, &kRect, tx_text("Shadow text", 11, 11, 12.0f, 10.0f, 0.02f, 0.9f, 0, 1));
    TX_RECT_STEP("text: Draw(GEText, rectangle) with a shadow under bright text", 0, &kRect, tx_text("Shadow text", 11, 11, 12.0f, 10.0f, 0.9f, 0.9f, 0, 0));
    g_tx.font.style = 0;

    /* a device context that is not one: every GDI call fails, the draw records the error and answers 1 */
    tx_make_surface(2);
    g_life.dd->text_dc = (void *)0x1234;
    TX_STEP("text: Draw(GEText) when the text output fails", 2, tx_text("Fails", 5, 5, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0));
    g_tx.font.style = 0x10;
    TX_STEP("text: Draw(GEText) when the shadow and the text output fail", 2, tx_text("Fails", 5, 5, 4.0f, 6.0f, 0.9f, 0.2f, 1, 0));
    TX_RECT_STEP("text: Draw(GEText, rectangle) when the text output fails", 2, &kRect, tx_text("Fails", 5, 5, 4.0f, 6.0f, 0.9f, 0.2f, 2, 0));
    g_tx.font.style = 0;
    g_life.dd->text_dc = g_tx.dc;
}

/* Our build only: what cannot be drawn is refused without a GDI call (the original dereferences it, or hands a bitmap font to the engine). */
static void tx_ours_only(void)
{
    tx_reset();
    tx_text("Refused", 7, 7, 4.0f, 6.0f, 0.9f, 0.2f, 0, 0);
    static const GERectangleLayout kRect = {30.0f, 10.2f, 70.9f, 4.7f};
    long null_surface = ((TextDraw)LIFE_SLOT(Draw_4))(g_life.obj, &g_tx.text, NULL);
    long null_text = ((TextDraw)LIFE_SLOT(Draw_4))(g_life.obj, NULL, g_tx.surface[0]);
    long null_rect_surface = ((TextRectDraw)LIFE_SLOT(Draw_3))(g_life.obj, &g_tx.text, &kRect, NULL);
    uint8_t *chars = g_tx.text.text.chars;
    g_tx.text.text.chars = NULL;
    long no_chars = ((TextDraw)LIFE_SLOT(Draw_4))(g_life.obj, &g_tx.text, g_tx.surface[0]);
    g_tx.text.text.chars = chars;
    g_tx.font.fontBitmap = (void *)0x1000;
    long bitmap_font = ((TextRectDraw)LIFE_SLOT(Draw_3))(g_life.obj, &g_tx.text, &kRect, g_tx.surface[0]);
    g_tx.font.fontBitmap = NULL;
    int untouched = 1;
    for (int i = 0; i < TX_WIDTH * TX_HEIGHT; i++) {
        untouched &= g_tx.pixels[i] == 0x00808080u;
    }
    CHECK(null_surface == EEWEB_NOT_SUPPORTED && null_rect_surface == EEWEB_NOT_SUPPORTED && null_text == EEWEB_LAYOUT_VIOLATION &&
              no_chars == EEWEB_LAYOUT_VIOLATION && bitmap_font == EEWEB_NOT_SUPPORTED && untouched && g_tx.font.refCount == 5 && GetBkMode(g_tx.dc) == TRANSPARENT,
          "%s: text: a missing surface or text, text without characters (layout canary) and a rectangle on a bitmap font are refused without a draw (%lx %lx %lx %lx %lx, font count %u)",
          g_life.e->label, null_surface, null_rect_surface, null_text, no_chars, bitmap_font, (unsigned)g_tx.font.refCount);
}

static void check_text(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 1, 0, "the text draws")) {
        return;
    }
    memset(&g_tx, 0, sizeof g_tx);
    g_tx.dc = CreateCompatibleDC(NULL);
    BITMAPINFO info;
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = TX_WIDTH;
    info.bmiHeader.biHeight = -TX_HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    g_tx.bitmap = CreateDIBSection(g_tx.dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    CHECK(g_tx.dc != NULL && g_tx.bitmap != NULL && bits != NULL, "%s: text: a memory device context with a DIB section", e->label);
    if (!g_tx.dc || !g_tx.bitmap || !bits) {
        life_end();
        return;
    }
    g_tx.pixels = (uint32_t *)bits;
    g_tx.original_bitmap = SelectObject(g_tx.dc, g_tx.bitmap);
    g_tx.initial_font = GetCurrentObject(g_tx.dc, OBJ_FONT);
    g_tx.font.refCount = 5;
    g_tx.font.osFont = GetStockObject(SYSTEM_FIXED_FONT);
    g_tx.font.vptr = vftable("GEFont");
    g_tx.text.font = &g_tx.font;
    g_life.dd->text_dc = g_tx.dc;
    tx_make_surface(0);
    tx_make_surface(1);
    tx_draws();
    if (e->ours) {
        tx_ours_only();
    }
    g_life.dd->text_dc = NULL;
    SelectObject(g_tx.dc, g_tx.original_bitmap);
    DeleteObject(g_tx.bitmap);
    DeleteDC(g_tx.dc);
    life_end();
}
