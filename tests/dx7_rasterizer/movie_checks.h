/*
 * CreateMovie of DX7Rasterizer (task P2.6.7k): the two overloads (slots 9 and 8) and the DX7Movie they build (0x60 bytes, 8 slots).  Included by
 * screen_checks.h after target_checks.h and run from the screen probe; same method as lifecycle_checks.h, over the fake DirectDraw
 * and the scripted Bink of fakebink.c (a binkw32.dll of its own in both test beds).
 *
 * A movie plays on a thread of its own, so a step is deterministic only while that thread stands still: the fake Bink parks it in BinkOpen
 * and in every BinkWait that answers "ready" until the harness releases a token (one token plays one frame), and the harness takes a
 * snapshot only when the thread is parked (FakeBinkSettle).  A step shows the return value, what the DirectDraw fake saw, what Bink was
 * asked (the log of the fake Bink, with the surfaces as fsN) and the 0x60 bytes of the movie object, the engine's own handles as set / null.
 * Literals of movie_expected.inc were recorded from the original DLL (EEWEB_LIFE_RECORD=1 prints them instead of checking).
 *
 * The engine's globals the movie reads are set here: GERasterizer::sActiveRasterizer (the screen to blit on and the surfaces to make) and,
 * in one group, SEMixer::sActiveMixer (the DirectSound the sound goes to).
 *
 * Not covered, and why: the engine's own call sites (they need GEInitialize); a movie file (the fake Bink opens nothing, so what the real
 * Bink does with the file name, the sound mixing and the decoding is outside the plugin and outside this test); the object copy
 * constructor and assignment of DX7Movie (no caller: the engine copies nothing it gets as a GEMovie *).
 */

typedef struct FakeSpec {
    const void *const *vtbl;
    UString volume, directory, file;
} FakeSpec;

/* FSFileSpec as the movie uses it: three getters that answer an engine string (slots 5, 7 and 10). */
static const UString *THISCALL mv_spec_directory(FakeSpec *spec)
{
    return &spec->directory;
}
static const UString *THISCALL mv_spec_file(FakeSpec *spec)
{
    return &spec->file;
}
static const UString *THISCALL mv_spec_volume(FakeSpec *spec)
{
    return &spec->volume;
}

static const void *const kMvSpecVtbl[16] = {NULL, NULL, NULL, NULL, NULL, (void *)mv_spec_directory, NULL, (void *)mv_spec_file, NULL, NULL, (void *)mv_spec_volume};

/* SEMixer: the movie calls the function whose address the mixer object holds at offset 0xa8 (no arguments; it answers the DirectSound object). */
static void *__cdecl mv_mixer_sound(void)
{
    return (void *)0x5d5d0001;
}
static void *__cdecl mv_mixer_other_sound(void)
{
    return (void *)0x5d5d0002;
}

static struct {
    void (*reset)(unsigned, unsigned, unsigned);
    void (*open_fails)(int);
    void (*busy_waits)(unsigned);
    void (*set_namer)(const char *(*)(const void *));
    void (*release)(long);
    int (*settle)(unsigned);
    const char *(*take_log)(void);
    FakeSpec spec;
    void *movie;
    HMODULE module;
    void *mixer[2][64];
    void **active_rasterizer;
    void **active_mixer;
} g_mv;

typedef void *(THISCALL *MvCreate2)(void *self, const void *spec, unsigned long screen);
typedef void *(THISCALL *MvCreate4)(void *self, const void *spec, unsigned long screen, const void *rect, unsigned char looping);
typedef unsigned char(THISCALL *MvBool)(void *self);
typedef long(THISCALL *MvLong)(void *self);
typedef void(THISCALL *MvVoid)(void *self);
typedef UString *(THISCALL *MvName)(void *self, UString *out);

#define MV_SLOT(name) slot(*(void *const **)g_mv.movie, EEWEB_SLOT_GEMovie_##name)

