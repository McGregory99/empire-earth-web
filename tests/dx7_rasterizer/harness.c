/*
 * Test harness for the renderer plugin spike (task P2.1).
 *
 * It loads a rasterizer plugin DLL by path, exactly like the engine's
 * PIMModule::GetModule does (LoadLibraryExA with LOAD_WITH_ALTERED_SEARCH_PATH,
 * flag 8), and checks the engine-facing contract:
 *
 *   PIMGetName        -> const UString &   (cdecl, no arguments)
 *   PIMGetType        -> unsigned long     ('rast' == 0x72617374)
 *   PIMIsValid        -> bool              (AL only: the engine does `test al, al`)
 *   RPIMGetRasterizer -> GERasterizer *    (only called when PIMIsValid is true)
 *
 * Usage:  rasterizer_harness.exe original|ours <dll>
 *         rasterizer_harness.exe probe-isvalid <dll>      (internal: child process)
 *
 * "original" runs the checks that hold for Stainless Steel's DX7HRTnLDisplay.dll
 * (the oracle for types and conventions).  "ours" runs the checks for our
 * EEWebRasterizer.dll, a null rasterizer.  The exit status is the number of
 * failed checks.
 */
#include "low_level_engine.h" /* UString and the engine's own constructors; includes <windows.h> */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIM_TYPE_RAST 0x72617374u /* 'rast' */

typedef const void *(__cdecl *PimGetName)(void);
typedef uint32_t (__cdecl *PimGetType)(void);
typedef unsigned char (__cdecl *PimIsValid)(void);
typedef void *(__cdecl *PimGetRasterizer)(void);

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                \
    do {                                \
        g_checks++;                     \
        if (cond) {                     \
            printf("ok   ");            \
        } else {                        \
            g_failures++;               \
            printf("FAIL ");            \
        }                               \
        printf(__VA_ARGS__);            \
        printf("\n");                   \
        fflush(stdout);                 \
    } while (0)

#define INFO(...)                       \
    do {                                \
        printf("info ");                \
        printf(__VA_ARGS__);            \
        printf("\n");                   \
        fflush(stdout);                 \
    } while (0)

/* What each variant is expected to export (ordinals as the linker assigned them). */
struct Expect {
    const char *label;
    unsigned ordinal[4]; /* PIMGetName, PIMGetType, PIMIsValid, RPIMGetRasterizer */
    int is_valid;        /* expected low byte of PIMIsValid, or -1 = do not assert */
    const char *name;    /* text of the UString that PIMGetName returns */
};

static const char *const kExportNames[4] = {"PIMGetName", "PIMGetType", "PIMIsValid",
                                            "RPIMGetRasterizer"};

static const struct Expect kOriginal = {"original", {252, 253, 254, 255}, -1, "Direct3D Hardware TnL"};
static const struct Expect kOurs = {"ours", {1, 2, 3, 4}, 0, "EEWeb Null Rasterizer"};

/* GetProcAddress as a data pointer (avoids function-pointer cast warnings). */
static void *proc_address(HMODULE h, const char *name_or_ordinal)
{
    FARPROC proc = GetProcAddress(h, name_or_ordinal);
    void *out;
    memcpy(&out, &proc, sizeof out);
    return out;
}

static HMODULE load_plugin(const char *path)
{
    char full[MAX_PATH];
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) {
        return NULL;
    }
    /* Flag 8 == LOAD_WITH_ALTERED_SEARCH_PATH, as in PIMModule::GetModule. */
    return LoadLibraryExA(full, NULL, 8);
}

static void check_exports(HMODULE h, const struct Expect *e, void *procs[4])
{
    for (int i = 0; i < 4; i++) {
        procs[i] = proc_address(h, kExportNames[i]);
        CHECK(procs[i] != NULL, "%s exports %s", e->label, kExportNames[i]);
        void *by_ordinal = proc_address(h, MAKEINTRESOURCEA(e->ordinal[i]));
        CHECK(procs[i] != NULL && by_ordinal == procs[i], "%s: %s has ordinal %u", e->label,
              kExportNames[i], e->ordinal[i]);
    }
}

