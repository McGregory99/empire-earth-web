/*
 * The state setters of DX7Rasterizer (task P2.6.4): render state, ambient light and fog, the viewport,
 * the lights, and the capability getters.  Included by screen_checks.h and run from the screen probe.
 *
 * What is compared is the original's behaviour, observed two ways at once, after every call:
 *   - the device log: every IDirect3DDevice7 call the fake device saw (RS = SetRenderState, MAT, LIGHT,
 *     ENABLE, VP, TR) with its arguments, in order;
 *   - the state diff: the 32-bit words the call changed in the rasterizer's base object (b), in the
 *     engine's per-screen state record (s: GERScreenState, what the engine's own GERasterizer::SetX
 *     stores) and in the plugin's screen record (r: DX7ScreenRecord, the caches the plugin compares).
 * Both variants must produce the literal strings below, which were recorded from the original DLL
 * (EEWEB_STATE_RECORD=1 prints them instead of checking).  That pins the order of the device calls, the
 * argument values, which calls are skipped as redundant, and what a failing device call leaves behind.
 *
 * The device is installed by the test: the record's `device` field is set to a fake IDirect3DDevice7
 * (the real activation, which creates the device itself, is tested by lifecycle_checks.h); it is removed again before
 * Deactivate, which would release it.  The record's caches are set explicitly where a step depends on them.
 */
#include <math.h>

typedef struct StateEnv {
    const struct Expect *e;
    FakeDD *dd;
    void *obj;
    void *const *vtable;
    void *screen[2];
    FakeDevice *device[2];
    int screens;
    uint32_t base_before[0x7c / 4], state_before[sizeof(GERScreenStateLayout) / 4], record_before[sizeof(DX7ScreenRecordLayout) / 4];
    int recording;
} StateEnv;

static StateEnv g_se;

typedef void(THISCALL *VoidU)(void *self, unsigned long a);
typedef void(THISCALL *VoidUU)(void *self, unsigned long a, unsigned long b);
typedef void(THISCALL *VoidUUU)(void *self, unsigned long a, unsigned long b, unsigned long c);
typedef void(THISCALL *VoidFU)(void *self, float a, unsigned long b);
typedef void(THISCALL *VoidFFU)(void *self, float a, float b, unsigned long c);
typedef void(THISCALL *VoidPU)(void *self, const void *a, unsigned long b);
typedef void(THISCALL *VoidP)(void *self, const void *a);
typedef void(THISCALL *VoidBU)(void *self, unsigned char a, unsigned long b);
typedef long(THISCALL *LongPUP)(void *self, const void *a, unsigned long b, unsigned long *c);
typedef long(THISCALL *LongUPU)(void *self, unsigned long a, const void *b, unsigned long c);
typedef long(THISCALL *LongUU)(void *self, unsigned long a, unsigned long b);
typedef long(THISCALL *LongU)(void *self, unsigned long a);
typedef unsigned long(THISCALL *ULongUU)(void *self, unsigned long a, unsigned long b);
typedef unsigned char(THISCALL *BoolU)(void *self, unsigned long a);
typedef long(THISCALL *LongP)(void *self, void *a);

#define SE_SLOT(name) slot(g_se.vtable, EEWEB_SLOT_GERasterizer_##name)
#define CALL_VU(name, a) ((VoidU)SE_SLOT(name))(g_se.obj, (a))
#define CALL_VUU(name, a, b) ((VoidUU)SE_SLOT(name))(g_se.obj, (a), (b))
#define CALL_VUUU(name, a, b, c) ((VoidUUU)SE_SLOT(name))(g_se.obj, (a), (b), (c))
#define CALL_VFU(name, a, b) ((VoidFU)SE_SLOT(name))(g_se.obj, (a), (b))
#define CALL_VFFU(name, a, b, c) ((VoidFFU)SE_SLOT(name))(g_se.obj, (a), (b), (c))
#define CALL_VPU(name, a, b) ((VoidPU)SE_SLOT(name))(g_se.obj, (a), (b))
#define CALL_VP(name, a) ((VoidP)SE_SLOT(name))(g_se.obj, (a))
#define CALL_VBU(name, a, b) ((VoidBU)SE_SLOT(name))(g_se.obj, (a), (b))

static DX7ScreenRecordLayout *state_record(int i)
{
    return ((DX7ScreenRecordLayout **)((DX7RasterizerLayout *)g_se.obj)->screenRecords.first)[i];
}

static GERScreenStateLayout *state_state(int i)
{
    return ((GERScreenStateLayout **)((DX7RasterizerLayout *)g_se.obj)->base.screens.first)[i];
}

/* The engine's own std::vector arithmetic: how many elements the plugin-side vector holds. */
static unsigned state_record_count(void)
{
    const StdVectorLayout *v = &((DX7RasterizerLayout *)g_se.obj)->screenRecords;
    return v->first ? (unsigned)(((char *)v->last - (char *)v->first) / 4) : 0;
}

static void state_snapshot(void)
{
    memcpy(g_se.base_before, g_se.obj, sizeof g_se.base_before);
    memcpy(g_se.state_before, state_state(0), sizeof g_se.state_before);
    memcpy(g_se.record_before, state_record(0), sizeof g_se.record_before);
}

