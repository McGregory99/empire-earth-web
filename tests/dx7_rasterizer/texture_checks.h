/*
 * Textures of DX7Rasterizer (task P2.6.6): SetTexture (slot 61) with the per-screen stage cache of the screen record,
 * CanDoCurrentState (slot 5), the texture-handle slots 72, 73, 76 and DestroyTextureHandle (74).  Included by screen_checks.h after lifecycle_checks.h and run from the screen probe.
 *
 * Same method as lifecycle_checks.h: two windowed screens are activated over the fake DirectDraw / Direct3D, every step logs the calls
 * the fakes saw (device SetTexture, SetTextureStageState, ValidateDevice, vertex buffer Lock and Unlock), the return value, the engine's
 * last native error and the 32-bit words the step changed in the engine's per-screen state, the screen record and the textures'
 * reference counts.  Both variants must produce the literals of texture_expected.inc, recorded from the original DLL
 * (EEWEB_LIFE_RECORD=1 prints them instead of checking); the steps where our build differs are in texture_expected_ours.inc.
 *
 * The textures are plain GETexture objects built here (the engine's own vftable, the layout of ge_layouts.h): the engine's
 * GETexture::Promote and GETexture::Dereference that SetTexture calls only touch the promoted flag, the reference count and (for a
 * texture that asks the manager to remove it, which these do not) the manager.  Their surface is a real DX7Surface of the variant
 * under test, made over the fake DirectDraw, so the DirectDraw surface the device is handed is one of the fake's (named fs<id>).
 */

#define TEX_SLOT_CREATE_HANDLE EEWEB_SLOT_GERasterizer_CreateTextureHandle /* 72, 73 and 76 are one function in the original (return 0) */
#define TEX_SLOT_DEMOTE EEWEB_SLOT_GERasterizer_DemoteTexture
#define TEX_SLOT_DESTROY_HANDLE EEWEB_SLOT_GERasterizer_DestroyTextureHandle
#define TEX_SLOT_PROMOTE EEWEB_SLOT_GERasterizer_PromoteTexture

typedef struct TexSpec {
    unsigned color, alpha, addressing, minFilter, magFilter, mipFilter;
    float bias;
} TexSpec;

static struct {
    GETextureLayout *t[24];
    int count;
} g_tex;

typedef unsigned char(THISCALL *TexCanDo)(void *self, void *texture, unsigned long screen);
typedef long(THISCALL *TexHandle)(void *self, void *texture);
typedef void(THISCALL *TexDestroy)(void *self, void *texture);

static GETextureLayout *tex_make(int screen, const TexSpec *spec)
{
    GESurfaceDescriptionLayout desc = make_desc(1, 1, 0, 64, 64, 32, 1, 0);
    long status = 0;
    void *surface = new_surface(g_life.screen[screen], NULL, g_life.dd, &desc, &status);
    GETextureLayout *t = (GETextureLayout *)calloc(1, sizeof *t);
    t->vptr = vftable("GETexture");
    t->refCount = 5; /* never reaches the count at which the engine would delete it or ask its manager */
    t->stage.texture = t;
    t->baseTexture = t;
    t->surface = (struct GESurfaceLayout *)surface;
    t->stage.colorMode = spec->color;
    t->stage.alphaMode = spec->alpha;
    t->stage.addressingMode = spec->addressing;
    t->stage.minFilter = spec->minFilter;
    t->stage.magFilter = spec->magFilter;
    t->stage.mipFilter = spec->mipFilter;
    t->stage.mipMapBias = spec->bias;
    life_add_ge(t);
    if (g_tex.count < 24) {
        g_tex.t[g_tex.count++] = t;
    }
    return t;
}

static void tex_report(void)
{
    for (int i = 0; i < g_tex.count; i++) {
        const GETextureLayout *t = g_tex.t[i];
        life_extra("%st%d=%lu/%u/%u", i ? " " : "", i, (unsigned long)t->refCount, (unsigned)t->hasChanged, (unsigned)t->isPromoted);
    }
}

