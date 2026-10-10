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
#include "rasterizer_abi.h"   /* the null rasterizer's vtable contract */

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

#define THISCALL __attribute__((thiscall))
typedef long(THISCALL *MethodNoArg)(void *self);
typedef long(THISCALL *MethodLong)(void *self, long a);
typedef long(THISCALL *MethodFloatLong)(void *self, float f, long a);
typedef void *(THISCALL *DeletingDtor)(void *self, unsigned flags);

/* What each variant is expected to export (ordinals as the linker assigned them). */
struct Expect {
    const char *label;
    unsigned ordinal[4]; /* PIMGetName, PIMGetType, PIMIsValid, RPIMGetRasterizer */
    int is_valid;        /* expected low byte of PIMIsValid, or -1 = do not assert */
    const char *name;    /* text of the UString that PIMGetName returns */
    const char *rtti;    /* RTTI name of the class RPIMGetRasterizer's object belongs to */
    const char *bases[4]; /* expected RTTI class chain (most derived first), NULL-terminated */
};

static const char *const kExportNames[4] = {"PIMGetName", "PIMGetType", "PIMIsValid",
                                            "RPIMGetRasterizer"};

static const struct Expect kOriginal = {"original", {252, 253, 254, 255}, -1, "Direct3D Hardware TnL",
                                        ".?AVDX7TnLRasterizer@@",
                                        {".?AVDX7TnLRasterizer@@", ".?AVDX7Rasterizer@@", ".?AVGERasterizer@@", NULL}};
static const struct Expect kOurs = {"ours", {1, 2, 3, 4}, 0, "EEWeb Null Rasterizer",
                                    EEWEB_RTTI_NULL, {EEWEB_RTTI_NULL, EEWEB_RTTI_BASE, NULL, NULL}};

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

/* ---- MSVC RTTI (x86: absolute pointers, no RVAs) ------------------------------------------- */
struct RttiTypeDescriptor {
    const void *vftable; /* the vtable of std::type_info */
    void *spare;
    char name[1];        /* ".?AVClass@@" */
};
struct RttiBaseClass {
    const struct RttiTypeDescriptor *type;
    uint32_t contained_bases;
    int32_t mdisp, pdisp, vdisp;
    uint32_t attributes;
};
struct RttiHierarchy {
    uint32_t signature, attributes, count;
    const struct RttiBaseClass *const *bases;
};
struct RttiLocator {
    uint32_t signature, offset, cd_offset;
    const struct RttiTypeDescriptor *type;
    const struct RttiHierarchy *hierarchy;
};

/* The complete object locator sits in the slot before the first virtual function. */
static const struct RttiLocator *locator_of(void *const *vtable)
{
    return (const struct RttiLocator *)vtable[-1];
}

static void check_rtti(const struct Expect *e, HMODULE h, void *const *vtable)
{
    const struct RttiLocator *col = locator_of(vtable);
    CHECK(col != NULL && in_module(h, col) && col->signature == 0 && col->offset == 0 && col->cd_offset == 0,
          "%s: vtable[-1] is an MSVC complete object locator (signature 0, offset 0)", e->label);
    if (!col || !in_module(h, col) || !col->type || !col->hierarchy) {
        return;
    }
    CHECK(strcmp(col->type->name, e->rtti) == 0, "%s: RTTI type name is %s (got %s)", e->label, e->rtti,
          col->type->name);
    const struct RttiHierarchy *chain = col->hierarchy;
    CHECK(chain->count >= 1 && chain->count < 16, "%s: class hierarchy lists %u class(es)", e->label,
          (unsigned)chain->count);
    uint32_t expected = 0;
    while (expected < 4 && e->bases[expected]) {
        expected++;
    }
    for (uint32_t i = 0; i < chain->count && i < 16; i++) {
        const char *name = chain->bases[i]->type->name;
        CHECK(i < expected && strcmp(name, e->bases[i]) == 0, "%s: hierarchy[%u] is %s", e->label, (unsigned)i, name);
    }
    CHECK(chain->count == expected, "%s: hierarchy lists exactly the %u expected class(es)", e->label, (unsigned)expected);
}

static void *slot(void *const *vtable, int index)
{
    return vtable[index];
}

static long counter(void *obj, long which)
{
    MethodLong get_counter;
    void *fn = slot(*(void *const **)obj, EEWEB_VSLOT_GET_COUNTER);
    memcpy(&get_counter, &fn, sizeof get_counter);
    return get_counter(obj, which);
}

