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

static int g_cursor;
static int load_trace(void);
static void load_trace_again(void) {
    int cursor = g_cursor;
    free(g_text);
    free(g_lines);
    load_trace();
    g_cursor = cursor;
}

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

static void hash_text(char out[40], const void *p, unsigned n) {
    unsigned long long h = fnv1a(p, n, 0xcbf29ce484222325ull);
    sprintf(out, "fnv1a64:%08x%08x", (unsigned)(h >> 32), (unsigned)h);
}

static void cycle_b(HMODULE plugin, HWND hwnd) {
    ScenarioB r;
    ScenarioBFn fn = (ScenarioBFn)GetProcAddress(plugin, "scenario_b");
    unsigned char vertices[VB_VERTICES * VB_VERTEX_SIZE];
    unsigned char texture[TEX_SIZE * TEX_SIZE * 4];
    char hash[40], pat[1024];

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_b");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario B ran to the end");

    check_hr(r.hr_caps, DD_OK, "GetCaps result");
    check_hr(r.hr_enum_formats, DD_OK, "EnumTextureFormats result");
    check(r.format_callbacks > 0, "EnumTextureFormats invoked the plugin's callback");
    check_hr(r.hr_texture, DD_OK, "CreateSurface(texture) result");
    check_hr(r.hr_tex_lock, DD_OK, "surface Lock result");
    check_hr(r.hr_tex_unlock, DD_OK, "surface Unlock result");
    check_hr(r.hr_set_texture, DD_OK, "SetTexture with a wrapped surface (the proxy hands the real one down)");
    check_hr(r.hr_render_state, DD_OK, "SetRenderState result");
    check(r.got_render_state == 1, "GetRenderState returns the state that was set");
    check_hr(r.hr_stage_state, DD_OK, "SetTextureStageState result");
    check_hr(r.hr_transform, DD_OK, "SetTransform result");
    check_hr(r.hr_viewport, DD_OK, "SetViewport result");
    check_hr(r.hr_material, DD_OK, "SetMaterial result");
    check_hr(r.hr_light, DD_OK, "SetLight result");
    check_hr(r.hr_light_enable, DD_OK, "LightEnable result");
    check_hr(r.hr_vb_create, DD_OK, "CreateVertexBuffer result");
    check_hr(r.hr_vb_lock, DD_OK, "vertex buffer Lock result");
    check_hr(r.hr_vb_unlock, DD_OK, "vertex buffer Unlock result");
    check_hr(r.hr_draw_vb, DD_OK, "DrawPrimitiveVB with a wrapped vertex buffer");
    check_hr(r.hr_draw_indexed, DD_OK, "DrawIndexedPrimitiveVB result");
    check_hr(r.hr_draw_prim, DD_OK, "DrawPrimitive with application vertices result");
    check_hr(r.hr_vb_desc, DD_OK, "GetVertexBufferDesc result");
    check(r.vb_fvf == VB_FVF && r.vb_vertices == VB_VERTICES, "GetVertexBufferDesc reports the creation parameters");
    check_hr(r.hr_clipper, DD_OK, "CreateClipper result (unwrapped interface)");
    check_hr(r.hr_clipper_raw, DD_OK, "a call on the unwrapped clipper works");
    check_hr(r.hr_clipper_use, DD_OK, "SetClipper with the unwrapped clipper");
    check_hr(r.hr_qi_dd4, DD_OK, "QueryInterface(IDirectDraw4) result (unwrapped interface)");
    check_hr(r.hr_dd4_caps, DD_OK, "a call on the unwrapped IDirectDraw4 works");

    load_trace_again();
    check(g_nlines > 0, "the trace could be read after scenario B");
    if (!g_nlines)
        return;
    expect("T1 dd7#2.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1, ddsCaps={dwCaps=0x200,*}}, lplpDDSurface->surf#3, pUnkOuter=NULL) = DD_OK");
    expect("T1 d3d7#2.CreateDevice(rclsid=IID_IDirect3DHALDevice, surface=surf#4, device->dev7#2) = DD_OK");
    expect("T1 dev7#2.GetCaps(desc=>{dwDevCaps=0x*, dpcLineCaps={dwSize=*}, dpcTriCaps={dwSize=*}, *}) = DD_OK");
    expect("T1   cb EnumTextureFormats(lpDDPixFmt={dwSize=32, dwFlags=0x*}, lpContext=P) = 1");
    expect("T1 dev7#2.EnumTextureFormats(cb=P, ctx=P) = DD_OK");
    expect("T1 dd7#2.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1007, dwHeight=8, dwWidth=8, ddpfPixelFormat={dwSize=32, dwFlags=0x41, dwRGBBitCount=32, dwRBitMask=0xff0000, dwGBitMask=0xff00, dwBBitMask=0xff, dwRGBAlphaBitMask=0xff000000}, ddsCaps={dwCaps=0x1000, dwCaps2=0x10,*}}, lplpDDSurface->surf#5, pUnkOuter=NULL) = DD_OK");

    memset(texture, 0, sizeof texture);
    fill_texture(texture, TEX_SIZE * 4);
    hash_text(hash, texture, sizeof texture);
    expect("T1 surf#5.Lock(lpDestRect=NULL, lpDDSurfaceDesc=>{dwSize=124, dwFlags=0x*, *}, dwFlags=0x*, hEvent=NULL) = DD_OK");
    sprintf(pat, "T1 surf#5.Unlock(lpSurfaceData=NULL) = DD_OK locked={rect=NULL, bytes=256, hash=%s}", hash);
    expect(pat);
    expect("T1 dev7#2.SetTexture(stage=0, surface=surf#5) = DD_OK");
    expect("T1 dev7#2.SetRenderState(dwRenderStateType=ZENABLE(7), dwRenderState=0x1) = DD_OK");
    expect("T1 dev7#2.GetRenderState(dwRenderStateType=ZENABLE(7), lpdwRenderState=>0x1) = DD_OK");
    expect("T1 dev7#2.SetTextureStageState(dwStage=0, d3dTexStageStateType=COLOROP(1), dwState=0x4) = DD_OK");
    expect("T1 dev7#2.SetTransform(state=WORLD(1), matrix={f32[16]=3f800000 00000000 00000000 00000000 00000000 3f800000 00000000 00000000 00000000 00000000 3f800000 00000000 3f000000 00000000 00000000 3f800000}) = DD_OK");
    expect("T1 dev7#2.SetViewport(data={dwX=0, dwY=0, dwWidth=128, dwHeight=128, dvMinZ=f32:00000000, dvMaxZ=f32:3f800000}) = DD_OK");
    expect("T1 dev7#2.SetMaterial(data={diffuse={f32:3f800000, f32:3f800000, f32:3f800000, f32:3f800000}, *}) = DD_OK");
    expect("T1 dev7#2.SetLight(idx=0, data={dltType=DIRECTIONAL, *}) = DD_OK");
    expect("T1 dev7#2.LightEnable(dwLightIndex=0, bEnable=1) = DD_OK");
    expect("T1 d3d7#2.CreateVertexBuffer(desc={dwSize=16, dwCaps=0x10000, dwFVF=0x152, dwNumVertices=4}, buffer->vb#1, flags=0x0) = DD_OK");

    memset(vertices, 0, sizeof vertices);
    fill_vertices(vertices);
    hash_text(hash, vertices, sizeof vertices);
    expect("T1 vb#1.Lock(flags=0x*, data=>P, data_size=>0x90) = DD_OK");
    sprintf(pat, "T1 vb#1.Unlock() = DD_OK locked={bytes=144, hash=%s}", hash);
    expect(pat);
    expect("T1 dev7#2.DrawPrimitiveVB(primitive_type=TRIANGLELIST(4), vb=vb#1, start_vertex=0, vertex_count=3, flags=0x0) = DD_OK");
    {
        unsigned short idx[6] = {0, 1, 2, 2, 1, 3};
        char h1[40], h2[40];
        hash_text(h1, idx, sizeof idx);
        hash_text(h2, vertices, 3 * VB_VERTEX_SIZE);
        sprintf(pat, "T1 dev7#2.DrawIndexedPrimitiveVB(primitive_type=TRIANGLELIST(4), vb=vb#1, start_vertex=0, vertex_count=4, indices=u16[6]:%s, index_count=6, flags=0x0) = DD_OK", h1);
        expect(pat);
        sprintf(pat, "T1 dev7#2.DrawPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x152, vertices=fvf[3x36]:%s, vertex_count=3, flags=0x0) = DD_OK", h2);
        expect(pat);
    }
    expect("T1 vb#1.GetVertexBufferDesc(desc=>{dwSize=16, dwCaps=0x*, dwFVF=0x152, dwNumVertices=4}) = DD_OK");
    expect("T1 dd7#2.CreateClipper(dwFlags=0x0, lplpDDClipper->raw, pUnkOuter=NULL) = DD_OK");
    expect("T1 surf#3.SetClipper(lpDDClipper=P) = DD_OK");
    expect("T1 dd7#2.QueryInterface(riid=IID_IDirectDraw4, ppvObject->raw) = DD_OK");
    expect("T1 surf#3.GetFlipStatus(dwFlags=0x1) = ? [timing]");
    expect("T1 dd7#2.GetScanLine(lpdwScanLine=>?) = ? [timing]");
    expect_count("T1 surf#3.GetFlipStatus(*", 1);
    expect_count("T1 dd7#2.GetScanLine(*", 1);
}

