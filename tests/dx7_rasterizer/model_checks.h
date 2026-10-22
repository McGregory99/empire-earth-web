/*
 * The model draws of DX7Rasterizer (task P2.6.7h): the four vertex copies DrawModelHierarchy uses (CopyStdVertices, CopyStdColorVertices,
 * CopyAndSmoothStdVertices and CopyAndSmoothStdColorVertices, protected members called through the names the DLLs export them under),
 * then slot 75 DrawModelHierarchy and the slots 19 (Draw(GEModel *)) and 24 (DrawSky) that call it.  Included by screen_checks.h after
 * terrain_checks.h and run from the screen probe; same method as lifecycle_checks.h (literals of model_expected.inc recorded from the
 * original DLL with EEWEB_LIFE_RECORD=1).
 *
 * A copy takes a model (its vertex arrays, current and next frame, fraction between them), a material (first vertex and count) and a
 * destination in the ring (36-byte vertices), and writes the vertices of the material: the position and the normal, then the colour
 * and the texture coordinate; the smoothing variants blend the two frames in extended precision, ((1 - f) * current + f * next).
 */

typedef void(THISCALL *ModelCopyFn)(void *self, void *model, void *material, void *destination);

static struct {
    ModelCopyFn copy_std, copy_color, smooth_std, smooth_color;
    GEModelLayout *model;
    GEMaterialLayout material;
    void *arrays[4];
    unsigned char destination[36 * 12];
} g_mc;

static void mc_floats(void *array, unsigned bytes, uint32_t seed)
{
    uint32_t *words = (uint32_t *)(uintptr_t)((uint32_t *)array)[1];
    uint32_t state = seed;
    for (unsigned i = 0; i < bytes / 4; i++) {
        state = state * 1664525u + 1013904223u;
        /* finite floats of many magnitudes and both signs, with a few exact zeros and ones */
        uint32_t exponent = 100 + ((state >> 24) % 55);
        uint32_t bits = (state & 0x007fffffu) | (exponent << 23) | ((state & 0x00800000u) << 8);
        if ((state >> 16) % 29 == 0) {
            bits = 0;
        } else if ((state >> 16) % 31 == 0) {
            bits = 0x3f800000u;
        }
        words[i] = bits;
    }
}

static void mc_model(unsigned kind, unsigned vertices)
{
    for (int i = 0; i < 4; i++) {
        g_mc.arrays[i] = arr_new_vertices(kind, vertices);
        mc_floats(g_mc.arrays[i], vertices * (kind == 0 ? 32 : 36), 7u + 13u * (unsigned)i + kind);
    }
    free(g_mc.model);
    g_mc.model = (GEModelLayout *)calloc(1, sizeof(GEModelLayout));
    g_mc.model->vertexArrays = (struct GEVertexArrayLayout **)g_mc.arrays;
    g_mc.model->currentFrame = 1;
    g_mc.model->nextFrame = 3;
}

#define MC_STEP(name, fn, start, length, fraction, ...)                                                              \
    do {                                                                                                             \
        memset(&g_mc.material, 0, sizeof g_mc.material);                                                             \
        g_mc.material.vertexStart = (start);                                                                         \
        g_mc.material.vertexLength = (length);                                                                       \
        g_mc.model->fractionalFramePosition = (fraction);                                                            \
        memset(g_mc.destination, 0xa5, sizeof g_mc.destination);                                                     \
        LS(name, LIFE_NODIFF, {                                                                                      \
            __VA_ARGS__;                                                                                             \
            (fn)(g_life.obj, g_mc.model, &g_mc.material, g_mc.destination);                                          \
            char text_[2048] = "";                                                                                   \
            arr_words(text_, sizeof text_, g_mc.destination, sizeof g_mc.destination);                               \
            life_extra("%s", text_);                                                                                 \
        });                                                                                                          \
    } while (0)