/*
 * What RPIMGetRasterizer returns.  Both variants: a vtable inside the DLL and MSVC RTTI.  Ours is
 * driven through its vtable exactly like the engine would (thiscall through positional slots),
 * which pins the layout rules listed in rasterizer_abi.h.
 */
static void check_rasterizer_object(const struct Expect *e, HMODULE h, void *get_rasterizer_raw, void *is_valid_raw)
{
    PimGetRasterizer get_rasterizer;
    memcpy(&get_rasterizer, &get_rasterizer_raw, sizeof get_rasterizer);
    void *obj = get_rasterizer();
    CHECK(obj != NULL, "%s: RPIMGetRasterizer() returns an object", e->label);
    if (!obj) {
        return;
    }
    CHECK(get_rasterizer() == obj, "%s: RPIMGetRasterizer() returns the same object every time", e->label);
    void *const *vtable = *(void *const **)obj;
    CHECK(in_module(h, vtable), "%s: the object's vtable lies inside the plugin", e->label);
    check_rtti(e, h, vtable);
    if (e->bases[0] == NULL) {
        return;
    }

    /* Ours: every slot, called as the engine would. */
    long activations_before = counter(obj, EEWEB_COUNTER_ACTIVATIONS);
    MethodNoArg m0;
    MethodLong m1;
    MethodFloatLong m2;
    void *fn;
    fn = slot(vtable, EEWEB_VSLOT_ACTIVATE);
    memcpy(&m0, &fn, sizeof m0);
    CHECK(m0(obj) == EEWEB_ACTIVATE_FAILED, "%s: slot %d is Activate()", e->label, EEWEB_VSLOT_ACTIVATE);
    fn = slot(vtable, EEWEB_VSLOT_GET_SCREEN_COUNT);
    memcpy(&m0, &fn, sizeof m0);
    CHECK(m0(obj) == 0, "%s: slot %d is GetScreenCount()", e->label, EEWEB_VSLOT_GET_SCREEN_COUNT);
    fn = slot(vtable, EEWEB_VSLOT_DRAW_SCALED);
    memcpy(&m2, &fn, sizeof m2);
    CHECK(m2(obj, 2.5f, 5) == EEWEB_DRAW_SCALED_BASE + 5 + 2, "%s: slot %d is Draw(float, long), the overload declared second",
          e->label, EEWEB_VSLOT_DRAW_SCALED);
    fn = slot(vtable, EEWEB_VSLOT_DRAW_PLAIN);
    memcpy(&m1, &fn, sizeof m1);
    CHECK(m1(obj, 5) == EEWEB_DRAW_PLAIN_BASE + 5, "%s: slot %d is Draw(long), the overload declared first", e->label,
          EEWEB_VSLOT_DRAW_PLAIN);
    fn = slot(vtable, EEWEB_VSLOT_DESCRIBE);
    memcpy(&m0, &fn, sizeof m0);
    CHECK(m0(obj) == EEWEB_DESCRIBE_RESULT, "%s: slot %d is Describe(), inherited from the base class", e->label,
          EEWEB_VSLOT_DESCRIBE);
    CHECK(counter(obj, EEWEB_COUNTER_ACTIVATIONS) == activations_before + 1,
          "%s: the Activate() call above was counted", e->label);

    /* PIMIsValid runs the DLL's internal exercise of the class: Activate / GetScreenCount /
     * Deactivate on the shared object, and new + dynamic_cast + virtual delete on a temporary. */
    PimIsValid is_valid;
    memcpy(&is_valid, &is_valid_raw, sizeof is_valid);
    long before[EEWEB_COUNTER_COUNT], after[EEWEB_COUNTER_COUNT];
    for (int k = 0; k < EEWEB_COUNTER_COUNT; k++) {
        before[k] = counter(obj, k);
    }
    CHECK(is_valid() == 0, "%s: PIMIsValid() is still false", e->label);
    for (int k = 0; k < EEWEB_COUNTER_COUNT; k++) {
        after[k] = counter(obj, k);
    }
    CHECK(after[EEWEB_COUNTER_ACTIVATIONS] == before[EEWEB_COUNTER_ACTIVATIONS] + 1,
          "%s: PIMIsValid activated the rasterizer through its vtable", e->label);
    CHECK(after[EEWEB_COUNTER_DEACTIVATIONS] == before[EEWEB_COUNTER_DEACTIVATIONS] + 1,
          "%s: PIMIsValid deactivated it again", e->label);
    CHECK(after[EEWEB_COUNTER_SELF_CHECKS] == before[EEWEB_COUNTER_SELF_CHECKS] + 1,
          "%s: PIMIsValid ran the internal class self-check once", e->label);
    CHECK(after[EEWEB_COUNTER_SELF_CHECK_FAILURES] == 0,
          "%s: the self-check (new, virtual calls, dynamic_cast, virtual delete) found no fault", e->label);
    CHECK(after[EEWEB_COUNTER_DESTROYED] == before[EEWEB_COUNTER_DESTROYED] + 1,
          "%s: the temporary object's deleting destructor ran exactly once", e->label);

    /* Last, because it destroys the shared object: the deleting-destructor slot with flags 0
     * (destroy, do not free) must run the derived destructor and then the base one.  (The
     * vptr is not checked: optimizers drop the stores that reset it in a dying object.) */
    DeletingDtor destroy;
    fn = slot(vtable, EEWEB_VSLOT_DELETING_DESTRUCTOR);
    memcpy(&destroy, &fn, sizeof destroy);
    long derived_before = counter(obj, EEWEB_COUNTER_DESTROYED);
    long base_before = counter(obj, EEWEB_COUNTER_BASE_DESTROYED);
    destroy(obj, 0);
    CHECK(counter(obj, EEWEB_COUNTER_DESTROYED) == derived_before + 1,
          "%s: slot %d (deleting destructor) ran the derived destructor once", e->label,
          EEWEB_VSLOT_DELETING_DESTRUCTOR);
    CHECK(counter(obj, EEWEB_COUNTER_BASE_DESTROYED) == base_before + 1, "%s: ... and then the base destructor once",
          e->label);
}