static void state_diff(char *out, size_t size)
{
    const uint32_t *base = (const uint32_t *)g_se.obj;
    const uint32_t *state = (const uint32_t *)state_state(0);
    const uint32_t *record = (const uint32_t *)state_record(0);
    for (unsigned i = 1; i < sizeof g_se.base_before / 4; i++) {
        if (base[i] != g_se.base_before[i]) {
            size_t used = strlen(out);
            snprintf(out + used, size - used, " b%x=%x", i * 4, (unsigned)base[i]);
        }
    }
    for (unsigned i = 0; i < sizeof g_se.state_before / 4; i++) {
        if (state[i] != g_se.state_before[i]) {
            size_t used = strlen(out);
            snprintf(out + used, size - used, " s%x=%x", i * 4, (unsigned)state[i]);
        }
    }
    for (unsigned i = 0; i < sizeof g_se.record_before / 4; i++) {
        if (record[i] != g_se.record_before[i]) {
            size_t used = strlen(out);
            snprintf(out + used, size - used, " r%x=%x", i * 4, (unsigned)record[i]);
        }
    }
}

/* The device log of one step on one line ("RS(f,1);RS(25,1)"), then " |" and the state diff. */
static void state_actual(char *out, size_t size, int with_diff)
{
    out[0] = 0;
    const char *log = g_se.dd->log;
    size_t n = strlen(log);
    for (size_t i = 0; i < n && i + 1 < size; i++) {
        out[i] = log[i] == '\n' ? ';' : log[i];
        out[i + 1] = 0;
    }
    if (with_diff) {
        strncat(out, " |", size - strlen(out) - 1);
        state_diff(out, size);
    }
}

static void state_step_begin(void)
{
    log_clear(g_se.dd);
    state_snapshot();
}

/* What the original did, by step name (state_expected.inc, recorded with EEWEB_STATE_RECORD=1 from the original DLL). */
static const struct {
    const char *name;
    const char *expect;
} kStateExpected[] = {
#include "state_expected.inc"
};

static const char *state_expected(const char *name)
{
    for (unsigned i = 0; i < sizeof kStateExpected / sizeof kStateExpected[0]; i++) {
        if (strcmp(kStateExpected[i].name, name) == 0) {
            return kStateExpected[i].expect;
        }
    }
    return NULL;
}

static void state_compare(const char *name, const char *actual)
{
    if (g_se.recording) {
        printf("REC\t%s\t%s\n", name, actual);
        return;
    }
    const char *expect = state_expected(name);
    int same = expect != NULL && strcmp(actual, expect) == 0;
    CHECK(same, "%s: state: %s", g_se.e->label, name);
    if (!same) {
        printf("     expected: %s\n     actual:   %s\n", expect ? expect : "(nothing recorded)", actual);
    }
}

static void state_step_end(const char *name, int with_diff)
{
    char actual[4096];
    state_actual(actual, sizeof actual, with_diff);
    state_compare(name, actual);
}

/* One call and what it did.  S compares the device log and the state diff, SL only the device log (for steps whose
 * state changes are not the point: the viewport copies hundreds of words). */
#define S(name, call) \
    do { state_step_begin(); call; state_step_end(name, 1); } while (0)

static void state_set_caches(unsigned long blend_src, unsigned long blend_dst, unsigned long z_enable, unsigned z_write, unsigned blend_on)
{
    DX7ScreenRecordLayout *r = state_record(0);
    r->blendSourceCache = (uint32_t)blend_src;
    r->blendDestCache = (uint32_t)blend_dst;
    r->zEnableCache = (uint32_t)z_enable;
    r->zWriteCache = (uint8_t)z_write;
    r->blendEnabledCache = (uint8_t)blend_on;
}

/* The engine's record of a new screen leaves some of the settings the setters compare with unset (they hold whatever the heap held),
 * so every run starts them from known values: all zero, the ambient intensity 1, the present flag on. */
static void state_reset_defaults(void)
{
    GERasterizerLayout *base = &((DX7RasterizerLayout *)g_se.obj)->base;
    memset(&base->ambientColor, 0, sizeof base->ambientColor);
    memset(&base->computedAmbientColor, 0, sizeof base->computedAmbientColor);
    base->ambientIntensity = 1.0f;
    base->culling = 0;
    for (int i = 0; i < g_se.screens; i++) {
        uint8_t *state = (uint8_t *)state_state(i);
        memset(state + offsetof(GERScreenStateLayout, fogColor), 0, offsetof(GERScreenStateLayout, screen) - offsetof(GERScreenStateLayout, fogColor));
        memset(state + offsetof(GERScreenStateLayout, alphaTestRefValue), 0, sizeof(GERScreenStateLayout) - offsetof(GERScreenStateLayout, alphaTestRefValue));
        ((GERScreenStateLayout *)state)->presentFlag = 1;
    }
}

static void *state_lle(const char *name)
{
    return proc_address(GetModuleHandleA("Low-Level Engine.dll"), name);
}

/* Build `screens` screens over the fake DirectDraw, add them to the rasterizer and put a fake Direct3D device into each
 * of their records (the first one with no cached values, the way a freshly activated screen starts). */