static void tex_set(void *texture, int screen)
{
    g_life.cur = screen;
    ((VoidPU)LIFE_SLOT(SetTexture))(g_life.obj, texture, (unsigned long)screen);
}

/* A step that binds `texture` on `screen` and reports the textures' counts. */
#define TEX_SET(name, texture, screen)                                   \
    do {                                                                  \
        g_life.cur = (screen);                                            \
        LS(name, LIFE_DIFF, { tex_set((texture), (screen)); tex_report(); }); \
    } while (0)

static void tex_set_texture(void)
{
    static const TexSpec kPlain = {0, 0, 0, 0, 0, 0, 0.0f};
    GETextureLayout *t0 = tex_make(0, &kPlain);
    GETextureLayout *t1 = tex_make(0, &kPlain);
    TEX_SET("SetTexture(NULL) with nothing bound", NULL, 0);
    TEX_SET("SetTexture(T0) the first time", t0, 0);
    TEX_SET("SetTexture(T0) again: nothing changed", t0, 0);
    t0->hasChanged = 1;
    TEX_SET("SetTexture(T0) again after it changed", t0, 0);
    TEX_SET("SetTexture(T1): another surface, same states", t1, 0);

    /* Every state value of the stage, in the order of the tables: 0..3 are the engine's modes, 4 is outside them (it maps to zero). */
    static const TexSpec kSpecs[] = {
        {1, 1, 1, 1, 1, 1, 0.5f},  {2, 2, 2, 2, 2, 2, -1.5f}, {3, 3, 3, 3, 3, 3, 2.0f},  {4, 4, 4, 4, 4, 4, 0.0f},
        {0, 0, 0, 0, 0, 0, 0.0f},  {3, 0, 1, 2, 0, 1, 0.25f}, {0, 3, 2, 1, 0, 2, 0.25f}, {5, 9, 7, 6, 8, 3, 1.0f},
    };
    for (unsigned i = 0; i < sizeof kSpecs / sizeof kSpecs[0]; i++) {
        char name[64];
        snprintf(name, sizeof name, "SetTexture(a new texture with the states of row %u)", i);
        GETextureLayout *t = tex_make(0, &kSpecs[i]);
        TEX_SET(name, t, 0);
    }

    /* One state at a time on the bound texture: the engine marks the texture changed and binds it again. */
    GETextureLayout *t = g_tex.t[g_tex.count - 1];
    static const struct {
        const char *name;
        int field;
        unsigned value;
    } kMutations[] = {
        {"color mode 0 -> 1", 0, 1},  {"color mode 1 -> 2", 0, 2},   {"color mode 2 -> 3", 0, 3},   {"alpha mode -> 0", 1, 0},
        {"alpha mode -> 1", 1, 1},    {"alpha mode -> 3", 1, 3},     {"alpha mode -> 2", 1, 2},     {"addressing -> 3", 2, 3},
        {"addressing -> 0", 2, 0},    {"minification -> 2", 3, 2},   {"magnification -> 1", 4, 1}, {"mip filter -> 0", 5, 0},
    };
    for (unsigned i = 0; i < sizeof kMutations / sizeof kMutations[0]; i++) {
        unsigned *fields[] = {&t->stage.colorMode, &t->stage.alphaMode, &t->stage.addressingMode, &t->stage.minFilter, &t->stage.magFilter, &t->stage.mipFilter};
        *fields[kMutations[i].field] = kMutations[i].value;
        t->hasChanged = 1;
        char name[96];
        snprintf(name, sizeof name, "SetTexture(the same texture after: %s)", kMutations[i].name);
        TEX_SET(name, t, 0);
    }
    t->stage.mipMapBias = -3.5f;
    t->hasChanged = 1;
    TEX_SET("SetTexture(the same texture after: mip map bias -> -3.5)", t, 0);
    t->stage.mipMapBias = -3.5f;
    t->hasChanged = 1;
    TEX_SET("SetTexture(the same texture marked changed, nothing different)", t, 0);

    /* A texture bound on screen 0 and on screen 1, and a second screen's own caches. */
    GETextureLayout *t2 = tex_make(1, &kSpecs[1]);
    TEX_SET("SetTexture(T on screen 1)", t2, 1);
    TEX_SET("SetTexture(the screen 0 texture on screen 1)", t, 1);
    TEX_SET("SetTexture(NULL) on screen 1", NULL, 1);
    TEX_SET("SetTexture(NULL) on screen 1 again", NULL, 1);

    /* The bound texture is let go of with the engine's Dereference: a count of 2 or more goes down by one, a count of 1 stays. */
    GETextureLayout *low = tex_make(0, &kPlain);
    TEX_SET("SetTexture(a texture to be let go of)", low, 0);
    low->refCount = 1;
    TEX_SET("SetTexture(NULL) lets go of a texture with a count of 1", NULL, 0);
    TEX_SET("SetTexture(NULL) again", NULL, 0);
    GETextureLayout *low2 = tex_make(0, &kPlain);
    TEX_SET("SetTexture(a texture whose count will be 2)", low2, 0);
    low2->refCount = 2;
    TEX_SET("SetTexture(T0) lets go of a texture with a count of 2", t0, 0);
    TEX_SET("SetTexture(T0) after the same texture was bound before", t0, 0);

    /* The bound texture bound again, marked changed: the old reference is dropped before the new one is taken (the counts decide what
     * the engine's Dereference does). */
    GETextureLayout *again = tex_make(0, &kPlain);
    TEX_SET("SetTexture(a texture bound for the next steps)", again, 0);
    again->refCount = 1;
    again->hasChanged = 1;
    TEX_SET("SetTexture(the bound texture again, changed, with a count of 1)", again, 0);
    again->refCount = 2;
    again->hasChanged = 1;
    TEX_SET("SetTexture(the bound texture again, changed, with a count of 2)", again, 0);
    again->refCount = 3;
    again->hasChanged = 1;
    TEX_SET("SetTexture(the bound texture again, changed, with a count of 3)", again, 0);

    /* Direct3D refuses every call: the original ignores the results and still records what it sent. */
    GETextureLayout *refused = tex_make(0, &kSpecs[0]);
    g_life.dd->tex_fail = 100;
    TEX_SET("SetTexture while the device refuses everything", refused, 0);
    TEX_SET("SetTexture(NULL) while the device refuses everything", NULL, 0);
    g_life.dd->tex_fail = 0;

    /* Leave a texture bound on each screen: Deactivate unbinds them. */
    TEX_SET("SetTexture(T1) before Deactivate", t1, 0);
    TEX_SET("SetTexture(T on screen 1) before Deactivate", t2, 1);
}

