/*
 * TransformToCamera(GETerrainMesh &, GEViewport &, float, bool water, bool orthographic) of DX7Rasterizer (task P2.6.7f), slot 67.
 * Included by screen_checks.h after text_checks.h and run from the screen probe.
 *
 * The slot works on a terrain mesh (a 1.5 MB engine object, built here on the heap from the layouts of ge_layouts.h) and a viewport:
 * it moves the visible window of the 101 x 101 grid of terrain points (and of water points, in the second set of render vertices)
 * through the viewport's camera inverse, with a sine ripple that advances a module-wide phase, and stores a six-bit outcode (front,
 * rear, left, right, top, bottom) in every vertex it transformed; then it sorts the polygons of every terrain and water material
 * into "inside" and "overlapping" lists.  It calls no Direct3D function, so what is compared is memory: a hash of every render
 * vertex of both arrays, the number and the first few of the vertices that changed, the histogram of the outcode bits, the number of
 * bytes changed anywhere else in the mesh (none), the vertices of the edge list and the polygon lists, as indices of the polygons
 * that went in.  The phase is module state: both variants start from a freshly loaded module and run the same steps in the same
 * order.  Literals of terrain_expected.inc were recorded from the original DLL (EEWEB_LIFE_RECORD=1 prints them instead of checking).
 */

typedef void(THISCALL *TransformFn)(void *self, void *mesh, void *viewport, float time, unsigned char water, unsigned char ortho);

#define TC_COLUMNS 101
#define TC_VERTICES (TC_COLUMNS * TC_COLUMNS)
#define TC_MATERIALS 4
#define TC_POLYGONS 12
#define TC_EDGE 4

static struct {
    GETerrainMeshLayout *mesh;
    GEViewportLayout *viewport;
    GETMPointRowLayout rows[TC_COLUMNS];
    GETMPointLayout *points[TC_COLUMNS];
    GETMTileRowLayout tile_rows[3];
    GE3DPointLayout edge_points[TC_EDGE];
    GETMRenderVertexLayout edge_vertices[TC_EDGE];
    GETMRenderVertexLayout *edge_pointers[TC_EDGE];
    /* the materials: the polygons of each, the lists the call fills, the vertices that carry the outcodes the polygons are sorted by */
    GETMMaterialLayout materials[TC_MATERIALS];
    GETMMaterialLayout water_materials[TC_MATERIALS];
    GETMPolygonLayout polygons[2][TC_MATERIALS][TC_POLYGONS];
    GETMPolygonLayout *input[2][TC_MATERIALS][TC_POLYGONS];
    GETMPolygonLayout *inside[2][TC_MATERIALS][TC_POLYGONS + 2];
    GETMPolygonLayout *overlap[2][TC_MATERIALS][TC_POLYGONS + 2];
    GETMRenderVertexLayout flag_vertices[2][TC_MATERIALS][TC_POLYGONS][3];
    unsigned input_count[2][TC_MATERIALS];
    uint32_t seed;
    unsigned char *snapshot;
} g_tc;

static uint32_t tc_next(void)
{
    g_tc.seed = g_tc.seed * 1664525u + 1013904223u;
    return g_tc.seed >> 8;
}

static float tc_float(float low, float high)
{
    return low + (high - low) * (float)(tc_next() & 0xffff) / 65535.0f;
}

static const uint32_t kTcFlagTriples[TC_POLYGONS][3] = {
    {0, 0, 0},       {1, 0, 0},       {0, 2, 0},          {1, 1, 0},          {0, 0, 0x3f},       {1, 2, 4},
    {3, 3, 3},       {0x3f, 0x3f, 0x3f}, {0x3f, 0x3f, 0x3e}, {0x15, 0x15, 0x35}, {1, 2, 0},       {0x20, 0x20, 0x20},
};

/* A mesh, a viewport and a window of the grid.  `nan_every` > 0 makes every n-th height a NaN; `water` is the share of points the
 * water visibility array marks (0..4 of 4). */