/* Returns the module, still loaded once, or NULL. */
static HMODULE run_plugin_checks(const struct Expect *e, const char *path)
{
    HMODULE h = load_plugin(path);
    CHECK(h != NULL, "%s: LoadLibraryExA(path, 0, 8) (GetLastError=%lu)", e->label,
          h ? 0UL : GetLastError());
    if (!h) {
        return NULL;
    }

    void *procs[4];
    check_exports(h, e, procs);
    if (!procs[1] || !procs[2]) {
        return h;
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
        unsigned al = is_valid();
        CHECK(al == (unsigned)e->is_valid, "%s: PIMIsValid() returns %d in AL (got %u)", e->label, e->is_valid, al);
    }
    if (procs[3]) {
        check_rasterizer_object(e, h, procs[3], procs[2]);
    }
    return h;
}

/*
 * The engine's start-up pattern: PIMModule::GetModule loads every DLL of the game folder, asks it
 * a question and frees it again, so a plugin that is not selected goes through load / unload
 * several times (and our null rasterizer is freed after PIMIsValid says false).  Every cycle must
 * really unload the module (its statics, including the counters, start from zero on the next
 * load) and must leave the heap intact: DllMain's DETACH path destroys what ATTACH built.
 */
#define RELOAD_CYCLES 4

static const char *base_name(const char *path, char *out, size_t size)
{
    char full[MAX_PATH];
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) {
        out[0] = 0;
        return out;
    }
    const char *slash = strrchr(full, '\\');
    snprintf(out, size, "%s", slash ? slash + 1 : full);
    return out;
}

/* Busy blocks and bytes of every heap of the process, to see what a load / unload cycle leaves behind. */
static void heap_usage(size_t *blocks, size_t *bytes)
{
    HANDLE heaps[64];
    DWORD n = GetProcessHeaps(64, heaps);
    *blocks = 0;
    *bytes = 0;
    for (DWORD i = 0; i < n && i < 64; i++) {
        PROCESS_HEAP_ENTRY entry;
        memset(&entry, 0, sizeof entry);
        if (!HeapLock(heaps[i])) {
            continue;
        }
        while (HeapWalk(heaps[i], &entry)) {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
                (*blocks)++;
                *bytes += entry.cbData;
            }
        }
        HeapUnlock(heaps[i]);
    }
}

/* Is `p` a live (allocated) block of one of the process's heaps? */
static int live_heap_block(const void *p)
{
    HANDLE heaps[64];
    DWORD n = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < n && i < 64; i++) {
        if (HeapValidate(heaps[i], 0, p)) {
            return 1;
        }
    }
    return 0;
}

/* Unload `h`.  `name_buffer` is the heap block behind the plugin's name string: DllMain's DETACH path must free it. */
static void check_unloaded(const struct Expect *e, HMODULE h, const char *base, const char *when, const void *name_buffer)
{
    int live_before = name_buffer != NULL && live_heap_block(name_buffer);
    BOOL freed = FreeLibrary(h);
    int live_after = name_buffer != NULL && live_heap_block(name_buffer);
    CHECK(freed != 0, "%s: %s: FreeLibrary succeeds", e->label, when);
    if (live_before) {
        CHECK(!live_after, "%s: %s: DllMain's DETACH path freed the name string's buffer", e->label, when);
    } else {
        INFO("%s: %s: the name buffer is not a block of a Win32 heap, its release is not observable", e->label, when);
    }
    CHECK(GetModuleHandleA(base) == NULL, "%s: %s: the module is really unloaded (refcount was 1)", e->label, when);
    CHECK(HeapValidate(GetProcessHeap(), 0, NULL) != 0, "%s: %s: the process heap is intact", e->label, when);
}

