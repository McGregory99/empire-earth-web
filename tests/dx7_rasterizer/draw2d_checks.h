/*
 * The 2D primitive draws of DX7Rasterizer (task P2.6.7b): Draw(GEBitmap *, screen) (slot 21), Draw(GELine *, count, screen) (20),
 * Draw(GEPoint *, count, screen) (18) and Draw(GERhombus *, screen) (17).  Included by screen_checks.h after array_checks.h and run from
 * the screen probe.
 *
 * Same method as lifecycle_checks.h, on two windowed screens over the fake DirectDraw / Direct3D: every step logs the calls the fakes
 * saw (the projection set-up of Set2DMode, the texture bind, the vertex buffer's Lock and Unlock, DrawPrimitiveVB or
 * DrawIndexedPrimitiveVB with every argument), the return value, the engine's last native error and the words the step changed in the
 * engine's per-screen state and the screen record (the cursor above all), and then the vertices the draw wrote into the ring (the
 * fake buffer is filled with 0xa5 before) and the vertex arrays the draw read (a texture whose scale is not 1 makes the draw rewrite
 * the texture coordinates in them).  Both variants must produce the literals of draw2d_expected.inc, recorded from the original DLL
 * (EEWEB_LIFE_RECORD=1 prints them instead of checking); the steps where our build differs are in draw2d_expected_ours.inc.
 *
 * The bitmaps, lines, points and rhombus are plain engine objects built here from the layouts of ge_layouts.h; their vertex arrays
 * are made by the variant under test (CreateVertexArray, P2.6.7a) and filled with distinct finite floats.
 */

typedef long(THISCALL *D2Bitmap)(void *self, void *bitmap, unsigned long screen);
typedef long(THISCALL *D2Many)(void *self, void *objects, unsigned long count, unsigned long screen);

#define D2_BUSY 0x8876021cUL /* DDERR_WASSTILLDRAWING: the draw is retried */
#define D2_OTHER 0x80004005UL

static struct {
    unsigned char *vb[2];
} g_d2;

static void d2_fill(void *array, unsigned vertices, unsigned stride_words, float seed)
{
    float *data = (float *)(uintptr_t)((uint32_t *)array)[1];
    for (unsigned i = 0; i < vertices * stride_words; i++) {
        data[i] = seed + (float)i * 0.37f;
    }
}

static void *d2_array(unsigned vertices, float seed)
{
    void *array = arr_new_vertices(1, vertices);
    d2_fill(array, vertices, 9, seed);
    return array;
}

static GETextureLayout *d2_texture(int screen, float scale, float u, float v)
{
    TexSpec spec = {0, 0, 0, 0, 0, 0, 0.0f};
    GETextureLayout *t = tex_make(screen, &spec);
    t->frameUVSize = scale;
    t->frameTopLeft.u = u;
    t->frameTopLeft.v = v;
    return t;
}

static GETextureLayout **d2_texture_table(GETextureLayout *first, GETextureLayout *second)
{
    GETextureLayout **table = (GETextureLayout **)calloc(2, sizeof *table);
    table[0] = first;
    table[1] = second;
    return table;
}

static GEBitmapLayout *d2_bitmap(GETextureLayout *first, GETextureLayout *second, float seed)
{
    GEBitmapLayout *bitmap = (GEBitmapLayout *)calloc(1, sizeof *bitmap);
    bitmap->vertexArray = (struct GEVertexArrayLayout *)d2_array(4, seed);
    bitmap->textures = d2_texture_table(first, second);
    bitmap->screenCount = 2;
    return bitmap;
}

/* Lines and points: `count` objects of 0xc bytes, each with its own vertex array of `vertices` vertices and the same texture table. */
static GELineLayout *d2_lines(unsigned count, unsigned vertices, GETextureLayout *first, GETextureLayout *second, float seed)
{
    GELineLayout *lines = (GELineLayout *)calloc(count ? count : 1, sizeof *lines);
    GETextureLayout **table = d2_texture_table(first, second);
    for (unsigned i = 0; i < count; i++) {
        lines[i].vertexArray = (struct GEVertexArrayLayout *)d2_array(vertices, seed + 10.0f * (float)i);
        lines[i].textures = table;
        lines[i].screenCount = 2;
    }
    return lines;
}

static GERhombusLayout *d2_rhombus(float seed)
{
    GERhombusLayout *rhombus = (GERhombusLayout *)calloc(1, sizeof *rhombus);
    rhombus->vertexArray = (struct GEVertexArrayLayout *)d2_array(4, seed);
    return rhombus;
}