static void tc_world(uint32_t seed, unsigned x0, unsigned y0, unsigned width, unsigned height, unsigned nan_every, unsigned water)
{
    g_tc.seed = seed;
    free(g_tc.mesh);
    free(g_tc.viewport);
    for (int i = 0; i < TC_COLUMNS; i++) {
        free(g_tc.points[i]);
        g_tc.points[i] = NULL;
    }
    g_tc.mesh = (GETerrainMeshLayout *)calloc(1, sizeof(GETerrainMeshLayout) + 64);
    g_tc.viewport = (GEViewportLayout *)calloc(1, sizeof(GEViewportLayout));
    GETerrainMeshLayout *mesh = g_tc.mesh;
    GEViewportLayout *vp = g_tc.viewport;
    mesh->vptr = vftable("GETerrainMesh");
    memset(mesh->renderVertices, 0xa5, sizeof mesh->renderVertices);
    memset(mesh->waterRenderVertices, 0xa5, sizeof mesh->waterRenderVertices);
    memset(g_tc.rows, 0, sizeof g_tc.rows);
    for (unsigned r = 0; r < TC_COLUMNS; r++) {
        g_tc.points[r] = (GETMPointLayout *)calloc(TC_COLUMNS + 4, sizeof(GETMPointLayout));
        unsigned first = x0 > (r % 3) ? x0 - (r % 3) : 0;
        g_tc.rows[r].firstIndex = first;
        g_tc.rows[r].count = TC_COLUMNS + 4;
        g_tc.rows[r].points = g_tc.points[r];
        for (unsigned c = 0; c < TC_COLUMNS + 4; c++) {
            float z = tc_float(-3.0f, 2.0f);
            uint32_t pick = tc_next() % 23;
            if (pick == 0) {
                z = -0.5f;
            } else if (pick == 1) {
                z = 0.0f;
            } else if (pick == 2) {
                z = -0.0f;
            } else if (pick == 3) {
                z = -0.4999f;
            } else if (pick == 4) {
                z = 7.25f;
            }
            if (nan_every && (r * 7 + c) % nan_every == 0) {
                z = __builtin_nanf("");
            }
            g_tc.points[r][c].z = z;
        }
    }
    mesh->pointRows = g_tc.rows;
    mesh->pointRowCount = TC_COLUMNS;
    memset(g_tc.tile_rows, 0, sizeof g_tc.tile_rows);
    mesh->visibleRows.first = g_tc.tile_rows;
    mesh->visibleRows.last = g_tc.tile_rows + 3;
    mesh->visibleRows.end = g_tc.tile_rows + 3;
    mesh->visibleTileWidth = width;
    mesh->visibleTileHeight = height;
    mesh->visibleBottomLeftX = x0;
    mesh->visibleBottomLeftY = y0;
    mesh->waterVisiblePercentage = 0.75f;
    for (int i = 0; i < TC_VERTICES; i++) {
        uint32_t pick = tc_next() % 9;
        mesh->pointVisibility[i] = pick == 0 ? 0 : pick == 1 ? 0xff : 1;
        uint32_t pick2 = tc_next() % 4;
        mesh->waterPointVisibility[i] = pick2 < water ? (pick2 == 1 ? 0x80 : 1) : 0;
    }
    /* the edge of the map */
    for (int i = 0; i < TC_EDGE; i++) {
        g_tc.edge_points[i].x = tc_float(-30.0f, 30.0f);
        g_tc.edge_points[i].y = tc_float(-30.0f, 30.0f);
        g_tc.edge_points[i].z = tc_float(-4.0f, 4.0f);
        memset(&g_tc.edge_vertices[i], 0xa5, sizeof g_tc.edge_vertices[i]);
        g_tc.edge_pointers[i] = &g_tc.edge_vertices[i];
    }
    mesh->edgeWorldPoints.first = g_tc.edge_points;
    mesh->edgeWorldPoints.last = g_tc.edge_points + TC_EDGE;
    mesh->edgeWorldPoints.end = g_tc.edge_points + TC_EDGE;
    mesh->edgeRenderVertices.first = g_tc.edge_pointers;
    mesh->edgeRenderVertices.last = g_tc.edge_pointers + TC_EDGE;
    mesh->edgeRenderVertices.end = g_tc.edge_pointers + TC_EDGE;
    /* no materials unless a step adds them */
    memset(g_tc.materials, 0, sizeof g_tc.materials);
    memset(g_tc.water_materials, 0, sizeof g_tc.water_materials);

    /* the viewport: a camera that scales, turns and moves, a field of view and the planes */
    float *f = (float *)vp;
    f[0] = 2.5f; /* orthographic rectangle: 4 floats */
    f[1] = 5.0f;
    f[2] = -5.0f;
    f[3] = 5.0f;
    vp->leftPlane.point.x = -5.0f;
    vp->leftPlane.normal.x = 1.0f;
    vp->leftPlane.normal.y = 0.25f;
    vp->rightPlane.point.x = 10.0f;
    vp->rightPlane.normal.x = -1.0f;
    vp->rightPlane.normal.y = -0.125f;
    float c = 0.8660254f;
    float s = 0.5f;
    float matrix[16] = {6.0f * c, -6.0f * s, 0.0f, -20.0f, 6.0f * s, 6.0f * c, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    memcpy(vp->cameraInverse.m, matrix, sizeof matrix);
    vp->fieldOfView = 0.9f;
    vp->frontClip = 22.0f;
    vp->halfHeight = 0.75f;
    vp->halfWidth = 1.6f;
    vp->rearClip = 50.0f;
    vp->yClippingPlane = 0.8f;
}

/* The rasterizer object's ripple settings: speed (0x40), height of the water (0x44) and depth of the ripple in the terrain (0x48). */
static void tc_ripple(float speed, float water_height, float terrain_depth)
{
    GERasterizerLayout *base = (GERasterizerLayout *)g_life.obj;
    base->waterRipple[0] = speed;
    base->waterRipple[1] = water_height;
    base->waterRipple[2] = terrain_depth;
}

/* Material `index` of the terrain (0) or the water (1) with polygons of the flag triples of kTcFlagTriples from `first` for `count`. */
static void tc_material(int water, int index, unsigned first, unsigned count, int null_vector)
{
    GETMMaterialLayout *material = water ? &g_tc.water_materials[index] : &g_tc.materials[index];
    for (unsigned i = 0; i < count; i++) {
        unsigned kind = (first + i) % TC_POLYGONS;
        for (int v = 0; v < 3; v++) {
            GETMRenderVertexLayout *vertex = &g_tc.flag_vertices[water][index][i][v];
            memset(vertex, 0, sizeof *vertex);
            vertex->clipFlags = kTcFlagTriples[kind][v];
            g_tc.polygons[water][index][i].vertices[v] = vertex;
        }
        g_tc.input[water][index][i] = &g_tc.polygons[water][index][i];
    }
    g_tc.input_count[water][index] = count;
    memset(g_tc.inside[water][index], 0xde, sizeof g_tc.inside[water][index]);
    memset(g_tc.overlap[water][index], 0xde, sizeof g_tc.overlap[water][index]);
    memset(material, 0, sizeof *material);
    if (!null_vector) {
        material->unknownVector.first = g_tc.input[water][index];
        material->unknownVector.last = g_tc.input[water][index] + count;
        material->unknownVector.end = g_tc.input[water][index] + count;
    }
    material->polygons.first = g_tc.inside[water][index];
    material->polygons.last = g_tc.inside[water][index] + TC_POLYGONS + 2;
    material->polygons.end = material->polygons.last;
    material->overlayPolygons.first = g_tc.overlap[water][index];
    material->overlayPolygons.last = g_tc.overlap[water][index] + TC_POLYGONS + 2;
    material->overlayPolygons.end = material->overlayPolygons.last;
}

static void tc_set_materials(int water, int count)
{
    GETMMaterialLayout *list = water ? g_tc.water_materials : g_tc.materials;
    StdVectorLayout *vector = water ? &g_tc.mesh->waterMaterials : &g_tc.mesh->materials;
    vector->first = count ? list : NULL;
    vector->last = count ? list + count : NULL;
    vector->end = vector->last;
}

static void tc_list(char *out, size_t size, int water, int index, void *const *list)
{
    for (unsigned i = 0; i < TC_POLYGONS + 2; i++) {
        uintptr_t value = (uintptr_t)list[i];
        size_t used = strlen(out);
        if (value == 0) {
            snprintf(out + used, size - used, "Z");
        } else if (value == 0xdedededeu) {
            snprintf(out + used, size - used, "X");
        } else {
            int found = -1;
            for (unsigned k = 0; k < g_tc.input_count[water][index]; k++) {
                if ((uintptr_t)g_tc.input[water][index][k] == value) {
                    found = (int)k;
                }
            }
            if (found >= 0) {
                snprintf(out + used, size - used, "i%d", found);
            } else {
                snprintf(out + used, size - used, "n%x", (unsigned)value);
            }
        }
        if (i + 1 < TC_POLYGONS + 2) {
            used = strlen(out);
            snprintf(out + used, size - used, ",");
        }
    }
}

static uint32_t tc_hash(const void *data, size_t bytes)
{
    const uint32_t *words = (const uint32_t *)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < bytes / 4; i++) {
        hash = (hash ^ words[i]) * 16777619u;
    }
    return hash;
}

