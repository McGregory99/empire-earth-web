/*
 * The device and scene lifecycle of DX7Rasterizer (task P2.6.5): ActivateScreen (and the TnL class's ActivateTheScreen and
 * CreateVertexBuffers it calls), Deactivate, SetRenderTarget, BeginScene, EndScene, Clear, ClearZBuffer and ShowScene.
 * Included by screen_checks.h after state_checks.h and run from the screen probe.
 *
 * What is compared is the original's behaviour, observed three ways at once after every step:
 *   - the calls the fakes saw, in order, with their arguments: DirectDraw (CreateSurface, QueryInterface, Release), the
 *     surfaces (AddAttachedSurface, GetAttachedSurface, Blt, Flip, Release), Direct3D (CreateDevice, CreateVertexBuffer,
 *     Release), the device (SetViewport, SetRenderState, SetTextureStageState, SetRenderTarget, BeginScene, EndScene, Clear,
 *     Release, and whatever the setters ActivateScreen calls send) and the vertex buffer (Release);
 *   - the return value and the engine's last recorded native error (ESRecordNativeError, read back with ESGetNativeError);
 *   - the 32-bit words the step changed in the rasterizer's base object (b), the engine's per-screen state (s), the plugin's
 *     screen record (r), the screen (c, only its texture manager and rasterizer index) and the texture manager (t).  A word
 *     that holds the address of an object the test knows is printed as its name (scr0, tm0, win0, dev0, d3d0, vb0, ge0, ...), so
 *     the heap addresses of the two variants do not matter, and the reference counts of the surfaces the engine holds.
 * Both variants must produce the literal strings of lifecycle_expected.inc, which were recorded from the original DLL
 * (EEWEB_LIFE_RECORD=1 prints them instead of checking).
 */

typedef struct LifeSnap {
    uint32_t base[0x7c / 4], state[sizeof(GERScreenStateLayout) / 4], record[sizeof(DX7ScreenRecordLayout) / 4], tm[0x1c / 4], scr[2];
    unsigned long refs[48];
} LifeSnap;

#define LIFE_DIFF 1
#define LIFE_NODIFF 0

static struct {
    const struct Expect *e;
    const ScreenApi *api;
    FakeDD *dd;
    void *obj;
    void *const *vtable;
    int screens;
    void *screen[2];
    void *tm[2];
    void *win[2];
    int cur; /* the screen whose records the diff follows */
    int check_leaks; /* life_end asserts that our build leaves nothing created unreleased (the original leaks on its failure paths) */
    int recording;
    int fullscreen;
    void *named[128];
    char names[128][12];
    int named_count;
    void *ge[48]; /* the surfaces of the engine's per-screen state and the ones the tests make, in order of appearance */
    int ge_count;
    char extra[24576];
    char label[96];
    LifeSnap before;
} g_life;

typedef long(THISCALL *LifeActivate)(void *self, unsigned long screen, void *tm, void *window, unsigned long backBuffers, unsigned char zbuffer,
                                     unsigned long zformat);
typedef long(THISCALL *LongPU)(void *self, void *p, unsigned long a);
typedef long(THISCALL *LifeNoArg)(void *self);
typedef long(THISCALL *LifeClear)(void *self, const void *color, const void *rect, unsigned long screen);
typedef long(THISCALL *LifeClearZ)(void *self, float z, const void *rect, unsigned long screen);

#define LIFE_SLOT(name) slot(g_life.vtable, EEWEB_SLOT_GERasterizer_##name)

static DX7ScreenRecordLayout *life_record(int i)
{
    return ((DX7ScreenRecordLayout **)((DX7RasterizerLayout *)g_life.obj)->screenRecords.first)[i];
}

static GERScreenStateLayout *life_state(int i)
{
    return ((GERScreenStateLayout **)((DX7RasterizerLayout *)g_life.obj)->base.screens.first)[i];
}

static void life_add_name(void *p, const char *fmt, int n)
{
    if (p && g_life.named_count < 128) {
        g_life.named[g_life.named_count] = p;
        snprintf(g_life.names[g_life.named_count], sizeof g_life.names[0], fmt, n);
        g_life.named_count++;
    }
}

static void life_add_ge(void *p)
{
    if (!p) {
        return;
    }
    for (int i = 0; i < g_life.ge_count; i++) {
        if (g_life.ge[i] == p) {
            return;
        }
    }
    if (g_life.ge_count < 48) {
        g_life.ge[g_life.ge_count++] = p;
    }
}

/* Register what the state of the current screen points to, then rebuild the table of names. */
static void life_names_refresh(void)
{
    const StdVectorLayout *states = &((DX7RasterizerLayout *)g_life.obj)->base.screens;
    if (states->first && (char *)states->last - (char *)states->first > 4 * g_life.cur) {
        GERScreenStateLayout *state = life_state(g_life.cur);
        life_add_ge(state->primarySurface);
        life_add_ge(state->secondarySurface);
        life_add_ge(state->renderTarget);
    }
    g_life.named_count = 0;
    for (int i = 0; i < g_life.screens; i++) {
        life_add_name(g_life.screen[i], "scr%d", i);
        life_add_name(g_life.tm[i], "tm%d", i);
        life_add_name(g_life.win[i], "win%d", i);
    }
    for (int i = 0; i < g_life.dd->device_count; i++) {
        life_add_name(g_life.dd->devices[i], "dev%d", i);
    }
    for (int i = 0; i < g_life.dd->d3d_count; i++) {
        life_add_name(g_life.dd->d3ds[i], "d3d%d", i);
    }
    for (int i = 0; i < g_life.dd->vb_count; i++) {
        life_add_name(g_life.dd->vbs[i], "vb%d", i);
    }
    for (int i = 0; i < g_life.ge_count; i++) {
        life_add_name(g_life.ge[i], "ge%d", i);
    }
    /* the DirectDraw surfaces behind the engine's surfaces: what a texture bind hands to the device (stage cache word 0) */
    for (int id = 1; id <= g_life.dd->surface_count && id < 64; id++) {
        life_add_name(g_life.dd->by_id[id], "fs%d", id);
    }
}

