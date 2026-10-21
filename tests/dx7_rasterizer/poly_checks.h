/*
 * Draw(GEPolygonArray &, polygons, GEVertexArray &, vertices, GETexture *, screen, bool) of DX7Rasterizer (task P2.6.7c), slot 13.
 * Included by screen_checks.h after draw2d_checks.h and run from the screen probe.
 *
 * Same method as draw2d_checks.h (two windowed screens over the fake DirectDraw / Direct3D, every step shows the calls, the return
 * value, the words changed and the vertices written into the ring; literals of poly_expected.inc recorded from the original DLL,
 * the steps where our build differs in poly_expected_ours.inc).  The draw sets the 3D projection (Set3DMode), the identity world
 * matrix and the normalize-normals render state, binds the texture, copies the vertices (a colour array as it is, a standard array
 * with the colour 0xffffffff added) and draws the polygons' indices as a triangle list; the engine's scene polygon count grows.
 */

typedef long(THISCALL *PolyDrawFn)(void *self, void *polygons, unsigned long polygon_count, void *vertices, unsigned long vertex_count, void *texture,
                                 unsigned long screen, unsigned long flag);

static struct {
    void *colored, *plain;       /* vertex arrays of kind 1 and 0 */
    void *polygons;
    GETextureLayout *texture[2];
} g_poly;

static void poly_prepare(void)
{
    g_poly.colored = arr_new_vertices(1, 6);
    d2_fill(g_poly.colored, 6, 9, 1.0f);
    g_poly.plain = arr_new_vertices(0, 6);
    d2_fill(g_poly.plain, 6, 8, 2.0f);
    g_poly.polygons = arr_new_poly(5);
    for (unsigned long i = 0; i < 5; i++) {
        ((ArrSet4)PA_SLOT(g_poly.polygons, SetVertexIndices))(g_poly.polygons, i, i, (i + 1) % 6, (i + 3) % 6);
    }
    g_poly.texture[0] = d2_texture(0, 1.0f, 0.0f, 0.0f);
    g_poly.texture[1] = d2_texture(1, 1.0f, 0.0f, 0.0f);
}

#define POLY_STEP(name, screen, cursor, vertices, polygon_count, vertex_count, texture, ...)                                               \
    do {                                                                                                                                   \
        d2_prepare(screen, cursor);                                                                                                        \
        LS(name, LIFE_DIFF, {                                                                                                              \
            __VA_ARGS__;                                                                                                                   \
            ret = ((PolyDrawFn)LIFE_SLOT(Draw_1))(g_life.obj, g_poly.polygons, polygon_count, vertices, vertex_count, texture, (unsigned long)(screen), 1); \
            d2_dump_ring(screen, vertex_count);                                                                                            \
        });                                                                                                                                \
    } while (0)

static void poly_draws(void)
{
    POLY_STEP("poly: Draw(GEPolygonArray) of a colour vertex array", 0, 0, g_poly.colored, 3, 6, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) again, the texture is bound already", 0, 6, g_poly.colored, 5, 4, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) of a standard vertex array adds the colour", 0, 10, g_poly.plain, 2, 5, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) without a texture", 0, 15, g_poly.colored, 1, 3, NULL);
    POLY_STEP("poly: Draw(GEPolygonArray) on screen 1", 1, 4, g_poly.plain, 4, 6, g_poly.texture[1]);
    POLY_STEP("poly: Draw(GEPolygonArray) on screen 1 of a colour array", 1, 10, g_poly.colored, 4, 6, g_poly.texture[1]);
    POLY_STEP("poly: Draw(GEPolygonArray) with fewer vertices left starts the ring again", 0, 0x986, g_poly.colored, 2, 5, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) with exactly the vertices left", 0, 0x985, g_poly.plain, 2, 5, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) with no polygon", 0, 20, g_poly.colored, 0, 3, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) with no vertex", 0, 24, g_poly.colored, 2, 0, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) while DrawIndexedPrimitiveVB is busy twice", 0, 28, g_poly.colored, 2, 4, g_poly.texture[0],
              d2_script(FD_DRAW_INDEXED, D2_BUSY, 2));
    POLY_STEP("poly: Draw(GEPolygonArray) when DrawIndexedPrimitiveVB fails", 0, 32, g_poly.colored, 2, 4, g_poly.texture[0],
              d2_script(FD_DRAW_INDEXED, D2_OTHER, 1));
    POLY_STEP("poly: Draw(GEPolygonArray) when the vertex buffer unlock fails", 0, 36, g_poly.colored, 2, 4, g_poly.texture[0],
              g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
              g_life.dd->vb_script[FV_UNLOCK].times = 1);
    /* the 3D set-up follows the 2D draws: a bitmap leaves the 2D projection current */
    {
        GEBitmapLayout *bitmap = d2_bitmap(g_poly.texture[0], g_poly.texture[1], 1.0f);
        D2_BITMAP("poly: a bitmap between two polygon draws", 0, 40, bitmap);
    }
    POLY_STEP("poly: Draw(GEPolygonArray) after a 2D draw sets the 3D projection again", 0, 44, g_poly.colored, 2, 4, g_poly.texture[0]);
    POLY_STEP("poly: Draw(GEPolygonArray) with the 3D projection current", 0, 48, g_poly.colored, 2, 4, g_poly.texture[0]);
}

/* Our build only: arrays that cannot be read are refused without a call (the original reads past them). */
static void poly_ours_only(void)
{
    log_clear(g_life.dd);
    void *few = arr_new_vertices(1, 2);
    void *few_polygons = arr_new_poly(1);
    long vertices = ((PolyDrawFn)LIFE_SLOT(Draw_1))(g_life.obj, g_poly.polygons, 2, few, 3, g_poly.texture[0], 0, 1);
    long polygons = ((PolyDrawFn)LIFE_SLOT(Draw_1))(g_life.obj, few_polygons, 2, g_poly.colored, 3, g_poly.texture[0], 0, 1);
    long screen = ((PolyDrawFn)LIFE_SLOT(Draw_1))(g_life.obj, g_poly.polygons, 2, g_poly.colored, 3, g_poly.texture[0], 9, 1);
    long too_many = ((PolyDrawFn)LIFE_SLOT(Draw_1))(g_life.obj, g_poly.polygons, 2, g_poly.colored, 0x999, g_poly.texture[0], 0, 1);
    CHECK(vertices != 0 && polygons != 0 && screen != 0 && too_many != 0 && strstr(g_life.dd->log, "DIPVB") == NULL,
          "%s: poly: arrays with too few elements, a screen that does not exist and more vertices than the ring holds are refused without a draw (%ld %ld %ld %ld, log \"%s\")",
          g_life.e->label, vertices, polygons, screen, too_many, g_life.dd->log);
}

static void check_poly(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "the polygon draws on two windowed screens")) {
        return;
    }
    memset(&g_arr, 0, sizeof g_arr);
    memset(&g_tex, 0, sizeof g_tex);
    memset(&g_poly, 0, sizeof g_poly);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    g_d2.vb[0] = ((FakeVB *)g_life.dd->vbs[0])->memory;
    g_d2.vb[1] = ((FakeVB *)g_life.dd->vbs[1])->memory;
    poly_prepare();
    poly_draws();
    if (e->ours) {
        poly_ours_only();
    }
    life_end();
}