/* What the call did to one array of render vertices: a hash of all of them, how many changed, the histogram of the outcode bits
 * and the first changed vertex. */
static void tc_report_vertices(const char *tag, const GETMRenderVertexLayout *vertices)
{
    unsigned changed = 0, first = TC_VERTICES, bits[6] = {0, 0, 0, 0, 0, 0}, other = 0;
    for (unsigned i = 0; i < TC_VERTICES; i++) {
        const uint32_t *w = (const uint32_t *)&vertices[i];
        if (w[0] != 0xa5a5a5a5u) {
            changed++;
            if (first == TC_VERTICES) {
                first = i;
            }
            for (int b = 0; b < 6; b++) {
                bits[b] += (vertices[i].clipFlags >> b) & 1;
            }
            other += (vertices[i].clipFlags >> 6) != 0;
        }
    }
    life_extra(" %s=%08x n=%u bits=%u/%u/%u/%u/%u/%u hi=%u", tag, (unsigned)tc_hash(vertices, TC_VERTICES * sizeof(GETMRenderVertexLayout)), changed, bits[0],
               bits[1], bits[2], bits[3], bits[4], bits[5], other);
    if (first < TC_VERTICES) {
        const uint32_t *w = (const uint32_t *)&vertices[first];
        life_extra(" v%u=%x,%x,%x,%x", first, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[8]);
    }
}