static int state_begin(const struct Expect *e, const ScreenApi *api, void *obj, int screens)
{
    memset(&g_se, 0, sizeof g_se);
    g_se.e = e;
    g_se.obj = obj;
    g_se.vtable = *(void *const **)obj;
    g_se.recording = getenv("EEWEB_STATE_RECORD") != NULL;
    g_se.dd = (FakeDD *)calloc(1, sizeof(FakeDD));
    fake_dd_init(g_se.dd);
    g_se.screens = screens;
    typedef long(THISCALL * AddScreen)(void *self, void *screen);
    for (int i = 0; i < screens; i++) {
        g_se.screen[i] = make_screen(api, g_se.dd, "state screen", 1, 1);
        long added = ((AddScreen)slot(g_se.vtable, EEWEB_SLOT_GERasterizer_AddValidScreen))(obj, g_se.screen[i]);
        CHECK(added == 0, "%s: state: AddValidScreen(screen %d) succeeds (got %ld)", e->label, i, added);
    }
    CHECK(state_record_count() == (unsigned)screens, "%s: state: the rasterizer keeps %d screen record(s) at 0x68 (got %u)", e->label, screens,
          state_record_count());
    if (state_record_count() != (unsigned)screens) {
        return 0;
    }
    for (int i = 0; i < screens; i++) {
        DX7ScreenRecordLayout *record = state_record(i);
        CHECK(record != NULL && (void *)record->screen == g_se.screen[i], "%s: state: record %d holds its screen at 0x20", e->label, i);
        FakeDevice *device = (FakeDevice *)calloc(1, sizeof *device);
        device->vtbl = g_se.dd->device_vtbl;
        device->dd = g_se.dd;
        device->id = i;
        g_se.device[i] = device;
        void *screen = record->screen;
        memset(record, 0, sizeof *record); /* the original leaves most of the record unset; every cache starts at 0 here */
        record->screen = (struct GEScreenLayout *)screen;
        record->device = device;
        record->sceneDrawn = 1;
    }
    state_reset_defaults();
    return 1;
}

static void state_end(void)
{
    typedef long(THISCALL * Deactivate)(void *self);
    for (int i = 0; i < g_se.screens; i++) {
        if (state_record_count() > (unsigned)i) {
            state_record(i)->device = NULL; /* the fake device is not the plugin's to release */
        }
        free(g_se.device[i]);
    }
    CHECK(((Deactivate)slot(g_se.vtable, EEWEB_SLOT_GERasterizer_Deactivate))(g_se.obj) == 0, "%s: state: Deactivate() releases the screens", g_se.e->label);
    CHECK(state_record_count() == 0, "%s: state: ... and empties the record vector", g_se.e->label);
    free(g_se.dd);
}

/* ---- the scenarios ----------------------------------------------------------------------------------- */