static void d2_dump_array(const char *tag, void *array, unsigned vertices)
{
    char text[4096] = "";
    arr_words(text, sizeof text, (const void *)(uintptr_t)((uint32_t *)array)[1], vertices * 36);
    life_extra(" %s=%s", tag, text);
}

/* What the draw wrote: the `vertices` vertices of the ring that end at the record's cursor. */
static void d2_dump_ring(int screen, unsigned vertices)
{
    unsigned long cursor = life_record(screen)->vertexCursor;
    char text[4096] = "";
    if (cursor >= vertices) {
        arr_words(text, sizeof text, g_d2.vb[screen] + (cursor - vertices) * 0x24, vertices * 0x24);
    }
    life_extra(" ring=%s", text);
}

static void d2_prepare(int screen, unsigned long cursor)
{
    g_life.cur = screen;
    memset(g_d2.vb[screen], 0xa5, 0x98a * 0x24);
    life_record(screen)->vertexCursor = cursor;
}

static void d2_script(int method, unsigned long code, int times)
{
    g_life.dd->dev_script[method].code = (long)code;
    g_life.dd->dev_script[method].times = times;
}

#define D2_BITMAP(name, screen, cursor, bitmap, ...)                                                                           \
    do {                                                                                                                       \
        d2_prepare(screen, cursor);                                                                                            \
        LS(name, LIFE_DIFF, {                                                                                                  \
            __VA_ARGS__;                                                                                                       \
            ret = ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, bitmap, (unsigned long)(screen));                                  \
            d2_dump_ring(screen, 4);                                                                                           \
            d2_dump_array("vertices", (bitmap)->vertexArray, 4);                                                               \
        });                                                                                                                    \
    } while (0)

#define D2_MANY(name, slot_name, screen, cursor, objects, count, vertices, vertices_per_object, ...)                           \
    do {                                                                                                                       \
        d2_prepare(screen, cursor);                                                                                            \
        LS(name, LIFE_DIFF, {                                                                                                  \
            __VA_ARGS__;                                                                                                       \
            ret = ((D2Many)LIFE_SLOT(slot_name))(g_life.obj, objects, (unsigned long)(count), (unsigned long)(screen));        \
            d2_dump_ring(screen, vertices);                                                                                    \
            for (int i_ = 0; i_ < (int)(count) && i_ < 3; i_++) {                                                              \
                d2_dump_array("v", (objects)[i_].vertexArray, vertices_per_object);                                            \
            }                                                                                                                  \
        });                                                                                                                    \
    } while (0)

static void d2_bitmaps(void)
{
    GETextureLayout *plain0 = d2_texture(0, 1.0f, 0.25f, 0.5f);
    GETextureLayout *scaled0 = d2_texture(0, 0.5f, 0.25f, 0.125f);
    GETextureLayout *nan0 = d2_texture(0, __builtin_nanf(""), 0.25f, 0.125f);
    GETextureLayout *plain1 = d2_texture(1, 1.0f, 0.0f, 0.0f);
    GETextureLayout *scaled1 = d2_texture(1, 2.0f, -0.5f, 3.0f);
    GEBitmapLayout *plain = d2_bitmap(plain0, plain1, 1.0f);
    GEBitmapLayout *scaled = d2_bitmap(scaled0, scaled1, 2.0f);
    GEBitmapLayout *nan = d2_bitmap(nan0, NULL, 3.0f);
    GEBitmapLayout *bare = d2_bitmap(NULL, NULL, 4.0f);

    D2_BITMAP("draw2d: Draw(GEBitmap) with a texture of scale 1", 0, 0, plain);
    D2_BITMAP("draw2d: Draw(GEBitmap) again, the texture is bound already", 0, 4, plain);
    D2_BITMAP("draw2d: Draw(GEBitmap) with another texture rescales the coordinates", 0, 8, scaled);
    D2_BITMAP("draw2d: Draw(GEBitmap) rescales again from the rewritten coordinates", 0, 12, scaled);
    D2_BITMAP("draw2d: Draw(GEBitmap) with a texture whose scale is not a number", 0, 16, nan);
    D2_BITMAP("draw2d: Draw(GEBitmap) without a texture for the screen answers 9", 0, 20, bare);
    D2_BITMAP("draw2d: Draw(GEBitmap) on screen 1", 1, 3, plain);
    D2_BITMAP("draw2d: Draw(GEBitmap) on screen 1 with a scaled texture", 1, 7, scaled);
    D2_BITMAP("draw2d: Draw(GEBitmap) without a texture on screen 1", 1, 11, nan);
    D2_BITMAP("draw2d: Draw(GEBitmap) with 2 vertices left starts the ring again", 0, 0x988, plain);
    D2_BITMAP("draw2d: Draw(GEBitmap) with exactly 4 vertices left", 0, 0x986, plain);
    D2_BITMAP("draw2d: Draw(GEBitmap) while DrawIndexedPrimitiveVB is busy twice", 0, 30, plain, d2_script(FD_DRAW_INDEXED, D2_BUSY, 2));
    D2_BITMAP("draw2d: Draw(GEBitmap) when DrawIndexedPrimitiveVB fails", 0, 34, plain, d2_script(FD_DRAW_INDEXED, D2_OTHER, 1));
    D2_BITMAP("draw2d: Draw(GEBitmap) when the vertex buffer unlock fails", 0, 38, plain,
              g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
              g_life.dd->vb_script[FV_UNLOCK].times = 1);
    D2_BITMAP("draw2d: Draw(GEBitmap) while the unlock is busy and the draw is busy", 0, 42, plain,
              g_life.dd->vb_script[FV_UNLOCK].code = (long)0x887601AEUL;
              g_life.dd->vb_script[FV_UNLOCK].times = 1;
              d2_script(FD_DRAW_INDEXED, D2_BUSY, 1));
}