static void tc_report(void)
{
    GETerrainMeshLayout *mesh = g_tc.mesh;
    tc_report_vertices("t", mesh->renderVertices);
    tc_report_vertices("w", mesh->waterRenderVertices);
    /* every byte outside the two arrays is as it was */
    unsigned others = 0;
    const unsigned char *now = (const unsigned char *)mesh;
    for (size_t i = 0; i < sizeof *mesh; i++) {
        int inside = (i >= offsetof(GETerrainMeshLayout, renderVertices) && i < offsetof(GETerrainMeshLayout, renderVertices) + sizeof mesh->renderVertices) ||
                     (i >= offsetof(GETerrainMeshLayout, waterRenderVertices) && i < offsetof(GETerrainMeshLayout, waterRenderVertices) + sizeof mesh->waterRenderVertices);
        others += !inside && now[i] != g_tc.snapshot[i];
    }
    life_extra(" others=%u", others);
    char edge[256] = "";
    for (int i = 0; i < TC_EDGE; i++) {
        const uint32_t *w = (const uint32_t *)&g_tc.edge_vertices[i];
        size_t used = strlen(edge);
        snprintf(edge + used, sizeof edge - used, "%s%x/%x/%x", i ? "," : "", (unsigned)w[0], (unsigned)w[1], (unsigned)w[2]);
    }
    life_extra(" edge=%s", edge);
    for (int water = 0; water < 2; water++) {
        for (int index = 0; index < TC_MATERIALS; index++) {
            GETMMaterialLayout *material = water ? &g_tc.water_materials[index] : &g_tc.materials[index];
            StdVectorLayout *vector = water ? &mesh->waterMaterials : &mesh->materials;
            if (!vector->first || (char *)vector->last - (char *)vector->first <= (long)(index * sizeof(GETMMaterialLayout))) {
                continue;
            }
            char inside[256] = "", overlap[256] = "";
            tc_list(inside, sizeof inside, water, index, (void *const *)material->polygons.first);
            tc_list(overlap, sizeof overlap, water, index, (void *const *)material->overlayPolygons.first);
            life_extra(" %s%d[P:%s|O:%s]", water ? "W" : "M", index, inside, overlap);
        }
    }
}