static void state_render_toggles(void)
{
    /* SetAlphaTesting (RS 0xf): compares with the engine's state, records it only when the device accepted it. */
    S("SetAlphaTesting(1)", CALL_VUU(SetAlphaTesting, 1, 0));
    S("SetAlphaTesting(1) again", CALL_VUU(SetAlphaTesting, 1, 0));
    S("SetAlphaTesting(0)", CALL_VUU(SetAlphaTesting, 0, 0));
    g_se.dd->rs_fail = 1;
    S("SetAlphaTesting(1), device fails", CALL_VUU(SetAlphaTesting, 1, 0));
    S("SetAlphaTesting(1) retried", CALL_VUU(SetAlphaTesting, 1, 0));
    S("SetAlphaTesting(5)", CALL_VUU(SetAlphaTesting, 5, 0));

    /* SetAlphaTestingCompareState (RS 0x19): the original compares with the alpha-testing field, not its own. */
    S("SetAlphaTestingCompareState(0) while alpha testing is 5", CALL_VUU(SetAlphaTestingCompareState, 0, 0));
    for (unsigned long mode = 1; mode <= 9; mode++) {
        char name[64];
        snprintf(name, sizeof name, "SetAlphaTestingCompareState(%lu)", mode);
        S(name, CALL_VUU(SetAlphaTestingCompareState, mode, 0));
    }
    S("SetAlphaTestingCompareState(5) equal to alpha testing", CALL_VUU(SetAlphaTestingCompareState, 5, 0));

    /* SetAlphaTestingRefValue (RS 0x18): the value times 255, truncated. */
    S("SetAlphaTestingRefValue(0.5)", CALL_VFU(SetAlphaTestingRefValue, 0.5f, 0));
    S("SetAlphaTestingRefValue(0.5) again", CALL_VFU(SetAlphaTestingRefValue, 0.5f, 0));
    S("SetAlphaTestingRefValue(1.0)", CALL_VFU(SetAlphaTestingRefValue, 1.0f, 0));
    S("SetAlphaTestingRefValue(0.999)", CALL_VFU(SetAlphaTestingRefValue, 0.999f, 0));
    S("SetAlphaTestingRefValue(0.0)", CALL_VFU(SetAlphaTestingRefValue, 0.0f, 0));
    g_se.dd->rs_fail = 1;
    S("SetAlphaTestingRefValue(0.25), device fails", CALL_VFU(SetAlphaTestingRefValue, 0.25f, 0));

    /* SetAntiAliasing (RS 2): 2 for mode 1, otherwise 0. */
    S("SetAntiAliasing(1)", CALL_VUU(SetAntiAliasing, 1, 0));
    S("SetAntiAliasing(1) again", CALL_VUU(SetAntiAliasing, 1, 0));
    S("SetAntiAliasing(2)", CALL_VUU(SetAntiAliasing, 2, 0));
    S("SetAntiAliasing(0)", CALL_VUU(SetAntiAliasing, 0, 0));

    /* SetColorKeying (RS 0x29), SetDithering (RS 0x1a), SetFog (RS 0x1c), SetSpecular (RS 0x1d), SetLighting (RS 0x89):
     * a boolean for "mode == 1". */
    S("SetColorKeying(1)", CALL_VUU(SetColorKeying, 1, 0));
    S("SetColorKeying(1) again", CALL_VUU(SetColorKeying, 1, 0));
    S("SetColorKeying(2)", CALL_VUU(SetColorKeying, 2, 0));
    S("SetDithering(1)", CALL_VUU(SetDithering, 1, 0));
    S("SetDithering(1) again", CALL_VUU(SetDithering, 1, 0));
    S("SetDithering(0)", CALL_VUU(SetDithering, 0, 0));
    S("SetFog(1)", CALL_VUU(SetFog, 1, 0));
    S("SetFog(1) again", CALL_VUU(SetFog, 1, 0));
    S("SetFog(3)", CALL_VUU(SetFog, 3, 0));
    S("SetSpecular(1)", CALL_VUU(SetSpecular, 1, 0));
    S("SetSpecular(1) again", CALL_VUU(SetSpecular, 1, 0));
    S("SetSpecular(0)", CALL_VUU(SetSpecular, 0, 0));
    g_se.dd->rs_fail = 1;
    S("SetSpecular(1), device fails", CALL_VUU(SetSpecular, 1, 0));
    S("SetLighting(1)", CALL_VUU(SetLighting, 1, 0));
    S("SetLighting(1) again", CALL_VUU(SetLighting, 1, 0));
    g_se.dd->rs_fail = 1;
    S("SetLighting(0), device fails: the original does not look at the result", CALL_VUU(SetLighting, 0, 0));

    /* SetDrawStyle (RS 8): 0 -> 3, 1 -> 2, 2 -> 1, others 0.  SetShading (RS 9): (mode == 1) + 1. */
    for (unsigned long style = 0; style <= 3; style++) {
        char name[64];
        snprintf(name, sizeof name, "SetDrawStyle(%lu)", style);
        S(name, CALL_VUU(SetDrawStyle, style, 0));
    }
    S("SetDrawStyle(3) again", CALL_VUU(SetDrawStyle, 3, 0));
    S("SetShading(1)", CALL_VUU(SetShading, 1, 0));
    S("SetShading(1) again", CALL_VUU(SetShading, 1, 0));
    S("SetShading(0)", CALL_VUU(SetShading, 0, 0));
    S("SetShading(2)", CALL_VUU(SetShading, 2, 0));

    /* SetZCompare (RS 0x17): the engine's compare modes 0..7 map to 1,2,4,3,6,7,5,8; anything else sends 0. */
    for (unsigned long mode = 0; mode <= 8; mode++) {
        char name[64];
        snprintf(name, sizeof name, "SetZCompare(%lu)", mode);
        S(name, CALL_VUU(SetZCompare, mode, 0));
    }
    S("SetZCompare(8) again", CALL_VUU(SetZCompare, 8, 0));
}

static void state_blending_and_z(void)
{
    /* SetBlending: RS 0x1b (enable), 0x13 (source), 0x14 (destination), each compared with the plugin's own cache. */
    state_set_caches(0xdead, 0xbeef, 0, 0, 0xee);
    S("SetBlending(2,3) with unknown caches", CALL_VUUU(SetBlending, 2, 3, 0));
    S("SetBlending(2,3) again", CALL_VUUU(SetBlending, 2, 3, 0));
    S("SetBlending(1,0): blending off", CALL_VUUU(SetBlending, 1, 0, 0));
    S("SetBlending(1,5): destination only", CALL_VUUU(SetBlending, 1, 5, 0));
    S("SetBlending(12,12): last table entry", CALL_VUUU(SetBlending, 12, 12, 0));
    S("SetBlending(13,13): off the table", CALL_VUUU(SetBlending, 13, 13, 0));
    S("SetBlending(7,13)", CALL_VUUU(SetBlending, 7, 13, 0));
    S("SetBlending(5,14): the destination keeps the source's value", CALL_VUUU(SetBlending, 5, 14, 0));
    state_set_caches(0, 0, 0, 0, 0);
    S("SetBlending(1,0) with the caches at zero but the engine state different", CALL_VUUU(SetBlending, 1, 0, 0));
    state_set_caches(2, 3, 0, 0, 1);
    S("SetBlending(3,4) when the caches already hold the source of an earlier mode", CALL_VUUU(SetBlending, 3, 4, 0));

    /* SetZBuffer: RS 7 (z enable: bit 0 or 1 set) and RS 0xe (z write: bit 0), each compared with a cache. */
    state_set_caches(0, 0, 0xdead, 0xee, 0);
    S("SetZBuffer(3) with unknown caches", CALL_VUU(SetZBuffer, 3, 0));
    S("SetZBuffer(3) again", CALL_VUU(SetZBuffer, 3, 0));
    S("SetZBuffer(2): enabled, no write", CALL_VUU(SetZBuffer, 2, 0));
    S("SetZBuffer(1): enabled, write", CALL_VUU(SetZBuffer, 1, 0));
    S("SetZBuffer(0): off", CALL_VUU(SetZBuffer, 0, 0));
    S("SetZBuffer(4): bit 2 only is off", CALL_VUU(SetZBuffer, 4, 0));
    state_set_caches(0, 0, 1, 1, 0);
    S("SetZBuffer(0) with the caches saying on", CALL_VUU(SetZBuffer, 0, 0));
    state_set_caches(0, 0, 1, 0, 0);
    S("SetZBuffer(3) with the z write cache off only", CALL_VUU(SetZBuffer, 3, 0));
}