static void reload_cycles(const struct Expect *e, HMODULE first, const char *path)
{
    char base[MAX_PATH];
    base_name(path, base, sizeof base);
    if (first) {
        check_unloaded(e, first, base, "after the contract checks", NULL);
    }
    for (int cycle = 1; cycle <= RELOAD_CYCLES; cycle++) {
        char when[48];
        size_t blocks_before, bytes_before;
        heap_usage(&blocks_before, &bytes_before);
        snprintf(when, sizeof when, "reload %d of %d", cycle, RELOAD_CYCLES);
        HMODULE h = load_plugin(path);
        CHECK(h != NULL, "%s: %s: LoadLibraryExA (GetLastError=%lu)", e->label, when, h ? 0UL : GetLastError());
        if (!h) {
            continue;
        }
        PimGetName get_name;
        const void *name_buffer = NULL;
        void *name_raw = proc_address(h, "PIMGetName");
        memcpy(&get_name, &name_raw, sizeof get_name);
        CHECK(name_raw != NULL, "%s: %s: PIMGetName is exported", e->label, when);
        if (name_raw) {
            check_name(e, get_name); /* a fresh load builds a fresh name object */
            name_buffer = ((const UString *)get_name())->chars;
        }
        void *get_raster_raw = proc_address(h, "RPIMGetRasterizer");
        void *is_valid_raw = proc_address(h, "PIMIsValid");
        if (e->bases[0] != NULL && get_raster_raw && is_valid_raw) {
            PimGetRasterizer get_rasterizer;
            PimIsValid is_valid;
            memcpy(&get_rasterizer, &get_raster_raw, sizeof get_rasterizer);
            memcpy(&is_valid, &is_valid_raw, sizeof is_valid);
            void *obj = get_rasterizer();
            CHECK(obj != NULL, "%s: %s: RPIMGetRasterizer() returns an object", e->label, when);
            if (obj) {
                /* the DLL's statics start from zero on every fresh load */
                long fresh = 0;
                for (int k = 0; k < EEWEB_COUNTER_COUNT; k++) {
                    fresh |= counter(obj, k);
                }
                CHECK(fresh == 0, "%s: %s: all %d counters are zero on a fresh load", e->label, when,
                      EEWEB_COUNTER_COUNT);
                CHECK(is_valid() == 0, "%s: %s: PIMIsValid() is false", e->label, when);
                CHECK(counter(obj, EEWEB_COUNTER_ACTIVATIONS) == 1 && counter(obj, EEWEB_COUNTER_DEACTIVATIONS) == 1 &&
                          counter(obj, EEWEB_COUNTER_SELF_CHECKS) == 1 &&
                          counter(obj, EEWEB_COUNTER_SELF_CHECK_FAILURES) == 0 &&
                          counter(obj, EEWEB_COUNTER_DESTROYED) == 1,
                      "%s: %s: one PIMIsValid call counted once (activate, deactivate, self-check, one temporary destroyed)",
                      e->label, when);
            }
        }
        check_unloaded(e, h, base, when, name_buffer);
        size_t blocks_after, bytes_after;
        heap_usage(&blocks_after, &bytes_after);
        /* The first cycle may warm caches of the loader or of the original's CRT; from the second on, a full
         * load / unload cycle must give back every block it took. */
        if (cycle >= 2) {
            CHECK(blocks_after == blocks_before && bytes_after == bytes_before,
                  "%s: %s: load + unload leaves the heap as it was (blocks %zu -> %zu, bytes %zu -> %zu)", e->label,
                  when, blocks_before, blocks_after, bytes_before, bytes_after);
        } else {
            INFO("%s: %s: heap blocks %zu -> %zu, bytes %zu -> %zu", e->label, when, blocks_before, blocks_after,
                 bytes_before, bytes_after);
        }
    }
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
    HMODULE first = run_plugin_checks(e, argv[2]);
    if (e == &kOriginal) {
        record_original_isvalid(argv[0], argv[2]);
    }
    reload_cycles(e, first, argv[2]);
    printf("%s: %d checks, %d failed\n", e->label, g_checks, g_failures);
    return g_failures;
}