#define TC_STEP(name, time, water, ortho, ...)                                                                                              \
    do {                                                                                                                                    \
        LS(name, LIFE_NODIFF, {                                                                                                             \
            __VA_ARGS__;                                                                                                                    \
            memcpy(g_tc.snapshot, g_tc.mesh, sizeof(GETerrainMeshLayout));                                                                  \
            ((TransformFn)LIFE_SLOT(TransformToCamera))(g_life.obj, g_tc.mesh, g_tc.viewport, (time), (water), (ortho));                    \
            tc_report();                                                                                                                    \
        });                                                                                                                                 \
    } while (0)

static void tc_materials_all(void)
{
    tc_material(0, 0, 0, TC_POLYGONS, 0);
    tc_material(0, 1, 3, 5, 0);
    tc_material(0, 2, 0, 0, 0);
    tc_material(0, 3, 4, 3, 1);
    tc_set_materials(0, TC_MATERIALS);
    tc_material(1, 0, 6, TC_POLYGONS, 0);
    tc_material(1, 1, 2, 4, 0);
    tc_set_materials(1, 2);
}

static void tc_steps(void)
{
    GETerrainMeshLayout *mesh;
    tc_ripple(0.75f, 0.5f, 0.25f);

    tc_world(1, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain with the viewport's planes", 0.033f, 0, 0);
    tc_world(2, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain and the water with the viewport's planes", 0.033f, 1, 0);
    tc_world(3, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain with an orthographic viewport", 0.033f, 0, 1);
    tc_world(4, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain and the water with an orthographic viewport", 0.033f, 1, 1);

    /* the phase advances on every call: the same world again gives other heights of the ripple */
    tc_world(1, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera again, the ripple has moved on", 0.5f, 0, 0);
    tc_world(2, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water again, the ripple has moved on", 0.125f, 1, 0);
    tc_world(3, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera orthographic again, the ripple has moved on", 0.25f, 0, 1);
    tc_world(4, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water orthographic again, the ripple has moved on", 1.5f, 1, 1);

    /* different ripple settings */
    tc_ripple(-1.25f, 2.0f, 1.0f);
    tc_world(5, 0, 0, 4, 3, 0, 4);
    TC_STEP("terrain: TransformToCamera with other ripple settings", 0.1f, 1, 0);
    tc_world(5, 0, 0, 4, 3, 0, 4);
    TC_STEP("terrain: TransformToCamera orthographic with other ripple settings", 0.1f, 1, 1);
    tc_ripple(0.75f, 0.5f, 0.25f);

    /* the early returns: no visible row, a still ripple or no visible water on a water pass; a terrain pass always goes on */
    tc_world(6, 3, 2, 6, 5, 0, 3);
    mesh = g_tc.mesh;
    TC_STEP("terrain: TransformToCamera without visible rows does nothing", 0.5f, 0, 0, mesh->visibleRows.last = mesh->visibleRows.first);
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water with a still ripple does nothing", 0.5f, 1, 0, tc_ripple(0.0f, 0.5f, 0.25f));
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain with a still ripple goes on", 0.5f, 0, 0, tc_ripple(0.0f, 0.5f, 0.25f));
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water without visible water does nothing", 0.5f, 1, 0, tc_ripple(0.75f, 0.5f, 0.25f);
            g_tc.mesh->waterVisiblePercentage = 0.0f);
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the terrain without visible water goes on", 0.5f, 0, 0, g_tc.mesh->waterVisiblePercentage = 0.0f);
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water with a ripple that is not a number does nothing", 0.5f, 1, 0, tc_ripple(__builtin_nanf(""), 0.5f, 0.25f));
    tc_world(6, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera of the water with water that is not a number does nothing", 0.5f, 1, 1, tc_ripple(0.75f, 0.5f, 0.25f);
            g_tc.mesh->waterVisiblePercentage = __builtin_nanf(""));
    tc_ripple(0.75f, 0.5f, 0.25f);

    /* the largest window the arrays hold, with some heights that are not numbers */
    tc_world(7, 0, 0, 99, 99, 37, 2);
    TC_STEP("terrain: TransformToCamera of the whole grid with heights that are not numbers", 0.033f, 0, 0);
    tc_world(7, 0, 0, 99, 99, 37, 2);
    TC_STEP("terrain: TransformToCamera of the whole grid and the water", 0.033f, 1, 0);
    tc_world(8, 0, 0, 99, 99, 41, 2);
    TC_STEP("terrain: TransformToCamera orthographic of the whole grid with heights that are not numbers", 0.033f, 0, 1);
    tc_world(8, 0, 0, 99, 99, 41, 2);
    TC_STEP("terrain: TransformToCamera orthographic of the whole grid and the water", 0.033f, 1, 1);
    tc_world(9, 10, 20, 0, 0, 0, 3);
    TC_STEP("terrain: TransformToCamera of a window of 2 x 2 points", 0.033f, 1, 0);
    tc_world(9, 10, 20, 0, 0, 0, 3);
    TC_STEP("terrain: TransformToCamera orthographic of a window of 2 x 2 points", 0.033f, 1, 1);

    /* the viewport's clip distances and rectangle decide the outcodes */
    tc_world(10, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera with a far front plane", 0.033f, 0, 0, g_tc.viewport->frontClip = 30.0f; g_tc.viewport->rearClip = 31.0f);
    tc_world(10, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera orthographic with a far front plane", 0.033f, 1, 1, g_tc.viewport->frontClip = 30.0f; g_tc.viewport->rearClip = 31.0f);
    tc_world(10, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera with a wide field of view", 0.033f, 1, 0, g_tc.viewport->fieldOfView = 2.5f; g_tc.viewport->halfHeight = 3.0f);
    tc_world(10, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera with a half width of exactly one", 0.033f, 0, 0, g_tc.viewport->halfWidth = 1.0f);
    tc_world(10, 3, 2, 6, 5, 0, 3);
    TC_STEP("terrain: TransformToCamera orthographic with another clipping scale", 0.033f, 1, 1, g_tc.viewport->yClippingPlane = -1.75f;
            ((float *)g_tc.viewport)[1] = 15.0f; ((float *)g_tc.viewport)[2] = -5.0f);

    /* the polygon lists */
    tc_world(11, 3, 2, 2, 2, 0, 1);
    tc_materials_all();
    TC_STEP("terrain: TransformToCamera sorts the polygons of the terrain and the water materials", 0.033f, 0, 0);
    tc_world(11, 3, 2, 2, 2, 0, 1);
    tc_materials_all();
    TC_STEP("terrain: TransformToCamera sorts the polygons on a water pass", 0.033f, 1, 1);
    tc_world(12, 3, 2, 2, 2, 0, 1);
    tc_material(0, 0, 0, TC_POLYGONS, 0);
    tc_set_materials(0, 1);
    tc_set_materials(1, 0);
    TC_STEP("terrain: TransformToCamera without water materials", 0.033f, 0, 1);
    tc_world(12, 3, 2, 2, 2, 0, 1);
    tc_set_materials(0, 0);
    tc_material(1, 0, 8, 6, 0);
    tc_set_materials(1, 1);
    TC_STEP("terrain: TransformToCamera without terrain materials", 0.033f, 1, 0);
}

static void check_terrain(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 1, 0, "the terrain transform")) {
        return;
    }
    memset(&g_tc, 0, sizeof g_tc);
    g_tc.snapshot = (unsigned char *)malloc(sizeof(GETerrainMeshLayout) + 64);
    tc_steps();
    life_end();
}