static void state_ambient_fog_material(void)
{
    /* SetAmbientColor / SetAmbientIntensity: RS 0x8b with alpha 255, each channel colour * intensity * 255 rounded to nearest
     * even and not clamped (values above 1 spill into the neighbouring channel). */
    const float ambient[3] = {0.5f, 0.25f, 1.0f};
    S("SetAmbientColor(0.5,0.25,1)", CALL_VPU(SetAmbientColor, ambient, 0));
    S("SetAmbientColor again: no deduplication", CALL_VPU(SetAmbientColor, ambient, 0));
    S("SetAmbientIntensity(0.5)", CALL_VFU(SetAmbientIntensity, 0.5f, 0));
    S("SetAmbientIntensity(2)", CALL_VFU(SetAmbientIntensity, 2.0f, 0));
    const float half[3] = {0.5f, 0.5f, 0.5f};
    S("SetAmbientColor(0.5,0.5,0.5) at intensity 2", CALL_VPU(SetAmbientColor, half, 0));
    S("SetAmbientIntensity(1)", CALL_VFU(SetAmbientIntensity, 1.0f, 0));
    S("SetAmbientColor(0.5,0.5,0.5) rounds 127.5", CALL_VPU(SetAmbientColor, half, 0));
    const float tie[3] = {0.5f / 255.0f * 3.0f, 0.5f / 255.0f, 1.5f / 255.0f}; /* 1.5, 0.5, 1.5 before rounding */
    S("SetAmbientColor ties", CALL_VPU(SetAmbientColor, tie, 0));
    const float over[3] = {1.5f, -0.5f, 0.0f};
    S("SetAmbientColor(1.5,-0.5,0) is not clamped", CALL_VPU(SetAmbientColor, over, 0));

    /* SetDebugColor: no device call, the packed colour lands in the object at 0x78. */
    const float debug[3] = {1.0f, 0.5f, 0.25f};
    S("SetDebugColor(1,0.5,0.25)", CALL_VP(SetDebugColor, debug));
    const float debug_tie[3] = {0.5f / 255.0f, 2.5f / 255.0f, 1.0f};
    S("SetDebugColor ties", CALL_VP(SetDebugColor, debug_tie));

    /* SetFogColor: RS 0x22 with alpha 255, channel * 255 truncated, compared float by float with the engine's state. */
    const float fog[3] = {0.5f, 0.25f, 1.0f};
    S("SetFogColor(0.5,0.25,1)", CALL_VPU(SetFogColor, fog, 0));
    S("SetFogColor again", CALL_VPU(SetFogColor, fog, 0));
    const float fog_blue[3] = {0.5f, 0.25f, 0.999f};
    S("SetFogColor(blue only changes)", CALL_VPU(SetFogColor, fog_blue, 0));
    g_se.dd->rs_fail = 1;
    const float fog_black[3] = {0.0f, 0.0f, 0.0f};
    S("SetFogColor(0,0,0), device fails", CALL_VPU(SetFogColor, fog_black, 0));
    S("SetFogColor(0,0,0) retried", CALL_VPU(SetFogColor, fog_black, 0));

    /* SetFogRange: RS 0x24 (start) and RS 0x25 (end) with the float bits, each only when it differs from the engine's value;
     * the engine call is always made. */
    S("SetFogRange(10,20)", CALL_VFFU(SetFogRange, 10.0f, 20.0f, 0));
    S("SetFogRange(10,20) again", CALL_VFFU(SetFogRange, 10.0f, 20.0f, 0));
    S("SetFogRange(10,30)", CALL_VFFU(SetFogRange, 10.0f, 30.0f, 0));
    S("SetFogRange(5,30)", CALL_VFFU(SetFogRange, 5.0f, 30.0f, 0));

    /* SetFogType, SetFogValue, SetDisplayNoTextureMode, SetDisplayOverdrawMode, SetWaitForVSync, SetProjectionMode: the original
     * hands them to the engine's own implementation without a device call. */
    S("SetFogType(3)", CALL_VUU(SetFogType, 3, 0));
    S("SetFogValue(0.75)", CALL_VFU(SetFogValue, 0.75f, 0));
    S("SetProjectionMode(1)", CALL_VUU(SetProjectionMode, 1, 0));
    S("SetProjectionMode(0)", CALL_VUU(SetProjectionMode, 0, 0));
    S("SetDisplayNoTextureMode(true)", CALL_VBU(SetDisplayNoTextureMode, 1, 0));
    S("SetDisplayNoTextureMode(false)", CALL_VBU(SetDisplayNoTextureMode, 0, 0));
    S("SetDisplayOverdrawMode(true)", CALL_VBU(SetDisplayOverdrawMode, 1, 0));
    S("SetDisplayOverdrawMode(false)", CALL_VBU(SetDisplayOverdrawMode, 0, 0));
    S("SetWaitForVSync(true)", CALL_VBU(SetWaitForVSync, 1, 0));
    S("SetWaitForVSync(false)", CALL_VBU(SetWaitForVSync, 0, 0));

    /* SetShadingColorSource: not 0 -> a white material, RS 0x8d, 0x93 on, RS 0x91 on; 0 -> the three off. */
    S("SetShadingColorSource(1)", CALL_VUU(SetShadingColorSource, 1, 0));
    S("SetShadingColorSource(1) again", CALL_VUU(SetShadingColorSource, 1, 0));
    S("SetShadingColorSource(2)", CALL_VUU(SetShadingColorSource, 2, 0));
    S("SetShadingColorSource(0)", CALL_VUU(SetShadingColorSource, 0, 0));
    g_se.dd->rs_fail = 3;
    S("SetShadingColorSource(1), the last device call fails", CALL_VUU(SetShadingColorSource, 1, 0));
    S("SetShadingColorSource(1) retried", CALL_VUU(SetShadingColorSource, 1, 0));
}