static void cycle_d(HMODULE plugin, HWND hwnd) {
    ScenarioD r;
    ScenarioDFn fn = (ScenarioDFn)GetProcAddress(plugin, "scenario_d");
    unsigned char region[4 * 4 * 4];
    unsigned x, y;
    char hash[40], pat[1024];

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_d");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario D ran to the end");
    check_hr(r.hr_srt, DD_OK, "SetRenderTarget with a wrapped surface");
    check_hr(r.hr_get_rt, DD_OK, "GetRenderTarget");
    check(r.got_rt != NULL && r.got_rt == r.target2, "GetRenderTarget returns the wrapper that SetRenderTarget was given (identity)");
    check_hr(r.hr_att1, DD_OK, "GetAttachedSurface(mipmap) result");
    check_hr(r.hr_att2, DD_OK, "GetAttachedSurface(mipmap) again");
    check(r.att1 != NULL && r.att1 == r.att2, "the same attached surface comes back as the same wrapper");
    check_hr(r.hr_enum_att, DD_OK, "EnumAttachedSurfaces result");
    check(r.enum_callbacks >= 1, "EnumAttachedSurfaces invoked the plugin's callback");
    check(r.enum_seen != NULL && r.enum_seen == r.att1, "the callback is given the attached surface's wrapper (identity)");
    check(r.hr_not_found == DDERR_NOTFOUND && r.bad_surface == NULL, "a failing GetAttachedSurface reports DDERR_NOTFOUND and leaves the output NULL");
    check_hr(r.hr_get_tex, DD_OK, "GetTexture");
    check(r.got_tex != NULL && r.got_tex == r.tex, "GetTexture returns the wrapper that SetTexture was given (identity)");
    check_hr(r.hr_bltfast, DD_OK, "BltFast with a wrapped source");
    check(FAILED(r.hr_bad_create), "CreateSurface with a bad description fails");
    check_hr(r.hr_lock1, DD_OK, "Lock of a rectangle");
    check(r.hr_lock2_busy == DDERR_SURFACEBUSY, "a second Lock of the same surface fails (DDERR_SURFACEBUSY)");
    check_hr(r.hr_unlock, DD_OK, "Unlock of the rectangle");
    check_hr(r.hr_vidmem, DD_OK, "GetAvailableVidMem");

    load_trace_again();
    check(g_nlines > 0, "the trace could be read after scenario D");
    if (!g_nlines)
        return;
    expect("T1 dd7#3.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x7, dwHeight=128, dwWidth=128, ddsCaps={dwCaps=0x2040,*}}, lplpDDSurface->surf#7, pUnkOuter=NULL) = DD_OK");
    expect("T1 d3d7#3.CreateDevice(rclsid=IID_IDirect3DHALDevice, surface=surf#7, device->dev7#3) = DD_OK");
    expect("T1 dev7#3.SetRenderTarget(surface=surf#8, flags=0x0) = DD_OK");
    expect("T1 dev7#3.GetRenderTarget(surface->surf#8) = DD_OK");
    expect("T1 surf#9.GetAttachedSurface(lpDDSCaps={dwCaps=0x400000,*}, lplpDDAttachedSurface->surf#10) = DD_OK");
    expect("T1 surf#9.GetAttachedSurface(lpDDSCaps={dwCaps=0x400000,*}, lplpDDAttachedSurface->surf#10) = DD_OK");
    expect("T1   cb EnumAttachedSurfaces(lpDDSurface=surf#10, lpDDSurfaceDesc={dwSize=124,*}, lpContext=P) = 1");
    expect("T1 surf#9.EnumAttachedSurfaces(lpContext=P, lpEnumSurfacesCallback=P) = DD_OK");
    expect("T1 surf#10.GetCaps(lpDDSCaps=>{dwCaps=0x*,*}) = DD_OK");
    expect("T1 surf#7.GetAttachedSurface(lpDDSCaps={dwCaps=0x4,*}, lplpDDAttachedSurface->NULL) = DDERR_NOTFOUND");
    expect("T1 dev7#3.SetTexture(stage=0, surface=surf#11) = DD_OK");
    expect("T1 dev7#3.GetTexture(stage=0, surface->surf#11) = DD_OK");
    expect("T1 surf#13.BltFast(dwX=1, dwY=2, lpDDSrcSurface=surf#12, lpSrcRect={0,0,8,8}, dwTrans=0x10) = DD_OK");
    expect("T1 dd7#3.CreateSurface(lpDDSurfaceDesc={dwSize=0,*}, lplpDDSurface->NULL, pUnkOuter=NULL) = E_INVALIDARG");
    memset(region, 0, sizeof region);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            ((unsigned *)region)[y * 4 + x] = rect_pixel(LOCK_RECT_L + x, LOCK_RECT_T + y);
    hash_text(hash, region, sizeof region);
    expect("T1 surf#11.Lock(lpDestRect={2,2,6,6}, lpDDSurfaceDesc=>{dwSize=124,*}, dwFlags=0x21, hEvent=NULL) = DD_OK");
    expect("T1 surf#11.Lock(lpDestRect=NULL, lpDDSurfaceDesc=>-, dwFlags=0x1, hEvent=NULL) = DDERR_SURFACEBUSY");
    sprintf(pat, "T1 surf#11.Unlock(lpSurfaceData={2,2,6,6}) = DD_OK locked={rect={2,2,6,6}, bytes=64, hash=%s}", hash);
    expect(pat);
    expect("T1 dd7#3.GetAvailableVidMem(lpDDCaps={dwCaps=0x1000, dwCaps2=0x0, dwCaps3=0x0, dwCaps4=0x0}, lpdwTotal=>*, lpdwFree=>*) = DD_OK");
}