/* How the Bink log names a pointer: a fake DirectDraw surface by its id. */
static const char *mv_namer(const void *p)
{
    static char text[24];
    if (p) {
        for (int id = 1; id <= g_life.dd->surface_count && id < 64; id++) {
            if (g_life.dd->by_id[id] == p) {
                snprintf(text, sizeof text, "fs%d", id);
                return text;
            }
        }
        if ((uintptr_t)p == 0x5d5d0001 || (uintptr_t)p == 0x5d5d0002) {
            snprintf(text, sizeof text, "sound%u", (unsigned)((uintptr_t)p & 3));
            return text;
        }
        return "ptr";
    }
    return "null";
}

/* The thread stands still (parked in a Bink call, or the movie is over); false would make the snapshot unstable. */
static void mv_settle(void)
{
    if (!g_mv.settle(30000)) {
        life_extra("settle=timeout ");
    }
}

static const char *mv_handle(uint32_t word)
{
    return word ? "set" : "null";
}

/* The movie object, word by word: the engine's handles only as set or null, the name as text. */
static void mv_report(void)
{
    const unsigned char *m = (const unsigned char *)g_mv.movie;
    const uint32_t *w = (const uint32_t *)m;
    const UString *name = (const UString *)(m + 0x44);
    life_extra("rc=%u self=%s loop=%u cs=%s thread=%s events=%s,%s buffer=%s src=%x,%x,%x,%x dst=%x,%x,%x,%x name=\"%s\" len=%u cap=%u screen=%u "
               "playing=%u stop=%u finished=%u ready=%u just=%u rect=%u",
               (unsigned)w[1], w[2] == (uint32_t)(uintptr_t)m ? "ok" : "other", m[0xc], mv_handle(w[4]), mv_handle(w[5]), mv_handle(w[6]), mv_handle(w[7]),
               mv_handle(w[8]), (unsigned)w[9], (unsigned)w[10], (unsigned)w[11], (unsigned)w[12], (unsigned)w[13], (unsigned)w[14], (unsigned)w[15],
               (unsigned)w[16], name->chars ? name->chars : "(null)", (unsigned)name->length, (unsigned)name->capacity, (unsigned)w[21], m[0x58], m[0x59],
               m[0x5a], m[0x5b], m[0x5c], m[0x5d]);
}

static void mv_bink(void)
{
    life_extra(" | bink=%s", g_mv.take_log());
}

#define MV_STEP(name, ...)                      \
    do {                                        \
        LS(name, LIFE_NODIFF, {                 \
            __VA_ARGS__;                        \
            mv_report();                        \
            mv_bink();                          \
        });                                     \
    } while (0)

/* A step that ends with BinkClose: the thread sleeps 100 ms after it and then lets go of its reference, so the object is not shown. */
#define MV_STEP_LOG(name, ...)                  \
    do {                                        \
        LS(name, LIFE_NODIFF, {                 \
            __VA_ARGS__;                        \
            mv_bink();                          \
        });                                     \
    } while (0)

/* Stop() blocks until the thread is over, and the thread may stand at a gate: run it on a helper thread, let the movie go on once the
 * stop flag is up, and wait for the helper. */
static DWORD WINAPI mv_stop_thread(LPVOID unused)
{
    (void)unused;
    ((MvVoid)MV_SLOT(Stop))(g_mv.movie);
    return 0;
}

static void mv_stop_running(void)
{
    HANDLE helper = CreateThread(NULL, 0, mv_stop_thread, NULL, 0, NULL);
    for (int i = 0; i < 5000 && !((volatile unsigned char *)g_mv.movie)[0x59]; i++) {
        Sleep(1);
    }
    g_mv.release(1);
    if (helper) {
        if (WaitForSingleObject(helper, 20000) != WAIT_OBJECT_0) {
            life_extra("stop=timeout ");
        }
        CloseHandle(helper);
    }
}

static void mv_make_spec(const char *volume, const char *directory, const char *file)
{
    if (!g_mv.spec.vtbl) {
        g_mv.spec.vtbl = kMvSpecVtbl;
        ustring_construct(&g_mv.spec.volume);
        ustring_construct(&g_mv.spec.directory);
        ustring_construct(&g_mv.spec.file);
    }
    ustring_assign_chars(&g_mv.spec.volume, volume);
    ustring_assign_chars(&g_mv.spec.directory, directory);
    ustring_assign_chars(&g_mv.spec.file, file);
}