static const char *life_label(uint32_t word, char *buf, size_t size)
{
    if (word) {
        for (int i = 0; i < g_life.named_count; i++) {
            if ((uint32_t)(uintptr_t)g_life.named[i] == word) {
                return g_life.names[i];
            }
        }
    }
    snprintf(buf, size, "%x", (unsigned)word);
    return buf;
}

static void life_snapshot(LifeSnap *snap)
{
    memcpy(snap->base, g_life.obj, sizeof snap->base);
    memcpy(snap->state, life_state(g_life.cur), sizeof snap->state);
    memcpy(snap->record, life_record(g_life.cur), sizeof snap->record);
    memcpy(snap->tm, g_life.tm[g_life.cur], sizeof snap->tm);
    memcpy(snap->scr, (const char *)g_life.screen[g_life.cur] + 4, sizeof snap->scr);
    for (int i = 0; i < g_life.ge_count; i++) {
        snap->refs[i] = ((const unsigned long *)g_life.ge[i])[1];
    }
}

static void life_diff_words(char *out, size_t size, char tag, const uint32_t *before, const uint32_t *after, unsigned count, unsigned first)
{
    for (unsigned i = first; i < count; i++) {
        if (before[i] != after[i]) {
            char buf[16];
            size_t used = strlen(out);
            snprintf(out + used, size - used, " %c%x=%s", tag, i * 4, life_label(after[i], buf, sizeof buf));
        }
    }
}

static void life_step_begin(int with_diff)
{
    log_clear(g_life.dd);
    g_life.extra[0] = 0;
    if (with_diff) {
        life_names_refresh();
        life_snapshot(&g_life.before);
    }
}

static void life_extra(const char *fmt, ...)
{
    va_list ap;
    size_t used = strlen(g_life.extra);
    va_start(ap, fmt);
    vsnprintf(g_life.extra + used, sizeof g_life.extra - used, fmt, ap);
    va_end(ap);
}

static const struct {
    const char *name;
    const char *expect;
} kLifeExpected[] = {
#include "lifecycle_expected.inc"
#include "texture_expected.inc"
#include "ring_expected.inc"
#include "array_expected.inc"
#include "draw2d_expected.inc"
#include "poly_expected.inc"
#include "line3d_expected.inc"
#include "text_expected.inc"
#include "terrain_expected.inc"
#include "model_expected.inc"
#include "terrain_draw_expected.inc"
#include "target_expected.inc"
};

/* The steps where our build intentionally differs from the original: the original leaves the surfaces and the Direct3D object of a
 * failed activation unreleased, ours gives them back (the extra Release calls below); and the steps the original cannot run at all. */
static const struct {
    const char *name;
    const char *expect;
} kLifeExpectedOurs[] = {
#include "lifecycle_expected_ours.inc"
#include "texture_expected_ours.inc"
#include "ring_expected_ours.inc"
#include "array_expected_ours.inc"
#include "draw2d_expected_ours.inc"
#include "poly_expected_ours.inc"
#include "line3d_expected_ours.inc"
#include "text_expected_ours.inc"
#include "terrain_expected_ours.inc"
#include "model_expected_ours.inc"
#include "terrain_draw_expected_ours.inc"
#include "target_expected_ours.inc"
    {NULL, NULL}};

static void life_compare(const char *name, const char *actual)
{
    if (g_life.recording) {
        printf("REC\t%s\t%s\n", name, actual);
        return;
    }
    const char *expect = NULL;
    for (unsigned i = 0; i < sizeof kLifeExpected / sizeof kLifeExpected[0]; i++) {
        if (strcmp(kLifeExpected[i].name, name) == 0) {
            expect = kLifeExpected[i].expect;
        }
    }
    for (unsigned i = 0; g_life.e->ours && kLifeExpectedOurs[i].name; i++) {
        if (strcmp(kLifeExpectedOurs[i].name, name) == 0) {
            expect = kLifeExpectedOurs[i].expect;
        }
    }
    int same = expect != NULL && strcmp(actual, expect) == 0;
    CHECK(same, "%s: lifecycle: %s", g_life.e->label, name);
    if (!same) {
        printf("     expected: %s\n     actual:   %s\n", expect ? expect : "(nothing recorded)", actual);
    }
}

