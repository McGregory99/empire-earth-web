/* The trace sink: one text line per call, assembled by the callers, written under a lock. */
#include "ddt.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_ready;
static HANDLE g_file = INVALID_HANDLE_VALUE, g_times = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_t0, g_freq;
static int g_opened, g_capped;
static unsigned g_presents;
static char g_out[1 << 16];
static unsigned g_out_len;
static char g_last[1024];
static DWORD g_tls = TLS_OUT_OF_INDEXES;
static unsigned g_nthreads;
static char g_modlist[16][64];
static int g_nmods;
static struct { char name[64]; unsigned calls; int traced; } g_callers[32];
static int g_ncallers;

void buf_init(Buf *b, char *storage, unsigned cap) {
    b->p = storage;
    b->len = 0;
    b->cap = cap;
    if (cap)
        b->p[0] = 0;
}

void buf_puts(Buf *b, const char *s) {
    unsigned n = (unsigned)strlen(s);
    if (b->len + n + 1 > b->cap)
        n = b->cap > b->len + 1 ? b->cap - b->len - 1 : 0;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    int n;
    if (b->len + 1 >= b->cap)
        return;
    va_start(ap, fmt);
    n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (unsigned)n >= b->cap - b->len)
        b->len = b->cap - 1;
    else
        b->len += (unsigned)n;
}

uint64_t fnv1a64_update(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n--) {
        h ^= *p++;
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t fnv1a64(const void *data, size_t n) { return fnv1a64_update(FNV1A64_INIT, data, n); }

static void lock(void) {
    /* The critical section is created on first use; a race here would need two threads making their first
     * traced call in the same instant, before any line exists. */
    if (!g_cs_ready) {
        static volatile LONG once;
        if (InterlockedCompareExchange(&once, 1, 0) == 0) {
            InitializeCriticalSection(&g_cs);
            g_cs_ready = 1;
        } else {
            while (!g_cs_ready)
                Sleep(0);
        }
    }
    EnterCriticalSection(&g_cs);
}

static void unlock(void) { LeaveCriticalSection(&g_cs); }

/* Count the calls per calling module (traced or not) for the <trace>.callers diagnostic. */
static void note_caller_locked(const char *name, int traced) {
    int i;
    for (i = 0; i < g_ncallers; i++)
        if (!lstrcmpiA(g_callers[i].name, name)) {
            g_callers[i].calls++;
            return;
        }
    if (g_ncallers < 32) {
        lstrcpynA(g_callers[g_ncallers].name, name, sizeof g_callers[0].name);
        g_callers[g_ncallers].calls = 1;
        g_callers[g_ncallers].traced = traced;
        g_ncallers++;
    }
}

static void write_callers_locked(void) {
    char path[MAX_PATH + 12], line[128];
    HANDLE h;
    DWORD done;
    int i, n;
    if (!g_cfg.trace_path[0])
        return;
    snprintf(path, sizeof path, "%s.callers", g_cfg.trace_path);
    h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    for (i = 0; i < g_ncallers; i++) {
        n = snprintf(line, sizeof line, "%s %u %s\n", g_callers[i].name, g_callers[i].calls,
                     g_callers[i].traced ? "traced" : "untraced");
        WriteFile(h, line, (DWORD)n, &done, NULL);
    }
    CloseHandle(h);
}

static void flush_locked(void) {
    DWORD done;
    if (g_file != INVALID_HANDLE_VALUE && g_out_len)
        WriteFile(g_file, g_out, g_out_len, &done, NULL);
    g_out_len = 0;
}

static void put_locked(const char *s, unsigned n) {
    if (g_out_len + n > sizeof g_out)
        flush_locked();
    if (n > sizeof g_out) {
        DWORD done;
        WriteFile(g_file, s, n, &done, NULL);
        return;
    }
    memcpy(g_out + g_out_len, s, n);
    g_out_len += n;
}

static void open_locked(void) {
    char head[1024];
    int n;
    char *c, *tok;
    char list[512];

    g_opened = 1;
    if (!g_cfg.trace_path[0])
        return;
    g_file = CreateFileA(g_cfg.trace_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE)
        return;
    /* <trace>.times: wall-clock milliseconds of every present, a diagnostic that stays out of the normalized trace */
    {
        char times[MAX_PATH + 8];
        snprintf(times, sizeof times, "%s.times", g_cfg.trace_path);
        g_times = CreateFileA(times, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
    }
    lstrcpynA(list, g_cfg.modules, sizeof list);
    for (tok = list; tok && *tok && g_nmods < 16;) {
        c = strchr(tok, ',');
        if (c)
            *c++ = 0;
        lstrcpynA(g_modlist[g_nmods++], tok, sizeof g_modlist[0]);
        tok = c;
    }
    n = snprintf(head, sizeof head, "# ddraw_trace v1\n# modules: %s\n", g_cfg.modules);
    put_locked(head, (unsigned)n);
}

static int active_locked(void) {
    if (!g_opened)
        open_locked();
    return g_file != INVALID_HANDLE_VALUE && !g_capped;
}

int trace_active(void) {
    int a;
    ddt_init();
    if (!g_cfg.trace_path[0])
        return 0;
    lock();
    a = active_locked();
    unlock();
    return a;
}

/* Is the call coming from one of the configured modules (the renderer plugins)?  The caller's module is
 * found from the return address into the proxy's thunk. */
int trace_wants(void *ra) {
    HMODULE m = NULL;
    char path[MAX_PATH], *base;
    int i, hit = 0;

    if (!trace_active())
        return 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)ra, &m) || !m)
        return 0;
    if (!GetModuleFileNameA(m, path, sizeof path))
        return 0;
    base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    for (i = 0; i < g_nmods; i++)
        if (!lstrcmpiA(base, g_modlist[i])) {
            hit = 1;
            break;
        }
    lock();
    note_caller_locked(base, hit);
    unlock();
    return hit;
}