static void state_culling(void)
{
    /* SetCulling has no screen argument: every screen record with a device gets RS 0x16 (mode 1 -> 2, mode 2 -> 3, else 1). */
    S("SetCulling(1)", CALL_VU(SetCulling, 1));
    S("SetCulling(1) again", CALL_VU(SetCulling, 1));
    S("SetCulling(2)", CALL_VU(SetCulling, 2));
    S("SetCulling(0)", CALL_VU(SetCulling, 0));
    S("SetCulling(7)", CALL_VU(SetCulling, 7));
    state_record(1)->device = NULL;
    S("SetCulling(1) skips the screen without a device", CALL_VU(SetCulling, 1));
    state_record(1)->device = g_se.device[1];
    S("SetCulling(2) reaches both again", CALL_VU(SetCulling, 2));
    state_record(0)->device = NULL;
    S("SetCulling(1) skips a first screen without a device", CALL_VU(SetCulling, 1));
    state_record(0)->device = g_se.device[0];
}

/* ---- the viewport -------------------------------------------------------------------------------------- */

static void state_make_viewport(void *viewport, const float rect[4], float near_z, float far_z)
{
    typedef void(THISCALL * Construct)(void *self);
    typedef void(THISCALL * SetRect)(void *self, const void *rect);
    typedef void(THISCALL * SetDepth)(void *self, float a, float b);
    Construct construct = (Construct)state_lle("??0GEViewport@@QAE@XZ");
    SetRect set_rect = (SetRect)state_lle("?SetViewRectangle@GEViewport@@QAEXABVGERectangle@@@Z");
    SetDepth set_depth = (SetDepth)state_lle("?SetDepthRange@GEViewport@@QAEXMM@Z");
    construct(viewport);
    set_rect(viewport, rect);
    set_depth(viewport, near_z, far_z);
}

static void state_viewport(void)
{
    uint8_t viewport[sizeof(GEViewportLayout) + 16];
    struct {
        const char *name;
        float rect[4]; /* bottom, left, right, top */
        float near_z, far_z;
    } cases[] = {
        {"SetViewport(640x480)", {480.0f, 0.0f, 640.0f, 0.0f}, 0.0f, 1.0f},
        {"SetViewport(fractions)", {100.7f, 10.3f, 200.2f, 20.9f}, 0.25f, 0.75f},
        {"SetViewport(halves)", {101.5f, 10.5f, 200.5f, 20.5f}, 0.0f, 1.0f},
        {"SetViewport(negative origin)", {50.0f, -5.5f, 100.0f, -3.25f}, 0.0f, 1.0f},
        {"SetViewport(one pixel)", {20.0f, 10.0f, 11.0f, 19.0f}, 0.5f, 0.5f},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        state_make_viewport(viewport, cases[i].rect, cases[i].near_z, cases[i].far_z);
        state_record(0)->sceneDrawn = 0;
        state_step_begin();
        CALL_VPU(SetViewport, viewport, 0);
        /* The log, the record's D3DVIEWPORT7 and its scene flag; the engine's own copy of the viewport is a few hundred words. */
        char extra[256];
        const DX7ScreenRecordLayout *r = state_record(0);
        snprintf(extra, sizeof extra, " |%x,%x,%x,%x,%x,%x,%u", r->viewport[0], r->viewport[1], r->viewport[2], r->viewport[3], r->viewport[4],
                 r->viewport[5], r->sceneDrawn);
        char actual[4096];
        state_actual(actual, sizeof actual, 0);
        strncat(actual, extra, sizeof actual - strlen(actual) - 1);
        state_compare(cases[i].name, actual);
    }
}

/* ---- the lights ---------------------------------------------------------------------------------------- */

static void state_make_light(GELightLayout *light, unsigned style, float seed)
{
    memset(light, 0, sizeof *light);
    light->color.r = seed;
    light->computedColor.r = seed + 0.1f;
    light->computedColor.g = seed + 0.2f;
    light->computedColor.b = seed + 0.3f;
    light->location.x = seed + 1.0f;
    light->location.y = seed + 2.0f;
    light->location.z = seed + 3.0f;
    light->direction.x = seed + 4.0f;
    light->direction.y = -(seed + 5.0f);
    light->direction.z = 0.0f;
    light->attenuation0 = seed + 6.0f;
    light->attenuation1 = seed + 7.0f;
    light->attenuation2 = seed + 8.0f;
    light->range = seed + 9.0f;
    light->style = style;
}

