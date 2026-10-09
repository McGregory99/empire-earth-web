/* Unit harness for the tracing ddraw proxy.  Runs under Wine with the proxy installed as ddraw.dll in front
 * of Wine's builtin ddraw (see tools/ddraw_trace/Makefile, target `test`), makes calls through a fake plugin
 * module and from the exe itself, and checks (a) that results pass through intact and (b) the trace lines. */
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scenario.h"

static int g_fail, g_checks;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok)
        g_fail++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void check_hr(HRESULT got, HRESULT want, const char *what) {
    char b[160];
    sprintf(b, "%s (hr=%08lx)", what, (unsigned long)got);
    check(got == want, b);
}

/* ---- trace lines --------------------------------------------------------------------------------- */
static char *g_text;
static char **g_lines;
static int g_nlines;

static int load_trace(void) {
    const char *path = getenv("DDT_TRACE");
    FILE *f = path ? fopen(path, "rb") : NULL;
    long n;
    char *p;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_text = malloc((size_t)n + 1);
    n = (long)fread(g_text, 1, (size_t)n, f);
    g_text[n] = 0;
    fclose(f);
    g_nlines = 0;
    g_lines = malloc(sizeof(char *) * ((size_t)n + 2));
    for (p = g_text; *p;) {
        char *e = strchr(p, '\n');
        g_lines[g_nlines++] = p;
        if (!e)
            break;
        if (e > p && e[-1] == '\r')
            e[-1] = 0;
        *e = 0;
        p = e + 1;
    }
    return 1;
}

/* '*' matches any run of characters, including none. */
static int glob(const char *pat, const char *s) {
    if (!*pat)
        return !*s;
    if (*pat == '*') {
        do {
            if (glob(pat + 1, s))
                return 1;
        } while (*s++);
        return 0;
    }
    return *pat == *s && glob(pat + 1, s + 1);
}

static int g_cursor;

/* The next line at or after the cursor that matches; the cursor then moves past it. */
static void expect(const char *pat) {
    int i;
    char b[512];
    for (i = g_cursor; i < g_nlines; i++) {
        if (glob(pat, g_lines[i])) {
            g_cursor = i + 1;
            check(1, pat);
            return;
        }
    }
    sprintf(b, "trace line, in order: %s", pat);
    check(0, b);
}

static int count_matching(const char *pat) {
    int i, n = 0;
    for (i = 0; i < g_nlines; i++)
        n += glob(pat, g_lines[i]);
    return n;
}

static void expect_count(const char *pat, int want) {
    char b[512];
    int got = count_matching(pat);
    sprintf(b, "%d line(s) match (found %d): %s", want, got, pat);
    check(got == want, b);
}

/* ---- scenarios ----------------------------------------------------------------------------------- */
static BOOL WINAPI exe_enum_cb(GUID *g, LPSTR d, LPSTR n, LPVOID c) {
    (void)g; (void)d; (void)n;
    ++*(int *)c;
    return TRUE;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

static HWND make_window(void) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ddt_harness";
    RegisterClassA(&wc);
    return CreateWindowA("ddt_harness", "ddt", WS_OVERLAPPEDWINDOW, 0, 0, 256, 256, NULL, NULL, wc.hInstance, NULL);
}

static void cycle_a(HMODULE plugin, HWND hwnd) {
    ScenarioA r;
    ScenarioAFn fn = (ScenarioAFn)GetProcAddress(plugin, "scenario_a");
    int exe_callbacks = 0;
    HRESULT hr_exe;

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_a");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario A ran to the end");

    /* pass-through: the proxy must hand the real results back */
    check_hr(r.hr_enum_ex, DD_OK, "DirectDrawEnumerateExA result");
    check(r.enum_ex_callbacks > 0, "DirectDrawEnumerateExA invoked the plugin's callback");
    check_hr(r.hr_enum, DD_OK, "DirectDrawEnumerateA result");
    check(r.enum_callbacks > 0, "DirectDrawEnumerateA invoked the plugin's callback");
    check_hr(r.hr_create, DD_OK, "DirectDrawCreateEx result");
    check_hr(r.hr_coop, DD_OK, "SetCooperativeLevel result");
    check_hr(r.hr_qi, DD_OK, "QueryInterface(IDirect3D7) result");
    check(r.d3d_first != NULL && r.d3d_first == r.d3d_again, "two queries for IDirect3D7 give one interface pointer");
    check_hr(r.hr_primary, DD_OK, "CreateSurface(primary) result");
    check_hr(r.hr_target, DD_OK, "CreateSurface(3D target) result");
    check_hr(r.hr_device, DD_OK, "CreateDevice(HAL) result");
    check_hr(r.hr_begin, DD_OK, "BeginScene result");
    check_hr(r.hr_clear, DD_OK, "Clear result");
    check_hr(r.hr_end, DD_OK, "EndScene result");

    /* attribution: the exe is not a plugin module, so its calls must not appear */
    hr_exe = DirectDrawEnumerateA(exe_enum_cb, &exe_callbacks);
    check(SUCCEEDED(hr_exe) && exe_callbacks > 0, "a call from the exe still passes through");

    check(load_trace(), "trace file exists");
    if (!g_nlines)
        return;
    expect("# ddraw_trace v1");
    expect("# modules: fakeplugin.dll");
    expect("T1 DirectDrawEnumerateExA(lpCallback=P, lpContext=P, dwFlags=0x7) = DD_OK");
    expect("T1 DirectDrawEnumerateA(lpCallback=P, lpContext=P) = DD_OK");
    expect_count("T1 DirectDrawEnumerateA(*", 1);
    expect("T1 DirectDrawCreateEx(lpGuid=NULL, lplpDD->dd7#1, iid=IID_IDirectDraw7, pUnkOuter=NULL) = DD_OK");
    expect("T1 dd7#1.SetCooperativeLevel(hWnd=P, dwFlags=0x8) = DD_OK");
    expect("T1 dd7#1.QueryInterface(riid=IID_IDirect3D7, ppvObject->d3d7#1) = DD_OK");
    expect("T1 dd7#1.QueryInterface(riid=IID_IDirect3D7, ppvObject->d3d7#1) = DD_OK");
    expect("T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1, ddsCaps={dwCaps=0x200,*}}, lplpDDSurface->surf#1, pUnkOuter=NULL) = DD_OK");
    expect("T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x7, dwHeight=128, dwWidth=128, ddsCaps={dwCaps=0x2040,*}}, lplpDDSurface->surf#2, pUnkOuter=NULL) = DD_OK");
    expect("T1 d3d7#1.CreateDevice(rclsid=IID_IDirect3DHALDevice, surface=surf#2, device->dev7#1) = DD_OK");
    expect("T1 dev7#1.BeginScene() = DD_OK");
    expect("T1 dev7#1.Clear(count=0, rects=NULL, flags=0x1, color=0xff102030, z=f32:3f800000, stencil=0) = DD_OK");
    expect("T1 dev7#1.EndScene() = DD_OK");
    expect("T1 dev7#1.Release() = 0");
}

int main(void) {
    HWND hwnd = make_window();
    HMODULE plugin = LoadLibraryA("fakeplugin.dll");

    check(hwnd != NULL, "test window");
    check(plugin != NULL, "fake plugin loads");
    if (hwnd && plugin)
        cycle_a(plugin, hwnd);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