static void cycle_t(HMODULE plugin, HWND hwnd) {
    ScenarioT r;
    ScenarioTFn fn = (ScenarioTFn)GetProcAddress(plugin, "scenario_t");
    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_t");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario T ran to the end");
    check(r.thread_calls == 3, "the second thread made its three calls");
    load_trace_again();
    check(g_nlines > 0, "the trace could be read after scenario T");
    if (!g_nlines)
        return;
    /* each thread's run of identical polls collapses on its own: the other thread's lines in between must not break it */
    expect_count("T1 surf#14.GetFlipStatus(dwFlags=0x1) = ? [timing]", 1);
    expect_count("T2 dd7#4.GetAvailableVidMem(*", 3);
}

static void cycle_c(HMODULE plugin, HWND hwnd) {
    ScenarioC r;
    ScenarioCFn fn = (ScenarioCFn)GetProcAddress(plugin, "scenario_c");
    const char *trace = getenv("DDT_TRACE");
    char done[600];
    int i;

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_c");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario C ran to the end");
    for (i = 0; i < 6; i++) {
        char what[64];
        sprintf(what, "Blt #%d to the primary passes through after the cap too", i + 1);
        check_hr(r.hr_blt[i], DD_OK, what);
    }
    load_trace_again();
    check(g_nlines > 0, "the trace could be read after scenario C");
    if (!g_nlines)
        return;
    check_hr(r.hr_partial, DD_OK, "sprite Blt to the primary passes through");
    check_hr(r.hr_fill, DD_OK, "colour fill of the primary passes through");
    /* the cap is DDT_MAX_PRESENTS=3: a sprite blit and a colour fill do not count, three whole-surface blits do, then
     * the trace ends */
    expect("T1 surf#15.Blt(lpDestRect={10,10,42,42}, lpDDSrcSurface=surf#16, lpSrcRect={10,10,42,42}, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK");
    expect("T1 surf#15.Blt(lpDestRect=NULL, lpDDSrcSurface=NULL, lpSrcRect=NULL, dwFlags=0x1000400, lpDDBltFx={dwSize=100,*}) = DD_OK");
    expect_count("T1 surf#15.Blt(*", 5);
    expect("T1 surf#15.Blt(lpDestRect=NULL, lpDDSrcSurface=surf#16, lpSrcRect=NULL, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK");
    expect("# end: 3 presents");
    check(g_nlines > 0 && !strcmp(g_lines[g_nlines - 1], "# end: 3 presents"), "nothing is traced after the cap");
    sprintf(done, "%s.done", trace ? trace : "");
    check(GetFileAttributesA(done) != INVALID_FILE_ATTRIBUTES, "the cap writes the .done marker for the driver script");

    /* diagnostics outside the normalized trace: wall-clock milliseconds of each present, in <trace>.times */
    {
        char times[600], text[4096];
        FILE *f;
        size_t n;
        int lines = 0, ok = 1, i2;
        long prev = -1;
        sprintf(times, "%s.times", trace ? trace : "");
        f = fopen(times, "rb");
        n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        if (f)
            fclose(f);
        text[n] = 0;
        for (i2 = 0; text[i2];) {
            int idx = 0;
            long ms = 0;
            if (sscanf(text + i2, "%d %ld", &idx, &ms) != 2 || idx != lines + 1 || ms < prev)
                ok = 0;
            prev = ms;
            lines++;
            while (text[i2] && text[i2] != '\n')
                i2++;
            if (text[i2])
                i2++;
        }
        check(lines == 3 && ok, "<trace>.times has one 'n milliseconds' line per traced present, increasing");
    }

    /* diagnostics: which modules called ddraw, and whether their calls were traced (attribution evidence) */
    {
        char callers[600], text[2048];
        FILE *f;
        size_t n;
        sprintf(callers, "%s.callers", trace ? trace : "");
        f = fopen(callers, "rb");
        n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        if (f)
            fclose(f);
        text[n] = 0;
        check(strstr(text, "fakeplugin.dll ") && strstr(text, " traced\n") != NULL, "<trace>.callers lists the plugin as traced");
        check(strstr(text, "harness.exe 1 untraced") != NULL, "<trace>.callers lists the exe's single call as untraced");
    }
}