static void state_lights(void)
{
    GELightLayout point, directional;
    state_make_light(&point, 0, 1.0f);
    state_make_light(&directional, 1, 10.0f);
    unsigned long id = 99;
    typedef long(THISCALL * AddLight)(void *self, const void *light, unsigned long screen, unsigned long *id);
    AddLight add_light = (AddLight)SE_SLOT(AddLight);

    /* The eight light slots are a table of the DLL, shared by every screen; AddLight takes the first free one. */
    S("RemoveAllLights on an empty table", ((LongU)SE_SLOT(RemoveAllLights))(g_se.obj, 0));
    long result;
    S("AddLight(point)", result = add_light(g_se.obj, &point, 0, &id));
    CHECK(result == 0 && id == 0, "%s: state: AddLight(point) returns 0 and light 0 (got %ld, %lu)", g_se.e->label, result, id);
    S("AddLight(directional)", result = add_light(g_se.obj, &directional, 0, &id));
    CHECK(result == 0 && id == 1, "%s: state: AddLight(directional) returns 0 and light 1 (got %ld, %lu)", g_se.e->label, result, id);
    S("RemoveLight(0)", result = ((LongUU)SE_SLOT(RemoveLight))(g_se.obj, 0, 0));
    CHECK(result == 0, "%s: state: RemoveLight(0) returns 0 (got %ld)", g_se.e->label, result);
    S("RemoveLight(0) again", result = ((LongUU)SE_SLOT(RemoveLight))(g_se.obj, 0, 0));
    CHECK(result == 9, "%s: state: RemoveLight of a free light returns 9 (got %ld)", g_se.e->label, result);
    S("RemoveLight(8)", result = ((LongUU)SE_SLOT(RemoveLight))(g_se.obj, 8, 0));
    CHECK(result == 9, "%s: state: RemoveLight(8) returns 9 (got %ld)", g_se.e->label, result);
    S("AddLight reuses the freed light 0", result = add_light(g_se.obj, &point, 0, &id));
    CHECK(result == 0 && id == 0, "%s: state: AddLight takes the first free light (got %ld, %lu)", g_se.e->label, result, id);
    S("UpdateLight(1)", result = ((LongUPU)SE_SLOT(UpdateLight))(g_se.obj, 1, &point, 0));
    CHECK(result == 0, "%s: state: UpdateLight(1) returns 0 (got %ld)", g_se.e->label, result);
    S("UpdateLight(5) of a free light", result = ((LongUPU)SE_SLOT(UpdateLight))(g_se.obj, 5, &point, 0));
    CHECK(result == 9, "%s: state: UpdateLight of a free light returns 9 (got %ld)", g_se.e->label, result);
    g_se.dd->set_light_result = (long)0x80004005;
    S("UpdateLight(1), SetLight fails", result = ((LongUPU)SE_SLOT(UpdateLight))(g_se.obj, 1, &directional, 0));
    CHECK(result == 1, "%s: state: UpdateLight returns 1 when SetLight fails (got %ld)", g_se.e->label, result);
    S("AddLight(point), SetLight fails: the slot stays taken", result = add_light(g_se.obj, &point, 0, &id));
    CHECK(result == 1 && id == 2, "%s: state: AddLight returns SetLight's failure as 1 (got %ld, %lu)", g_se.e->label, result, id);
    g_se.dd->set_light_result = 0;
    g_se.dd->enable_result = (long)0x80004005;
    S("AddLight, LightEnable fails", result = add_light(g_se.obj, &point, 0, &id));
    CHECK(result == 1 && id == 3, "%s: state: AddLight returns 1 when LightEnable fails (got %ld, %lu)", g_se.e->label, result, id);
    S("RemoveLight(3), LightEnable fails", result = ((LongUU)SE_SLOT(RemoveLight))(g_se.obj, 3, 0));
    CHECK(result == 1, "%s: state: RemoveLight returns 1 when LightEnable fails (got %ld)", g_se.e->label, result);
    g_se.dd->enable_result = 0;
    for (int i = 0; i < 5; i++) {
        long filled = add_light(g_se.obj, &point, 0, &id);
        CHECK(filled == 0, "%s: state: AddLight fills light %lu (got %ld)", g_se.e->label, id, filled);
    }
    S("AddLight with all eight taken", result = add_light(g_se.obj, &point, 0, &id));
    CHECK(result == 7 && id == 8, "%s: state: AddLight returns 7 and id 8 when no light is free (got %ld, %lu)", g_se.e->label, result, id);
    S("RemoveAllLights", ((LongU)SE_SLOT(RemoveAllLights))(g_se.obj, 0));
    S("RemoveAllLights again", ((LongU)SE_SLOT(RemoveAllLights))(g_se.obj, 0));
}

/* ---- the capability getters ---------------------------------------------------------------------------- */

