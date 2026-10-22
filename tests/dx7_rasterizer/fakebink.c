/*
 * A scripted stand-in for Bink's binkw32.dll (task P2.6.7k), put in the test beds of both the original DX7HRTnLDisplay.dll and ours in place of
 * the real library.  DX7Movie plays a movie on a thread of its own that calls nine Bink functions; this one answers them from a script and
 * logs every call, and it parks the movie thread at fixed points so the harness can look at the movie object while the thread stands still:
 *
 *   - BinkOpen waits for a token before it opens the file (the thread has done nothing observable until then),
 *   - a BinkWait that answers "ready" waits for a token before it returns (one token plays one frame).
 *
 * The harness drives it through the plain-C exports at the bottom (the Bink functions themselves are exported under the names the real
 * library has, with the underscore and the @n of stdcall: binkw32.def).  Nothing here depends on the plugin that loads it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define STDCALL __attribute__((stdcall))
#define EXPORT __declspec(dllexport)

/* The fields of the real BINK structure the plugin reads (see the decompilation of DX7Movie): width, height, frame count, current frame. */
typedef struct FakeBinkFile {
    unsigned width, height, frames, frame;
    unsigned pad[16];
} FakeBinkFile;

static CRITICAL_SECTION g_lock;
static HANDLE g_tokens;
static char g_log[8192];
static unsigned g_width = 64, g_height = 48, g_frames = 3;
static int g_open_fails;
static unsigned g_busy_waits;
static LONG g_parked;     /* threads waiting at a gate */
static LONG g_released;   /* tokens given out and not yet taken by a thread */
static LONG g_closed;     /* BinkClose has been called */
static LONG g_open_calls;
static FakeBinkFile g_file;
static const char *(*g_namer)(const void *);
static unsigned g_surface_type = 0x1d;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    EnterCriticalSection(&g_lock);
    size_t used = strlen(g_log);
    if (used) {
        g_log[used++] = ';';
    }
    va_start(ap, fmt);
    vsnprintf(g_log + used, sizeof g_log - used, fmt, ap);
    va_end(ap);
    LeaveCriticalSection(&g_lock);
}

static void gate(void)
{
    InterlockedIncrement(&g_parked);
    WaitForSingleObject(g_tokens, INFINITE);
    InterlockedDecrement(&g_parked); /* in this order: the harness takes "parked and nothing released" for standing still */
    InterlockedDecrement(&g_released);
}

static const char *name_of(const void *p)
{
    return g_namer ? g_namer(p) : (p ? "ptr" : "null");
}

static void *STDCALL fake_open(const char *name, unsigned flags)
{
    gate();
    InterlockedIncrement(&g_open_calls);
    log_line("BinkOpen(\"%s\",0x%x)", name ? name : "(null)", flags);
    if (g_open_fails) {
        return NULL;
    }
    g_file.width = g_width;
    g_file.height = g_height;
    g_file.frames = g_frames;
    g_file.frame = 1;
    return &g_file;
}

static int STDCALL fake_wait(FakeBinkFile *bink)
{
    (void)bink;
    EnterCriticalSection(&g_lock);
    int busy = g_busy_waits > 0;
    if (busy) {
        g_busy_waits--;
    }
    LeaveCriticalSection(&g_lock);
    if (busy) {
        log_line("BinkWait -> 1");
        return 1;
    }
    gate();
    log_line("BinkWait -> 0");
    return 0;
}

static int STDCALL fake_do_frame(FakeBinkFile *bink)
{
    log_line("BinkDoFrame(frame %u)", bink->frame);
    return 0;
}

static unsigned STDCALL fake_dd_surface_type(void *surface)
{
    log_line("BinkDDSurfaceType(%s)", name_of(surface));
    return g_surface_type;
}

static int STDCALL fake_copy_to_buffer(FakeBinkFile *bink, void *dest, int pitch, unsigned height, unsigned x, unsigned y, unsigned flags)
{
    /* Leave a mark in the destination so the harness can see the frame arrived: the frame number in the first byte. */
    if (dest) {
        ((unsigned char *)dest)[0] = (unsigned char)bink->frame;
    }
    log_line("BinkCopyToBuffer(%s,pitch=%d,height=%u,x=%u,y=%u,flags=0x%x)", dest ? "buffer" : "null", pitch, height, x, y, flags);
    return 0;
}