static void mv_dereference(void *movie)
{
    typedef void(THISCALL * Dereference)(void *self);
    Dereference dereference;
    void *raw = proc_address(GetModuleHandleA("Low-Level Engine.dll"), "?Dereference@UReferenceCounter@@QAEXXZ");
    memcpy(&dereference, &raw, sizeof dereference);
    dereference(movie);
}

/* One movie of `frames` frames made with the short overload, played to its end: the thread is over when Stop() returns. */
static void mv_play_to_end(const char *label, unsigned frames, unsigned screen)
{
    char name[160];
    g_mv.reset(64, 48, frames);
    snprintf(name, sizeof name, "movie: %s: CreateMovie", label);
    MV_STEP(name, g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, screen); ret = g_mv.movie != NULL; mv_settle());
    snprintf(name, sizeof name, "movie: %s: BinkOpen is let through", label);
    MV_STEP(name, g_mv.release(1); mv_settle());
    for (unsigned frame = 1; frame <= frames; frame++) {
        snprintf(name, sizeof name, "movie: %s: frame %u", label, frame);
        if (frame < frames) {
            MV_STEP(name, g_mv.release(1); mv_settle());
        } else {
            MV_STEP_LOG(name, g_mv.release(1); mv_settle());
        }
    }
}

/* The thread's last act sets the event Stop() waits for, and the wait takes the event back: only one Stop() per movie can return. */
static void mv_end(const char *label, int stopped)
{
    char name[160];
    if (!stopped) {
        snprintf(name, sizeof name, "movie: %s: Stop after the end waits for the thread", label);
        MV_STEP(name, ((MvVoid)MV_SLOT(Stop))(g_mv.movie));
    }
    snprintf(name, sizeof name, "movie: %s: RenderFrame after the end answers 7", label);
    MV_STEP(name, ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    snprintf(name, sizeof name, "movie: %s: the last reference goes", label);
    LS(name, LIFE_NODIFF, {
        mv_dereference(g_mv.movie);
        mv_bink();
    });
    g_mv.movie = NULL;
}

static void mv_basic(void)
{
    mv_make_spec("C:\\", "Movies\\", "intro.bik");
    g_mv.reset(64, 48, 3);
    MV_STEP("movie: CreateMovie(file, screen 0) builds the movie", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    {
        void *const *vtable = *(void *const **)g_mv.movie;
        unsigned slots = vtable_slot_count(g_mv.module, vtable);
        const struct RttiLocator *col = locator_of(vtable);
        CHECK(slots == EEWEB_SLOTS_GEMovie && strcmp(col->type->name, ".?AVDX7Movie@@") == 0, "%s: movie: the object is a DX7Movie with %u slots (%s)",
              g_life.e->label, slots, col->type->name);
    }
    MV_STEP("movie: the getters before the thread has opened the file", UString text; ustring_construct(&text);
            ((MvName)MV_SLOT(GetFilename))(g_mv.movie, &text);
            life_extra("filename=\"%s\" looping=%u playing=%u ready=%u just=%u ", text.chars, ((MvBool)MV_SLOT(GetIsLooping))(g_mv.movie) & 1,
                       ((MvBool)MV_SLOT(GetIsPlaying))(g_mv.movie) & 1, ((MvBool)MV_SLOT(GetIsReady))(g_mv.movie) & 1,
                       ((MvBool)MV_SLOT(GetJustStarted))(g_mv.movie) & 1);
            ustring_destruct(&text));
    MV_STEP("movie: BinkOpen is let through", g_mv.release(1); mv_settle());
    MV_STEP("movie: the getters once the file is open", life_extra("looping=%u playing=%u ready=%u just=%u ", ((MvBool)MV_SLOT(GetIsLooping))(g_mv.movie) & 1,
                                                             ((MvBool)MV_SLOT(GetIsPlaying))(g_mv.movie) & 1, ((MvBool)MV_SLOT(GetIsReady))(g_mv.movie) & 1,
                                                             ((MvBool)MV_SLOT(GetJustStarted))(g_mv.movie) & 1));
    MV_STEP("movie: frame 1", g_mv.release(1); mv_settle());
    MV_STEP("movie: the getters after the first frame", life_extra("looping=%u playing=%u ready=%u just=%u ", ((MvBool)MV_SLOT(GetIsLooping))(g_mv.movie) & 1,
                                                                ((MvBool)MV_SLOT(GetIsPlaying))(g_mv.movie) & 1, ((MvBool)MV_SLOT(GetIsReady))(g_mv.movie) & 1,
                                                                ((MvBool)MV_SLOT(GetJustStarted))(g_mv.movie) & 1));
    MV_STEP("movie: RenderFrame blits the frame on the screen's render target", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    MV_STEP("movie: frame 2", g_mv.release(1); mv_settle());
    MV_STEP("movie: RenderFrame again", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    MV_STEP_LOG("movie: frame 3, the last", g_mv.release(1); mv_settle());
    mv_end("a movie of three frames", 0);
}

/* A looping movie in a rectangle of its own on the second screen: the frames wrap, the end is never reached and Stop ends it. */
static void mv_looping(void)
{
    static const GERectangleLayout kRect = {300.5f, 20.25f, 120.75f, 40.0f};
    mv_make_spec("D:\\", "", "loop.bik");
    g_mv.reset(64, 48, 3);
    MV_STEP("movie: CreateMovie(file, screen 1, rectangle, looping)", g_mv.movie = ((MvCreate4)LIFE_SLOT(CreateMovie_1))(g_life.obj, &g_mv.spec, 1, &kRect, 1);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: the looping movie opens its file", g_mv.release(1); mv_settle());
    for (int frame = 1; frame <= 4; frame++) {
        char name[64];
        snprintf(name, sizeof name, "movie: the looping movie, frame %d", frame);
        MV_STEP(name, g_mv.release(1); mv_settle());
    }
    MV_STEP("movie: the looping movie says it loops", life_extra("looping=%u ", ((MvBool)MV_SLOT(GetIsLooping))(g_mv.movie) & 1));
    MV_STEP("movie: RenderFrame blits into the rectangle of the second screen", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    MV_STEP("movie: Stop while the thread is between two frames", mv_stop_running());
    mv_end("the looping movie", 1);
}

/* A rectangle of its own without looping: the movie ends after its last frame and the rectangle is not replaced by the video's size. */
static void mv_rectangle(void)
{
    static const GERectangleLayout kRect = {200.0f, 10.0f, 300.0f, 5.0f};
    mv_make_spec("E:\\", "a\\b\\", "c.bik");
    g_mv.reset(40, 30, 2);
    MV_STEP("movie: CreateMovie(file, screen 0, rectangle, not looping)", g_mv.movie = ((MvCreate4)LIFE_SLOT(CreateMovie_1))(g_life.obj, &g_mv.spec, 0, &kRect, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: the movie in a rectangle opens its file", g_mv.release(1); mv_settle());
    MV_STEP("movie: the movie in a rectangle, frame 1", g_mv.release(1); mv_settle());
    MV_STEP("movie: RenderFrame blits into the rectangle", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    MV_STEP_LOG("movie: the movie in a rectangle, frame 2, the last", g_mv.release(1); mv_settle());
    mv_end("the movie in a rectangle", 0);
}

/* Bink cannot open the file: the thread ends without a frame buffer. */
static void mv_open_fails(void)
{
    mv_make_spec("C:\\", "Movies\\", "missing.bik");
    g_mv.reset(64, 48, 3);
    g_mv.open_fails(1);
    MV_STEP("movie: CreateMovie of a file Bink cannot open", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: BinkOpen fails: the thread ends, Stop waits for it", g_mv.release(1); ((MvVoid)MV_SLOT(Stop))(g_mv.movie));
    MV_STEP("movie: RenderFrame of the movie that never played answers 7", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    LS("movie: the last reference of the movie that never played goes", LIFE_NODIFF, {
        mv_dereference(g_mv.movie);
        mv_bink();
    });
    g_mv.movie = NULL;
}

/* The frame buffer cannot be made (the screen's CreateSurface fails): the movie counts as finished at once. */
static void mv_buffer_fails(void)
{
    mv_make_spec("C:\\", "Movies\\", "nobuffer.bik");
    g_mv.reset(64, 48, 3);
    g_life.dd->create_results[0] = LIFE_ERR_OTHER;
    g_life.dd->create_result_count = 1;
    g_life.dd->create_result_pos = 0;
    MV_STEP("movie: CreateMovie when the frame buffer cannot be made", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP_LOG("movie: BinkOpen is let through, the movie ends at once", g_mv.release(1); mv_settle());
    g_life.dd->create_result_count = 0;
    mv_end("the movie without a frame buffer", 0);
}

/* The decoder is told about the DirectSound object of the engine's mixer, once for each object. */
static void mv_sound(void)
{
    mv_make_spec("C:\\", "Movies\\", "sound.bik");
    *g_mv.active_mixer = g_mv.mixer[0];
    mv_play_to_end("with a mixer", 1, 0);
    mv_end("the movie with a mixer", 0);
    mv_play_to_end("with the same mixer again", 1, 0);
    mv_end("the movie with the same mixer again", 0);
    *g_mv.active_mixer = g_mv.mixer[1];
    mv_play_to_end("with another DirectSound object", 1, 0);
    mv_end("the movie with another DirectSound object", 0);
    *g_mv.active_mixer = NULL;
    mv_play_to_end("without a mixer after one", 1, 0);
    mv_end("the movie without a mixer after one", 0);
}

/* BinkWait answers "not yet" twice: the thread sleeps and asks again. */
static void mv_busy(void)
{
    mv_make_spec("C:\\", "Movies\\", "busy.bik");
    g_mv.reset(64, 48, 1);
    g_mv.busy_waits(2);
    MV_STEP("movie: CreateMovie while Bink is not ready", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: BinkWait answers not ready twice before it answers ready", g_mv.release(1); mv_settle());
    MV_STEP_LOG("movie: the only frame", g_mv.release(1); mv_settle());
    mv_end("the movie that waited", 0);
}

/* Stop before the first frame: the file is opened, the frame buffer made, and the movie ends without a frame. */
static void mv_stop_early(void)
{
    mv_make_spec("C:\\", "Movies\\", "early.bik");
    g_mv.reset(64, 48, 3);
    MV_STEP("movie: CreateMovie to be stopped before its first frame", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: Stop before BinkOpen has been let through", mv_stop_running());
    MV_STEP("movie: RenderFrame of the stopped movie answers 7", ret = ((MvLong)MV_SLOT(RenderFrame))(g_mv.movie));
    LS("movie: the last reference of the stopped movie goes", LIFE_NODIFF, {
        mv_dereference(g_mv.movie);
        mv_bink();
    });
    g_mv.movie = NULL;
}

/* The frame buffer cannot be locked for a frame: the frame is not copied and not flagged ready, and the movie goes on with the next one. */
static void mv_lock_fails(void)
{
    mv_make_spec("C:\\", "Movies\\", "locked.bik");
    g_mv.reset(64, 48, 2);
    MV_STEP("movie: CreateMovie whose first frame cannot be copied", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0);
            ret = g_mv.movie != NULL; mv_settle());
    MV_STEP("movie: the movie opens its file", g_mv.release(1); mv_settle());
    LIFE_SCRIPT(g_life.dd->surface_count, FS_LOCK, LIFE_ERR_LOST, 1);
    MV_STEP("movie: frame 1, the frame buffer cannot be locked", g_mv.release(1); mv_settle());
    MV_STEP_LOG("movie: frame 2, the last", g_mv.release(1); mv_settle());
    mv_end("the movie whose first frame was lost", 0);
}

/* Two movies: the second one's thread waits for the first one's to be over (they share one critical section). */
static void mv_two(void)
{
    void *first, *second;
    mv_make_spec("C:\\", "Movies\\", "first.bik");
    g_mv.reset(64, 48, 1);
    MV_STEP("movie: two movies: the first is made", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 0); ret = g_mv.movie != NULL;
            mv_settle());
    first = g_mv.movie;
    mv_make_spec("C:\\", "Movies\\", "second.bik");
    MV_STEP("movie: two movies: the second is made while the first holds the critical section", g_mv.movie = ((MvCreate2)LIFE_SLOT(CreateMovie_2))(g_life.obj, &g_mv.spec, 1);
            ret = g_mv.movie != NULL; Sleep(50));
    second = g_mv.movie;
    g_mv.movie = first;
    MV_STEP("movie: two movies: the first opens its file", g_mv.release(1); mv_settle());
    MV_STEP_LOG("movie: two movies: the first plays its only frame", g_mv.release(1); mv_settle());
    MV_STEP("movie: two movies: the first is over", ((MvVoid)MV_SLOT(Stop))(g_mv.movie));
    g_mv.movie = second;
    MV_STEP("movie: two movies: the second is let in and stands before BinkOpen", mv_settle());
    MV_STEP("movie: two movies: the second opens its file", g_mv.release(1); mv_settle());
    MV_STEP_LOG("movie: two movies: the second plays its only frame", g_mv.release(1); mv_settle());
    mv_end("the second of two movies", 0);
    g_mv.movie = first;
    LS("movie: two movies: the last reference of the first goes", LIFE_NODIFF, {
        mv_dereference(g_mv.movie);
        mv_bink();
    });
    g_mv.movie = NULL;
}

static void mv_run(void)
{
    mv_basic();
    mv_looping();
    mv_rectangle();
    mv_open_fails();
    mv_buffer_fails();
    mv_sound();
    mv_busy();
    mv_stop_early();
    mv_lock_fails();
    mv_two();
}

static void check_movie(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    HMODULE bink = LoadLibraryA("binkw32.dll");
    void *raw[7];
    const char *names[7] = {"FakeBinkReset", "FakeBinkOpenFails", "FakeBinkBusyWaits", "FakeBinkSetNamer", "FakeBinkRelease", "FakeBinkSettle", "FakeBinkTakeLog"};
    for (int i = 0; i < 7; i++) {
        raw[i] = bink ? proc_address(bink, names[i]) : NULL;
    }
    CHECK(bink && raw[0] && raw[1] && raw[2] && raw[3] && raw[4] && raw[5] && raw[6], "%s: movie: the scripted Bink library is loaded", e->label);
    HMODULE engine = GetModuleHandleA("Low-Level Engine.dll");
    void **active_rasterizer = (void **)proc_address(engine, "?sActiveRasterizer@GERasterizer@@1PAV1@A");
    void **active_mixer = (void **)proc_address(engine, "?sActiveMixer@SEMixer@@0PAV1@A");
    CHECK(active_rasterizer && active_mixer, "%s: movie: the engine's active rasterizer and mixer variables are reachable", e->label);
    if (!bink || !raw[0] || !raw[1] || !raw[2] || !raw[3] || !raw[4] || !raw[5] || !raw[6] || !active_rasterizer || !active_mixer ||
        !life_begin(e, api, obj, 2, 0, "the movies")) {
        return;
    }
    memset(&g_mv, 0, sizeof g_mv);
    g_mv.module = h;
    memcpy(&g_mv.reset, &raw[0], sizeof raw[0]);
    memcpy(&g_mv.open_fails, &raw[1], sizeof raw[1]);
    memcpy(&g_mv.busy_waits, &raw[2], sizeof raw[2]);
    memcpy(&g_mv.set_namer, &raw[3], sizeof raw[3]);
    memcpy(&g_mv.release, &raw[4], sizeof raw[4]);
    memcpy(&g_mv.settle, &raw[5], sizeof raw[5]);
    memcpy(&g_mv.take_log, &raw[6], sizeof raw[6]);
    g_mv.active_rasterizer = active_rasterizer;
    g_mv.active_mixer = active_mixer;
    g_mv.mixer[0][42] = (void *)mv_mixer_sound;
    g_mv.mixer[1][42] = (void *)mv_mixer_other_sound;
    g_mv.set_namer(mv_namer);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    void *saved_rasterizer = *active_rasterizer;
    void *saved_mixer = *active_mixer;
    *active_rasterizer = obj;
    *active_mixer = NULL;
    mv_run();
    *active_rasterizer = saved_rasterizer;
    *active_mixer = saved_mixer;
    life_end();
}