static void life_step_end(const char *name, long ret, int with_diff)
{
    char actual[32768];
    const char *log = g_life.dd->log;
    size_t n = strlen(log);
    size_t used = 0;
    for (size_t i = 0; i < n && used + 1 < sizeof actual; i++) {
        actual[used++] = log[i] == '\n' ? ';' : log[i];
    }
    actual[used] = 0;
    snprintf(actual + used, sizeof actual - used, " | ret=%lx ne=%lx", ret, engine_native_error());
    if (with_diff) {
        LifeSnap after;
        life_names_refresh();
        life_snapshot(&after);
        life_diff_words(actual, sizeof actual, 'b', g_life.before.base, after.base, 0x7c / 4, 1);
        life_diff_words(actual, sizeof actual, 's', g_life.before.state, after.state, sizeof after.state / 4, 0);
        life_diff_words(actual, sizeof actual, 'r', g_life.before.record, after.record, sizeof after.record / 4, 0);
        life_diff_words(actual, sizeof actual, 'c', g_life.before.scr, after.scr, 2, 0);
        life_diff_words(actual, sizeof actual, 't', g_life.before.tm, after.tm, sizeof after.tm / 4, 0);
        for (int i = 0; i < g_life.ge_count; i++) {
            if (after.refs[i] != g_life.before.refs[i]) {
                size_t u = strlen(actual);
                snprintf(actual + u, sizeof actual - u, " ref(ge%d)=%lu", i, after.refs[i]);
            }
        }
    }
    if (g_life.extra[0]) {
        size_t u = strlen(actual);
        snprintf(actual + u, sizeof actual - u, " | %s", g_life.extra);
    }
    life_compare(name, actual);
}

/* One step: `stmt` sets `ret`.  The flags say whether the state words are compared as well as the calls. */
#define LS(name, flags, ...)                \
    do {                                    \
        long ret = 0;                       \
        life_step_begin(flags);             \
        __VA_ARGS__;                        \
        life_step_end(name, ret, flags);    \
    } while (0)

static void life_set_window(int i, float x, float y, unsigned width, unsigned height)
{
    uint32_t *words = (uint32_t *)g_life.win[i];
    memcpy(&words[1], &x, 4);
    memcpy(&words[2], &y, 4);
    words[3] = width;
    words[4] = height;
}

/* Screens over the fake DirectDraw, added to the rasterizer with the record caches and engine settings at known values. */
static int life_begin(const struct Expect *e, const ScreenApi *api, void *obj, int screens, int fullscreen, const char *label)
{
    memset(&g_life, 0, sizeof g_life);
    memset(&g_se, 0, sizeof g_se);
    g_life.e = e;
    g_life.api = api;
    g_life.obj = obj;
    g_life.vtable = *(void *const **)obj;
    g_life.recording = getenv("EEWEB_LIFE_RECORD") != NULL;
    g_life.dd = (FakeDD *)calloc(1, sizeof(FakeDD));
    fake_dd_init(g_life.dd);
    g_life.screens = screens;
    g_life.fullscreen = fullscreen;
    snprintf(g_life.label, sizeof g_life.label, "%s", label);
    g_se.e = e;
    g_se.obj = obj;
    g_se.vtable = g_life.vtable;
    g_se.dd = g_life.dd;
    g_se.screens = screens;
    typedef long(THISCALL * AddScreen)(void *self, void *screen);
    for (int i = 0; i < screens; i++) {
        g_life.screen[i] = make_screen(api, g_life.dd, "life screen", 1, 1);
        long added = ((AddScreen)slot(g_life.vtable, EEWEB_SLOT_GERasterizer_AddValidScreen))(obj, g_life.screen[i]);
        CHECK(added == 0, "%s: lifecycle: AddValidScreen(screen %d) succeeds (got %ld)", e->label, i, added);
        g_life.tm[i] = engine_new(0x1c);
        g_life.win[i] = engine_new(0x1c);
        /* A real (hidden) window: the engine reads the client origin from the operating system. */
        HWND hwnd = CreateWindowExA(0, "STATIC", "life", WS_POPUP, 37 + 63 * i, 51 + 149 * i, 640 + 160 * i, 480 + 120 * i, NULL, NULL, NULL, NULL);
        CHECK(hwnd != NULL, "%s: lifecycle: the window of screen %d exists", e->label, i);
        ((uint32_t *)g_life.win[i])[0] = (uint32_t)(uintptr_t)hwnd;
        life_set_window(i, 0.0f, 0.0f, 640 + 160 * (unsigned)i, 480 + 120 * (unsigned)i);
    }
    unsigned count = ((DX7RasterizerLayout *)obj)->screenRecords.first
                         ? (unsigned)(((char *)((DX7RasterizerLayout *)obj)->screenRecords.last - (char *)((DX7RasterizerLayout *)obj)->screenRecords.first) / 4)
                         : 0;
    CHECK(count == (unsigned)screens, "%s: lifecycle: the rasterizer keeps %d screen record(s) (got %u)", e->label, screens, count);
    if (count != (unsigned)screens) {
        return 0;
    }
    for (int i = 0; i < screens; i++) {
        DX7ScreenRecordLayout *record = life_record(i);
        void *screen = record->screen;
        memset(record, 0, sizeof *record); /* the original leaves most of the record unset; every field starts at 0 here */
        record->screen = (struct GEScreenLayout *)screen;
        record->sceneDrawn = 1;
    }
    state_reset_defaults();
    if (fullscreen) {
        typedef long(THISCALL * Capture)(void *self, const void *window, unsigned char fullscreen);
        for (int i = 0; i < screens; i++) {
            long captured = ((Capture)screen_slot(g_life.screen[i], EEWEB_SLOT_GEScreen_Capture))(g_life.screen[i], g_life.win[i], 1);
            CHECK(captured == 0, "%s: lifecycle: the fake screen %d is captured full screen (got %ld)", e->label, i, captured);
        }
    }
    /* The probes of the screens' constructors made surfaces and devices: the numbers of the logs start again from here. */
    memset(g_life.dd->by_id, 0, sizeof g_life.dd->by_id);
    g_life.dd->surface_count = 0;
    g_life.dd->device_count = g_life.dd->d3d_count = g_life.dd->vb_count = 0;
    g_life.dd->d3d_released = g_life.dd->device_released = g_life.dd->vb_released = 0;
    log_clear(g_life.dd);
    return 1;
}