void trace_emit(int depth, const char *text, int timing) {
    char line[8400];
    unsigned n;
    unsigned tid;

    if (!g_cfg.trace_path[0])
        return;
    lock();
    if (!active_locked()) {
        unlock();
        return;
    }
    if (g_tls == TLS_OUT_OF_INDEXES)
        g_tls = TlsAlloc();
    tid = (unsigned)(uintptr_t)TlsGetValue(g_tls);
    if (!tid) {
        tid = ++g_nthreads;
        TlsSetValue(g_tls, (void *)(uintptr_t)tid);
    }
    n = (unsigned)snprintf(line, sizeof line, "T%u %*s%s\n", tid, depth * 2, "", text);
    if (n >= sizeof line)
        n = sizeof line - 1;
    /* A run of identical timing-dependent calls (polling) is written once: its length is not reproducible. */
    if (timing && !strncmp(g_last, line, sizeof g_last)) {
        unlock();
        return;
    }
    lstrcpynA(g_last, timing ? line : "", sizeof g_last);
    put_locked(line, n);
    if (g_cfg.flush_each)
        flush_locked();
    unlock();
}

void trace_present(void) {
    char done[MAX_PATH + 8], line[64];
    int n;
    HANDLE h;

    if (!g_cfg.trace_path[0])
        return;
    lock();
    if (active_locked()) {
        LARGE_INTEGER now;
        DWORD written;
        ++g_presents;
        if (g_times != INVALID_HANDLE_VALUE && g_freq.QuadPart) {
            QueryPerformanceCounter(&now);
            n = snprintf(line, sizeof line, "%u %ld\n", g_presents,
                         (long)((now.QuadPart - g_t0.QuadPart) * 1000 / g_freq.QuadPart));
            WriteFile(g_times, line, (DWORD)n, &written, NULL);
        }
        if (g_cfg.max_presents && g_presents == g_cfg.max_presents) {
            n = snprintf(line, sizeof line, "# end: %u presents\n", g_presents);
            put_locked(line, (unsigned)n);
            flush_locked();
            write_callers_locked();
            g_capped = 1;
            /* A marker file tells the driver script that the capture is complete. */
            snprintf(done, sizeof done, "%s.done", g_cfg.trace_path);
            h = CreateFileA(done, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    }
    unlock();
}

void trace_flush(void) {
    if (!g_cs_ready)
        return;
    lock();
    flush_locked();
    unlock();
}

void ddt_shutdown(void) {
    trace_flush();
    if (g_cs_ready && !g_capped) {
        lock();
        write_callers_locked();
        unlock();
    }
}
