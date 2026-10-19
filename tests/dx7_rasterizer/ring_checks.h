/*
 * The ring vertex buffer of DX7Rasterizer (task P2.6.6): the protected helpers LockVB and UnlockVB the draw slots use, called through
 * the names the DLLs export them under.  Included by screen_checks.h after texture_checks.h and run from the screen probe; same method
 * as lifecycle_checks.h (the steps and the literals of ring_expected.inc, recorded from the original DLL with EEWEB_LIFE_RECORD=1).
 * The record's cursor (0x28) is set by the test: in the draw slots it is advanced after each draw, which is not part of this unit.
 */

#define RING_VERTICES 0x98a
#define RING_BUSY 0x887601AEL /* the error LockVB and UnlockVB wait on (-0x7789fe52) */
#define RING_OTHER 0x88760064L

typedef long(THISCALL *RingLock)(void *self, unsigned long count, unsigned long screen, void **vertices);
typedef long(THISCALL *RingUnlock)(void *self, unsigned long screen);

static struct {
    RingLock lock;
    RingUnlock unlock;
} g_ring;

/* Where the pointer LockVB hands back points: the buffer's memory, nothing, or what the caller put there before. */
static const char *ring_pointer_name(void *pointer, int screen, void *stale)
{
    if (pointer == NULL) {
        return "null";
    }
    if (pointer == stale) {
        return "stale";
    }
    return pointer == (void *)((FakeVB *)g_life.dd->vbs[screen])->memory ? "vb" : "other";
}

static void ring_lock_step(const char *name, int screen, unsigned long cursor, unsigned long count, void *stale)
{
    void *vertices = stale;
    g_life.cur = screen;
    life_record(screen)->vertexCursor = cursor;
    LS(name, LIFE_DIFF, {
        ret = g_ring.lock(g_life.obj, count, (unsigned long)screen, &vertices);
        life_extra("pointer=%s", ring_pointer_name(vertices, screen, stale));
    });
}

static void ring_lock(void)
{
    void *const kStale = (void *)0x1234;
    ring_lock_step("LockVB(1) at the start of the ring", 0, 0, 1, NULL);
    ring_lock_step("LockVB(1) in the middle of the ring", 0, 100, 1, NULL);
    ring_lock_step("LockVB(the whole ring) at the start", 0, 0, RING_VERTICES, NULL);
    ring_lock_step("LockVB(one more than the ring)", 0, 0, RING_VERTICES + 1, NULL);
    ring_lock_step("LockVB(10) with exactly 10 left", 0, RING_VERTICES - 10, 10, NULL);
    ring_lock_step("LockVB(11) with 10 left starts the ring again", 0, RING_VERTICES - 10, 11, NULL);
    ring_lock_step("LockVB(1) at the end of the ring", 0, RING_VERTICES, 1, NULL);
    ring_lock_step("LockVB(0) at the end of the ring", 0, RING_VERTICES, 0, NULL);
    ring_lock_step("LockVB(whole ring) with 1 vertex used", 0, 1, RING_VERTICES, NULL);
    ring_lock_step("LockVB(1) with the cursor past the end", 0, RING_VERTICES + 6, 1, NULL);
    ring_lock_step("LockVB(5) on screen 1", 1, 7, 5, NULL);
    ring_lock_step("LockVB(whole ring) on screen 1 with 7 used", 1, 7, RING_VERTICES, NULL);

    g_life.dd->vb_script[FV_LOCK].code = RING_BUSY;
    g_life.dd->vb_script[FV_LOCK].times = 2;
    ring_lock_step("LockVB while Direct3D answers busy twice", 0, 5, 3, NULL);
    g_life.dd->vb_script[FV_LOCK].code = RING_BUSY;
    g_life.dd->vb_script[FV_LOCK].times = 2;
    ring_lock_step("LockVB while Direct3D answers busy twice, with an old pointer", 0, 5, 3, kStale);
    g_life.dd->vb_null_lock_times = 2;
    ring_lock_step("LockVB while the lock succeeds without a pointer twice", 0, 5, 3, NULL);
    g_life.dd->vb_null_lock_times = 2;
    ring_lock_step("LockVB when the lock succeeds without a pointer, with an old pointer", 0, 5, 3, kStale);
    g_life.dd->vb_null_lock_times = 0;
    g_life.dd->vb_script[FV_LOCK].code = RING_OTHER;
    g_life.dd->vb_script[FV_LOCK].times = 1;
    ring_lock_step("LockVB when the lock fails", 0, 5, 3, NULL);
    g_life.dd->vb_script[FV_LOCK].code = RING_OTHER;
    g_life.dd->vb_script[FV_LOCK].times = 1;
    ring_lock_step("LockVB when the lock fails, with an old pointer", 0, 5, 3, kStale);
    g_life.dd->vb_script[FV_LOCK].code = RING_BUSY;
    g_life.dd->vb_script[FV_LOCK].times = 1;
    g_life.dd->vb_null_lock_times = 1;
    ring_lock_step("LockVB busy, then a lock without a pointer, then a pointer", 1, RING_VERTICES - 1, 2, NULL);
    g_life.dd->vb_null_lock_times = 0;
}

