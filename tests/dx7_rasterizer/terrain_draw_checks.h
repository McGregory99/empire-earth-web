/*
 * The terrain draws of DX7Rasterizer (task P2.6.7i).  This file starts with DrawTerrainMaterial (slot 80, a protected virtual the
 * terrain draws call per material).  Included by screen_checks.h after model_checks.h and run from the screen probe; same method as
 * draw2d_checks.h (two windowed screens over the fake DirectDraw / Direct3D, every step shows the calls, the answer, the words
 * changed and the vertices written into the ring; literals of terrain_draw_expected.inc recorded from the original DLL).
 *
 * DrawTerrainMaterial(viewport, polygons, screen) draws the polygons of a list as a triangle list: three vertices per polygon, 36 bytes
 * each: position copied from the render vertex, normal zero, colour = alpha (the polygon's), red, green and blue (the vertex's colour
 * times its intensity) each times 255 and rounded, packed one byte after the other, texture coordinate from the polygon.  The list is
 * a vector of polygon pointers that ends with a zero, whose last slot holds the number of polygons (what TransformToCamera makes).
 */

typedef long(THISCALL *TerrainMaterialFn)(void *self, void *viewport, void *polygons, unsigned long screen);

#define TD_VERTICES 24
#define TD_POLYGONS 12

static struct {
    GETMRenderVertexLayout vertices[TD_VERTICES];
    GETMPolygonLayout polygons[TD_POLYGONS];
    GETMPolygonLayout *list[TD_POLYGONS + 4];
    StdVectorLayout vector;
} g_td;

static float td_value(uint32_t *state, float low, float high)
{
    *state = *state * 1664525u + 1013904223u;
    return low + (high - low) * (float)((*state >> 8) & 0xffff) / 65535.0f;
}

/* Polygon i uses the vertices 3i, 3i + 1, 3i + 2 (modulo the pool) with colours, intensities, alphas and coordinates of known values. */
static void td_pool(uint32_t seed)
{
    uint32_t state = seed;
    memset(&g_td, 0, sizeof g_td);
    for (int i = 0; i < TD_VERTICES; i++) {
        GETMRenderVertexLayout *v = &g_td.vertices[i];
        v->position.x = td_value(&state, -50.0f, 50.0f);
        v->position.y = td_value(&state, -50.0f, 50.0f);
        v->position.z = td_value(&state, -5.0f, 5.0f);
        v->color.r = td_value(&state, 0.0f, 1.0f);
        v->color.g = td_value(&state, 0.0f, 1.0f);
        v->color.b = td_value(&state, 0.0f, 1.0f);
        v->intensity = td_value(&state, 0.0f, 1.0f);
        v->unknown1c = td_value(&state, 0.0f, 1.0f);
        v->clipFlags = 0x15;
    }
    for (int i = 0; i < TD_POLYGONS; i++) {
        GETMPolygonLayout *p = &g_td.polygons[i];
        for (int k = 0; k < 3; k++) {
            p->vertices[k] = &g_td.vertices[(3 * i + k) % TD_VERTICES];
            p->alpha[k] = td_value(&state, 0.0f, 1.0f);
            p->uv[k].x = td_value(&state, -2.0f, 2.0f);
            p->uv[k].y = td_value(&state, -2.0f, 2.0f);
        }
    }
}

/* The list of the first `count` polygons, starting at polygon `first`: the pointers, a zero, and the count in the last slot. */
static StdVectorLayout *td_list(unsigned first, unsigned count)
{
    memset(g_td.list, 0, sizeof g_td.list);
    for (unsigned i = 0; i < count; i++) {
        g_td.list[i] = &g_td.polygons[(first + i) % TD_POLYGONS];
    }
    g_td.list[count] = NULL;
    g_td.list[TD_POLYGONS + 3] = (GETMPolygonLayout *)(uintptr_t)count;
    memset(&g_td.vector, 0, sizeof g_td.vector);
    g_td.vector.first = g_td.list;
    g_td.vector.last = g_td.list + TD_POLYGONS + 4;
    g_td.vector.end = g_td.vector.last;
    return &g_td.vector;
}

