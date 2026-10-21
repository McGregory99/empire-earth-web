/*
 * Draw3DLine(std::vector<std::pair<GE3DPoint, float>> &, GETexture *, float, screen, bool) of DX7Rasterizer (task P2.6.7d), slot 22.
 * Included by screen_checks.h after poly_checks.h and run from the screen probe.
 *
 * Same method as draw2d_checks.h (two windowed screens over the fake DirectDraw / Direct3D, every step shows the calls, the return
 * value, the words changed and the vertices written into the ring; literals of line3d_expected.inc recorded from the original DLL,
 * the steps where our build differs in line3d_expected_ours.inc).  The engine's screen state carries distinct camera and projection
 * matrices, so the transforms the draw sends show which of them it used.  The vector is built here the way MSVC 6 lays one out (an
 * allocator word, then first, last and end) over 16-byte elements: a point (3 floats) and the length of the segment that starts there.
 */

typedef long(THISCALL *Line3DFn)(void *self, void *points, void *texture, float start, unsigned long screen, unsigned long orthographic);

static struct {
    StdVectorLayout vectors[8];
    float *elements[8];
} g_l3;

/* A polyline of `count` points; point i is (i, 2i+1, -i) scaled by `spread`, the segment after it `length` long. */
static StdVectorLayout *l3_vector(int slot, unsigned count, float spread, float length)
{
    free(g_l3.elements[slot]);
    g_l3.elements[slot] = (float *)calloc(count ? count : 1, 16);
    for (unsigned i = 0; i < count; i++) {
        float *e = g_l3.elements[slot] + 4 * i;
        e[0] = spread * (float)i;
        e[1] = spread * (float)(2 * i + 1);
        e[2] = -spread * (float)i;
        e[3] = length * (1.0f + 0.37f * (float)(i % 5));
    }
    StdVectorLayout *v = &g_l3.vectors[slot];
    memset(v, 0, sizeof *v);
    v->first = g_l3.elements[slot];
    v->last = g_l3.elements[slot] + 4 * count;
    v->end = v->last;
    return v;
}

static void l3_state(void)
{
    for (int screen = 0; screen < 2; screen++) {
        GERScreenStateLayout *state = life_state(screen);
        for (int i = 0; i < 16; i++) {
            state->viewport.cameraInverse.m[i] = (i % 5 == 0 ? 1.5f : 0.0f) + 0.125f * (float)i + (float)screen;
            state->viewport.orthographicProjection.m[i] = 100.0f + (float)i;
            state->viewport.perspectiveProjection.m[i] = 200.0f + (float)i;
        }
    }
}

#define L3_STEP(name, screen, cursor, vector, texture, start, ortho, vertices, ...)                                                          \
    do {                                                                                                                                      \
        d2_prepare(screen, cursor);                                                                                                           \
        LS(name, LIFE_DIFF, {                                                                                                                 \
            __VA_ARGS__;                                                                                                                      \
            ret = ((Line3DFn)LIFE_SLOT(Draw3DLine))(g_life.obj, vector, texture, start, (unsigned long)(screen), ortho);                      \
            d2_dump_ring(screen, vertices);                                                                                                   \
        });                                                                                                                                   \
    } while (0)