/* The depth buffer the activation attached to the render target, as the engine sees it (GESurface slots). */
static void life_depth_report(void)
{
    void *target = life_state(g_life.cur)->secondarySurface;
    typedef void *(THISCALL * Attached)(void *self, unsigned long index);
    unsigned long count = target ? S_ULONG(target, EEWEB_SLOT_GESurface_GetAttachedSurfaceCount) : 0;
    void *depth = count ? ((Attached)SURF(target, EEWEB_SLOT_GESurface_GetAttachedSurface))(target, count - 1) : NULL;
    if (!depth) {
        life_extra("depth=none");
        return;
    }
    life_extra("depth=type%lu,format%lu,%lux%lu,%lubits", S_ULONG(depth, EEWEB_SLOT_GESurface_GetType), S_ULONG(depth, EEWEB_SLOT_GESurface_GetPixelFormat),
               S_ULONG(depth, EEWEB_SLOT_GESurface_GetWidth), S_ULONG(depth, EEWEB_SLOT_GESurface_GetHeight), S_ULONG(depth, EEWEB_SLOT_GESurface_GetPixelDepth));
}

static long life_activate(int screen, unsigned back_buffers, int zbuffer, unsigned zformat)
{
    g_life.cur = screen;
    return ((LifeActivate)LIFE_SLOT(ActivateScreen))(g_life.obj, (unsigned long)screen, g_life.tm[screen], g_life.win[screen], back_buffers,
                                                      (unsigned char)zbuffer, zformat);
}

/* What the fakes created and nobody released: surfaces with a reference left, Direct3D objects, devices and vertex buffers. */
static int life_leaks(void)
{
    const FakeDD *dd = g_life.dd;
    int leaks = (dd->d3d_count - dd->d3d_released) + (dd->device_count - dd->device_released) + (dd->vb_count - dd->vb_released);
    for (int id = 1; id <= dd->surface_count && id < 64; id++) {
        const FakeSurface *surface = (const FakeSurface *)dd->by_id[id];
        leaks += surface && surface->refs > 0;
    }
    return leaks;
}

/* Last, and without a diff: the screens and their records are gone afterwards. */
static void life_end(void)
{
    typedef long(THISCALL * Release)(void *self, const void *window);
    if (g_life.fullscreen) {
        for (int i = 0; i < g_life.screens; i++) {
            ((Release)screen_slot(g_life.screen[i], EEWEB_SLOT_GEScreen_Release))(g_life.screen[i], g_life.win[i]);
        }
    }
    life_names_refresh();
    for (int i = 0; i < g_life.screens; i++) {
        DestroyWindow((HWND)(uintptr_t)((uint32_t *)g_life.win[i])[0]);
    }
    char name[160];
    snprintf(name, sizeof name, "Deactivate after: %s", g_life.label);
    LS(name, LIFE_NODIFF, ret = ((LifeNoArg)LIFE_SLOT(Deactivate))(g_life.obj));
    if (g_life.check_leaks && g_life.e->ours) {
        int leaks = life_leaks();
        CHECK(leaks == 0, "%s: lifecycle: nothing is left unreleased after Deactivate: %s (%d leaked)", g_life.e->label, g_life.label, leaks);
    }
    free(g_life.dd);
}

/* The fake DirectDraw surface ids: a screen's activation creates the primary, the off-screen target and the depth buffer in this order,
 * so screen 0 owns s1, s2 and s3 and screen 1 s4, s5 and s6. */
#define LIFE_SCRIPT(id, method, error, count) SCRIPT(g_life.dd, id, method, error, count)

static void life_cur(int screen)
{
    g_life.cur = screen;
}

static void *life_new_target(int screen, unsigned width, unsigned height)
{
    GESurfaceDescriptionLayout desc = make_desc(1, 1, 0, width, height, 32, 1, 0);
    long status = 0;
    void *surface = new_surface(g_life.screen[screen], NULL, g_life.dd, &desc, &status);
    life_add_ge(surface);
    return surface;
}

static void life_rect(float *rect, float bottom, float left, float right, float top)
{
    rect[0] = bottom;
    rect[1] = left;
    rect[2] = right;
    rect[3] = top;
}

