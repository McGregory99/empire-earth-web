/* The exports of ddraw.dll.  Exports that are not traced are forwarded by tail jump, which keeps the stack
 * exactly as the caller left it and so works for any argument count. */
#include "ddt.h"

#define EXPORT_LIST(X) \
    X(0, AcquireDDThreadLock) \
    X(1, CompleteCreateSysmemSurface) \
    X(2, D3DParseUnknownCommand) \
    X(3, DDInternalLock) \
    X(4, DDInternalUnlock) \
    X(5, DirectDrawCreate) \
    X(6, DirectDrawCreateClipper) \
    X(7, DirectDrawCreateEx) \
    X(8, DirectDrawEnumerateA) \
    X(9, DirectDrawEnumerateExA) \
    X(10, DirectDrawEnumerateExW) \
    X(11, DirectDrawEnumerateW) \
    X(12, DllCanUnloadNow) \
    X(13, DllGetClassObject) \
    X(14, ReleaseDDThreadLock)

#define NAME_ENTRY(i, n) #n,
static const char *const export_names[] = {EXPORT_LIST(NAME_ENTRY)};
#define EXPORT_COUNT (sizeof export_names / sizeof *export_names)

static FARPROC export_target[EXPORT_COUNT];

static HRESULT WINAPI missing_export(void) { return E_FAIL; }

void *__cdecl ddt_raw_enter(int idx, void *return_address) {
    (void)return_address;
    if (!export_target[idx]) {
        FARPROC p = ddt_next_proc(export_names[idx]);
        export_target[idx] = p ? p : (FARPROC)missing_export;
    }
    return (void *)export_target[idx];
}

#define RAW_THUNK(i, n) \
    __attribute__((naked)) void ddt_x_##n(void) { \
        __asm__("pushl (%esp)\n\t" \
                "pushl $" #i "\n\t" \
                "call _ddt_raw_enter\n\t" \
                "addl $8, %esp\n\t" \
                "jmp *%eax\n\t"); \
    }
EXPORT_LIST(RAW_THUNK)