/* The proxy classifies every argument of the 145 wrapped methods from the SDK's type strings; one that looks like a COM
 * interface but would be passed through unwrapped makes DDT_SelfCheck report a problem.  `broken` runs against a build whose
 * type map lacks LPDIRECTDRAWSURFACE7 (the Makefile builds one): the check must notice. */
static void check_selfcheck(int broken) {
    HMODULE m = GetModuleHandleA("ddraw.dll");
    int(WINAPI * fn)(void) = m ? (int(WINAPI *)(void))GetProcAddress(m, "DDT_SelfCheck") : NULL;
    int problems;
    char what[128];
    check(fn != NULL, "the proxy exports DDT_SelfCheck");
    if (!fn)
        return;
    problems = fn();
    sprintf(what, "DDT_SelfCheck reports %d problem(s)", problems);
    if (broken)
        check(problems > 0, "a type map without LPDIRECTDRAWSURFACE7 is noticed by the self-check");
    else
        check(problems == 0, what);
}

/* Run against the scripted mock ddraw (separate process: DDT_NEXT=mockddraw.dll, DDT_MAX_PRESENTS=3). */
static void cycle_mock(HMODULE plugin, HWND hwnd) {
    ScenarioM r;
    ScenarioMFn fn = (ScenarioMFn)GetProcAddress(plugin, "scenario_m");
    const char *trace = getenv("DDT_TRACE");
    char times[600], text[1024];
    FILE *f;
    size_t n;

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_m");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario M ran to the end");
    check_hr(r.hr_flip_first[0], DDERR_WASSTILLDRAWING, "the mock's first Flip is busy");
    check_hr(r.hr_flip_first[1], DDERR_WASSTILLDRAWING, "the mock's second Flip is busy");
    check_hr(r.hr_flip_first[2], DD_OK, "the mock's third Flip succeeds");
    check_hr(r.hr_flip_override, DD_OK, "Flip with an override surface reaches the real object unwrapped (the mock refuses a wrapper)");
    check_hr(r.hr_blt_busy, DDERR_WASSTILLDRAWING, "Blt reports busy");
    check_hr(r.hr_blt_ok, DD_OK, "Blt with a wrapped source reaches the real object unwrapped");
    check_hr(r.hr_bltfast, DD_OK, "BltFast with a wrapped source reaches the real object unwrapped");
    check_hr(r.hr_flip_after_cap, DD_OK, "calls after the cap pass through");
    check_hr(r.hr_attached, DD_OK, "GetAttachedSurface result");
    check(r.attached != NULL && r.attached == r.back, "GetAttachedSurface hands back the back buffer's own wrapper (identity)");

    load_trace_again();
    check(g_nlines > 0, "the mock run's trace could be read");
    if (!g_nlines)
        return;
    expect("T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1, ddsCaps={dwCaps=0x200,*}}, lplpDDSurface->surf#1, pUnkOuter=NULL) = DD_OK");
    expect("T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1, ddsCaps={dwCaps=0x40,*}}, lplpDDSurface->surf#2, pUnkOuter=NULL) = DD_OK");
    expect("T1 surf#1.GetAttachedSurface(lpDDSCaps={dwCaps=0x4,*}, lplpDDAttachedSurface->surf#2) = DD_OK");
    expect("T1 surf#1.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x0) = DDERR_WASSTILLDRAWING [timing]");
    expect("T1 surf#1.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x0) = DD_OK");
    expect("T1 surf#1.Flip(lpDDSurfaceTargetOverride=surf#2, dwFlags=0x0) = DD_OK");
    expect("T1 surf#1.Blt(lpDestRect=NULL, lpDDSrcSurface=surf#2, lpSrcRect=NULL, dwFlags=0x8000000, lpDDBltFx=NULL) = DDERR_WASSTILLDRAWING [timing]");
    expect("T1 surf#1.Blt(lpDestRect=NULL, lpDDSrcSurface=surf#2, lpSrcRect=NULL, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK");
    expect("# end: 3 presents");
    expect_count("T1 surf#1.Flip(*", 3);
    expect_count("*WASSTILLDRAWING*", 2);
    check(g_nlines > 0 && !strcmp(g_lines[g_nlines - 1], "# end: 3 presents"),
          "failed presents were not counted: the cap came after the third successful one");
    sprintf(times, "%s.times", trace ? trace : "");
    f = fopen(times, "rb");
    n = f ? fread(text, 1, sizeof text - 1, f) : 0;
    if (f)
        fclose(f);
    text[n] = 0;
    check(strstr(text, "3 ") != NULL && strstr(text, "4 ") == NULL, "<trace>.times has exactly the three successful presents");
}