static void tex_can_do(void)
{
    GETextureLayout *t0 = g_tex.t[0];
    typedef struct {
        const char *name;
        int screen;
        int useTexture;
        long result;
        unsigned long passes;
    } Case;
    static const Case kCases[] = {
        {"CanDoCurrentState(T0) on screen 0", 0, 1, 0, 1},
        {"CanDoCurrentState(T0) with several passes", 0, 1, 0, 3},
        {"CanDoCurrentState(NULL) on screen 0", 0, 0, 0, 1},
        {"CanDoCurrentState(T0) on screen 1", 1, 1, 0, 1},
        {"CanDoCurrentState(T0) refused: too many passes", 0, 1, (long)0x88760008, 0},
        {"CanDoCurrentState(T0) refused with another error", 1, 1, (long)0x887600b4, 2},
    };
    for (unsigned i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        const Case *c = &kCases[i];
        g_life.dd->validate_result = c->result;
        g_life.dd->validate_passes = c->passes;
        g_life.cur = c->screen;
        LS(c->name, LIFE_DIFF, {
            ret = ((TexCanDo)LIFE_SLOT(CanDoCurrentState))(g_life.obj, c->useTexture ? (void *)t0 : NULL, (unsigned long)c->screen);
            tex_report();
        });
    }
    g_life.dd->validate_result = 0;
}

