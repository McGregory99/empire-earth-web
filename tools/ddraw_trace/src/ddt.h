/* Shared declarations of the tracing ddraw proxy. */
#ifndef DDT_H
#define DDT_H

#include <windows.h>
#include <stdint.h>

/* ---- configuration (main.c) ------------------------------------------------------------------- */
typedef struct DdtConfig {
    char next_name[MAX_PATH];  /* module the proxy forwards to; relative names resolve next to the proxy */
    char trace_path[MAX_PATH]; /* trace output file; empty: tracing disabled (pure pass-through) */
    char modules[512];         /* comma separated module base names whose calls are traced */
    unsigned max_presents;     /* stop tracing after this many present-equivalent calls; 0 = unlimited */
    int flush_each;            /* flush the trace after every line (debugging) */
} DdtConfig;

extern DdtConfig g_cfg;
void ddt_init(void);               /* idempotent, thread safe */
HMODULE ddt_next_module(void);     /* the module forwarded to, loaded on first use; NULL if it cannot be loaded */
FARPROC ddt_next_proc(const char *name);

/* ---- raw forwarding of exports that are not traced (exports.c) --------------------------------- */
void *__cdecl ddt_raw_enter(int export_index, void *return_address);

#endif