/* Run with DDT_ARM_FILE and DDT_MAX_PRESENTS=2 (separate process): nothing is traced until the file exists. */
static void cycle_arm(HMODULE plugin, HWND hwnd) {
    ScenarioR r;
    ScenarioRFn fn = (ScenarioRFn)GetProcAddress(plugin, "scenario_r");
    const char *trace = getenv("DDT_TRACE");
    char done[600];
    int i;

    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_r");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario R ran to the end");
    check(r.armed_file_made, "the plugin made the arm file after its second present");
    for (i = 0; i < 8; i++)
        check_hr(r.hr_blt[i], DD_OK, "every present passes through, armed or not");
    load_trace_again();
    check(g_nlines > 0, "the armed trace could be read");
    if (!g_nlines)
        return;
    /* the arm file appeared before present 3; present 3 itself is still outside the window (its line is written before the
     * proxy looks), so the window holds presents 4 and 5 and the cap of 2 ends it there */
    expect_count("*CreateSurface*", 0);
    expect_count("*SetCooperativeLevel*", 0);
    expect_count("T1 surf#1.Blt(lpDestRect=NULL, lpDDSrcSurface=surf#2, lpSrcRect=NULL, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK", 2);
    expect_count("# armed*", 1);
    check(g_nlines > 0 && !strcmp(g_lines[g_nlines - 1], "# end: 2 presents"), "the cap counts presents from the arming, not from the start");
    sprintf(done, "%s.done", trace ? trace : "");
    check(GetFileAttributesA(done) != INVALID_FILE_ATTRIBUTES, "the armed capture writes the .done marker");
}

int main(int argc, char **argv) {
    HWND hwnd = make_window();
    HMODULE plugin = LoadLibraryA("fakeplugin.dll");

    check(hwnd != NULL, "test window");
    check(plugin != NULL, "fake plugin loads");
    if (argc > 1 && !strcmp(argv[1], "selfcheck-broken")) {
        check_selfcheck(1);
    } else if (hwnd && plugin && argc > 1 && !strcmp(argv[1], "arm")) {
        load_trace();
        cycle_arm(plugin, hwnd);
    } else if (hwnd && plugin && argc > 1 && !strcmp(argv[1], "mock")) {
        load_trace();
        cycle_mock(plugin, hwnd);
    } else if (hwnd && plugin) {
        check_selfcheck(0);
        cycle_a(plugin, hwnd);
        cycle_b(plugin, hwnd);
        cycle_d(plugin, hwnd);
        cycle_t(plugin, hwnd);
        cycle_c(plugin, hwnd);
    }
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