static void tex_handles(void)
{
    GETextureLayout *t0 = g_tex.t[0];
    static const struct {
        const char *name;
        int slot;
    } kSlots[] = {
        {"texture handle slot 72 (CreateTextureHandle)", TEX_SLOT_CREATE_HANDLE},
        {"texture handle slot 73 (DemoteTexture)", TEX_SLOT_DEMOTE},
        {"texture handle slot 76 (PromoteTexture)", TEX_SLOT_PROMOTE},
    };
    g_life.cur = 0;
    for (unsigned i = 0; i < sizeof kSlots / sizeof kSlots[0]; i++) {
        char name[96];
        snprintf(name, sizeof name, "%s with a texture", kSlots[i].name);
        LS(name, LIFE_DIFF, {
            ret = ((TexHandle)slot(g_life.vtable, kSlots[i].slot))(g_life.obj, t0);
            tex_report();
        });
        snprintf(name, sizeof name, "%s with NULL", kSlots[i].name);
        LS(name, LIFE_DIFF, ret = ((TexHandle)slot(g_life.vtable, kSlots[i].slot))(g_life.obj, NULL));
    }
    LS("DestroyTextureHandle(T0)", LIFE_DIFF, {
        ((TexDestroy)slot(g_life.vtable, TEX_SLOT_DESTROY_HANDLE))(g_life.obj, t0);
        tex_report();
    });
    LS("DestroyTextureHandle(NULL)", LIFE_DIFF, ((TexDestroy)slot(g_life.vtable, TEX_SLOT_DESTROY_HANDLE))(g_life.obj, NULL));
}

/* Our build only: what the original dereferences without looking is refused, with nothing sent and nothing changed. */
static void tex_ours_only(void)
{
    static const TexSpec kPlain = {0, 0, 0, 0, 0, 0, 0.0f};
    GETextureLayout *good = tex_make(0, &kPlain);
    GETextureLayout *broken = (GETextureLayout *)calloc(1, sizeof *broken);
    broken->vptr = vftable("GETexture");
    broken->refCount = 5;
    broken->stage.texture = broken;
    broken->baseTexture = broken;
    broken->surface = (struct GESurfaceLayout *)calloc(1, 0x100); /* not a DX7Surface: no DirectDraw surface behind it */
    life_cur(0);
    GERScreenStateLayout *state = life_state(0);
    void *bound = state->currentTexture;
    uint32_t count = state->textureChangeCount;
    log_clear(g_life.dd);
    tex_set(broken, 0);
    CHECK(g_life.dd->log[0] == 0 && state->currentTexture == bound && state->textureChangeCount == count && broken->refCount == 5,
          "%s: textures: a texture whose surface is not a DX7Surface is refused without a call or a change (log \"%s\")", g_life.e->label,
          g_life.dd->log);
    tex_set(good, 7); /* no such screen */
    tex_set(NULL, 7);
    CHECK(g_life.dd->log[0] == 0 && good->refCount == 5, "%s: textures: a screen that does not exist is refused without a call or a change", g_life.e->label);
    unsigned char passes = ((TexCanDo)LIFE_SLOT(CanDoCurrentState))(g_life.obj, good, 7);
    CHECK(passes == 0 && g_life.dd->log[0] == 0 && good->refCount == 5, "%s: textures: CanDoCurrentState on a screen that does not exist says no (got %u)",
          g_life.e->label, (unsigned)passes);
    passes = ((TexCanDo)LIFE_SLOT(CanDoCurrentState))(g_life.obj, broken, 0);
    CHECK(passes == 0 && g_life.dd->log[0] == 0 && broken->refCount == 5,
          "%s: textures: CanDoCurrentState with a texture that has no DX7Surface says no and asks the device nothing (got %u, log \"%s\")", g_life.e->label,
          (unsigned)passes, g_life.dd->log);
}


static void check_textures(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    if (!life_begin(e, api, obj, 2, 0, "textures on two windowed screens")) {
        return;
    }
    memset(&g_tex, 0, sizeof g_tex);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    tex_set_texture();
    tex_can_do();
    tex_handles();
    if (e->ours) {
        tex_ours_only();
    }
    life_end();
}