static void d2_lines_points(void)
{
    GETextureLayout *plain0 = d2_texture(0, 1.0f, 0.25f, 0.5f);
    GETextureLayout *scaled0 = d2_texture(0, 2.0f, 0.5f, -1.0f);
    GETextureLayout *scaled1 = d2_texture(1, 0.25f, 1.5f, 2.5f);
    GELineLayout *bare = d2_lines(1, 2, NULL, NULL, 1.0f);
    GELineLayout *three = d2_lines(3, 2, plain0, NULL, 2.0f);
    GELineLayout *scaled = d2_lines(2, 2, scaled0, scaled1, 3.0f);
    GELineLayout *on1 = d2_lines(2, 2, NULL, scaled1, 4.0f);
    GELineLayout *none = d2_lines(1, 2, plain0, NULL, 5.0f); /* the draw reads the texture table of the first object even when asked for none */

    D2_MANY("draw2d: Draw(GELine) one line without a texture", Draw_8, 0, 0, bare, 1, 2, 2);
    D2_MANY("draw2d: Draw(GELine) three lines with a texture of scale 1", Draw_8, 0, 2, three, 3, 6, 2);
    D2_MANY("draw2d: Draw(GELine) with a scaled texture rewrites the first vertex of every line", Draw_8, 0, 8, scaled, 2, 4, 2);
    D2_MANY("draw2d: Draw(GELine) rescales again from the rewritten coordinates", Draw_8, 0, 12, scaled, 2, 4, 2);
    D2_MANY("draw2d: Draw(GELine) on screen 1 takes the texture of the first line", Draw_8, 1, 5, on1, 2, 4, 2);
    D2_MANY("draw2d: Draw(GELine) with no line", Draw_8, 0, 16, none, 0, 0, 2);
    D2_MANY("draw2d: Draw(GELine) with 3 vertices left starts the ring again", Draw_8, 0, 0x987, three, 2, 4, 2);
    D2_MANY("draw2d: Draw(GELine) with exactly 4 vertices left", Draw_8, 0, 0x986, three, 2, 4, 2);
    D2_MANY("draw2d: Draw(GELine) while DrawPrimitiveVB is busy twice", Draw_8, 0, 20, three, 1, 2, 2, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    D2_MANY("draw2d: Draw(GELine) when DrawPrimitiveVB fails", Draw_8, 0, 24, three, 1, 2, 2, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    D2_MANY("draw2d: Draw(GELine) when the vertex buffer unlock fails", Draw_8, 0, 28, three, 1, 2, 2,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);

    GEPointLayout *bare_points = (GEPointLayout *)d2_lines(3, 1, NULL, NULL, 6.0f);
    GEPointLayout *points = (GEPointLayout *)d2_lines(3, 1, plain0, NULL, 7.0f);
    GEPointLayout *scaled_points = (GEPointLayout *)d2_lines(3, 1, scaled0, scaled1, 8.0f);
    GEPointLayout *points1 = (GEPointLayout *)d2_lines(2, 1, NULL, scaled1, 9.0f);
    GEPointLayout *no_points = (GEPointLayout *)d2_lines(1, 1, plain0, NULL, 10.0f);
    D2_MANY("draw2d: Draw(GEPoint) three points without a texture", Draw_6, 0, 0, bare_points, 3, 3, 1);
    D2_MANY("draw2d: Draw(GEPoint) three points with a texture of scale 1", Draw_6, 0, 3, points, 3, 3, 1);
    D2_MANY("draw2d: Draw(GEPoint) with a scaled texture rewrites every point", Draw_6, 0, 6, scaled_points, 3, 3, 1);
    D2_MANY("draw2d: Draw(GEPoint) on screen 1", Draw_6, 1, 2, points1, 2, 2, 1);
    D2_MANY("draw2d: Draw(GEPoint) with no point", Draw_6, 0, 9, no_points, 0, 0, 1);
    D2_MANY("draw2d: Draw(GEPoint) with 2 vertices left starts the ring again", Draw_6, 0, 0x988, points, 3, 3, 1);
    D2_MANY("draw2d: Draw(GEPoint) while DrawPrimitiveVB is busy once", Draw_6, 0, 12, points, 2, 2, 1, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 1));
    D2_MANY("draw2d: Draw(GEPoint) when the vertex buffer unlock fails", Draw_6, 0, 14, points, 2, 2, 1,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);
}

/* The rewrite of the texture coordinates in the original's x87 code (the product and the sum stay in extended precision until the
 * store): 192 scales and offsets that are not powers of two, drawn as bitmaps, lines and points, as a hash of the arrays afterwards. */
static void d2_sweep(void)
{
    LS("draw2d: the coordinate rewrite of bitmaps, lines and points, 192 scales", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        GETextureLayout *texture = d2_texture(0, 1.0f, 0.0f, 0.0f);
        GEBitmapLayout *bitmap = d2_bitmap(texture, NULL, 1.0f);
        GELineLayout *lines = d2_lines(2, 2, texture, NULL, 1.0f);
        GEPointLayout *points = (GEPointLayout *)d2_lines(2, 1, texture, NULL, 1.0f);
        for (int k = 0; k < 64; k++) {
            texture->frameUVSize = 0.07f + (float)k * 0.0311f;
            texture->frameTopLeft.u = 0.3f * (float)k - 7.7f;
            texture->frameTopLeft.v = 1.0f / (3.0f + (float)k);
            for (int which = 0; which < 3; which++) {
                void *arrays[3] = {bitmap->vertexArray, lines[0].vertexArray, points[0].vertexArray};
                unsigned vertices[3] = {4, 2, 1};
                d2_fill(arrays[which], vertices[which], 9, 0.11f * (float)(k + 1));
                life_record(0)->vertexCursor = 0;
                if (which == 0) {
                    ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, bitmap, 0);
                } else if (which == 1) {
                    ((D2Many)LIFE_SLOT(Draw_8))(g_life.obj, lines, 1, 0);
                } else {
                    ((D2Many)LIFE_SLOT(Draw_6))(g_life.obj, points, 1, 0);
                }
                const uint32_t *words = (const uint32_t *)(uintptr_t)((uint32_t *)arrays[which])[1];
                for (unsigned i = 0; i < vertices[which] * 9; i++) {
                    hash = (hash ^ words[i]) * 16777619u;
                }
                const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
                for (unsigned i = 0; i < vertices[which] * 9; i++) {
                    hash = (hash ^ ring[i]) * 16777619u;
                }
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

static void d2_rhombi(void)
{
    GERhombusLayout *rhombus = d2_rhombus(5.0f);
    typedef long(THISCALL * D2Rhombus)(void *self, void *rhombus, unsigned long screen);
#define D2_RHOMBUS(name, screen, cursor, ...)                                                                                  \
    do {                                                                                                                       \
        d2_prepare(screen, cursor);                                                                                            \
        LS(name, LIFE_DIFF, {                                                                                                  \
            __VA_ARGS__;                                                                                                       \
            ret = ((D2Rhombus)LIFE_SLOT(Draw_5))(g_life.obj, rhombus, (unsigned long)(screen));                                \
            d2_dump_ring(screen, 4);                                                                                           \
        });                                                                                                                    \
    } while (0)
    D2_RHOMBUS("draw2d: Draw(GERhombus) unbinds the texture", 0, 0);
    D2_RHOMBUS("draw2d: Draw(GERhombus) again", 0, 4);
    D2_RHOMBUS("draw2d: Draw(GERhombus) on screen 1", 1, 9);
    D2_RHOMBUS("draw2d: Draw(GERhombus) with 2 vertices left starts the ring again", 0, 0x988);
    D2_RHOMBUS("draw2d: Draw(GERhombus) with 3 vertices left starts the ring again", 0, 0x987);
    D2_RHOMBUS("draw2d: Draw(GERhombus) with exactly 4 vertices left", 0, 0x986);
    D2_RHOMBUS("draw2d: Draw(GERhombus) while DrawIndexedPrimitiveVB is busy twice", 0, 20, d2_script(FD_DRAW_INDEXED, D2_BUSY, 2));
    D2_RHOMBUS("draw2d: Draw(GERhombus) when the vertex buffer unlock fails", 0, 24, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
               g_life.dd->vb_script[FV_UNLOCK].times = 1);
#undef D2_RHOMBUS
}

/* Our build only: objects that cannot be drawn are refused without a call (the original dereferences them). */
static void d2_ours_only(void)
{
    log_clear(g_life.dd);
    GEBitmapLayout *no_array = (GEBitmapLayout *)calloc(1, sizeof *no_array);
    GERhombusLayout *no_vertices = (GERhombusLayout *)calloc(1, sizeof *no_vertices);
    long bitmap_null = ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, NULL, 0);
    long bitmap_bare = ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, no_array, 0);
    long bitmap_screen = ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, no_array, 9);
    long rhombus_bare = ((D2Bitmap)LIFE_SLOT(Draw_5))(g_life.obj, no_vertices, 0);
    long lines_null = ((D2Many)LIFE_SLOT(Draw_8))(g_life.obj, NULL, 2, 0);
    long points_null = ((D2Many)LIFE_SLOT(Draw_6))(g_life.obj, NULL, 2, 0);
    long lines_screen = ((D2Many)LIFE_SLOT(Draw_8))(g_life.obj, d2_lines(1, 2, NULL, NULL, 1.0f), 1, 9);
    CHECK(bitmap_null != 0 && bitmap_bare != 0 && bitmap_screen != 0 && rhombus_bare != 0 && lines_null != 0 && points_null != 0 && lines_screen != 0 &&
              strstr(g_life.dd->log, "DIPVB") == NULL && strstr(g_life.dd->log, "DPVB") == NULL,
          "%s: draw2d: an object without vertices or arrays and a screen that does not exist are refused without a draw (%ld %ld %ld %ld %ld %ld %ld, log \"%s\")",
          g_life.e->label, bitmap_null, bitmap_bare, bitmap_screen, rhombus_bare, lines_null, points_null, lines_screen, g_life.dd->log);
}

/* A draw whose device answers "still drawing" for ever gives up after a bound (the original waits for ever): the 30000 busy answers
 * scripted here are more than the 10000 attempts our build makes, and the draw ends with the cursor moved on. */
static void d2_ours_busy_bound(void)
{
    GEBitmapLayout *bitmap = d2_bitmap(d2_texture(0, 1.0f, 0.0f, 0.0f), d2_texture(1, 1.0f, 0.0f, 0.0f), 1.0f);
    d2_prepare(0, 8);
    d2_script(FD_DRAW_INDEXED, D2_BUSY, 30000);
    const unsigned long saved_error = engine_native_error();
    es_record_native_error(0x11111111UL); /* a marker the draw must replace */
    long ret = ((D2Bitmap)LIFE_SLOT(Draw_9))(g_life.obj, bitmap, 0);
    int left = g_life.dd->dev_script[FD_DRAW_INDEXED].times;
    g_life.dd->dev_script[FD_DRAW_INDEXED].times = 0;
    CHECK(ret == 0 && 30000 - left <= 10001 && life_record(0)->vertexCursor == 12,
          "%s: draw2d: a draw whose device is busy for ever gives up after at most 10000 retries (ret %ld, attempts %d, cursor %lu)", g_life.e->label, ret,
          30000 - left, (unsigned long)life_record(0)->vertexCursor);
    /* ... and says so: the engine's error log holds the busy answer (a draw that went through after a few busy answers leaves it alone) */
    CHECK(engine_native_error() == (unsigned long)D2_BUSY, "%s: draw2d: giving up on a busy device is recorded as a native error (got 0x%lx)", g_life.e->label,
          engine_native_error());
    es_record_native_error(saved_error); /* the literals of the steps that follow carry the engine's last error */
}


static void check_draw2d(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "the 2D draws on two windowed screens")) {
        return;
    }
    memset(&g_arr, 0, sizeof g_arr);
    memset(&g_tex, 0, sizeof g_tex);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    g_d2.vb[0] = ((FakeVB *)g_life.dd->vbs[0])->memory;
    g_d2.vb[1] = ((FakeVB *)g_life.dd->vbs[1])->memory;
    d2_bitmaps();
    d2_lines_points();
    d2_rhombi();
    d2_sweep();
    if (e->ours) {
        d2_ours_only();
        d2_ours_busy_bound();
    }
    life_end();
}