static void ring_unlock(void)
{
    static const struct {
        const char *name;
        int screen;
        long code;
        int times;
    } kCases[] = {
        {"UnlockVB", 0, 0, 0},
        {"UnlockVB on screen 1", 1, 0, 0},
        {"UnlockVB while Direct3D answers busy twice", 0, RING_BUSY, 2},
        {"UnlockVB when the unlock fails", 0, RING_OTHER, 1},
        {"UnlockVB when the unlock fails on screen 1", 1, (long)0x8876017c, 1},
    };
    for (unsigned i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        g_life.dd->vb_script[FV_UNLOCK].code = kCases[i].code;
        g_life.dd->vb_script[FV_UNLOCK].times = kCases[i].times;
        g_life.cur = kCases[i].screen;
        LS(kCases[i].name, LIFE_DIFF, ret = g_ring.unlock(g_life.obj, (unsigned long)kCases[i].screen));
    }
    g_life.dd->vb_script[FV_UNLOCK].times = 0;
}

/* Our build only: a screen without a vertex buffer is refused (the original dereferences it), and nothing waits for ever. */
static void ring_ours_only(void)
{
    void *vertices = (void *)0x1234;
    log_clear(g_life.dd);
    long locked = g_ring.lock(g_life.obj, 1, 9, &vertices);
    long unlocked = g_ring.unlock(g_life.obj, 9);
    CHECK(locked != 0 && vertices == NULL && unlocked != 0 && g_life.dd->log[0] == 0,
          "%s: ring: a screen that does not exist is refused without a call (lock %ld, unlock %ld, pointer %p, log \"%s\")", g_life.e->label, locked, unlocked,
          vertices, g_life.dd->log);
}

static void check_ring(const struct Expect *e, HMODULE h, const ScreenApi *api, void *obj)
{
    void *lock = proc_address(h, "?LockVB@DX7Rasterizer@@IAEJKKAAPAUD3DVERTEX_STD_COLOR@@@Z");
    void *unlock = proc_address(h, "?UnlockVB@DX7Rasterizer@@IAEJK@Z");
    CHECK(lock != NULL && unlock != NULL, "%s: ring: exports LockVB and UnlockVB", e->label);
    if (!lock || !unlock || !life_begin(e, api, obj, 2, 0, "the ring vertex buffer on two windowed screens")) {
        return;
    }
    memcpy(&g_ring.lock, &lock, sizeof lock);
    memcpy(&g_ring.unlock, &unlock, sizeof unlock);
    life_activate(0, 1, 1, 10);
    life_activate(1, 1, 1, 10);
    ring_lock();
    ring_unlock();
    if (e->ours) {
        ring_ours_only();
    }
    life_end();
}