#define TD_MATERIAL(name, screen, cursor, vector, count, ...)                                                                         \
    do {                                                                                                                                \
        d2_prepare(screen, cursor);                                                                                                     \
        LS(name, LIFE_DIFF, {                                                                                                           \
            __VA_ARGS__;                                                                                                                \
            ret = ((TerrainMaterialFn)slot(g_life.vtable, EEWEB_SLOT_DX7RasterizerAbi_DrawTerrainMaterial))(g_life.obj, &life_state(screen)->viewport, (vector), (unsigned long)(screen)); \
            d2_dump_ring(screen, 3 * (count));                                                                                          \
        });                                                                                                                             \
    } while (0)

static void td_material(void)
{
    td_pool(11);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial of one polygon", 0, 0, td_list(0, 1), 1);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial of five polygons", 0, 3, td_list(2, 5), 5);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial of a list with no polygon draws nothing", 0, 20, td_list(0, 0), 0);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial of twelve polygons", 0, 40, td_list(0, 12), 12);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial on screen 1", 1, 5, td_list(4, 3), 3);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial with room for exactly the vertices", 0, 0x98a - 6, td_list(1, 2), 2);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial with too little room starts the ring again", 0, 0x98a - 5, td_list(1, 2), 2);
    TD_MATERIAL("terrain draw: DrawTerrainMaterial while DrawPrimitiveVB is busy twice", 0, 100, td_list(0, 2), 2, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    TD_MATERIAL("terrain draw: DrawTerrainMaterial when DrawPrimitiveVB fails", 0, 110, td_list(0, 2), 2, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    TD_MATERIAL("terrain draw: DrawTerrainMaterial when the vertex buffer unlock fails", 0, 120, td_list(0, 2), 2, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
                g_life.dd->vb_script[FV_UNLOCK].times = 1);

    /* colours: each channel times 255, rounded to the nearest, with no clamp: a channel above 1 spills into the next byte */
    td_pool(12);
    g_td.vertices[0].intensity = 2.0f;
    g_td.vertices[1].intensity = -0.5f;
    g_td.vertices[2].intensity = __builtin_nanf("");
    g_td.vertices[3].color.r = 3.0f;
    g_td.vertices[3].color.g = -1.0f;
    g_td.vertices[4].color.b = 1000.0f;
    g_td.polygons[0].alpha[0] = 1.5f;
    g_td.polygons[0].alpha[1] = -0.25f;
    g_td.polygons[0].alpha[2] = __builtin_nanf("");
    g_td.polygons[1].alpha[0] = 0.5f / 255.0f;
    g_td.polygons[1].alpha[1] = 1.5f / 255.0f;
    g_td.polygons[1].alpha[2] = 2.5f / 255.0f;
    TD_MATERIAL("terrain draw: DrawTerrainMaterial with colours out of range", 0, 200, td_list(0, 2), 2);

    /* many alphas and intensities: the rounding of the product is x87 code */
    LS("terrain draw: the packed colour of 256 alphas and intensities", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        td_pool(13);
        for (int k = 0; k < 256; k++) {
            float alpha = ((float)k + 0.5f - 0.01f * (float)(k % 7)) / 255.0f;
            for (int c = 0; c < 3; c++) {
                g_td.polygons[0].alpha[c] = alpha + 0.002f * (float)c;
                g_td.vertices[c].intensity = 1.0f - (float)k / 400.0f;
                g_td.vertices[c].color.r = (float)(k + c) / 255.0f;
                g_td.vertices[c].color.g = (float)(255 - k) / 255.0f;
                g_td.vertices[c].color.b = ((float)k + 0.5f) / 256.0f;
            }
            d2_prepare(0, 0);
            ((TerrainMaterialFn)slot(g_life.vtable, EEWEB_SLOT_DX7RasterizerAbi_DrawTerrainMaterial))(g_life.obj, &life_state(0)->viewport, td_list(0, 1), 0);
            const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
            for (int v = 0; v < 3; v++) {
                hash = (hash ^ ring[9 * v + 6]) * 16777619u;
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

static void check_terrain_draws(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "the terrain draws")) {
        return;
    }
    memset(&g_arr, 0, sizeof g_arr);
    memset(&g_tex, 0, sizeof g_tex);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    g_d2.vb[0] = ((FakeVB *)g_life.dd->vbs[0])->memory;
    g_d2.vb[1] = ((FakeVB *)g_life.dd->vbs[1])->memory;
    td_material();
    life_end();
}