static int in_module(HMODULE module, const void *p)
{
    const unsigned char *base = (const unsigned char *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return (const unsigned char *)p >= base &&
           (const unsigned char *)p < base + nt->OptionalHeader.SizeOfImage;
}

/*
 * PIMGetName returns `const UString &`.  The engine only ever hands it to UString code, so the
 * object must have been built by the engine's own constructor: its vtable lives in
 * Low-Level Engine.dll, and the engine's copy constructor and operator== must accept it.
 */
static void check_name(const struct Expect *e, PimGetName get_name)
{
    const UString *name = (const UString *)get_name();
    CHECK(name != NULL, "%s: PIMGetName() returns an object", e->label);
    if (!name) {
        return;
    }
    CHECK(name->chars != NULL && strcmp(name->chars, e->name) == 0, "%s: name text is \"%s\" (got \"%s\")",
          e->label, e->name, name->chars ? name->chars : "(null)");
    CHECK(name->chars != NULL && name->length == strlen(name->chars), "%s: name length field is %u",
          e->label, (unsigned)name->length);
    HMODULE engine = GetModuleHandleA("Low-Level Engine.dll");
    CHECK(engine != NULL && in_module(engine, name->vtable), "%s: name vtable %p lies in Low-Level Engine.dll",
          e->label, name->vtable);
    CHECK(get_name() == (const void *)name, "%s: PIMGetName() returns the same object every time", e->label);

    UString copy;
    ustring_construct_copy(&copy, name);
    CHECK(copy.chars != NULL && strcmp(copy.chars, e->name) == 0, "%s: the engine copies the name", e->label);
    CHECK(ustring_equal(&copy, name) != 0, "%s: the engine finds the copy equal to the name", e->label);
    ustring_destruct(&copy);
}

static int run_plugin_checks(const struct Expect *e, const char *path)
{
    HMODULE h = load_plugin(path);
    CHECK(h != NULL, "%s: LoadLibraryExA(path, 0, 8) (GetLastError=%lu)", e->label,
          h ? 0UL : GetLastError());
    if (!h) {
        return 1;
    }

    void *procs[4];
    check_exports(h, e, procs);
    if (!procs[1] || !procs[2]) {
        return 1;
    }
    PimGetType get_type;
    PimIsValid is_valid;
    memcpy(&get_type, &procs[1], sizeof get_type);
    memcpy(&is_valid, &procs[2], sizeof is_valid);

    uint32_t type = get_type();
    CHECK(type == PIM_TYPE_RAST, "%s: PIMGetType() == 'rast' (got 0x%08x)", e->label, type);

    PimGetName get_name;
    memcpy(&get_name, &procs[0], sizeof get_name);
    check_name(e, get_name);

    if (e->is_valid >= 0) {
        for (int i = 0; i < 3; i++) {
            unsigned al = is_valid();
            CHECK(al == (unsigned)e->is_valid, "%s: PIMIsValid() call %d returns %d in AL (got %u)",
                  e->label, i + 1, e->is_valid, al);
        }
    }
    return 0;
}

/* Child process: load the DLL and call PIMIsValid once.  Used to record what the
 * original does without being able to take the harness down with it. */
static int probe_isvalid(const char *path)
{
    HMODULE h = load_plugin(path);
    if (!h) {
        printf("probe: LoadLibraryExA failed (%lu)\n", GetLastError());
        return 2;
    }
    void *raw = proc_address(h, "PIMIsValid");
    if (!raw) {
        printf("probe: no PIMIsValid\n");
        return 3;
    }
    PimIsValid is_valid;
    memcpy(&is_valid, &raw, sizeof is_valid);
    unsigned al = is_valid();
    printf("probe: PIMIsValid() returned AL=0x%02x\n", al);
    fflush(stdout);
    return 0;
}

/* Run `probe-isvalid` in a child process and record how it ended. */
static void record_original_isvalid(const char *self, const char *path)
{
    char cmd[2 * MAX_PATH + 64];
    snprintf(cmd, sizeof cmd, "\"%s\" probe-isvalid \"%s\"", self, path);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    fflush(stdout);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        INFO("original PIMIsValid: could not start the probe process (%lu)", GetLastError());
        return;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 0xDEAD);
        INFO("original PIMIsValid: still running after 30 s, probe terminated");
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        INFO("original PIMIsValid: probe process exit status 0x%08lx%s", code,
             code == 0 ? " (returned normally)" : " (did not return normally)");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3) {
        fprintf(stderr, "usage: %s original|ours|probe-isvalid <dll>\n", argv[0]);
        return 64;
    }
    if (strcmp(argv[1], "probe-isvalid") == 0) {
        return probe_isvalid(argv[2]);
    }
    const struct Expect *e = strcmp(argv[1], "original") == 0 ? &kOriginal
                           : strcmp(argv[1], "ours") == 0     ? &kOurs
                                                              : NULL;
    if (!e) {
        fprintf(stderr, "unknown variant '%s'\n", argv[1]);
        return 64;
    }
    run_plugin_checks(e, argv[2]);
    if (e == &kOriginal) {
        record_original_isvalid(argv[0], argv[2]);
    }
    printf("%s: %d checks, %d failed\n", e->label, g_checks, g_failures);
    return g_failures;
}