static void STDCALL fake_next_frame(FakeBinkFile *bink)
{
    bink->frame = bink->frame >= bink->frames ? 1 : bink->frame + 1;
    log_line("BinkNextFrame -> %u", bink->frame);
}

static void STDCALL fake_close(FakeBinkFile *bink)
{
    (void)bink;
    log_line("BinkClose");
    InterlockedIncrement(&g_closed);
}

/* The Bink API, under the names the real library exports. */
void *STDCALL BinkOpenImpl(const char *name, unsigned flags) { return fake_open(name, flags); }
int STDCALL BinkWaitImpl(FakeBinkFile *bink) { return fake_wait(bink); }
int STDCALL BinkDoFrameImpl(FakeBinkFile *bink) { return fake_do_frame(bink); }
unsigned STDCALL BinkDDSurfaceTypeImpl(void *surface) { return fake_dd_surface_type(surface); }
int STDCALL BinkCopyToBufferImpl(FakeBinkFile *bink, void *dest, int pitch, unsigned height, unsigned x, unsigned y, unsigned flags)
{
    return fake_copy_to_buffer(bink, dest, pitch, height, x, y, flags);
}
void STDCALL BinkNextFrameImpl(FakeBinkFile *bink) { fake_next_frame(bink); }
void STDCALL BinkCloseImpl(FakeBinkFile *bink) { fake_close(bink); }
void *STDCALL BinkOpenDirectSoundImpl(unsigned long param)
{
    (void)param;
    return NULL;
}
int STDCALL BinkSetSoundSystemImpl(void *open_function, void *direct_sound)
{
    /* The plugin passes the address of BinkOpenDirectSound as the library exports it: this very export. */
    log_line("BinkSetSoundSystem(%s,%s)", open_function == (void *)&BinkOpenDirectSoundImpl ? "BinkOpenDirectSound" : "other", name_of(direct_sound));
    return 1;
}

/* ---- the harness's side ---------------------------------------------------------------------------------------------------------------- */

/* Forget everything: log, tokens, parked threads' counters, the movie's size and length. */
EXPORT void FakeBinkReset(unsigned width, unsigned height, unsigned frames)
{
    EnterCriticalSection(&g_lock);
    g_log[0] = 0;
    g_width = width;
    g_height = height;
    g_frames = frames;
    g_open_fails = 0;
    g_busy_waits = 0;
    g_closed = 0;
    g_open_calls = 0;
    g_released = 0;
    LeaveCriticalSection(&g_lock);
    while (WaitForSingleObject(g_tokens, 0) == WAIT_OBJECT_0) {
    }
}

EXPORT void FakeBinkOpenFails(int fails) { g_open_fails = fails; }
EXPORT void FakeBinkBusyWaits(unsigned count) { g_busy_waits = count; }
EXPORT void FakeBinkSetNamer(const char *(*namer)(const void *)) { g_namer = namer; }

/* Let `count` parked calls go on. */
EXPORT void FakeBinkRelease(long count)
{
    InterlockedExchangeAdd(&g_released, count);
    ReleaseSemaphore(g_tokens, count, NULL);
}

/* Wait until the movie thread stands at a gate with no token left for it, or BinkClose has been called (once per call of BinkClose);
 * 0 on time-out. */
EXPORT int FakeBinkSettle(unsigned timeout_ms)
{
    for (unsigned waited = 0; waited <= timeout_ms; waited++) {
        if (g_parked > 0 && g_released == 0) {
            return 1;
        }
        if (g_closed > 0) {
            InterlockedExchange(&g_closed, 0); /* taken */
            return 1;
        }
        Sleep(1);
    }
    return 0;
}

EXPORT long FakeBinkClosed(void) { return g_closed; }
EXPORT long FakeBinkOpenCalls(void) { return g_open_calls; }

/* The log since the last call, calls separated by ';'; the buffer is valid until the next call. */
EXPORT const char *FakeBinkTakeLog(void)
{
    static char copy[sizeof g_log];
    EnterCriticalSection(&g_lock);
    memcpy(copy, g_log, sizeof copy);
    g_log[0] = 0;
    LeaveCriticalSection(&g_lock);
    return copy;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&g_lock);
        g_tokens = CreateSemaphoreA(NULL, 0, 1000000, NULL);
    }
    return TRUE;
}
