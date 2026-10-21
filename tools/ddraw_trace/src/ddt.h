/* Shared declarations of the tracing ddraw proxy. */
#ifndef DDT_H
#define DDT_H

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

/* ---- configuration (main.c) ------------------------------------------------------------------- */
typedef struct DdtConfig {
    char next_name[MAX_PATH];  /* module the proxy forwards to; relative names resolve next to the proxy */
    char trace_path[MAX_PATH]; /* trace output file; empty: tracing disabled (pure pass-through) */
    char modules[512];         /* comma separated module base names whose calls are traced */
    unsigned max_presents;     /* stop tracing after this many present-equivalent calls; 0 = unlimited */
    int flush_each;            /* flush the trace after every line (debugging) */
    char data_prefix[MAX_PATH]; /* if set: side data file "<prefix>.data" holding the bytes the trace only hashes (README, "Data file") */
    unsigned data_max;         /* cap on the data file in bytes; exceeding it fails the capture loudly */
    char test_fault[32];       /* tests only (DDT_TEST_FAULT): inject one failure, see ddt_fault; never set by the drivers */
    char arm_file[MAX_PATH];   /* if set: nothing is traced (nor counted) until this file exists; see trace_present */
} DdtConfig;

extern DdtConfig g_cfg;
void ddt_init(void);               /* idempotent, thread safe */
HMODULE ddt_next_module(void);     /* the module forwarded to, loaded on first use; NULL if it cannot be loaded */
FARPROC ddt_next_proc(const char *name);
void ddt_shutdown(void);           /* flush the trace (process detach) */
int ddt_fault(const char *name);   /* DDT_TEST_FAULT names this failure: "data-write", "data-alloc" or "shadow-alloc" */

/* ---- text buffers ----------------------------------------------------------------------------- */
typedef struct Buf {
    char *p;
    unsigned len, cap;
} Buf;

void buf_init(Buf *b, char *storage, unsigned cap);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ---- trace sink (trace.c) --------------------------------------------------------------------- */
int trace_active(void);                           /* configured, open and not yet capped */
int trace_wants(void *return_address);            /* active AND the caller is one of the traced modules */
void trace_emit(int depth, const char *text, int timing); /* one line: "T<n> " + indent + text */
int trace_present(void);                          /* count one present-equivalent call; may cap the trace.  1: this call armed the trace */
void trace_comment(const char *text);             /* one "# text" line, no thread prefix (only while the trace is recording) */
void trace_flush(void);

/* ---- side data file (trace.c) ----------------------------------------------------------------- */
enum { BLOB_VERTICES = 1, BLOB_INDICES = 2, BLOB_SURFACE = 3 };
enum { DATA_OK = 0, DATA_OFF = 1, DATA_FAIL = 2, DATA_UNAVAILABLE = 3 }; /* UNAVAILABLE: the pixels could not be read back */
#define BLOB_PARAMS 16
int data_enabled(void);                           /* DDT_DATA is configured */
/* Store one record (or find the identical one stored before): the payload is `rows` rows of `rowbytes` bytes, `pitch` apart.
 * DATA_OK: off and len name the record; DATA_OFF: the trace is not recording now; DATA_FAIL: the cap or an I/O error ended the
 * capture (an ERROR line is in the trace). */
int data_put(unsigned kind, unsigned object, const unsigned p[BLOB_PARAMS], const void *base, size_t pitch, size_t rows,
             size_t rowbytes, unsigned *off, unsigned *len);
/* An allocation or I/O failure of the capture itself (not the cap): says why in the trace, ends it and leaves the .failed marker. */
void data_fail(const char *what);
uint64_t fnv1a64(const void *data, size_t n);
uint64_t fnv1a64_update(uint64_t h, const void *data, size_t n);
#define FNV1A64_INIT 0xcbf29ce484222325ull

/* ---- names (names.c) -------------------------------------------------------------------------- */
const char *hr_name(HRESULT hr, char tmp[16]);    /* DD_OK, DDERR_WASSTILLDRAWING, ... or 0x... */
const char *guid_name(const GUID *g, char tmp[48]);
const char *rs_name(DWORD v);                     /* D3DRENDERSTATE_* without the prefix, or NULL */
const char *tss_name(DWORD v);
const char *xform_name(DWORD v);
const char *pt_name(DWORD v);
const char *light_name(DWORD v);

/* ---- structures (structs.c) ------------------------------------------------------------------- */
enum StructId {
    ST_DDSD2, ST_DDPF, ST_DDSCAPS2, ST_DDCAPS, ST_DEVDESC7, ST_VIEWPORT, ST_MATERIAL, ST_LIGHT, ST_MATRIX,
    ST_RECT, ST_POINT, ST_D3DRECT, ST_VBDESC, ST_BLTFX, ST_COLORKEY, ST_CLIPSTATUS, ST_DEVIDENT2, ST_COUNT
};
void fmt_struct(Buf *b, int st, const void *p);  /* "{field=value, ...}" or "NULL" */
void fmt_fourcc(Buf *b, DWORD v);

/* ---- wrapped objects and dispatch (dispatch.c) ------------------------------------------------ */
enum { IF_DD7, IF_D3D7, IF_DEV7, IF_SURF7, IF_VB7, IF_COUNT };

typedef struct Wrap {
    const void *vtbl; /* must be first: the plugin sees this as the COM object */
    void *real;
    int iface;
    unsigned id;
    int dead;         /* the real object was released to zero */
    struct Wrap *by_real_next, *by_self_next;
    unsigned family;  /* wrappers of one COM object (queried from each other) retire together */
    DWORD caps;       /* surfaces: DDSCAPS_* given at creation */
    DWORD vb_bytes;   /* vertex buffers: size from the creation description */
    /* surface and vertex buffer locks */
    int locked, lock_has_rect;
    void *lock_ptr;
    DWORD lock_pitch, lock_rows, lock_rowbytes; /* surfaces: the locked region is rows x rowbytes, rows pitch apart */
    DWORD lock_bytes;                           /* total bytes hashed */
    DWORD vb_fvf, vb_stride;                    /* vertex buffers: from the creation description */
    DWORD pf[7];                                /* surfaces, data capture: pixel format words (flags, fourcc, bit count, R, G, B, A masks) */
    DWORD surf_w, surf_h;                       /* surfaces: full size, as the last Lock reported it */
    int lock_ro;                                /* the surface lock in progress is DDLOCK_READONLY */
    int dirty;                                  /* a Blt wrote to this surface since its pixels were last stored */
    unsigned char *vb_shadow;                   /* vertex buffers, data capture: the contents as of the last Unlock */
    RECT lock_rect;
} Wrap;

void *wrap_new(int iface, void *real);            /* wrapper for a real interface pointer, or NULL */
void *unwrap(void *maybe_wrapper);                /* the real pointer behind one of our wrappers; others unchanged */
const char *wrap_label(void *maybe_wrapper, char tmp[24]); /* "surf#3", "NULL" or "raw" */

int ddt_selfcheck_problems(const char **first);   /* classification self-check: problems found, and the first one's text */
void dispatch_init(void);                         /* method tables, vtables, registry; called once from ddt_init */

#endif
