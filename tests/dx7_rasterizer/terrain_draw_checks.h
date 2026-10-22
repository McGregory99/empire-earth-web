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
            log_clear(g_life.dd);
            ((TerrainMaterialFn)slot(g_life.vtable, EEWEB_SLOT_DX7RasterizerAbi_DrawTerrainMaterial))(g_life.obj, &life_state(0)->viewport, td_list(0, 1), 0);
            const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
            for (int v = 0; v < 3; v++) {
                hash = (hash ^ ring[9 * v + 6]) * 16777619u;
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

/* ---- Draw(GETerrainMesh &, screen, orthographic), slot 14 ---- */

typedef long(THISCALL *TerrainDrawFn)(void *self, void *mesh, unsigned long screen, unsigned char orthographic);

/* The nodes of a std::map<GETexture *, ulong> as the MSVC 6 library lays them out: left, parent, right, the pair (key, value), colour,
 * "is the head".  The test builds a small tree by hand: the plugin only walks it in order. */
typedef struct TdNode {
    struct TdNode *left, *parent, *right;
    void *key;
    uint32_t value;
    uint8_t color, isnil;
    uint8_t pad[2];
} TdNode;

static struct {
    GETerrainMeshLayout *mesh;
    GETMMaterialLayout materials[4];
    GETMPolygonLayout *inside[4][TD_POLYGONS + 4];
    GETMPolygonLayout *overlap[4][TD_POLYGONS + 4];
    TdNode head, nodes[4];
    GETMTileRowLayout rows[2];
    GETextureLayout *textures[4];
} g_tdm;

/* The list of polygons [first, first + count) in a vector whose last slot holds the count. */
static void td_fill_list(StdVectorLayout *vector, GETMPolygonLayout **storage, unsigned first, unsigned count)
{
    memset(storage, 0, sizeof(GETMPolygonLayout *) * (TD_POLYGONS + 4));
    for (unsigned i = 0; i < count; i++) {
        storage[i] = &g_td.polygons[(first + i) % TD_POLYGONS];
    }
    storage[TD_POLYGONS + 3] = (GETMPolygonLayout *)(uintptr_t)count;
    memset(vector, 0, sizeof *vector);
    vector->first = storage;
    vector->last = storage + TD_POLYGONS + 4;
    vector->end = vector->last;
}

/* A mesh whose material map has `count` entries (the textures 0..count-1, value = the index of the material), the materials
 * holding the given numbers of polygons in their inside and overlap lists. */
static void td_terrain(unsigned count, const unsigned *inside, const unsigned *overlap, int visible_rows)
{
    td_pool(21);
    free(g_tdm.mesh);
    g_tdm.mesh = (GETerrainMeshLayout *)calloc(1, sizeof(GETerrainMeshLayout) + 64);
    GETerrainMeshLayout *mesh = g_tdm.mesh;
    mesh->vptr = vftable("GETerrainMesh");
    mesh->pointRows = (struct GETMPointRowLayout *)calloc(1, sizeof(GETMPointRowLayout));
    mesh->pointRowCount = 1;
    memset(g_tdm.rows, 0, sizeof g_tdm.rows);
    mesh->visibleRows.first = g_tdm.rows;
    mesh->visibleRows.last = visible_rows ? g_tdm.rows + 2 : g_tdm.rows;
    mesh->visibleRows.end = g_tdm.rows + 2;
    memset(g_tdm.materials, 0, sizeof g_tdm.materials);
    for (unsigned m = 0; m < 4; m++) {
        td_fill_list(&g_tdm.materials[m].polygons, g_tdm.inside[m], m * 2, m < count ? inside[m] : 0);
        td_fill_list(&g_tdm.materials[m].overlayPolygons, g_tdm.overlap[m], m * 3 + 1, m < count ? overlap[m] : 0);
    }
    mesh->materials.first = g_tdm.materials;
    mesh->materials.last = g_tdm.materials + 4;
    mesh->materials.end = mesh->materials.last;
    /* the map: in-order nodes 0..count-1 as a tree (root = the middle one) */
    memset(&g_tdm.head, 0, sizeof g_tdm.head);
    memset(g_tdm.nodes, 0, sizeof g_tdm.nodes);
    g_tdm.head.isnil = 1;
    g_tdm.head.left = g_tdm.head.right = g_tdm.head.parent = &g_tdm.head;
    if (count) {
        for (unsigned i = 0; i < count; i++) {
            g_tdm.nodes[i].key = g_tdm.textures[i];
            g_tdm.nodes[i].value = i;
            g_tdm.nodes[i].left = g_tdm.nodes[i].right = &g_tdm.head;
            g_tdm.nodes[i].parent = &g_tdm.head;
        }
        unsigned root = count / 2;
        g_tdm.head.parent = &g_tdm.nodes[root];
        g_tdm.nodes[root].parent = &g_tdm.head;
        for (unsigned i = 0; i < count; i++) {
            if (i == root) {
                continue;
            }
            if (i < root) {
                /* a chain to the left of the root */
                g_tdm.nodes[i].parent = i + 1 == root ? &g_tdm.nodes[root] : &g_tdm.nodes[i + 1];
                if (i + 1 == root) {
                    g_tdm.nodes[root].left = &g_tdm.nodes[i];
                } else {
                    g_tdm.nodes[i + 1].left = &g_tdm.nodes[i];
                }
            } else {
                g_tdm.nodes[i].parent = i == root + 1 ? &g_tdm.nodes[root] : &g_tdm.nodes[i - 1];
                if (i == root + 1) {
                    g_tdm.nodes[root].right = &g_tdm.nodes[i];
                } else {
                    g_tdm.nodes[i - 1].right = &g_tdm.nodes[i];
                }
            }
        }
        g_tdm.head.left = &g_tdm.nodes[0];
        g_tdm.head.right = &g_tdm.nodes[count - 1];
    }
    mesh->materialMap.head = &g_tdm.head;
    mesh->materialMap.size = count;
}

/* The viewport the screen draws with, set up by the engine: a camera that turns and moves, a field of view, the clip distances. */
static void td_viewport_angles(int screen, float x, float y, float z, float rear, double yaw, double pitch, double roll);

static void td_viewport(int screen, float x, float y, float z, float rear)
{
    td_viewport_angles(screen, x, y, z, rear, 0.5235987755982988, 0.3490658503988659, 0.0);
}

/* The camera turned `yaw` about the vertical axis, tilted `pitch` about the right axis and rolled `roll` about the forward axis. */
static void td_viewport_angles(int screen, float x, float y, float z, float rear, double yaw, double pitch, double roll)
{
    void *viewport = &life_state(screen)->viewport;
    GETransformationLayout camera;
    memset(&camera, 0, sizeof camera);
    /* M = Rz(yaw) * Rx(pitch) * Ry(roll) */
    {
        double cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch), cr = cos(roll), sr = sin(roll);
        double rz[3][3] = {{cy, -sy, 0}, {sy, cy, 0}, {0, 0, 1}};
        double rx[3][3] = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}};
        double ry[3][3] = {{cr, 0, sr}, {0, 1, 0}, {-sr, 0, cr}};
        double t[3][3], m[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                t[i][j] = rz[i][0] * rx[0][j] + rz[i][1] * rx[1][j] + rz[i][2] * rx[2][j];
            }
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                m[i][j] = t[i][0] * ry[0][j] + t[i][1] * ry[1][j] + t[i][2] * ry[2][j];
                camera.m[4 * i + j] = (float)m[i][j];
            }
        }
    }
    camera.m[15] = 1.0f;
    camera.m[3] = x;
    camera.m[7] = y;
    camera.m[11] = z;
    static const float kOrthographic[4] = {100.0f, -100.0f, 100.0f, -100.0f};
    static const float kView[4] = {480.0f, 0.0f, 640.0f, 0.0f};
    typedef void(THISCALL * Ptr)(void *, const void *);
    typedef void(THISCALL * Flt)(void *, float);
    typedef void(THISCALL * Flt2)(void *, float, float);
    ((Ptr)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetOrthographicRectangle@GEViewport@@QAEXABVGERectangle@@@Z"))(viewport, kOrthographic);
    ((Ptr)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetViewRectangle@GEViewport@@QAEXABVGERectangle@@@Z"))(viewport, kView);
    ((Ptr)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetCamera@GEViewport@@QAEXABVGETransformation@@@Z"))(viewport, &camera);
    ((Flt)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetFieldOfView@GEViewport@@QAEXM@Z"))(viewport, 0.9f);
    ((Flt)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetFrontClippingDistance@GEViewport@@QAEXM@Z"))(viewport, 1.0f);
    ((Flt)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetRearClippingDistance@GEViewport@@QAEXM@Z"))(viewport, rear);
    ((Flt2)proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?SetDepthRange@GEViewport@@QAEXMM@Z"))(viewport, 0.0f, 1.0f);
}

static void td_dump(int screen, unsigned long cursor)
{
    unsigned long after = life_record(screen)->vertexCursor;
    char text[4096] = "";
    life_extra(" after=%lu drawn=%u", after, (unsigned)life_record(screen)->sceneDrawn);
    if (after >= cursor && after - cursor <= 24) {
        arr_words(text, sizeof text, g_d2.vb[screen] + cursor * 0x24, (after - cursor) * 0x24);
    } else if (after < cursor && after <= 24) {
        life_extra(" restart");
        arr_words(text, sizeof text, g_d2.vb[screen], after * 0x24);
    }
    life_extra(" ring=%s", text);
}

#define TD_DRAW(name, screen, cursor, ortho, ...)                                                                                           \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            life_record(screen)->sceneDrawn = 0;                                                                                            \
            ret = ((TerrainDrawFn)LIFE_SLOT(Draw_2))(g_life.obj, g_tdm.mesh, (unsigned long)(screen), (ortho));                             \
            td_dump(screen, cursor);                                                                                                        \
        });                                                                                                                                 \
    } while (0)

static void td_terrain_draws(void)
{
    for (int i = 0; i < 4; i++) {
        g_tdm.textures[i] = i == 3 ? NULL : d2_texture(0, 1.0f, 0.0f, 0.0f);
    }
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_viewport(1, -30.0f, 5.0f, 12.0f, 500.0f);
    static const unsigned kInside[4] = {2, 3, 0, 1};
    static const unsigned kOverlap[4] = {1, 0, 2, 3};

    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) of three materials", 0, 0, 0);
    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) of three materials orthographic", 0, 40, 1);
    td_terrain(4, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) of four materials, one without a texture", 0, 80, 0);
    td_terrain(1, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) of one material", 0, 120, 0);
    td_terrain(2, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) of two materials on screen 1", 1, 7, 0);
    td_terrain(0, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) with no material", 0, 140, 0);
    {
        static const unsigned kNoInside[4] = {0, 0, 0, 0};
        static const unsigned kSome[4] = {2, 2, 2, 2};
        td_terrain(1, kNoInside, kSome, 1);
        TD_DRAW("terrain draw: Draw(GETerrainMesh) answers the status of the last material draw", 0, 145, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
                g_life.dd->vb_script[FV_UNLOCK].times = 1);
    }
    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) with room for the first material only", 0, 0x98a - 9, 0);
    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) while DrawPrimitiveVB is busy twice", 0, 150, 0, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) when DrawPrimitiveVB fails", 0, 190, 0, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    td_terrain(3, kInside, kOverlap, 1);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) when the vertex buffer unlock fails", 0, 230, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);

    /* no visible row: a black cover is drawn from the camera */
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows draws the cover", 0, 300, 0);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows orthographic", 0, 320, 1);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows on screen 1", 1, 20, 0);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows and a rear plane of 40", 0, 340, 0, td_viewport(0, 10.0f, 20.0f, 5.0f, 40.0f));
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows and a camera elsewhere", 0, 360, 0, td_viewport(0, -75.5f, 13.25f, -2.0f, 1200.0f));
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows with room for the cover only", 0, 0x98a - 12, 0);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows starts the ring again", 0, 0x98a - 11, 0);
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows while DrawPrimitiveVB is busy twice", 0, 380, 0, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows when DrawPrimitiveVB fails", 0, 400, 0, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    td_terrain(3, kInside, kOverlap, 0);
    TD_DRAW("terrain draw: Draw(GETerrainMesh) without visible rows when the vertex buffer unlock fails", 0, 420, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);

    /* the cover's points over many cameras: the float arithmetic is x87 code that keeps some values in extended precision */
    LS("terrain draw: the cover of Draw(GETerrainMesh) over 300 cameras", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        uint32_t seed = 4242;
        for (int k = 0; k < 300; k++) {
            float x = td_value(&seed, -120.0f, 120.0f), y = td_value(&seed, -120.0f, 120.0f), z = td_value(&seed, -20.0f, 20.0f);
            float rear = td_value(&seed, 5.0f, 900.0f);
            td_viewport_angles(0, x, y, z, rear, td_value(&seed, 0.0f, 6.28f), td_value(&seed, -1.2f, 1.2f), td_value(&seed, -0.8f, 0.8f));
            td_terrain(1, kInside, kOverlap, 0);
            d2_prepare(0, 0);
            log_clear(g_life.dd);
            ((TerrainDrawFn)LIFE_SLOT(Draw_2))(g_life.obj, g_tdm.mesh, 0, (unsigned char)(k & 1));
            const uint32_t *ring = (const uint32_t *)g_d2.vb[0];
            for (unsigned w = 0; w < 12 * 9; w++) {
                hash = (hash ^ ring[w]) * 16777619u;
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
}

/* ---- DrawWater(GETerrainMesh &, screen, orthographic), slot 27 ---- */

/* A mesh whose water materials (the vector at 0x30) are the first `count` of the test's materials, each with its own texture. */
static void td_water(unsigned count, const unsigned *inside, const unsigned *overlap, int visible_rows, int null_vector)
{
    td_terrain(0, inside, overlap, visible_rows);
    GETerrainMeshLayout *mesh = g_tdm.mesh;
    for (unsigned m = 0; m < 4; m++) {
        td_fill_list(&g_tdm.materials[m].polygons, g_tdm.inside[m], m * 3, m < count ? inside[m] : 0);
        td_fill_list(&g_tdm.materials[m].overlayPolygons, g_tdm.overlap[m], m * 2 + 1, m < count ? overlap[m] : 0);
        g_tdm.materials[m].texture = g_tdm.textures[m];
    }
    memset(&mesh->materials, 0, sizeof mesh->materials);
    mesh->waterMaterials.first = null_vector ? NULL : g_tdm.materials;
    mesh->waterMaterials.last = null_vector ? NULL : g_tdm.materials + count;
    mesh->waterMaterials.end = mesh->waterMaterials.last;
}

#define TD_WATER(name, screen, cursor, ortho, ...)                                                                                          \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            life_record(screen)->sceneDrawn = 0;                                                                                            \
            ret = ((TerrainDrawFn)LIFE_SLOT(DrawWater))(g_life.obj, g_tdm.mesh, (unsigned long)(screen), (ortho));                          \
            td_dump(screen, cursor);                                                                                                        \
        });                                                                                                                                 \
    } while (0)

