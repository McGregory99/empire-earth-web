/* The trace sink: one text line per call, assembled by the callers, written under a lock. */
#include "ddt.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_ready;
static HANDLE g_file = INVALID_HANDLE_VALUE, g_times = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_t0, g_freq;
static int g_opened, g_capped, g_armed;
static unsigned g_presents;
static char g_out[1 << 16];
static unsigned g_out_len;
/* Per thread: its number (in order of its first traced call) and its last timing line, so that another thread's lines
 * in between do not break a run of identical polls. */
typedef struct ThreadState {
    unsigned id;
    char last[1024];
} ThreadState;
static DWORD g_tls = TLS_OUT_OF_INDEXES;
static unsigned g_nthreads;
/* The side data file: records appended through a buffer, content-addressed so that identical records are stored once. */
#define DATA_FILE_HDR 16
#define REC_HDR 96
typedef struct DEnt { uint64_t key; unsigned off, len; } DEnt;
static HANDLE g_data = INVALID_HANDLE_VALUE;
static int g_data_failed;
static unsigned g_data_off, g_data_seq, g_data_cap_n, g_data_n;
static unsigned char g_dbuf[1 << 18];
static unsigned g_dbuf_len;
static DEnt *g_dtab;
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

static int g_data_werr; /* a write to the data file failed or was short: data_put turns it into a failed capture */

static void dwrite_locked(const void *p, DWORD n) {
    DWORD done = 0;
    if (g_data_werr || g_data == INVALID_HANDLE_VALUE)
        return;
    if (ddt_fault("data-write") || !WriteFile(g_data, p, n, &done, NULL) || done != n)
        g_data_werr = 1;
}

static void dflush_locked(void) {
    if (g_dbuf_len)
        dwrite_locked(g_dbuf, g_dbuf_len);
    g_dbuf_len = 0;
}

static void data_check_werr_locked(void);

static void flush_locked(void) {
    DWORD done;
    dflush_locked();
    if (g_file != INVALID_HANDLE_VALUE && g_out_len)
        WriteFile(g_file, g_out, g_out_len, &done, NULL);
    g_out_len = 0;
    data_check_werr_locked();
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
    if (g_cfg.data_prefix[0]) {
        char dpath[MAX_PATH + 8];
        const char *base;
        unsigned char fh[DATA_FILE_HDR] = {'D', 'D', 'T', 'D', 'A', 'T', 'A', '1', 1, 0, 0, 0, 0, 0, 0, 0};
        DWORD done;
        snprintf(dpath, sizeof dpath, "%s.data", g_cfg.data_prefix);
        base = strrchr(dpath, '\\');
        base = base ? base + 1 : dpath;
        n = snprintf(head, sizeof head, "# data: %s max=%u\n", base, g_cfg.data_max);
        put_locked(head, (unsigned)n);
        g_data = CreateFileA(dpath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_data == INVALID_HANDLE_VALUE || !WriteFile(g_data, fh, sizeof fh, &done, NULL)) {
            static const char msg[] = "# ERROR: cannot create the data file\n";
            put_locked(msg, sizeof msg - 1);
            g_data_failed = 1;
            g_capped = 1;
        }
        g_data_off = DATA_FILE_HDR;
    }
    {
        const char *first = NULL;
        int problems = ddt_selfcheck_problems(&first);
        if (problems) { /* the method tables and the type map disagree: the trace cannot be trusted */
            n = snprintf(head, sizeof head, "# ERROR: classification self-check found %d problem(s): %s\n", problems, first);
            put_locked(head, (unsigned)n);
        }
    }
}

static int open_ok_locked(void) {
    if (!g_opened)
        open_locked();
    return g_file != INVALID_HANDLE_VALUE;
}

/* Lines are written and presents counted only while the trace is open, not capped and, if DDT_ARM_FILE is configured, armed. */
static int active_locked(void) {
    return open_ok_locked() && !g_capped && (g_armed || !g_cfg.arm_file[0]);
}

/* A scenario that must trace only a settled state (a paused game) names a file that its driver creates once that state is
 * reached.  Every present looks for it until it exists; the present that sees it is the last one outside the trace, so the
 * window starts at a frame boundary and its presents are counted from there.  Object ids are assigned all the same while
 * unarmed, so the lines of the window name the objects as a full trace would. */