/* The same over many fractions and starts, as a hash: the blend is x87 code whose rounding matters. */
static void mc_sweep(const char *name, ModelCopyFn fn, unsigned kind)
{
    LS(name, LIFE_NODIFF, {
        uint32_t hash = 2166136261u;
        for (int k = 0; k < 160; k++) {
            mc_model(kind, 6);
            float fraction = -0.3f + 0.0089f * (float)k;
            if (k % 40 == 0) {
                fraction = 0.0f;
            } else if (k % 40 == 1) {
                fraction = 1.0f;
            }
            memset(&g_mc.material, 0, sizeof g_mc.material);
            g_mc.material.vertexStart = (unsigned)(k % 3);
            g_mc.material.vertexLength = 3;
            g_mc.model->fractionalFramePosition = fraction;
            memset(g_mc.destination, 0xa5, sizeof g_mc.destination);
            fn(g_life.obj, g_mc.model, &g_mc.material, g_mc.destination);
            const uint32_t *words = (const uint32_t *)g_mc.destination;
            for (unsigned w = 0; w < 27; w++) {
                hash = (hash ^ words[w]) * 16777619u;
            }
        }
        life_extra("hash=%08x", (unsigned)hash);
    });
}

static void mc_copies(void)
{
    mc_model(0, 10);
    MC_STEP("model: CopyStdVertices copies a range and adds the colour", g_mc.copy_std, 2, 5, 0.0f);
    MC_STEP("model: CopyStdVertices of no vertex", g_mc.copy_std, 4, 0, 0.0f);
    MC_STEP("model: CopyStdVertices of the whole array", g_mc.copy_std, 0, 10, 0.0f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction of 0.25", g_mc.smooth_std, 2, 5, 0.25f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction of 0", g_mc.smooth_std, 2, 5, 0.0f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction of 1", g_mc.smooth_std, 2, 5, 1.0f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction of 0.3333", g_mc.smooth_std, 1, 3, 0.3333f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction above 1", g_mc.smooth_std, 1, 3, 1.75f);
    MC_STEP("model: CopyAndSmoothStdVertices at a negative fraction", g_mc.smooth_std, 1, 3, -0.5f);
    MC_STEP("model: CopyAndSmoothStdVertices at a fraction that is not a number", g_mc.smooth_std, 1, 3, __builtin_nanf(""));
    MC_STEP("model: CopyAndSmoothStdVertices of no vertex", g_mc.smooth_std, 4, 0, 0.5f);
    mc_model(1, 10);
    MC_STEP("model: CopyStdColorVertices copies a range as it is", g_mc.copy_color, 2, 5, 0.0f);
    MC_STEP("model: CopyStdColorVertices of no vertex", g_mc.copy_color, 4, 0, 0.0f);
    MC_STEP("model: CopyStdColorVertices of the whole array", g_mc.copy_color, 0, 10, 0.0f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction of 0.25", g_mc.smooth_color, 2, 5, 0.25f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction of 0", g_mc.smooth_color, 2, 5, 0.0f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction of 1", g_mc.smooth_color, 2, 5, 1.0f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction of 0.3333", g_mc.smooth_color, 1, 3, 0.3333f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction above 1", g_mc.smooth_color, 1, 3, 1.75f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a negative fraction", g_mc.smooth_color, 1, 3, -0.5f);
    MC_STEP("model: CopyAndSmoothStdColorVertices at a fraction that is not a number", g_mc.smooth_color, 1, 3, __builtin_nanf(""));
    MC_STEP("model: CopyAndSmoothStdColorVertices of no vertex", g_mc.smooth_color, 4, 0, 0.5f);
    mc_sweep("model: CopyAndSmoothStdVertices over 160 fractions and ranges", g_mc.smooth_std, 0);
    mc_sweep("model: CopyAndSmoothStdColorVertices over 160 fractions and ranges", g_mc.smooth_color, 1);
}

/* ---- DrawModelHierarchy, Draw(GEModel *) and DrawSky ---- */

typedef long(THISCALL *ModelDrawFn)(void *self, void *model, unsigned long screen, float detail, unsigned char orthographic);
typedef void(THISCALL *MmFloat)(void *self, float value);
typedef void(THISCALL *MmFloat2)(void *self, float first, float second);
typedef void(THISCALL *MmPointer)(void *self, const void *argument);

#define MM_VERTICES 8
#define MM_MODELS 8

static struct {
    GEModelLayout *models[MM_MODELS];
    int count;
    GETextureLayout *textures[2][3];
    void *stack;
} g_mm;

static void *mm_lle(const char *name)
{
    return proc_address(GetModuleHandleA("Low-Level Engine.dll"), name);
}

/* The engine's own setters build the viewport of a screen: the planes, the matrices and the clip distances follow from them. */
static void mm_viewport(int screen)
{
    void *viewport = &life_state(screen)->viewport;
    static const float kOrthographic[4] = {100.0f, -100.0f, 100.0f, -100.0f}; /* the first and last are the vertical limits, the first the upper one */
    static const float kView[4] = {480.0f, 0.0f, 640.0f, 0.0f};
    GETransformationLayout camera;
    memset(&camera, 0, sizeof camera);
    camera.m[0] = camera.m[5] = camera.m[10] = camera.m[15] = 1.0f;
    MmPointer set_camera = (MmPointer)mm_lle("?SetCamera@GEViewport@@QAEXABVGETransformation@@@Z");
    MmPointer set_ortho = (MmPointer)mm_lle("?SetOrthographicRectangle@GEViewport@@QAEXABVGERectangle@@@Z");
    MmPointer set_view = (MmPointer)mm_lle("?SetViewRectangle@GEViewport@@QAEXABVGERectangle@@@Z");
    MmFloat set_fov = (MmFloat)mm_lle("?SetFieldOfView@GEViewport@@QAEXM@Z");
    MmFloat set_front = (MmFloat)mm_lle("?SetFrontClippingDistance@GEViewport@@QAEXM@Z");
    MmFloat set_rear = (MmFloat)mm_lle("?SetRearClippingDistance@GEViewport@@QAEXM@Z");
    MmFloat2 set_depth = (MmFloat2)mm_lle("?SetDepthRange@GEViewport@@QAEXMM@Z");
    CHECK(set_camera && set_ortho && set_view && set_fov && set_front && set_rear && set_depth, "%s: model: the engine's viewport setters exist", g_life.e->label);
    if (!set_camera || !set_ortho || !set_view || !set_fov || !set_front || !set_rear || !set_depth) {
        return;
    }
    set_ortho(viewport, kOrthographic);
    set_view(viewport, kView);
    set_camera(viewport, &camera);
    set_fov(viewport, 0.9f);
    set_front(viewport, 1.0f);
    set_rear(viewport, 2000.0f);
    set_depth(viewport, 0.0f, 1.0f);
}

typedef struct MmSpec {
    unsigned kind;      /* 0: standard vertex arrays, 1: colour vertex arrays */
    unsigned lods;
    float x, y, z;      /* where the model's graphics transform puts it */
    float scale, opacity, fraction;
    unsigned seed;
    unsigned single;    /* 1: the model draws only its first material */
} MmSpec;

/* A model of two materials: lod l draws polygons [0, 3 - l) of the first and [3, 6 - l*2) of the second (none of it at lod 2), the
 * first with a texture on screen 0, the second without. */
static GEModelLayout *mm_model(const MmSpec *spec)
{
    GEModelLayout *model = (GEModelLayout *)calloc(1, sizeof *model);
    model->vptr = vftable("GEModel");
    model->refCount = 1;
    model->graphicsTransform.m[0] = model->graphicsTransform.m[5] = model->graphicsTransform.m[10] = model->graphicsTransform.m[15] = 1.0f;
    model->graphicsTransform.m[3] = spec->x;
    model->graphicsTransform.m[7] = spec->y;
    model->graphicsTransform.m[11] = spec->z;
    model->graphicsScale = spec->scale;
    model->opacity = spec->opacity;
    model->fractionalFramePosition = spec->fraction;
    model->currentFrame = 1;
    model->nextFrame = 3;
    model->lodCount = spec->lods;
    model->materialCount = spec->single ? 1 : 2;
    GEBoundingBoxLayout *box = (GEBoundingBoxLayout *)calloc(4, sizeof *box);
    for (int i = 0; i < 4; i++) {
        box[i].bottomLeftFront.x = box[i].bottomLeftFront.y = box[i].bottomLeftFront.z = -1.0f - 0.25f * (float)i;
        box[i].topRightBack.x = box[i].topRightBack.y = box[i].topRightBack.z = 1.0f + 0.25f * (float)i;
        box[i].width = box[i].height = box[i].depth = 2.0f + 0.5f * (float)i;
    }
    model->boundingBoxes = box;
    model->graphicsBoundingBox = box + 1;
    float *radii = (float *)calloc(8, sizeof(float));
    for (int i = 0; i < 8; i++) {
        radii[i] = 1.75f;
    }
    model->boundingRadii = radii;
    model->polygonArrays = (struct GEPolygonArrayLayout **)calloc(spec->lods, sizeof(void *));
    for (unsigned lod = 0; lod < spec->lods; lod++) {
        void *polygons = arr_new_poly(6);
        for (unsigned long i = 0; i < 6; i++) {
            ((ArrSet4)PA_SLOT(polygons, SetVertexIndices))(polygons, i, (i + lod) % MM_VERTICES, (i + 1 + lod) % MM_VERTICES, (i + 3) % MM_VERTICES);
        }
        model->polygonArrays[lod] = (struct GEPolygonArrayLayout *)polygons;
    }
    model->vertexArrays = (struct GEVertexArrayLayout **)calloc(4, sizeof(void *));
    for (int frame = 0; frame < 4; frame++) {
        void *array = arr_new_vertices(spec->kind, MM_VERTICES);
        mc_floats(array, MM_VERTICES * (spec->kind == 0 ? 32 : 36), spec->seed + 5u * (unsigned)frame);
        model->vertexArrays[frame] = (struct GEVertexArrayLayout *)array;
    }
    model->materials = (struct GEMaterialLayout **)calloc(2, sizeof(void *));
    for (int m = 0; m < 2; m++) {
        GEMaterialLayout *material = (GEMaterialLayout *)calloc(1, sizeof *material);
        material->vptr = vftable("GEMaterial");
        material->refCount = 1;
        material->color.r = 0.9f - 0.3f * (float)m;
        material->color.g = 0.5f + 0.125f * (float)m;
        material->color.b = 0.25f;
        material->opacity = m ? 1.0f : 0.8f;
        material->textureCount = 2;
        material->textures = (struct GETextureLayout **)calloc(2, sizeof(void *));
        material->textures[0] = (struct GETextureLayout *)(m ? NULL : g_mm.textures[0][0]);
        material->textures[1] = (struct GETextureLayout *)g_mm.textures[1][m];
        material->lodCount = spec->lods;
        material->polygonStart = (uint32_t *)calloc(spec->lods, 4);
        material->polygonLength = (uint32_t *)calloc(spec->lods, 4);
        for (unsigned lod = 0; lod < spec->lods; lod++) {
            material->polygonStart[lod] = m ? 3 : 0;
            material->polygonLength[lod] = lod == 2 && m ? 0 : (m ? 3 - lod : 3 - lod);
        }
        material->vertexStart = (unsigned)m * 4;
        material->vertexLength = 4;
        model->materials[m] = (struct GEMaterialLayout *)material;
    }
    return model;
}

static void mm_children(GEModelLayout *parent, GEModelLayout *first, GEModelLayout *second)
{
    parent->subModels = (struct GEModelLayout **)calloc(2, sizeof(void *));
    parent->subModels[0] = first;
    parent->subModels[1] = second;
    parent->subModelCount = second ? 2 : 1;
}

static unsigned mm_stack_size(void)
{
    const uint32_t *vector = (const uint32_t *)g_mm.stack;
    return vector[1] ? (vector[2] - vector[1]) / 8 : 0;
}

/* Everything one step shows: the calls, the answer, the state words and the vertices the draw added to the ring. */
#define MM_STEP(name, slot_name, screen, cursor, model, detail, ortho, ...)                                                                \
    do {                                                                                                                                    \
        d2_prepare(screen, cursor);                                                                                                         \
        LS(name, LIFE_DIFF, {                                                                                                               \
            __VA_ARGS__;                                                                                                                    \
            ret = ((ModelDrawFn)LIFE_SLOT(slot_name))(g_life.obj, (model), (unsigned long)(screen), (detail), (ortho));                     \
            unsigned long after_ = life_record(screen)->vertexCursor;                                                                       \
            unsigned drawn_ = after_ >= (cursor) ? (unsigned)(after_ - (cursor)) : 0;                                                         \
            life_extra(" stack=%u after=%lu", mm_stack_size(), after_);                                                                     \
            char text_[4096] = "";                                                                                                          \
            if (after_ >= (cursor) && drawn_ <= 24) {                                                                                        \
                arr_words(text_, sizeof text_, g_d2.vb[screen] + (cursor) * 0x24, drawn_ * 0x24);                                          \
            } else if (after_ < (cursor) && after_ <= 24) {                                                                                  \
                /* the ring started again: the end of the old ring that was written, then the beginning of the new one */                    \
                arr_words(text_, sizeof text_, g_d2.vb[screen] + (cursor) * 0x24, (0x98a - (cursor) < 4 ? 0x98a - (cursor) : 4) * 0x24);    \
                life_extra(" restart");                                                                                                     \
                strcat(text_, "|");                                                                                                         \
                arr_words(text_, sizeof text_, g_d2.vb[screen], after_ * 0x24);                                                             \
            }                                                                                                                               \
            life_extra(" ring=%s", text_);                                                                                                  \
        });                                                                                                                                 \
    } while (0)

static void mm_draws(void)
{
    g_mm.stack = mm_lle("?sTransformStack@GERasterizer@@1V?$vector@UTransformStack@GERasterizer@@V?$allocator@UTransformStack@GERasterizer@@@std@@@std@@A");
    CHECK(g_mm.stack != NULL, "%s: model: the engine's transform stack exists", g_life.e->label);
    if (!g_mm.stack) {
        return;
    }
    mm_viewport(0);
    mm_viewport(1);
    g_mm.textures[0][0] = d2_texture(0, 1.0f, 0.0f, 0.0f);
    g_mm.textures[1][0] = d2_texture(1, 1.0f, 0.0f, 0.0f);
    g_mm.textures[1][1] = d2_texture(1, 1.0f, 0.0f, 0.0f);

    MmSpec colored = {1, 3, 0.0f, 30.0f, 0.0f, 1.0f, 1.0f, 0.0f, 100};
    MmSpec plain = {0, 3, 0.0f, 30.0f, 0.0f, 1.0f, 1.0f, 0.0f, 200};
    GEModelLayout *a = mm_model(&colored);
    MM_STEP("model: DrawModelHierarchy of a colour model in front of the camera", DrawModelHierarchy, 0, 0, a, 1.0f, 0, );
    GEModelLayout *b = mm_model(&plain);
    MM_STEP("model: DrawModelHierarchy of a standard model in front of the camera", DrawModelHierarchy, 0, 4, b, 1.0f, 0, );

    /* the level of detail comes from the size of the model on the screen, the viewport's width and the detail argument */
    MmSpec distances[6];
    const float kDistances[6] = {120.0f, 300.0f, 600.0f, 1000.0f, 1500.0f, 1900.0f};
    for (int i = 0; i < 6; i++) {
        distances[i] = colored;
        distances[i].y = kDistances[i];
        distances[i].seed = 300 + 10 * (unsigned)i;
    }
    GEModelLayout *far_models[6];
    for (int i = 0; i < 6; i++) {
        far_models[i] = mm_model(&distances[i]);
    }
    MM_STEP("model: DrawModelHierarchy of a model 120 away", DrawModelHierarchy, 0, 8, far_models[0], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away", DrawModelHierarchy, 0, 16, far_models[2], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 1900 away", DrawModelHierarchy, 0, 24, far_models[5], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 4", DrawModelHierarchy, 0, 32, far_models[2], 4.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 5", DrawModelHierarchy, 0, 36, far_models[2], 5.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 6", DrawModelHierarchy, 0, 40, far_models[2], 6.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 7", DrawModelHierarchy, 0, 44, far_models[2], 7.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 8", DrawModelHierarchy, 0, 52, far_models[2], 8.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 10", DrawModelHierarchy, 0, 60, far_models[2], 10.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 20", DrawModelHierarchy, 0, 48, far_models[2], 20.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 50", DrawModelHierarchy, 0, 56, far_models[2], 50.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 100", DrawModelHierarchy, 0, 64, far_models[2], 100.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 0.25", DrawModelHierarchy, 0, 100, far_models[2], 0.25f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail of 0", DrawModelHierarchy, 0, 108, far_models[2], 0.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a negative detail", DrawModelHierarchy, 0, 116, far_models[2], -2.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 600 away with a detail that is not a number", DrawModelHierarchy, 0, 124, far_models[2], __builtin_nanf(""), 0, );

    /* where the model is: at the edge of the view, behind the camera, beyond the rear plane */
    MmSpec spots[8];
    const float kX[8] = {5.0f, 11.0f, 14.0f, 18.0f, 26.0f, 60.0f, 0.0f, 0.0f};
    const float kY[8] = {30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, -50.0f, 5000.0f};
    GEModelLayout *spot_models[8];
    for (int i = 0; i < 8; i++) {
        spots[i] = colored;
        spots[i].x = kX[i];
        spots[i].y = kY[i];
        spots[i].seed = 400 + 10 * (unsigned)i;
        spot_models[i] = mm_model(&spots[i]);
    }
    MM_STEP("model: DrawModelHierarchy of a model 5 to the side", DrawModelHierarchy, 0, 132, spot_models[0], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 11 to the side", DrawModelHierarchy, 0, 140, spot_models[1], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 14 to the side", DrawModelHierarchy, 0, 148, spot_models[2], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 18 to the side", DrawModelHierarchy, 0, 156, spot_models[3], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 26 to the side", DrawModelHierarchy, 0, 164, spot_models[4], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model 60 to the side is not visible", DrawModelHierarchy, 0, 172, spot_models[5], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model behind the camera is not visible", DrawModelHierarchy, 0, 180, spot_models[6], 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model beyond the rear plane is not visible", DrawModelHierarchy, 0, 188, spot_models[7], 1.0f, 0, );
    MmSpec ortho_spots[8];
    const float kOx[8] = {0.0f, 0.0f, 30.0f, 0.0f, 90.0f, 150.0f, 0.0f, 0.0f};
    const float kOy[8] = {0.0f, 0.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const float kOz[8] = {0.0f, 30.0f, 0.0f, 30.0f, 0.0f, 0.0f, 90.0f, 150.0f};
    GEModelLayout *ortho_models[8];
    for (int i = 0; i < 8; i++) {
        ortho_spots[i] = colored;
        ortho_spots[i].x = kOx[i];
        ortho_spots[i].y = kOy[i];
        ortho_spots[i].z = kOz[i];
        ortho_spots[i].seed = 500 + 10 * (unsigned)i;
        ortho_models[i] = mm_model(&ortho_spots[i]);
    }
    for (int i = 0; i < 8; i++) {
        char name[160];
        snprintf(name, sizeof name, "model: DrawModelHierarchy orthographic of the model at (%g, %g, %g)", kOx[i], kOy[i], kOz[i]);
        MM_STEP(name, DrawModelHierarchy, 0, 196 + 8 * i, ortho_models[i], 1.0f, 1, );
    }

    /* a hierarchy: the transform and scale of a child are the parent's applied to its own; a child is drawn at the lowest detail */
    MmSpec parent = {1, 3, 0.0f, 40.0f, 0.0f, 1.0f, 1.0f, 0.0f, 600, 1};
    MmSpec child_a = {1, 3, 3.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 610, 1};
    MmSpec child_b = {0, 3, -3.0f, 0.0f, 2.0f, 2.0f, 0.75f, 0.0f, 620, 1};
    MmSpec grandchild = {0, 3, 1.0f, 1.0f, 1.0f, 1.5f, 1.0f, 0.4f, 630, 1};
    GEModelLayout *tree = mm_model(&parent);
    GEModelLayout *tree_a = mm_model(&child_a);
    GEModelLayout *tree_b = mm_model(&child_b);
    GEModelLayout *tree_g = mm_model(&grandchild);
    mm_children(tree, tree_a, tree_b);
    mm_children(tree_b, tree_g, NULL);
    MM_STEP("model: DrawModelHierarchy of a model with two children and a grandchild", DrawModelHierarchy, 0, 300, tree, 1.0f, 0, );
    MmSpec far_parent = parent;
    far_parent.y = 600.0f;
    far_parent.seed = 660;
    GEModelLayout *far_tree = mm_model(&far_parent);
    mm_children(far_tree, mm_model(&child_a), mm_model(&child_b));
    MM_STEP("model: DrawModelHierarchy of a hierarchy whose parent is drawn at a lower level and whose children are not", DrawModelHierarchy, 0, 340, far_tree, 20.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a hierarchy orthographic", DrawModelHierarchy, 0, 380, tree, 1.0f, 1, );
    MM_STEP("model: DrawModelHierarchy of a hierarchy on screen 1", DrawModelHierarchy, 1, 9, tree, 1.0f, 0, );
    MmSpec off = parent;
    off.x = 60.0f;
    off.seed = 640;
    MmSpec back = child_a;
    back.x = -58.0f;
    back.seed = 650;
    GEModelLayout *off_screen = mm_model(&off);
    mm_children(off_screen, mm_model(&back), NULL);
    MM_STEP("model: DrawModelHierarchy of a parent out of view still draws its children", DrawModelHierarchy, 0, 420, off_screen, 1.0f, 0, );

    /* what the answer is */
    MM_STEP("model: DrawModelHierarchy when the vertex buffer unlock fails the children are not drawn", DrawModelHierarchy, 0, 440, tree, 1.0f, 0,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER; g_life.dd->vb_script[FV_UNLOCK].times = 1);
    MM_STEP("model: DrawModelHierarchy while DrawIndexedPrimitiveVB is busy twice", DrawModelHierarchy, 0, 480, a, 1.0f, 0, d2_script(FD_DRAW_INDEXED, D2_BUSY, 2));
    MM_STEP("model: DrawModelHierarchy when DrawIndexedPrimitiveVB fails", DrawModelHierarchy, 0, 490, a, 1.0f, 0, d2_script(FD_DRAW_INDEXED, D2_OTHER, 1));
    MM_STEP("model: DrawModelHierarchy with the ring nearly full starts it again", DrawModelHierarchy, 0, 0x986, a, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy with room for the first material only", DrawModelHierarchy, 0, 0x984, a, 1.0f, 0, );

    /* what a model can be */
    MmSpec one_lod = colored;
    one_lod.lods = 1;
    one_lod.seed = 700;
    MmSpec two_lods = colored;
    two_lods.lods = 2;
    two_lods.seed = 710;
    MmSpec scaled = colored;
    scaled.scale = 2.0f;
    scaled.seed = 720;
    MmSpec faded = colored;
    faded.opacity = 0.5f;
    faded.seed = 730;
    MmSpec smooth_color = colored;
    smooth_color.fraction = 0.4f;
    smooth_color.seed = 740;
    MmSpec smooth_plain = plain;
    smooth_plain.fraction = 0.4f;
    smooth_plain.seed = 750;
    MmSpec nan_fraction = plain;
    nan_fraction.fraction = __builtin_nanf("");
    nan_fraction.seed = 760;
    MmSpec tiny = colored;
    tiny.scale = 0.0001f;
    tiny.seed = 770;
    GEModelLayout *m_one = mm_model(&one_lod);
    GEModelLayout *m_two = mm_model(&two_lods);
    GEModelLayout *m_scaled = mm_model(&scaled);
    GEModelLayout *m_faded = mm_model(&faded);
    GEModelLayout *m_smooth_color = mm_model(&smooth_color);
    GEModelLayout *m_smooth_plain = mm_model(&smooth_plain);
    GEModelLayout *m_nan = mm_model(&nan_fraction);
    GEModelLayout *m_tiny = mm_model(&tiny);
    MM_STEP("model: DrawModelHierarchy of a model with one level of detail", DrawModelHierarchy, 0, 500, m_one, 100.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model with two levels of detail", DrawModelHierarchy, 0, 510, m_two, 100.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model with two levels of detail at a middle detail", DrawModelHierarchy, 0, 520, m_two, 6.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model with a scale of 2", DrawModelHierarchy, 0, 530, m_scaled, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model with an opacity of one half", DrawModelHierarchy, 0, 540, m_faded, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a colour model between two frames", DrawModelHierarchy, 0, 550, m_smooth_color, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a standard model between two frames", DrawModelHierarchy, 0, 560, m_smooth_plain, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a standard model whose fraction is not a number", DrawModelHierarchy, 0, 570, m_nan, 1.0f, 0, );
    MM_STEP("model: DrawModelHierarchy of a model that is tiny on the screen", DrawModelHierarchy, 0, 580, m_tiny, 1.0f, 0, );
    GEModelLayout *no_material = mm_model(&parent);
    no_material->materialCount = 0;
    mm_children(no_material, mm_model(&child_a), NULL);
    MM_STEP("model: DrawModelHierarchy of a model with no material and a child", DrawModelHierarchy, 0, 590, no_material, 1.0f, 0, );

    /* Draw(GEModel *) sets the 3D projection and holds the critical section; DrawSky draws and answers 0 */
    MM_STEP("model: Draw(GEModel) sets the projection and draws", Draw_7, 0, 600, a, 1.0f, 0, );
    MM_STEP("model: Draw(GEModel) of a hierarchy orthographic", Draw_7, 0, 620, tree, 1.0f, 1, );
    MM_STEP("model: Draw(GEModel) on screen 1", Draw_7, 1, 40, b, 1.0f, 0, );
    MM_STEP("model: Draw(GEModel) answers the status of the vertex buffer unlock", Draw_7, 0, 660, tree, 1.0f, 0,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER; g_life.dd->vb_script[FV_UNLOCK].times = 1);
    MM_STEP("model: DrawSky draws like Draw(GEModel) and answers 0", DrawSky, 0, 700, a, 1.0f, 0, );
    MM_STEP("model: DrawSky answers 0 although the vertex buffer unlock fails", DrawSky, 0, 710, tree, 1.0f, 0,
            g_life.dd->vb_script[FV_UNLOCK].code = (long)D2_OTHER; g_life.dd->vb_script[FV_UNLOCK].times = 1);
}

static void check_model(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    void *copy_std = proc_address(h, "?CopyStdVertices@DX7Rasterizer@@IAEXPAVGEModel@@PAVGEMaterial@@PAUD3DVERTEX_STD_COLOR@@@Z");
    void *copy_color = proc_address(h, "?CopyStdColorVertices@DX7Rasterizer@@IAEXPAVGEModel@@PAVGEMaterial@@PAUD3DVERTEX_STD_COLOR@@@Z");
    void *smooth_std = proc_address(h, "?CopyAndSmoothStdVertices@DX7Rasterizer@@IAEXPAVGEModel@@PAVGEMaterial@@PAUD3DVERTEX_STD_COLOR@@@Z");
    void *smooth_color = proc_address(h, "?CopyAndSmoothStdColorVertices@DX7Rasterizer@@IAEXPAVGEModel@@PAVGEMaterial@@PAUD3DVERTEX_STD_COLOR@@@Z");
    CHECK(copy_std && copy_color && smooth_std && smooth_color, "%s: model: exports the four vertex copies", e->label);
    if (!copy_std || !copy_color || !smooth_std || !smooth_color || !life_begin(e, api, obj, 2, 0, "the model draws")) {
        return;
    }
    memset(&g_mc, 0, sizeof g_mc);
    memset(&g_arr, 0, sizeof g_arr);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    memcpy(&g_mc.copy_std, &copy_std, sizeof copy_std);
    memcpy(&g_mc.copy_color, &copy_color, sizeof copy_color);
    memcpy(&g_mc.smooth_std, &smooth_std, sizeof smooth_std);
    memcpy(&g_mc.smooth_color, &smooth_color, sizeof smooth_color);
    mc_copies();
    g_d2.vb[0] = ((FakeVB *)g_life.dd->vbs[0])->memory;
    g_d2.vb[1] = ((FakeVB *)g_life.dd->vbs[1])->memory;
    memset(&g_tex, 0, sizeof g_tex);
    memset(&g_mm, 0, sizeof g_mm);
    mm_draws();
    life_end();
}