static void state_getters(void)
{
    DX7ScreenRecordLayout *r = state_record(0);
    ULongUU caps = (ULongUU)SE_SLOT(GetCaps);
    BoolU modulate = (BoolU)SE_SLOT(GetCanModulateTextureAlpha);
    BoolU dirty = (BoolU)SE_SLOT(GetDirtyRectangleSupport);
    CHECK(caps(g_se.obj, 0, 0) == 0 && caps(g_se.obj, 7, 3) == 0, "%s: state: GetCaps() is 0 for every argument", g_se.e->label);
    r->canModulateTextureAlpha = 0;
    r->dirtyRectangleSupport = 0;
    CHECK(modulate(g_se.obj, 0) == 0 && dirty(g_se.obj, 0) == 0, "%s: state: both capability getters say no when the screen record's flags are 0", g_se.e->label);
    r->canModulateTextureAlpha = 1;
    CHECK(modulate(g_se.obj, 0) == 1 && dirty(g_se.obj, 0) == 0, "%s: state: GetCanModulateTextureAlpha reads the byte at 0x70", g_se.e->label);
    r->dirtyRectangleSupport = 1;
    r->canModulateTextureAlpha = 0;
    CHECK(modulate(g_se.obj, 0) == 0 && dirty(g_se.obj, 0) == 1, "%s: state: GetDirtyRectangleSupport reads the byte at 0x71", g_se.e->label);
    DX7ScreenRecordLayout *second = state_record(1);
    second->canModulateTextureAlpha = 1;
    CHECK(modulate(g_se.obj, 1) == 1 && modulate(g_se.obj, 0) == 0, "%s: state: the getters index the screen records by screen", g_se.e->label);
}

/* ---- the projection set-up ------------------------------------------------------------------------------ */

typedef long(THISCALL *ModeFn)(void *self, unsigned long screen);
typedef void(THISCALL *ConvertFn)(void *self, const float *transformation, float *matrix);

/* The transformations of the engine's screen state that Set3DMode reads, set to distinct values: the camera's inverse, and the
 * orthographic and perspective projections. */
static void state_fill_matrices(void)
{
    GERScreenStateLayout *state = state_state(0);
    for (int i = 0; i < 16; i++) {
        state->viewport.cameraInverse.m[i] = 0.5f + 0.25f * (float)i;
        state->viewport.orthographicProjection.m[i] = 100.0f + (float)i;
        state->viewport.perspectiveProjection.m[i] = 200.0f + (float)i;
    }
}

static void state_set_flags(unsigned mode_3d_current, unsigned scene_drawn)
{
    DX7ScreenRecordLayout *record = state_record(0);
    record->unknown6c = (uint8_t)mode_3d_current;
    record->sceneDrawn = (uint8_t)scene_drawn;
}

static void state_transforms(HMODULE h)
{
    ModeFn set2d = (ModeFn)proc_address(h, "?Set2DMode@DX7Rasterizer@@IAEJK@Z");
    ModeFn set3d = (ModeFn)proc_address(h, "?Set3DMode@DX7Rasterizer@@IAEJK@Z");
    ConvertFn convert = (ConvertFn)proc_address(h, "?ConvertToD3DMatrix@DX7Rasterizer@@IAEXABVGETransformation@@AAU_D3DMATRIX@@@Z");
    CHECK(set2d != NULL && set3d != NULL && convert != NULL, "%s: state: exports Set2DMode, Set3DMode and ConvertToD3DMatrix", g_se.e->label);
    if (!set2d || !set3d || !convert) {
        return;
    }
    float in[16], out[16];
    for (int i = 0; i < 16; i++) {
        in[i] = 1.5f + (float)i;
        out[i] = -1.0f;
    }
    convert(g_se.obj, in, out);
    int transposed = 1;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            transposed &= out[row * 4 + col] == in[col * 4 + row];
        }
    }
    CHECK(transposed, "%s: state: ConvertToD3DMatrix writes the transpose of the engine's row-major transformation", g_se.e->label);

    long result = -1;
    state_fill_matrices();
    state_set_flags(0, 0);
    S("Set3DMode, 3D not current", result = set3d(g_se.obj, 0));
    CHECK(result == 0, "%s: state: Set3DMode returns 0 (got %ld)", g_se.e->label, result);
    S("Set3DMode again: nothing to do", result = set3d(g_se.obj, 0));
    state_set_flags(1, 1);
    state_state(0)->projectionMode = 0;
    S("Set3DMode, orthographic, scene drawn since", result = set3d(g_se.obj, 0));
    state_state(0)->projectionMode = 1;
    state_set_flags(0, 1);
    S("Set3DMode, perspective, 3D not current and scene drawn", result = set3d(g_se.obj, 0));
    state_state(0)->projectionMode = 7;
    state_set_flags(0, 0);
    S("Set3DMode, projection mode 7 takes the orthographic matrix", result = set3d(g_se.obj, 0));

    state_set_flags(0, 0);
    S("Set2DMode when both flags are clear: nothing to do", result = set2d(g_se.obj, 0));
    CHECK(result == 0, "%s: state: Set2DMode returns 0 (got %ld)", g_se.e->label, result);
    state_record(0)->viewport[2] = 640;
    state_record(0)->viewport[3] = 480;
    state_set_flags(1, 0);
    S("Set2DMode 640x480 after the 3D set-up", result = set2d(g_se.obj, 0));
    state_record(0)->viewport[2] = 3;
    state_record(0)->viewport[3] = 7;
    state_set_flags(0, 1);
    S("Set2DMode 3x7 after a drawn scene", result = set2d(g_se.obj, 0));
    state_record(0)->viewport[2] = 1024;
    state_record(0)->viewport[3] = 768;
    state_set_flags(1, 1);
    S("Set2DMode 1024x768 with both flags set", result = set2d(g_se.obj, 0));
}

static void check_state_setters(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    if (!state_begin(e, api, obj, 2)) {
        return;
    }
    state_render_toggles();
    state_blending_and_z();
    state_ambient_fog_material();
    state_culling();
    state_viewport();
    state_lights();
    state_getters();
    state_transforms(h);
    state_end();
}
