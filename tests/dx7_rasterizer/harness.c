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
    const char *bases[2]; /* expected RTTI class chain (most derived first), NULL = only report it */
};

static const char *const kExportNames[4] = {"PIMGetName", "PIMGetType", "PIMIsValid",
                                            "RPIMGetRasterizer"};

static const struct Expect kOriginal = {"original", {252, 253, 254, 255}, -1,
                                        "Direct3D Hardware TnL", ".?AVDX7TnLRasterizer@@", {NULL, NULL}};
static const struct Expect kOurs = {"ours", {1, 2, 3, 4}, 0, "EEWeb Null Rasterizer",
                                    EEWEB_RTTI_NULL, {EEWEB_RTTI_NULL, EEWEB_RTTI_BASE}};

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
    for (uint32_t i = 0; i < chain->count && i < 16; i++) {
        const char *name = chain->bases[i]->type->name;
        if (e->bases[0]) {
            CHECK(i < 2 && strcmp(name, e->bases[i]) == 0, "%s: hierarchy[%u] is %s", e->label, (unsigned)i,
                  name);
        } else {
            INFO("%s: hierarchy[%u] is %s", e->label, (unsigned)i, name);
        }
    }
    if (e->bases[0]) {
        CHECK(chain->count == 2, "%s: hierarchy has exactly the class and its base", e->label);
    }
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
    CHECK(counter(obj, EEWEB_COUNTER_ACTIVATIONS) == 1, "%s: the Activate() call above was counted", e->label);

    /* PIMIsValid runs the DLL's internal exercise of the class: Activate / GetScreenCount /
     * Deactivate on the shared object, and new + dynamic_cast + virtual delete on a temporary. */
    PimIsValid is_valid;
    memcpy(&is_valid, &is_valid_raw, sizeof is_valid);
    long before[5], after[5];
    for (int k = 0; k < 5; k++) {
        before[k] = counter(obj, k);
    }
    CHECK(is_valid() == 0, "%s: PIMIsValid() is still false", e->label);
    for (int k = 0; k < 5; k++) {
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
     * (destroy, do not free) must run the derived destructor and then the base one. */
    DeletingDtor destroy;
    fn = slot(vtable, EEWEB_VSLOT_DELETING_DESTRUCTOR);
    memcpy(&destroy, &fn, sizeof destroy);
    long destroyed_before = counter(obj, EEWEB_COUNTER_DESTROYED);
    destroy(obj, 0);
    void *const *after_vtable = *(void *const **)obj;
    CHECK(after_vtable != vtable, "%s: slot %d (deleting destructor) ran: the vptr was reset", e->label,
          EEWEB_VSLOT_DELETING_DESTRUCTOR);
    CHECK(counter(obj, EEWEB_COUNTER_DESTROYED) == destroyed_before + 1, "%s: the derived destructor ran once", e->label);
    const struct RttiLocator *base_col = locator_of(after_vtable);
    CHECK(in_module(h, base_col) && strcmp(base_col->type->name, EEWEB_RTTI_BASE) == 0,
          "%s: the object was left with the base class vtable (the base destructor ran)", e->label);
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
        unsigned al = is_valid();
        CHECK(al == (unsigned)e->is_valid, "%s: PIMIsValid() returns %d in AL (got %u)", e->label, e->is_valid, al);
    }
    if (procs[3]) {
        check_rasterizer_object(e, h, procs[3], procs[2]);
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