static void arm_locked(void) {
    static const char mark[] = "# armed\n";
    if (g_armed || !g_cfg.arm_file[0] || !open_ok_locked() || g_capped)
        return;
    if (GetFileAttributesA(g_cfg.arm_file) == INVALID_FILE_ATTRIBUTES)
        return;
    g_armed = 1;
    put_locked(mark, sizeof mark - 1);
    if (g_times != INVALID_HANDLE_VALUE)
        QueryPerformanceCounter(&g_t0);
}

int trace_active(void) {
    int a;
    ddt_init();
    if (!g_cfg.trace_path[0])
        return 0;
    lock();
    a = open_ok_locked() && !g_capped; /* not "armed": the calls before the arming are still formatted, so that the present
                                          that arms the trace is seen (trace_emit then drops their lines) */
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
    ThreadState *ts;

    if (!g_cfg.trace_path[0])
        return;
    lock();
    if (!active_locked()) {
        unlock();
        return;
    }
    if (g_tls == TLS_OUT_OF_INDEXES)
        g_tls = TlsAlloc();
    ts = TlsGetValue(g_tls);
    if (!ts) {
        ts = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *ts); /* lives as long as the process: threads are few */
        if (!ts) {
            unlock();
            return;
        }
        ts->id = ++g_nthreads;
        TlsSetValue(g_tls, ts);
    }
    n = (unsigned)snprintf(line, sizeof line, "T%u %*s%s\n", ts->id, depth * 2, "", text);
    if (n >= sizeof line)
        n = sizeof line - 1;
    /* A run of identical timing-dependent calls (polling) is written once: its length is not reproducible. */
    if (timing && !strncmp(ts->last, line, sizeof ts->last)) {
        unlock();
        return;
    }
    lstrcpynA(ts->last, timing ? line : "", sizeof ts->last);
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
    if (!g_armed && g_cfg.arm_file[0]) {
        arm_locked();
    } else if (active_locked()) {
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

/* ---- side data file ---------------------------------------------------------------------------- */
int data_enabled(void) {
    ddt_init();
    return g_cfg.data_prefix[0] != 0;
}

static void le32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void dput_locked(const void *p, size_t n) {
    if (g_dbuf_len + n > sizeof g_dbuf)
        dflush_locked();
    if (n > sizeof g_dbuf) {
        dwrite_locked(p, (DWORD)n);
        return;
    }
    memcpy(g_dbuf + g_dbuf_len, p, n);
    g_dbuf_len += (unsigned)n;
}

/* The cap, an I/O error or an allocation failure makes the capture unusable: say so in the trace, end it, and leave a marker
 * instead of .done.  `line` is the complete ERROR line. */
static void data_fail_line_locked(const char *line) {
    char path[MAX_PATH + 8];
    HANDLE h;
    g_data_failed = 1; /* first: the flush below must not report the same failure again */
    put_locked(line, (unsigned)strlen(line));
    flush_locked();
    write_callers_locked();
    g_capped = 1;
    snprintf(path, sizeof path, "%s.failed", g_cfg.trace_path);
    h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    OutputDebugStringA(line);
}

static void data_fail_locked(const char *why, unsigned need) {
    char line[256];
    snprintf(line, sizeof line, "# ERROR: data cap of %u bytes exceeded (%s: record %u needs %u more bytes after %u written); capture invalid\n",
             g_cfg.data_max, why, g_data_seq, need, g_data_off);
    data_fail_line_locked(line);
}

static void data_fail_alloc_locked(const char *what) {
    char line[256];
    snprintf(line, sizeof line, "# ERROR: data allocation failed (%s: record %u after %u bytes written); capture invalid\n", what,
             g_data_seq, g_data_off);
    data_fail_line_locked(line);
}

static void data_check_werr_locked(void) {
    char line[256];
    if (!g_data_werr || g_data_failed || !g_cfg.data_prefix[0])
        return;
    snprintf(line, sizeof line, "# ERROR: data write failed (record %u after %u bytes written); capture invalid\n", g_data_seq, g_data_off);
    data_fail_line_locked(line);
}

void data_fail(const char *what) {
    lock();
    if (!g_data_failed && active_locked())
        data_fail_alloc_locked(what);
    unlock();
}

static DEnt *dfind_locked(uint64_t key) {
    unsigned i;
    if (!g_dtab)
        return NULL;
    for (i = (unsigned)key & (g_data_cap_n - 1); g_dtab[i].len; i = (i + 1) & (g_data_cap_n - 1))
        if (g_dtab[i].key == key)
            return &g_dtab[i];
    return NULL;
}

static int dinsert_locked(uint64_t key, unsigned off, unsigned len) {
    unsigned i;
    if (!g_dtab || (g_data_n + 1) * 2 > g_data_cap_n) { /* grow at half load */
        unsigned ncap = g_data_cap_n ? g_data_cap_n * 2 : 4096, k;
        DEnt *nt = ddt_fault("data-alloc") ? NULL : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ncap * sizeof *nt);
        if (!nt)
            return 0;
        for (k = 0; k < g_data_cap_n; k++)
            if (g_dtab[k].len) {
                for (i = (unsigned)g_dtab[k].key & (ncap - 1); nt[i].len; i = (i + 1) & (ncap - 1))
                    ;
                nt[i] = g_dtab[k];
            }
        if (g_dtab)
            HeapFree(GetProcessHeap(), 0, g_dtab);
        g_dtab = nt;
        g_data_cap_n = ncap;
    }
    for (i = (unsigned)key & (g_data_cap_n - 1); g_dtab[i].len; i = (i + 1) & (g_data_cap_n - 1))
        ;
    g_dtab[i].key = key;
    g_dtab[i].off = off;
    g_dtab[i].len = len;
    g_data_n++;
    return 1;
}