static void td_water_draws(void)
{
    static const unsigned kInside[4] = {2, 3, 0, 1};
    static const unsigned kOverlap[4] = {1, 0, 2, 3};
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_viewport(1, -30.0f, 5.0f, 12.0f, 500.0f);
    td_water(3, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater of three materials", 0, 0, 0);
    td_water(3, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater of three materials orthographic", 0, 40, 1);
    td_water(4, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater of four materials, one without a texture", 0, 80, 0);
    td_water(1, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater of one material", 0, 120, 0);
    td_water(2, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater of two materials on screen 1", 1, 7, 0);
    td_water(0, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater with an empty list of materials", 0, 140, 0);
    {
        static const unsigned kNoInside[4] = {0, 0, 0, 0};
        static const unsigned kSome[4] = {2, 2, 2, 2};
        td_water(1, kNoInside, kSome, 1, 0);
        TD_WATER("terrain draw: DrawWater answers the status of the last material draw", 0, 145, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
                 g_life.dd->vb_script[FV_UNLOCK].times = 1);
    }
    td_water(0, kInside, kOverlap, 1, 1);
    TD_WATER("terrain draw: DrawWater with no list of materials", 0, 150, 0);
    td_water(3, kInside, kOverlap, 0, 0);
    TD_WATER("terrain draw: DrawWater without visible rows does nothing", 0, 160, 0);
    td_water(3, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater with room for the first material only", 0, 0x98a - 9, 0);
    td_water(3, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater while DrawPrimitiveVB is busy twice", 0, 170, 0, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_water(3, kInside, kOverlap, 1, 0);
    TD_WATER("terrain draw: DrawWater when DrawPrimitiveVB fails", 0, 210, 0, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
}

/* ---- DrawTerrainGrid(GETerrainMesh &, screen, orthographic), slot 25 ---- */

/* A mesh with a window of the grid (columns x0 .. x0 + width, rows y0 .. y0 + height), a visibility pattern and render vertices of known
 * positions.  `zero_every` > 0 clears every n-th visibility byte. */
static void td_grid_mesh(unsigned x0, unsigned y0, unsigned width, unsigned height, unsigned zero_every, int visible_rows)
{
    static const unsigned kNone[4] = {0, 0, 0, 0};
    td_terrain(0, kNone, kNone, visible_rows);
    GETerrainMeshLayout *mesh = g_tdm.mesh;
    mesh->visibleTileWidth = width;
    mesh->visibleTileHeight = height;
    mesh->visibleBottomLeftX = x0;
    mesh->visibleBottomLeftY = y0;
    uint32_t state = 77;
    for (int i = 0; i < 101 * 101; i++) {
        mesh->pointVisibility[i] = zero_every && i % zero_every == 0 ? 0 : (i % 7 == 3 ? 0xff : 1);
        mesh->renderVertices[i].position.x = td_value(&state, -90.0f, 90.0f);
        mesh->renderVertices[i].position.y = td_value(&state, 1.0f, 200.0f);
        mesh->renderVertices[i].position.z = td_value(&state, -40.0f, 40.0f);
        mesh->renderVertices[i].color.r = 0.5f;
        mesh->renderVertices[i].clipFlags = 0;
    }
}

static void td_dump_hash(int screen, unsigned long cursor)
{
    unsigned long after = life_record(screen)->vertexCursor;
    life_extra(" after=%lu drawn=%u", after, (unsigned)life_record(screen)->sceneDrawn);
    if (after >= cursor && after - cursor <= 0x98a) {
        uint32_t hash = 2166136261u;
        const uint32_t *words = (const uint32_t *)(g_d2.vb[screen] + cursor * 0x24);
        for (unsigned long w = 0; w < (after - cursor) * 9; w++) {
            hash = (hash ^ words[w]) * 16777619u;
        }
        life_extra(" ring=%08x", (unsigned)hash);
        if (after > cursor) {
            char text[1024] = "";
            arr_words(text, sizeof text, g_d2.vb[screen] + cursor * 0x24, (after - cursor < 4 ? after - cursor : 4) * 0x24);
            life_extra(" first=%s", text);
        }
    } else {
        life_extra(" restart");
    }
    uint32_t all = 2166136261u;
    const uint32_t *ring = (const uint32_t *)g_d2.vb[screen];
    for (unsigned w = 0; w < 0x98a * 9; w++) {
        all = (all ^ ring[w]) * 16777619u;
    }
    life_extra(" whole=%08x", (unsigned)all);
}

#define TD_GRID(name, slot_name, screen, cursor, ortho, ...)                                                                                \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            life_record(screen)->sceneDrawn = 0;                                                                                            \
            ret = ((TerrainDrawFn)LIFE_SLOT(slot_name))(g_life.obj, g_tdm.mesh, (unsigned long)(screen), (ortho));                         \
            td_dump_hash(screen, cursor);                                                                                                   \
        });                                                                                                                                 \
    } while (0)

static void td_grid_draws(void)
{
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_viewport(1, -30.0f, 5.0f, 12.0f, 500.0f);
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    TD_GRID("terrain draw: DrawTerrainGrid of a window with some points not visible", DrawTerrainGrid, 0, 0, 0);
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    TD_GRID("terrain draw: DrawTerrainGrid orthographic", DrawTerrainGrid, 0, 100, 1);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid of a window whose points are all visible", DrawTerrainGrid, 0, 200, 0);
    td_grid_mesh(10, 20, 2, 2, 3, 1);
    TD_GRID("terrain draw: DrawTerrainGrid on screen 1", DrawTerrainGrid, 1, 7, 0);
    td_grid_mesh(5, 5, 0, 0, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid of a window of one point draws one cell", DrawTerrainGrid, 0, 300, 0);
    td_grid_mesh(5, 5, 1, 1, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid of a window of 2 x 2 points", DrawTerrainGrid, 0, 310, 0);
    td_grid_mesh(98, 97, 2, 2, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid at the end of the grid", DrawTerrainGrid, 0, 320, 0);
    td_grid_mesh(3, 2, 4, 3, 5, 0);
    TD_GRID("terrain draw: DrawTerrainGrid without visible rows does nothing", DrawTerrainGrid, 0, 330, 0);
    td_grid_mesh(0, 0, 3, 3, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid with room for nine cells", DrawTerrainGrid, 0, 0x98a - 36, 0);
    td_grid_mesh(0, 0, 3, 3, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid with the ring running out half way", DrawTerrainGrid, 0, 0x98a - 18, 0);
    td_grid_mesh(3, 2, 2, 2, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid while DrawPrimitiveVB is busy twice", DrawTerrainGrid, 0, 340, 0, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_grid_mesh(3, 2, 2, 2, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid when DrawPrimitiveVB fails", DrawTerrainGrid, 0, 360, 0, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    td_grid_mesh(3, 2, 2, 2, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid when the vertex buffer unlock fails once", DrawTerrainGrid, 0, 380, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);
    td_grid_mesh(3, 2, 0, 0, 0, 1);
    TD_GRID("terrain draw: DrawTerrainGrid answers the status of the last unlock", DrawTerrainGrid, 0, 400, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
            g_life.dd->vb_script[FV_UNLOCK].times = 1);
}

/* ---- DrawTerrainHighlight(GETerrainMesh &, rectangle, screen, orthographic, colour, alpha), slot 26 ---- */

typedef long(THISCALL *TerrainHighlightFn)(void *self, void *mesh, const int32_t *rectangle, unsigned long screen, unsigned char orthographic, const float *color,
                                           float alpha);

static GETMTileRowLayout g_tile_rows[105];

/* Tile rows 0 .. count - 1: row r holds the columns [first(r), first(r) + 100). */
static void td_tile_rows(unsigned count)
{
    memset(g_tile_rows, 0, sizeof g_tile_rows);
    for (unsigned r = 0; r < 105; r++) {
        g_tile_rows[r].firstIndex = r % 3;
        g_tile_rows[r].count = 100 - (r % 3);
    }
    g_tdm.mesh->tileRows = g_tile_rows;
    g_tdm.mesh->tileRowCount = count;
}

#define TD_HIGHLIGHT(name, screen, cursor, rect, ortho, color, alpha, ...)                                                                  \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            ret = ((TerrainHighlightFn)LIFE_SLOT(DrawTerrainHighlight))(g_life.obj, g_tdm.mesh, (rect), (unsigned long)(screen), (ortho), (color), (alpha)); \
            td_dump_hash(screen, cursor);                                                                                                   \
        });                                                                                                                                 \
    } while (0)

static void td_highlight_draws(void)
{
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_viewport(1, -30.0f, 5.0f, 12.0f, 500.0f);
    static const float kRed[3] = {1.0f, 0.25f, 0.0f};
    static const float kWild[3] = {2.0f, -0.5f, 0.3333f};
    static const int32_t kInside[4] = {6, 3, 7, 2};       /* rows 2 .. 6, columns 3 .. 7 */
    static const int32_t kSingle[4] = {4, 5, 5, 4};
    static const int32_t kWide[4] = {8, 0, 12, 3};        /* partly outside the window */
    static const int32_t kReversed[4] = {2, 7, 3, 6};     /* the loops do not run, the count is still the product of the sides */
    static const int32_t kOutside[4] = {60, 50, 70, 55};
    static const int32_t kBeyondRows[4] = {104, 90, 99, 98};
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of a rectangle inside the window", 0, 0, kInside, 0, kRed, 0.5f);
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight orthographic", 0, 100, kInside, 1, kRed, 0.5f);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of one tile", 0, 200, kSingle, 0, kRed, 1.0f);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of a rectangle that reaches beyond the window", 0, 300, kWide, 0, kRed, 0.25f);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of a rectangle whose sides are the wrong way round", 0, 400, kReversed, 0, kRed, 0.25f);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of a rectangle outside the window", 0, 410, kOutside, 0, kRed, 0.25f);
    td_grid_mesh(0, 0, 6, 5, 0, 1);
    td_tile_rows(6);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of rows beyond the tile rows", 0, 420, kInside, 0, kRed, 0.25f);
    td_grid_mesh(0, 0, 99, 99, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of a rectangle taller than the tile rows", 0, 430, kBeyondRows, 0, kRed, 0.25f);
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight with colours out of range", 0, 500, kInside, 0, kWild, 1.75f);
    td_grid_mesh(3, 2, 4, 3, 5, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight with an alpha of zero", 0, 520, kInside, 0, kRed, 0.0f);
    td_grid_mesh(10, 20, 3, 3, 3, 1);
    td_tile_rows(100);
    {
        static const int32_t kScreenOne[4] = {23, 10, 14, 20};
        TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight on screen 1", 1, 7, kScreenOne, 0, kRed, 0.5f);
    }
    td_grid_mesh(3, 2, 4, 3, 5, 0);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight without visible rows does nothing", 0, 540, kInside, 0, kRed, 0.5f);

    /* more than 407 tiles are drawn in batches that fill the ring: 407 tiles are 2442 vertices */
    static const int32_t k407[4] = {10, 3, 39, 0};
    static const int32_t k408[4] = {11, 3, 36, 0};
    td_grid_mesh(0, 0, 99, 99, 0, 1);
    td_tile_rows(105);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of exactly 407 tiles", 0, 0, k407, 0, kRed, 0.5f);
    td_grid_mesh(0, 0, 99, 99, 0, 1);
    td_tile_rows(105);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of 408 tiles", 0, 0, k408, 0, kRed, 0.5f);
    td_grid_mesh(0, 0, 99, 99, 0, 1);
    td_tile_rows(105);
    {
        static const int32_t kBig[4] = {99, 0, 99, 0};
        TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of 10000 tiles in batches", 0, 0, kBig, 0, kRed, 0.5f);
    }
    td_grid_mesh(0, 0, 99, 99, 0, 1);
    td_tile_rows(105);
    {
        static const int32_t kHalf[4] = {5, 0, 99, 0};
        TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight of 600 tiles with the ring part used", 0, 1000, kHalf, 0, kRed, 0.5f);
    }
    td_grid_mesh(3, 2, 4, 3, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight while DrawPrimitiveVB is busy twice", 0, 600, kInside, 0, kRed, 0.5f, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_grid_mesh(3, 2, 4, 3, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight when DrawPrimitiveVB fails", 0, 650, kInside, 0, kRed, 0.5f, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    td_grid_mesh(3, 2, 4, 3, 0, 1);
    td_tile_rows(100);
    TD_HIGHLIGHT("terrain draw: DrawTerrainHighlight when the vertex buffer unlock fails", 0, 700, kInside, 0, kRed, 0.5f, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
                 g_life.dd->vb_script[FV_UNLOCK].times = 1);
}

/* ---- DrawGridLines(GETerrainMesh &, lines, texture, v, screen, orthographic), slot 23 ---- */

typedef long(THISCALL *GridLinesFn)(void *self, void *mesh, void *lines, void *texture, float v, unsigned long screen, unsigned char orthographic);

static GETMPointRowLayout g_line_rows[101];
static GETMPointLayout *g_line_points[101];
static int32_t g_lines[1400 * 4];
static StdVectorLayout g_lines_vector;

/* A mesh with a window, a visibility pattern and point rows: row r holds the columns from r % 4, heights of both signs, zero and one
 * that is not a number. */
static void td_lines_mesh(unsigned x0, unsigned y0, unsigned width, unsigned height, unsigned zero_every)
{
    td_grid_mesh(x0, y0, width, height, zero_every, 1);
    GETerrainMeshLayout *mesh = g_tdm.mesh;
    uint32_t state = 303;
    for (unsigned r = 0; r < 101; r++) {
        free(g_line_points[r]);
        g_line_points[r] = (GETMPointLayout *)calloc(105, sizeof(GETMPointLayout));
        g_line_rows[r].firstIndex = r % 4;
        g_line_rows[r].count = 101 - r % 4;
        g_line_rows[r].points = g_line_points[r];
        for (unsigned c = 0; c < 105; c++) {
            float z = td_value(&state, -10.0f, 20.0f);
            uint32_t pick = (r * 5 + c) % 17;
            if (pick == 0) {
                z = 0.0f;
            } else if (pick == 1) {
                z = -0.0f;
            } else if (pick == 2) {
                z = __builtin_nanf("");
            }
            g_line_points[r][c].z = z;
        }
    }
    mesh->pointRows = g_line_rows;
    mesh->pointRowCount = 101;
}

/* The lines as four longs each: (x1, y1, x2, y2). */
static StdVectorLayout *td_lines(const int32_t *values, unsigned count)
{
    memcpy(g_lines, values, count * 16);
    memset(&g_lines_vector, 0, sizeof g_lines_vector);
    g_lines_vector.first = count ? g_lines : NULL;
    g_lines_vector.last = count ? g_lines + 4 * count : NULL;
    g_lines_vector.end = g_lines_vector.last;
    return &g_lines_vector;
}

#define TD_LINES(name, screen, cursor, lines, texture, v, ortho, ...)                                                                       \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            ret = ((GridLinesFn)LIFE_SLOT(DrawGridLines))(g_life.obj, g_tdm.mesh, (lines), (texture), (v), (unsigned long)(screen), (ortho));\
            td_dump_hash(screen, cursor);                                                                                                   \
        });                                                                                                                                 \
    } while (0)

static void td_lines_draws(void)
{
    td_viewport(0, 10.0f, 20.0f, 5.0f, 300.0f);
    td_viewport(1, -30.0f, 5.0f, 12.0f, 500.0f);
    GETextureLayout *texture0 = d2_texture(0, 1.0f, 0.0f, 0.0f);
    GETextureLayout *texture1 = d2_texture(1, 1.0f, 0.0f, 0.0f);
    static const int32_t kInside[] = {3, 2, 4, 2, 4, 2, 4, 3, 5, 3, 3, 5, 6, 4, 6, 5};
    static const int32_t kEdges[] = {
        2, 2, 4, 2,   /* x below the window */
        3, 1, 4, 2,   /* y below */
        3, 2, 4, 7,   /* y2 above */
        3, 2, 8, 2,   /* x2 above */
        3, 2, 7, 6,   /* the last column and row of the window: x0 + width + 1 is outside */
        3, 2, 6, 5,   /* the last that is inside */
        3, 2, 3, 2,   /* a line of one point */
    };
    td_lines_mesh(3, 2, 4, 3, 7);
    TD_LINES("terrain draw: DrawGridLines of lines inside the window", 0, 0, td_lines(kInside, 8), texture0, 0.25f, 0);
    td_lines_mesh(3, 2, 4, 3, 7);
    TD_LINES("terrain draw: DrawGridLines orthographic", 0, 100, td_lines(kInside, 8), texture0, 0.25f, 1);
    td_lines_mesh(3, 2, 4, 3, 7);
    TD_LINES("terrain draw: DrawGridLines without a texture", 0, 200, td_lines(kInside, 8), NULL, -1.5f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines with lines at the edges of the window", 0, 300, td_lines(kEdges, 7), texture0, 2.75f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines on screen 1", 1, 5, td_lines(kInside, 8), texture1, 0.0f, 0);
    td_lines_mesh(3, 2, 4, 3, 2);
    TD_LINES("terrain draw: DrawGridLines of lines whose points are not visible", 0, 400, td_lines(kInside, 8), texture0, 0.5f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines of an empty list of lines", 0, 500, td_lines(kInside, 0), texture0, 0.5f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines of lines that are all outside moves the cursor all the same", 0, 520, td_lines(kEdges, 3), texture0, 0.5f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines without visible rows does nothing", 0, 540, td_lines(kInside, 8), texture0, 0.5f, 0, g_tdm.mesh->visibleRows.last = g_tdm.mesh->visibleRows.first);
    /* heights that are not numbers (or negative) become zero: the point (11, 2) has array index 9 in its row, where (5 * row + index) % 17 == 2 */
    static const int32_t kNotANumber[] = {11, 2, 12, 2, 11, 2, 11, 3, 13, 4, 14, 4};
    td_lines_mesh(0, 0, 20, 20, 0);
    TD_LINES("terrain draw: DrawGridLines through points whose height is not a number or negative", 0, 560, td_lines(kNotANumber, 3), texture0, 0.5f, 0);
    /* a lot of lines: the ring holds 1221 of them */
    static int32_t many[1400 * 4];
    for (int i = 0; i < 1400; i++) {
        many[4 * i] = 3 + i % 4;
        many[4 * i + 1] = 2 + i % 3;
        many[4 * i + 2] = 4 + i % 3;
        many[4 * i + 3] = 3 + i % 4;
    }
    td_lines_mesh(0, 0, 8, 8, 0);
    TD_LINES("terrain draw: DrawGridLines of 1221 lines fills the ring", 0, 0, td_lines(many, 1221), texture0, 0.1f, 0);
    td_lines_mesh(0, 0, 8, 8, 0);
    TD_LINES("terrain draw: DrawGridLines of 1222 lines is cut to the ring", 0, 7, td_lines(many, 1222), texture0, 0.1f, 0);
    td_lines_mesh(0, 0, 8, 8, 0);
    TD_LINES("terrain draw: DrawGridLines of 1400 lines is cut to the ring", 0, 9, td_lines(many, 1400), texture0, 0.1f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines with the ring nearly full starts it again", 0, 0x98a - 8, td_lines(kInside, 8), texture0, 0.5f, 0);
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines while DrawPrimitiveVB is busy twice", 0, 600, td_lines(kInside, 8), texture0, 0.5f, 0, d2_script(FD_DRAW_PRIMITIVE, D2_BUSY, 2));
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines when DrawPrimitiveVB fails", 0, 650, td_lines(kInside, 8), texture0, 0.5f, 0, d2_script(FD_DRAW_PRIMITIVE, D2_OTHER, 1));
    td_lines_mesh(3, 2, 4, 3, 0);
    TD_LINES("terrain draw: DrawGridLines when the vertex buffer unlock fails", 0, 700, td_lines(kInside, 8), texture0, 0.5f, 0, g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER;
             g_life.dd->vb_script[FV_UNLOCK].times = 1);
}

/* ---- Light(GETerrainMesh &, screen), slot 34 ---- */

typedef long(THISCALL *TerrainLightFn)(void *self, void *mesh, unsigned long screen);
typedef long(THISCALL *AddLightFn)(void *self, const void *light, unsigned long screen, unsigned long *id);
typedef long(THISCALL *RemoveAllLightsFn)(void *self, unsigned long screen);

static GEVectorLayout g_normals[64];

/* The terrain of td_lines_mesh with a normal, a colour index and a colour for every point, a colour table, a water colour and the render
 * vertices' colours set to a pattern that no lighting writes. */
static void td_light_mesh(unsigned x0, unsigned y0, unsigned width, unsigned height, int visible_rows)
{
    td_lines_mesh(x0, y0, width, height, 9);
    GETerrainMeshLayout *mesh = g_tdm.mesh;
    uint32_t state = 909;
    for (int i = 0; i < 64; i++) {
        g_normals[i].x = td_value(&state, -1.0f, 1.0f);
        g_normals[i].y = td_value(&state, -1.0f, 1.0f);
        g_normals[i].z = td_value(&state, -1.0f, 1.0f);
    }
    memset(&g_normals[5], 0, sizeof g_normals[5]);
    for (unsigned r = 0; r < 101; r++) {
        for (unsigned c = 0; c < 105; c++) {
            g_line_points[r][c].normal = &g_normals[(r * 7 + c) % 64];
            g_line_points[r][c].colorIndex = (uint8_t)((r * 3 + c) % 256);
        }
    }
    for (int i = 0; i < 256; i++) {
        mesh->colorTable[i].r = td_value(&state, 0.0f, 1.0f);
        mesh->colorTable[i].g = td_value(&state, 0.0f, 1.0f);
        mesh->colorTable[i].b = td_value(&state, 0.0f, 1.0f);
    }
    mesh->waterColor.r = 0.2f;
    mesh->waterColor.g = 0.4f;
    mesh->waterColor.b = 0.6f;
    for (int i = 0; i < 101 * 101; i++) {
        mesh->waterPointVisibility[i] = i % 3 == 0;
        memset(&mesh->renderVertices[i].color, 0xa5, sizeof mesh->renderVertices[i].color);
        memset(&mesh->waterRenderVertices[i].color, 0xa5, sizeof mesh->waterRenderVertices[i].color);
    }
    mesh->visibleRows.last = visible_rows ? g_tdm.rows + 2 : g_tdm.rows;
}

/* The light `id` as the device will hand it back: a type, a diffuse colour and a direction. */
static void td_light_script(int id, unsigned long type, float r, float g, float b, float dx, float dy, float dz)
{
    DWORD *words = g_life.dd->get_light[id];
    memset(words, 0, sizeof g_life.dd->get_light[id]);
    words[0] = type;
    memcpy(&words[1], &r, 4);
    memcpy(&words[2], &g, 4);
    memcpy(&words[3], &b, 4);
    memcpy(&words[16], &dx, 4);
    memcpy(&words[17], &dy, 4);
    memcpy(&words[18], &dz, 4);
}

/* The lights 0 .. count - 1 taken in the DLL's table (the fakes hand back what td_light_script set). */
static void td_lights_taken(int count)
{
    GELightLayout light;
    ((RemoveAllLightsFn)LIFE_SLOT(RemoveAllLights))(g_life.obj, 0);
    for (int i = 0; i < count; i++) {
        unsigned long id = 0;
        state_make_light(&light, 1, 1.0f + (float)i);
        ((AddLightFn)LIFE_SLOT(AddLight))(g_life.obj, &light, 0, &id);
    }
    log_clear(g_life.dd);
    memset(g_life.dd->get_light, 0, sizeof g_life.dd->get_light);
}

static void td_colors_report(const char *tag, const GETMRenderVertexLayout *vertices)
{
    unsigned changed = 0, first = TC_VERTICES;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < TC_VERTICES; i++) {
        const uint32_t *w = (const uint32_t *)&vertices[i].color;
        if (w[0] != 0xa5a5a5a5u || w[1] != 0xa5a5a5a5u || w[2] != 0xa5a5a5a5u) {
            changed++;
            if (first == TC_VERTICES) {
                first = i;
            }
        }
        hash = (hash ^ w[0]) * 16777619u;
        hash = (hash ^ w[1]) * 16777619u;
        hash = (hash ^ w[2]) * 16777619u;
    }
    life_extra(" %s=%08x n=%u", tag, (unsigned)hash, changed);
    if (first < TC_VERTICES) {
        const uint32_t *w = (const uint32_t *)&vertices[first].color;
        life_extra(" v%u=%x,%x,%x", first, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2]);
    }
}

#define TD_LIGHT(name, ...)                                                                                                                 \
    do {                                                                                                                                    \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            ((TerrainLightFn)LIFE_SLOT(Light))(g_life.obj, g_tdm.mesh, 0);                                                                  \
            td_colors_report("t", g_tdm.mesh->renderVertices);                                                                              \
            td_colors_report("w", g_tdm.mesh->waterRenderVertices);                                                                         \
        });                                                                                                                                 \
    } while (0)

static void td_light_draws(void)
{
    GERasterizerLayout *base = (GERasterizerLayout *)g_life.obj;
    base->computedAmbientColor.r = 0.2f;
    base->computedAmbientColor.g = 0.3f;
    base->computedAmbientColor.b = 0.4f;
    td_lights_taken(3);
    td_light_script(0, 3, 1.0f, 0.5f, 0.25f, 0.3f, -0.4f, -0.8f);
    td_light_script(1, 3, 0.2f, 0.9f, 0.4f, -0.5f, 0.2f, -0.6f);
    td_light_script(2, 3, 0.0f, 0.0f, 0.7f, 0.1f, 0.1f, 0.5f);
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light of three directional lights");
    td_lights_taken(0);
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light without a light");
    td_lights_taken(4);
    td_light_script(0, 1, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f);
    td_light_script(1, 3, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    td_light_script(2, 3, __builtin_nanf(""), 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    td_light_script(3, 3, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, -1.0f);
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light skips point lights, black lights and colours that are not a number");
    td_lights_taken(2);
    td_light_script(0, 3, 0.6f, 0.6f, 0.6f, 0.0f, 0.0f, 0.0f);
    td_light_script(1, 3, 0.3f, 0.2f, 0.1f, 0.5f, 0.5f, 0.0f);
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light with directions that have no vertical part");
    td_lights_taken(3);
    td_light_script(0, 3, 1.0f, 0.5f, 0.25f, 0.3f, -0.4f, -0.8f);
    td_light_script(1, 3, 0.2f, 0.9f, 0.4f, -0.5f, 0.2f, -0.6f);
    td_light_script(2, 3, 0.0f, 0.0f, 0.7f, 0.1f, 0.1f, 0.5f);
    td_light_mesh(3, 2, 4, 3, 0);
    TD_LIGHT("terrain draw: Light without visible rows does nothing");
    td_light_mesh(3, 2, 96, 96, 1);
    TD_LIGHT("terrain draw: Light of the whole grid");
    td_light_mesh(10, 20, 0, 0, 1);
    TD_LIGHT("terrain draw: Light of a window of 2 x 2 points");
    base->computedAmbientColor.r = 3.0f;
    base->computedAmbientColor.g = 0.0f;
    base->computedAmbientColor.b = -0.5f;
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light with an ambient colour out of range");
    base->computedAmbientColor.r = 0.2f;
    base->computedAmbientColor.g = 0.3f;
    base->computedAmbientColor.b = 0.4f;
    td_lights_taken(8);
    for (int i = 0; i < 8; i++) {
        td_light_script(i, 3, 0.1f + 0.1f * (float)i, 0.9f - 0.1f * (float)i, 0.05f * (float)i, 0.2f * (float)i - 0.7f, 0.5f - 0.1f * (float)i, -0.1f - 0.1f * (float)i);
    }
    td_light_mesh(3, 2, 4, 3, 1);
    TD_LIGHT("terrain draw: Light of eight lights");

    /* many lights and normals as a hash: the products and sums are x87 code */
    LS("terrain draw: Light over 120 sets of lights", LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        uint32_t seed = 1717;
        for (int k = 0; k < 120; k++) {
            int lights = 1 + k % 4;
            td_lights_taken(lights);
            for (int i = 0; i < lights; i++) {
                td_light_script(i, 3, td_value(&seed, 0.0f, 1.5f), td_value(&seed, 0.0f, 1.5f), td_value(&seed, 0.0f, 1.5f), td_value(&seed, -1.0f, 1.0f),
                                td_value(&seed, -1.0f, 1.0f), td_value(&seed, -1.0f, 0.4f));
            }
            base->computedAmbientColor.r = td_value(&seed, 0.0f, 1.0f);
            base->computedAmbientColor.g = td_value(&seed, 0.0f, 1.0f);
            base->computedAmbientColor.b = td_value(&seed, 0.0f, 1.0f);
            td_light_mesh(3, 2, 3, 3, 1);
            ((TerrainLightFn)LIFE_SLOT(Light))(g_life.obj, g_tdm.mesh, 0);
            for (int v = 0; v < 101 * 101; v += 7) {
                const uint32_t *w = (const uint32_t *)&g_tdm.mesh->renderVertices[v].color;
                const uint32_t *x = (const uint32_t *)&g_tdm.mesh->waterRenderVertices[v].color;
                for (int i = 0; i < 3; i++) {
                    hash = (hash ^ w[i]) * 16777619u;
                    hash = (hash ^ x[i]) * 16777619u;
                }
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
    td_lights_taken(0);
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
    memset(&g_tdm, 0, sizeof g_tdm);
    td_terrain_draws();
    td_water_draws();
    td_grid_draws();
    td_highlight_draws();
    td_lines_draws();
    td_light_draws();
    life_end();
}
