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
    life_end();
}