/* Record layout: README, "Data file".  The key covers kind, object, payload length, the 16 params and the payload hash, so
 * the same bytes under other parameters (another vertex range, another surface) are a different record. */
int data_put(unsigned kind, unsigned object, const unsigned p[BLOB_PARAMS], const void *base, size_t pitch, size_t rows,
             size_t rowbytes, unsigned *off, unsigned *len) {
    uint64_t ph = FNV1A64_INIT, key;
    size_t plen = rows * rowbytes, r, total;
    unsigned char hdr[REC_HDR], pad[8] = {0};
    unsigned head[3 + BLOB_PARAMS], i;
    int rc = DATA_OK;
    DEnt *e;

    if (!g_cfg.data_prefix[0] || !g_cfg.trace_path[0])
        return DATA_OFF;
    for (r = 0; r < rows; r++)
        ph = fnv1a64_update(ph, (const char *)base + r * pitch, rowbytes);
    head[0] = kind;
    head[1] = object;
    head[2] = (unsigned)plen;
    memcpy(head + 3, p, BLOB_PARAMS * sizeof *p);
    key = fnv1a64_update(ph, head, sizeof head);
    total = REC_HDR + ((plen + 7) & ~(size_t)7);

    lock();
    if (g_data_failed) {
        rc = DATA_FAIL;
    } else if (!active_locked() || !(g_armed || !g_cfg.arm_file[0])) {
        rc = DATA_OFF;
    } else if ((e = dfind_locked(key)) != NULL) {
        *off = e->off;
        *len = e->len;
    } else if (total > g_cfg.data_max || g_data_off > g_cfg.data_max - total) {
        data_fail_locked("cap", (unsigned)(total > 0xFFFFFFFFu ? 0xFFFFFFFFu : total));
        rc = DATA_FAIL;
    } else {
        memset(hdr, 0, sizeof hdr);
        le32(hdr, 0x31524444u); /* "DDR1" */
        le32(hdr + 4, kind);
        le32(hdr + 8, (unsigned)total);
        le32(hdr + 12, g_data_seq++);
        le32(hdr + 16, object);
        le32(hdr + 20, (unsigned)plen);
        le32(hdr + 24, (unsigned)ph);
        le32(hdr + 28, (unsigned)(ph >> 32));
        for (i = 0; i < BLOB_PARAMS; i++)
            le32(hdr + 32 + 4 * i, p[i]);
        dput_locked(hdr, sizeof hdr);
        for (r = 0; r < rows; r++)
            dput_locked((const char *)base + r * pitch, rowbytes);
        dput_locked(pad, total - REC_HDR - plen);
        *off = g_data_off;
        *len = (unsigned)total;
        if (!dinsert_locked(key, *off, *len)) {
            data_fail_alloc_locked("record index");
            rc = DATA_FAIL;
        }
        g_data_off += (unsigned)total;
        if (g_cfg.flush_each)
            dflush_locked();
        if (rc == DATA_OK && g_data_werr) {
            data_check_werr_locked();
            rc = DATA_FAIL;
        }
    }
    unlock();
    return rc;
}