static void life_rect_words(const float *rect)
{
    uint32_t w[4];
    memcpy(w, rect, 16);
    life_extra("rect=%x,%x,%x,%x", (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
}

#define LIFE_ERR_BUSY 0x8876021cL /* DDERR_WASSTILLDRAWING */
#define LIFE_ERR_LOST 0x887601c2L /* DDERR_SURFACELOST */
#define LIFE_ERR_OTHER 0x88760064L

/* ---- activation -------------------------------------------------------------------------------------------- */

static void life_activation_windowed(const struct Expect *e, const ScreenApi *api, void *obj);
static void life_scene(void);

static void life_activation_windowed(const struct Expect *e, const ScreenApi *api, void *obj)
{
    if (!life_begin(e, api, obj, 2, 0, "two windowed screens with the scene calls and two lights")) {
        return;
    }
    /* A window of 640x480, an off-screen render target the size of the window and a depth buffer attached to it. */
    LS("ActivateScreen windowed, 1 off-screen target, z buffer 10", LIFE_DIFF, {
        ret = life_activate(0, 1, 1, 10);
        life_depth_report();
    });
    /* The second screen: another window size, depth format 9 (32 bits), the device calls again with its own prefix. */
    LS("ActivateScreen windowed screen 1, z buffer 9", LIFE_DIFF, {
        ret = life_activate(1, 1, 1, 9);
        life_depth_report();
    });
    life_scene();
    life_end();
}

/* Full screen: ShowScene flips the primary surface, with the engine's present flag. */
static void life_present_full_screen(void)
{
    life_cur(0);
    LS("ShowScene full screen", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    life_state(0)->presentFlag = 0;
    LS("ShowScene full screen without the present flag", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    life_state(0)->presentFlag = 1;
    LIFE_SCRIPT(1, FS_FLIP, LIFE_ERR_BUSY, 2);
    LS("ShowScene full screen while DirectDraw is still flipping", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    LIFE_SCRIPT(1, FS_FLIP, LIFE_ERR_LOST, 1);
    LS("ShowScene full screen with the surface lost", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    LIFE_SCRIPT(1, FS_FLIP, LIFE_ERR_OTHER, 1);
    LS("ShowScene full screen with another flip error", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    float rect[4] = {300, 0, 400, 0};
    float color[3] = {0.25f, 0.5f, 0.75f};
    LS("Clear full screen", LIFE_DIFF, ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, color, rect, 0));
    LS("BeginScene full screen", LIFE_DIFF, ret = ((LongPU)LIFE_SLOT(BeginScene))(g_life.obj, life_state(0)->secondarySurface, 0));
    LS("EndScene full screen", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
}

static void life_activation_variants(const struct Expect *e, const ScreenApi *api, void *obj)
{
    /* No depth buffer. */
    if (life_begin(e, api, obj, 1, 0, "windowed, no z buffer")) {
        LS("ActivateScreen windowed, no z buffer", LIFE_DIFF, ret = life_activate(0, 1, 0, 0));
        life_end();
    }
    /* Any depth buffer format (12), with the first attempt refused: the screen's own fallbacks (P2.6.2) run inside the activation. */
    if (life_begin(e, api, obj, 1, 0, "windowed, z buffer 12")) {
        g_life.dd->create_results[0] = 0;
        g_life.dd->create_results[1] = 0;
        g_life.dd->create_results[2] = (long)0x88760017;
        g_life.dd->create_result_count = 3;
        LS("ActivateScreen windowed, z buffer 12 with the first format refused", LIFE_DIFF, {
            ret = life_activate(0, 1, 1, 12);
            life_depth_report();
        });
        life_end();
    }
    /* Full screen: a flip chain of 2 back buffers, the render target is the first back buffer, the depth buffer is attached to it. */
    if (life_begin(e, api, obj, 1, 1, "full screen, 2 back buffers")) {
        LS("ActivateScreen full screen, 2 back buffers, z buffer 10", LIFE_DIFF, {
            ret = life_activate(0, 2, 1, 10);
            life_depth_report();
        });
        life_present_full_screen();
        life_end();
    }
    /* Full screen, 1 back buffer, no depth buffer. */
    if (life_begin(e, api, obj, 1, 1, "full screen, 1 back buffer")) {
        LS("ActivateScreen full screen, 1 back buffer, no z buffer", LIFE_DIFF, ret = life_activate(0, 1, 0, 0));
        life_end();
    }
}

static void life_activation_failures(const struct Expect *e, const ScreenApi *api, void *obj)
{
    /* The off-screen render target is refused: the activation answers with the screen's status and stops. */
    if (life_begin(e, api, obj, 1, 0, "the off-screen target is refused")) {
        g_life.check_leaks = 1;
        g_life.dd->create_results[0] = 0;
        g_life.dd->create_results[1] = (long)0x8876000a;
        g_life.dd->create_result_count = 2;
        LS("ActivateScreen: the off-screen target is refused", LIFE_DIFF, ret = life_activate(0, 1, 0, 0));
        life_end();
    }
    /* The depth buffer cannot be attached to the render target: its status is the activation's. */
    if (life_begin(e, api, obj, 1, 0, "the depth buffer cannot be attached")) {
        g_life.check_leaks = 1;
        g_life.dd->add_attached_code = (long)0x88760104;
        g_life.dd->add_attached_times = 1;
        LS("ActivateScreen: AddAttachedSurface of the depth buffer fails", LIFE_DIFF, ret = life_activate(0, 1, 1, 10));
        life_end();
    }
    /* The primary surface is refused but the off-screen target is made: the later status hides the failure. */
    if (life_begin(e, api, obj, 1, 0, "the primary surface is refused")) {
        g_life.check_leaks = 1;
        g_life.dd->create_results[0] = (long)0x8876000a;
        g_life.dd->create_results[1] = 0;
        g_life.dd->create_result_count = 2;
        LS("ActivateScreen: the primary surface is refused", LIFE_DIFF, ret = life_activate(0, 1, 0, 0));
        life_end();
    }
    /* (A refused depth buffer is not tested: the original attaches and releases a null surface and crashes.) */
    /* QueryInterface(IDirect3D7) fails. */
    if (life_begin(e, api, obj, 1, 0, "no IDirect3D7")) {
        g_life.check_leaks = 1;
        g_life.dd->qi_d3d_result = (long)0x80004002;
        LS("ActivateScreen: no IDirect3D7", LIFE_DIFF, ret = life_activate(0, 1, 1, 10));
        life_end();
    }
    /* CreateDevice fails: nothing of the device is kept, and the Direct3D object is not released. */
    if (life_begin(e, api, obj, 1, 0, "CreateDevice refused")) {
        g_life.check_leaks = 1;
        g_life.dd->create_device_result = (long)0x887600a2;
        LS("ActivateScreen: CreateDevice refused", LIFE_DIFF, ret = life_activate(0, 1, 1, 10));
        life_end();
    }
    /* CreateVertexBuffer fails: the device is kept (and released by Deactivate), the vertex buffer is not. */
    if (life_begin(e, api, obj, 1, 0, "CreateVertexBuffer refused")) {
        g_life.check_leaks = 1;
        g_life.dd->create_vb_result = (long)0x8876017c;
        LS("ActivateScreen: CreateVertexBuffer refused", LIFE_DIFF, ret = life_activate(0, 1, 1, 10));
        life_end();
    }
    /* Our build only (the original crashes on a window that is not there): no window for the off-screen target, and an activation of
     * a screen that is already active.  Both must leave nothing unreleased. */
    if (e->ours && life_begin(e, api, obj, 1, 0, "no window")) {
        g_life.check_leaks = 1;
        typedef long(THISCALL * Activate)(void *self, unsigned long screen, void *tm, void *window, unsigned long backBuffers, unsigned char zbuffer,
                                          unsigned long zformat);
        long ret = ((Activate)LIFE_SLOT(ActivateScreen))(g_life.obj, 0, g_life.tm[0], NULL, 1, 1, 10);
        CHECK(ret == EEWEB_NOT_SUPPORTED, "%s: lifecycle: ActivateScreen without a window is refused (got %ld)", e->label, ret);
        CHECK(g_life.dd->surface_count == 0, "%s: lifecycle: ActivateScreen without a window creates no surface (got %d)", e->label, g_life.dd->surface_count);
        life_end();
    }
    if (e->ours && life_begin(e, api, obj, 1, 0, "activated twice")) {
        g_life.check_leaks = 1;
        long first = life_activate(0, 1, 1, 10);
        long second = life_activate(0, 1, 1, 10);
        CHECK(first == 0 && second == 0, "%s: lifecycle: a screen can be activated again (got %ld, %ld)", e->label, first, second);
        life_end();
    }
}

/* ---- the scene calls, on the two windowed screens activated above ------------------------------------------------- */

static void life_set_render_target(void *surface, int screen)
{
    ((VoidPU)LIFE_SLOT(SetRenderTarget))(g_life.obj, surface, (unsigned long)screen);
}

static void life_render_target(void)
{
    void *back = life_state(0)->renderTarget;
    void *t1 = life_new_target(0, 320, 200);
    void *t2 = life_new_target(0, 256, 128);
    life_cur(0);
    LS("SetRenderTarget(the current target) does nothing", LIFE_DIFF, life_set_render_target(back, 0));
    LS("SetRenderTarget(T1)", LIFE_DIFF, life_set_render_target(t1, 0));
    LS("SetRenderTarget(T1) again", LIFE_DIFF, life_set_render_target(t1, 0));
    LS("SetRenderTarget(T2)", LIFE_DIFF, life_set_render_target(t2, 0));
    LS("SetRenderTarget(the first target again)", LIFE_DIFF, life_set_render_target(back, 0));
    life_cur(1);
    LS("SetRenderTarget(T1) on screen 1", LIFE_DIFF, life_set_render_target(t1, 1));
    life_cur(0);
}

static void life_begin_end_scene(void)
{
    void *back = life_state(0)->secondarySurface;
    void *t1 = g_life.ge[g_life.ge_count - 2];
    typedef long(THISCALL * BeginFn)(void *self, void *surface, unsigned long screen);
    BeginFn begin = (BeginFn)LIFE_SLOT(BeginScene);
    life_cur(0);
    LS("BeginScene(T1)", LIFE_DIFF, ret = begin(g_life.obj, t1, 0));
    LS("EndScene", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
    LS("BeginScene(the same target again)", LIFE_DIFF, ret = begin(g_life.obj, t1, 0));
    LS("EndScene again", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
    g_life.dd->dev_script[FD_BEGIN].code = LIFE_ERR_BUSY;
    g_life.dd->dev_script[FD_BEGIN].times = 2;
    LS("BeginScene(back) while Direct3D is still drawing", LIFE_DIFF, ret = begin(g_life.obj, back, 0));
    g_life.dd->dev_script[FD_BEGIN].code = LIFE_ERR_LOST;
    g_life.dd->dev_script[FD_BEGIN].times = 1;
    LS("BeginScene with the surface lost", LIFE_DIFF, ret = begin(g_life.obj, back, 0));
    g_life.dd->dev_script[FD_BEGIN].code = LIFE_ERR_OTHER;
    g_life.dd->dev_script[FD_BEGIN].times = 1;
    LS("BeginScene with another error", LIFE_DIFF, ret = begin(g_life.obj, back, 0));
    g_life.dd->dev_script[FD_END].code = LIFE_ERR_BUSY;
    g_life.dd->dev_script[FD_END].times = 3;
    LS("EndScene while Direct3D is still drawing", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
    g_life.dd->dev_script[FD_END].code = LIFE_ERR_LOST;
    g_life.dd->dev_script[FD_END].times = 1;
    LS("EndScene with the surface lost", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
    g_life.dd->dev_script[FD_END].code = LIFE_ERR_OTHER;
    g_life.dd->dev_script[FD_END].times = 1;
    LS("EndScene with another error", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 0));
    life_cur(1);
    LS("BeginScene on screen 1", LIFE_DIFF, ret = begin(g_life.obj, life_state(1)->secondarySurface, 1));
    LS("EndScene on screen 1", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(EndScene))(g_life.obj, 1));
    life_cur(0);
}

static void life_clear(void)
{
    static const struct {
        const char *name;
        float rect[4];
        float color[3];
    } kClears[] = {
        {"Clear the whole 640x480", {480, 0, 640, 0}, {1, 0, 0}},
        {"Clear with half-pixel rectangle and mixed colour", {479.5f, 0.5f, 639.5f, 1.5f}, {0.5f, 0.25f, 0.75f}},
        {"Clear with negative rectangle and truncated colour", {-3.2f, -0.7f, 10.5f, -2.5f}, {0.999f, 0.001f, 0.5f}},
        {"Clear with channels outside 0..1", {100, 50, 200, 10}, {1.5f, 0.5f, 0.2f}},
        {"Clear with negative and large channels", {100, 50, 200, 10}, {-0.1f, 0, 2}},
        {"Clear with rectangle at .5 steps", {2.5f, 3.5f, 4.5f, 5.5f}, {0.2f, 0.4f, 0.6f}},
    };
    life_cur(0);
    for (unsigned i = 0; i < sizeof kClears / sizeof kClears[0]; i++) {
        float rect[4];
        memcpy(rect, kClears[i].rect, sizeof rect);
        LS(kClears[i].name, LIFE_DIFF, {
            ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, kClears[i].color, rect, 0);
            life_rect_words(rect);
        });
    }
    float rect[4];
    float color[3] = {0.1f, 0.2f, 0.3f};
    life_rect(rect, 480, 0, 640, 0);
    g_life.dd->dev_script[FD_CLEAR].code = LIFE_ERR_BUSY;
    g_life.dd->dev_script[FD_CLEAR].times = 2;
    LS("Clear while Direct3D is still drawing", LIFE_DIFF, ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, color, rect, 0));
    g_life.dd->dev_script[FD_CLEAR].code = LIFE_ERR_LOST;
    g_life.dd->dev_script[FD_CLEAR].times = 1;
    LS("Clear with the surface lost", LIFE_DIFF, ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, color, rect, 0));
    g_life.dd->dev_script[FD_CLEAR].code = LIFE_ERR_OTHER;
    g_life.dd->dev_script[FD_CLEAR].times = 1;
    LS("Clear with another error", LIFE_DIFF, ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, color, rect, 0));
    life_rect(rect, 600, 0, 800, 0);
    LS("Clear on screen 1", LIFE_DIFF, ret = ((LifeClear)LIFE_SLOT(Clear))(g_life.obj, color, rect, 1));

    static const struct {
        const char *name;
        float z;
        float rect[4];
    } kClearZ[] = {
        {"ClearZBuffer 1.0 over the whole 640x480", 1.0f, {480, 0, 640, 0}},
        {"ClearZBuffer 0.5 with half-pixel rectangle", 0.5f, {479.5f, 0.5f, 639.5f, 1.5f}},
        {"ClearZBuffer 0 with negative rectangle", 0.0f, {-3.2f, -0.7f, 10.5f, -2.5f}},
    };
    for (unsigned i = 0; i < sizeof kClearZ / sizeof kClearZ[0]; i++) {
        float zrect[4];
        memcpy(zrect, kClearZ[i].rect, sizeof zrect);
        LS(kClearZ[i].name, LIFE_DIFF, {
            ret = ((LifeClearZ)LIFE_SLOT(ClearZBuffer))(g_life.obj, kClearZ[i].z, zrect, 0);
            life_rect_words(zrect);
        });
    }
    g_life.dd->dev_script[FD_CLEAR].code = LIFE_ERR_BUSY;
    g_life.dd->dev_script[FD_CLEAR].times = 2;
    LS("ClearZBuffer while Direct3D is still drawing", LIFE_DIFF, ret = ((LifeClearZ)LIFE_SLOT(ClearZBuffer))(g_life.obj, 1.0f, rect, 0));
    g_life.dd->dev_script[FD_CLEAR].code = LIFE_ERR_OTHER;
    g_life.dd->dev_script[FD_CLEAR].times = 1;
    LS("ClearZBuffer with an error", LIFE_DIFF, ret = ((LifeClearZ)LIFE_SLOT(ClearZBuffer))(g_life.obj, 1.0f, rect, 0));
    LS("ClearZBuffer on screen 1", LIFE_DIFF, ret = ((LifeClearZ)LIFE_SLOT(ClearZBuffer))(g_life.obj, 0.25f, rect, 1));
}

static unsigned long life_time(void)
{
    typedef unsigned long(__cdecl * GetTime)(void);
    GetTime fn;
    void *raw = engine_proc("?TMGetTime@@YAKXZ");
    memcpy(&fn, &raw, sizeof fn);
    return fn();
}

static void life_move_window(int screen, int x, int y)
{
    HWND hwnd = (HWND)(uintptr_t)((uint32_t *)g_life.win[screen])[0];
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void life_present_windowed(void)
{
    typedef struct {
        const char *name;
        int x, y;
    } Origin;
    static const Origin kOrigins[] = {
        {"ShowScene windowed over the window at (37, 51)", 37, 51},
        {"ShowScene windowed over the window at (-8, 0)", -8, 0},
        {"ShowScene windowed over the window at (1500, 900)", 1500, 900},
    };
    life_cur(0);
    for (unsigned i = 0; i < sizeof kOrigins / sizeof kOrigins[0]; i++) {
        life_move_window(0, kOrigins[i].x, kOrigins[i].y);
        LS(kOrigins[i].name, LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    }
    LIFE_SCRIPT(1, FS_BLT, LIFE_ERR_BUSY, 2);
    LS("ShowScene while DirectDraw is still blitting", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    LIFE_SCRIPT(1, FS_BLT, LIFE_ERR_LOST, 1);
    LS("ShowScene with the surface lost", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    LIFE_SCRIPT(1, FS_BLT, LIFE_ERR_OTHER, 1);
    LS("ShowScene with another blit error", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0));
    life_cur(1);
    LS("ShowScene windowed on screen 1", LIFE_DIFF, ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 1));
    life_cur(0);
}

/* The statistics the twelfth frame recomputes: per-frame values are exact multiples of 1/12; per-second values depend on the time
 * between two calls, so they are printed as whether they match the formula (events * 1000 / elapsed milliseconds). */
static unsigned fbits(float value)
{
    unsigned bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void life_stats_prepare(unsigned long last)
{
    GERScreenStateLayout *state = life_state(0);
    state->polyAreaAccumulator = 1234.5f;
    state->drawnPolyCount = 77;
    state->frameCounter = 11;
    state->scenePolyCount = 33;
    state->textureChangeCount = 5;
    state->lastStatisticsTime = (uint32_t)last;
    state->polyAreaPerSecond = state->polysPerSecond = state->framesPerSecond = state->scenePolysPerSecond = -1.0f;
}

static int life_ratio_ok(float value, double events, double elapsed)
{
    double expected = events * 1000.0 / elapsed;
    double diff = (double)value - expected;
    return (diff < 0 ? -diff : diff) <= expected * 1e-6;
}

static void life_stats_extra(unsigned long last)
{
    const GERScreenStateLayout *state = life_state(0);
    uint32_t w[4];
    memcpy(&w[0], &state->polyAreaPerFrame, 4);
    memcpy(&w[1], &state->polysPerFrame, 4);
    memcpy(&w[2], &state->scenePolysPerFrame, 4);
    memcpy(&w[3], &state->textureChangesPerFrame, 4);
    unsigned long elapsed = (unsigned long)(state->lastStatisticsTime - (uint32_t)last);
    life_extra("perframe=%x,%x,%x,%x counters=%x,%x,%x,%x,%x", (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3], (unsigned)state->frameCounter,
               (unsigned)state->drawnPolyCount, (unsigned)state->scenePolyCount, (unsigned)state->textureChangeCount, fbits(state->polyAreaAccumulator));
    if (elapsed > 0) {
        life_extra(" persec=%s,%s,%s,%s", life_ratio_ok(state->polyAreaPerSecond, 1234.5, (double)elapsed) ? "ok" : "BAD",
                   life_ratio_ok(state->polysPerSecond, 77, (double)elapsed) ? "ok" : "BAD",
                   life_ratio_ok(state->framesPerSecond, 12, (double)elapsed) ? "ok" : "BAD",
                   life_ratio_ok(state->scenePolysPerSecond, 33, (double)elapsed) ? "ok" : "BAD");
    }
}

static void life_stats(void)
{
    GERScreenStateLayout *state = life_state(0);
    unsigned long last = life_time() - 1500;
    life_stats_prepare(last);
    LS("ShowScene: the 12th frame recomputes the statistics", LIFE_NODIFF, {
        ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0);
        life_stats_extra(last);
    });
    /* No time has passed: the per-second values are zero.  The clock ticks in whole milliseconds, so a few tries are enough. */
    int hit = 0;
    for (int tries = 0; tries < 20000 && !hit; tries++) {
        last = life_time();
        life_stats_prepare(last);
        life_step_begin(LIFE_NODIFF);
        long ret = ((LongU)LIFE_SLOT(ShowScene))(g_life.obj, 0);
        hit = state->lastStatisticsTime == (uint32_t)last;
        if (hit) {
            life_extra("zero-elapsed per-second=%x,%x,%x,%x ", fbits(state->polyAreaPerSecond), fbits(state->polysPerSecond),
                       fbits(state->framesPerSecond), fbits(state->scenePolysPerSecond));
            life_stats_extra(last);
            life_step_end("ShowScene: the 12th frame in the same millisecond", ret, LIFE_NODIFF);
        }
    }
    CHECK(hit, "%s: lifecycle: two ShowScene calls land in the same millisecond", g_life.e->label);
    state->frameCounter = 0;
}

static void life_scene(void)
{
    life_render_target();
    life_begin_end_scene();
    life_clear();
    life_present_windowed();
    life_stats();
    /* Lights held at the time of Deactivate are removed from each device before the device is released. */
    GELightLayout light;
    state_make_light(&light, 0, 1.0f);
    unsigned long id = 99;
    typedef long(THISCALL * AddLight)(void *self, const void *light, unsigned long screen, unsigned long *id);
    LS("AddLight on screen 0", LIFE_NODIFF, ret = ((AddLight)LIFE_SLOT(AddLight))(g_life.obj, &light, 0, &id));
    LS("AddLight on screen 1", LIFE_NODIFF, ret = ((AddLight)LIFE_SLOT(AddLight))(g_life.obj, &light, 1, &id));
}

static void check_lifecycle(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    (void)h;
    life_activation_windowed(e, api, obj);
    life_activation_variants(e, api, obj);
    life_activation_failures(e, api, obj);
}