static void l3_draws(void)
{
    GETextureLayout *texture0 = d2_texture(0, 1.0f, 0.0f, 0.0f);
    GETextureLayout *texture1 = d2_texture(1, 1.0f, 0.0f, 0.0f);
    StdVectorLayout *three = l3_vector(0, 3, 1.0f, 0.4f);
    StdVectorLayout *four = l3_vector(1, 4, 0.5f, 0.9f);
    StdVectorLayout *one = l3_vector(2, 1, 1.0f, 0.3f);
    StdVectorLayout *two = l3_vector(3, 2, 2.0f, 0.5f);
    StdVectorLayout *flat = l3_vector(6, 3, 1.0f, 0.0f); /* segments of length 0: the start itself is wrapped */

    L3_STEP("line3d: Draw3DLine of three points without a texture", 0, 0, three, NULL, 0.25f, 0, 4);
    L3_STEP("line3d: Draw3DLine of four points with a texture", 0, 4, four, texture0, 0.0f, 0, 6);
    L3_STEP("line3d: Draw3DLine again, the texture is bound already", 0, 10, four, texture0, 0.5f, 0, 6);
    L3_STEP("line3d: Draw3DLine with the orthographic projection", 0, 16, three, texture0, 0.25f, 1, 4);
    L3_STEP("line3d: Draw3DLine on screen 1", 1, 3, four, texture1, 0.75f, 0, 6);
    L3_STEP("line3d: Draw3DLine of two points", 0, 22, two, texture0, 0.125f, 0, 2);
    L3_STEP("line3d: Draw3DLine of one point draws nothing", 0, 24, one, texture0, 0.125f, 0, 0);
    L3_STEP("line3d: Draw3DLine with a start below minus one", 0, 26, four, texture0, -1.5f, 0, 6);
    L3_STEP("line3d: Draw3DLine with a start between minus one and zero", 0, 32, four, texture0, -0.5f, 0, 6);
    L3_STEP("line3d: Draw3DLine with a start above one", 0, 38, four, texture0, 2.75f, 0, 6);
    L3_STEP("line3d: Draw3DLine with a start of exactly minus one", 0, 56, flat, texture0, -1.0f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of exactly minus two", 0, 60, flat, texture0, -2.0f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of exactly zero", 0, 64, flat, texture0, 0.0f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of exactly one", 0, 68, flat, texture0, 1.0f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of exactly two", 0, 72, flat, texture0, 2.0f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of 1.5", 0, 76, flat, texture0, 1.5f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of 2.5", 0, 80, flat, texture0, 2.5f, 0, 4);
    L3_STEP("line3d: Draw3DLine with a start of minus 2.5", 0, 84, flat, texture0, -2.5f, 0, 4);
    L3_STEP("line3d: Draw3DLine with exactly the vertices left", 0, 0x984, three, texture0, 0.25f, 0, 4);
    L3_STEP("line3d: Draw3DLine with fewer vertices left starts the ring again", 0, 0x986, four, texture0, 0.25f, 0, 6);
    L3_STEP("line3d: Draw3DLine while DrawPrimitiveVB is busy twice", 0, 44, three, texture0, 0.25f, 0, 4, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    L3_STEP("line3d: Draw3DLine when DrawPrimitiveVB fails", 0, 48, three, texture0, 0.25f, 0, 4, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    L3_STEP("line3d: Draw3DLine when the vertex buffer unlock fails", 0, 52, three, texture0, 0.25f, 0, 4,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);

    /* The longest line the ring holds, and one point more (the draw stops at the ring's size). */
    StdVectorLayout *full = l3_vector(4, 0x4c6, 0.01f, 0.02f);
    StdVectorLayout *over = l3_vector(5, 0x4c7, 0.01f, 0.02f);
    const struct {
        const char *name;
        StdVectorLayout *vector;
        unsigned long cursor;
    } kBig[] = {{"line3d: Draw3DLine of 1222 points fills the ring", full, 0}, {"line3d: Draw3DLine of 1223 points is cut to the ring", over, 7}};
    for (int i = 0; i < 2; i++) {
        d2_prepare(0, kBig[i].cursor);
        LS(kBig[i].name, LIFE_DIFF, {
            ret = ((Line3DFn)LIFE_SLOT(Draw3DLine))(g_life.obj, kBig[i].vector, texture0, 0.0f, 0, 0);
            /* the whole ring, as a hash: the last vertices show whether the line was cut at the right point */
            uint32_t hash = 2166136261u;
            const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
            for (unsigned word = 0; word < 0x98a * 9; word++) {
                hash = (hash ^ ring[word]) * 16777619u;
            }
            life_extra(" ring=%08x", (unsigned)hash);
        });
    }
}

/* The texture coordinate of every vertex the draw writes, over many starts and lengths: the wrap into 0..1 is x87 code. */
static void l3_sweep(void)
{
    LS("line3d: the texture coordinate over 160 starts and lengths", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        GETextureLayout *texture = d2_texture(0, 1.0f, 0.0f, 0.0f);
        for (int k = 0; k < 160; k++) {
            StdVectorLayout *line = l3_vector(7, 7, 1.0f, 0.173f * (float)(k % 16) + 0.01f);
            float start = -9.7f + 0.153f * (float)k;
            log_clear(g_life.dd);
            life_record(0)->vertexCursor = 0;
            memset(g_d2.vb[0], 0xa5, 12 * 0x24);
            ((Line3DFn)LIFE_SLOT(Draw3DLine))(g_life.obj, line, texture, start, 0, (unsigned long)(k & 1));
            const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
            for (int v = 0; v < 12; v++) {
                hash = (hash ^ ring[9 * v + 8]) * 16777619u;
                hash = (hash ^ ring[9 * v]) * 16777619u;
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

/* Our build only: a vector with no point and a screen that does not exist are refused without a draw (the original reads past an empty vector). */
static void l3_ours_only(void)
{
    log_clear(g_life.dd);
    StdVectorLayout *empty = l3_vector(7, 0, 1.0f, 1.0f);
    StdVectorLayout *three = l3_vector(0, 3, 1.0f, 0.4f);
    long none = ((Line3DFn)LIFE_SLOT(Draw3DLine))(g_life.obj, empty, NULL, 0.0f, 0, 0);
    long screen = ((Line3DFn)LIFE_SLOT(Draw3DLine))(g_life.obj, three, NULL, 0.0f, 9, 0);
    CHECK(none != 0 && screen != 0 && strstr(g_life.dd->log, "DPVB") == NULL,
          "%s: line3d: an empty polyline and a screen that does not exist are refused without a draw (%ld %ld, log \"%s\")", g_life.e->label,
          none, screen, g_life.dd->log);
}

static void check_line3d(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "the 3D lines on two windowed screens")) {
        return;
    }
    memset(&g_arr, 0, sizeof g_arr);
    memset(&g_tex, 0, sizeof g_tex);
    memset(&g_l3, 0, sizeof g_l3);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    g_d2.vb[0] = ((FakeVB *)g_life.dd->vbs[0])->memory;
    g_d2.vb[1] = ((FakeVB *)g_life.dd->vbs[1])->memory;
    l3_state();
    l3_draws();
    l3_sweep();
    if (e->ours) {
        l3_ours_only();
    }
    life_end();
}
