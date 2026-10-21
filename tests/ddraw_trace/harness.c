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


/* ---- the side data file (DDT_DATA): a minimal parser of the documented format, used by the data scenarios ---------------- */
#define DATA_HDR 16
#define REC_HDR 96
enum { K_VERTICES = 1, K_INDICES = 2, K_SURFACE = 3 };
typedef struct Rec {
    unsigned off, kind, total, seq, object, plen, p[16];
    unsigned long long hash;
    const unsigned char *payload;
} Rec;
static unsigned char *g_data;
static long g_data_len;
static Rec g_recs[256];
static int g_nrecs;

static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static int load_data(const char *path) {
    FILE *f = fopen(path, "rb");
    long off;
    g_nrecs = 0;
    free(g_data);
    g_data = NULL;
    g_data_len = 0;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    g_data_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_data = malloc((size_t)g_data_len + 1);
    g_data_len = (long)fread(g_data, 1, (size_t)g_data_len, f);
    fclose(f);
    if (g_data_len < DATA_HDR || memcmp(g_data, "DDTDATA1", 8) || rd32(g_data + 8) != 1)
        return 0;
    for (off = DATA_HDR; off + REC_HDR <= g_data_len && g_nrecs < 256;) {
        Rec *r = &g_recs[g_nrecs];
        const unsigned char *h = g_data + off;
        int i;
        if (rd32(h) != 0x31524444u) /* "DDR1" */
            return 0;
        r->off = (unsigned)off;
        r->kind = rd32(h + 4);
        r->total = rd32(h + 8);
        r->seq = rd32(h + 12);
        r->object = rd32(h + 16);
        r->plen = rd32(h + 20);
        r->hash = rd32(h + 24) | (unsigned long long)rd32(h + 28) << 32;
        for (i = 0; i < 16; i++)
            r->p[i] = rd32(h + 32 + 4 * i);
        r->payload = h + REC_HDR;
        if (r->total < REC_HDR + r->plen || r->total % 8 || off + (long)r->total > g_data_len)
            return 0;
        off += (long)r->total;
        g_nrecs++;
    }
    return off == g_data_len;
}

/* "<key>@<offset>+<length>" in a trace line */
static int line_ref(const char *line, const char *key, unsigned *off, unsigned *len) {
    char pat[64];
    const char *p;
    sprintf(pat, "%s@", key); /* the argument "indices=u16[6]:..." has the same name */
    p = strstr(line, pat);
    return p && sscanf(p + strlen(pat), "%u+%u", off, len) == 2;
}

static int check_ref(const char *line, const char *key, int rec, const char *what) {
    unsigned off = 0, len = 0;
    char b[256];
    int ok = line_ref(line, key, &off, &len) && rec < g_nrecs && off == g_recs[rec].off && len == g_recs[rec].total;
    sprintf(b, "%s: %s@%u+%u is record %d (offset %u, length %u)", what, key, off, len, rec, rec < g_nrecs ? g_recs[rec].off : 0,
            rec < g_nrecs ? g_recs[rec].total : 0);
    check(ok, b);
    return ok;
}

static int check_rec(int i, unsigned kind, unsigned object, unsigned plen, const unsigned char *want, const char *what) {
    char b[256];
    const Rec *r = &g_recs[i];
    int ok = i < g_nrecs && r->kind == kind && r->object == object && r->plen == plen && !memcmp(r->payload, want, plen) &&
             r->hash == fnv1a(want, plen, 0xcbf29ce484222325ull);
    sprintf(b, "record %d is %s (kind %u, object %u, %u payload bytes, bytes and hash as written)", i, what, kind, object, plen);
    check(ok, b);
    return ok;
}

static char *read_all(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    char *t;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    t = malloc((size_t)*n + 1);
    *n = (long)fread(t, 1, (size_t)*n, f);
    t[*n] = 0;
    fclose(f);
    return t;
}

static void run_scenario_v(HMODULE plugin, HWND hwnd, ScenarioV *r) {
    ScenarioVFn fn = (ScenarioVFn)GetProcAddress(plugin, "scenario_v");
    int i;
    memset(r, 0, sizeof *r);
    check(fn != NULL, "fake plugin exports scenario_v");
    if (!fn)
        return;
    check(fn(hwnd, r) == 0, "scenario V ran to the end");
    for (i = 0; i < 8; i++)
        check_hr(r->hr_draw[i], DD_OK, "draw call result passes through (the mock refuses a wrapped vb)");
}

/* DDT_DATA unset: the trace is what it always was, and no data file appears. */
static void cycle_data_off(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    char path[700];
    const char *trace = getenv("DDT_TRACE");
    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    check(g_nlines > 0, "the data-off trace could be read");
    expect_count("*blob=*", 0);
    expect_count("# data:*", 0);
    expect_count("T1 dev7#1.DrawIndexedPrimitiveVB(*", 5);
    sprintf(path, "%s.data", trace ? trace : "");
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, "no data file without DDT_DATA");
}

/* With the data file off, the trace is the same text (the header line and the blob suffixes aside). */
static void check_same_as_off(void) {
    const char *off_trace = getenv("DDT_OFF_TRACE");
    long off_n = 0;
    char *off_text;
    int i;
    off_text = off_trace ? read_all(off_trace, &off_n) : NULL;
    check(off_text != NULL, "the data-off trace of the same scenario is available");
    if (off_text) {
        char *p = off_text, *e;
        int same = 1;
        for (i = 0; i < g_nlines; i++) {
            char tmp[8600], *cut;
            if (!strncmp(g_lines[i], "# data:", 7) || !strncmp(g_lines[i], "# snapshot", 10))
                continue;
            strncpy(tmp, g_lines[i], sizeof tmp - 1);
            tmp[sizeof tmp - 1] = 0;
            cut = strstr(tmp, " blob=");
            if (cut)
                *cut = 0;
            e = strchr(p, '\n');
            if (!e || (size_t)(e - p) != strlen(tmp) || strncmp(p, tmp, strlen(tmp))) {
                same = 0;
                break;
            }
            p = e + 1;
        }
        check(same && !*p, "stripped of 'blob=' suffixes and the '# data:' header, the trace is byte-identical to the data-off trace");
        free(off_text);
    }
}

static void cycle_data(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    unsigned char exp[V_TOTAL * V_STRIDE], zero[4 * V_STRIDE], app[3 * V_STRIDE];
    WORD idx6[6] = {0, 1, 2, 2, 1, 3}, idx3[3] = {2, 1, 0};
    char path[700];
    const char *trace = getenv("DDT_TRACE");
    int i, k;
    static const int vrec[8] = {0, 2, 0, 2, 3, 4, 5, 5}, irec[8] = {1, 1, 1, -1, 1, 1, 6, -1};
    static const char *const names[8] = {"DrawIndexedPrimitiveVB", "DrawIndexedPrimitiveVB", "DrawIndexedPrimitiveVB", "DrawPrimitiveVB",
                                         "DrawIndexedPrimitiveVB", "DrawIndexedPrimitiveVB", "DrawIndexedPrimitive", "DrawPrimitive"};

    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    expect("# data: *");

    /* the records, in order of first use; identical draws share a record (content-addressed) */
    {
        char b[64];
        sprintf(b, "record count is %d", g_nrecs);
        check(g_nrecs == 7, b);
    }
    if (g_nrecs != 7)
        return;
    check(g_recs[0].off == DATA_HDR, "the first record starts right after the 16-byte file header");
    fill_pattern(exp, 0, 4, 1);
    check_rec(0, K_VERTICES, 1, 4 * V_STRIDE, exp, "vb#1 vertices 0..3, first generation");
    check(g_recs[0].p[0] == 0x152 && g_recs[0].p[1] == V_STRIDE && g_recs[0].p[2] == 0 && g_recs[0].p[3] == 4 && g_recs[0].p[4] == 0,
          "record 0 params: fvf 0x152, stride 36, start 0, count 4, source vb");
    check_rec(1, K_INDICES, 0, sizeof idx6, (const unsigned char *)idx6, "the 6 indices of the quad");
    check(g_recs[1].p[0] == 6, "record 1 param: 6 indices");
    fill_pattern(exp, 4, 4, 2);
    check_rec(2, K_VERTICES, 1, 4 * V_STRIDE, exp, "vb#1 vertices 4..7 (NOOVERWRITE append)");
    check(g_recs[2].p[2] == 4, "record 2 start_vertex is 4");
    fill_pattern(exp, 0, 4, 3);
    check_rec(3, K_VERTICES, 1, 4 * V_STRIDE, exp, "vb#1 vertices 0..3 after DISCARD (new content, same range)");
    memset(zero, 0, sizeof zero);
    check_rec(4, K_VERTICES, 1, 4 * V_STRIDE, zero, "vb#1 vertices 4..7 after DISCARD (zeros: not the old B and not the poisoned 0xEE)");
    fill_pattern(app, 0, 3, 4);
    check_rec(5, K_VERTICES, 0, 3 * V_STRIDE, app, "application vertex array");
    check(g_recs[5].p[4] == 1 && g_recs[5].p[3] == 3, "record 5 params: source application memory, count 3");
    check_rec(6, K_INDICES, 0, sizeof idx3, (const unsigned char *)idx3, "the 3 indices of the application draw");
    {
        long end = DATA_HDR;
        for (i = 0; i < g_nrecs; i++)
            end += g_recs[i].total;
        check(end == g_data_len, "the records tile the file exactly (sum of lengths + header = file size)");
    }

    /* the trace lines point at them */
    for (k = 0, i = 0; i < g_nlines && k < 8; i++) {
        char pat[96];
        sprintf(pat, "T1 dev7#1.%s(*", names[k]);
        if (!glob(pat, g_lines[i]))
            continue;
        if (!strcmp(names[k], "DrawIndexedPrimitiveVB") && k > 5)
            continue;
        check_ref(g_lines[i], "vertices=", vrec[k], names[k]);
        if (irec[k] >= 0)
            check_ref(g_lines[i], "indices=", irec[k], names[k]);
        else
            check(strstr(g_lines[i], "indices=@") == NULL, "a draw without indices has no indices reference");
        k++;
    }
    check(k == 8, "all 8 draw lines carry references");
    expect_count("*vb#1.Unlock*blob=*", 0);

    check_same_as_off();
}


static const char *nth_line(const char *pat, int n) {
    int i;
    for (i = 0; i < g_nlines; i++)
        if (glob(pat, g_lines[i]) && n-- == 0)
            return g_lines[i];
    return "";
}

static void run_scenario_s(HMODULE plugin, HWND hwnd) {
    ScenarioS r;
    ScenarioSFn fn = (ScenarioSFn)GetProcAddress(plugin, "scenario_s");
    int i, bad = 0;
    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_s");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario S ran to the end");
    for (i = 0; i < r.n; i++)
        if (r.hr[i] != DD_OK) {
            bad++;
            printf("note: call %d returned %08lx\n", i, (unsigned long)r.hr[i]);
        }
    check(r.n == 20 && !bad, "every call of scenario S succeeded (the mock refuses a Lock while a DC is out)");
    load_trace_again();
}

static void cycle_surf_off(HMODULE plugin, HWND hwnd) {
    char path[700];
    const char *trace = getenv("DDT_TRACE");
    run_scenario_s(plugin, hwnd);
    expect_count("*blob=*", 0);
    expect_count("# data:*", 0);
    sprintf(path, "%s.data", trace ? trace : "");
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, "no data file without DDT_DATA");
}

static int surf_rec(int i, unsigned object, unsigned origin, const unsigned rect[4], unsigned w, unsigned h, unsigned bpp, unsigned pf_flags,
                    const unsigned masks[4], const unsigned char *want, const char *what) {
    char b[200];
    const Rec *r = &g_recs[i];
    unsigned rw = rect[2] - rect[0], rh = rect[3] - rect[1], rowbytes = rw * bpp / 8;
    int ok = i < g_nrecs && r->kind == K_SURFACE && r->object == object && r->plen == rowbytes * rh && !memcmp(r->payload, want, r->plen) &&
             r->p[0] == rect[0] && r->p[1] == rect[1] && r->p[2] == rect[2] && r->p[3] == rect[3] && r->p[4] == w && r->p[5] == h &&
             r->p[6] == rowbytes && r->p[7] == rh && r->p[8] == origin && r->p[9] == pf_flags && r->p[10] == 0 && r->p[11] == bpp &&
             r->p[12] == masks[0] && r->p[13] == masks[1] && r->p[14] == masks[2] && r->p[15] == masks[3] &&
             r->hash == fnv1a(want, r->plen, 0xcbf29ce484222325ull);
    sprintf(b, "record %d is %s (object surf#%u, origin %u, %ux%u rect, rows without padding, format params, hash)", i, what, object, origin, rw, rh);
    check(ok, b);
    return ok;
}

static void check_hash_in_line(const char *line, int rec, const char *what) {
    char want[64], b[200];
    sprintf(want, "hash=fnv1a64:%08x%08x", (unsigned)(g_recs[rec].hash >> 32), (unsigned)g_recs[rec].hash);
    sprintf(b, "%s: the hash in the line is the record's payload hash", what);
    check(strstr(line, want) != NULL, b);
}

static void cycle_surf(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    unsigned a[64], rect[64], b[16], gdi[64], fill[64], x, y;
    static const unsigned argb[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, m565[4] = {0xf800, 0x07e0, 0x001f, 0};
    static const unsigned full8[4] = {0, 0, 8, 8}, full4[4] = {0, 0, 4, 4}, rc[4] = {S_RECT_L, S_RECT_T, S_RECT_R, S_RECT_B};
    unsigned short b565[16];
    const char *ln;

    run_scenario_s(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(g_nrecs == 6, "6 records: a (full), a (rect), b, c (GDI), c (as a texture after a blit), a (after a colour fill)");
    if (g_nrecs != 6)
        return;
    fill_texture((unsigned char *)a, 32);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            gdi[y * 8 + x] = 0xffc0de00u | (y * 8 + x);
            fill[y * 8 + x] = S_FILL_COLOR;
        }
    for (y = S_RECT_T; y < S_RECT_B; y++)
        for (x = S_RECT_L; x < S_RECT_R; x++)
            rect[(y - S_RECT_T) * 4 + x - S_RECT_L] = rect_pixel(x, y);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            b565[y * 4 + x] = s565_pixel(x, y);
    (void)b;
    check(g_recs[0].off == DATA_HDR, "the first record starts right after the file header");
    surf_rec(0, 3, 1, full8, 8, 8, 32, 0x41, argb, (unsigned char *)a, "surface a at Unlock (pitch 40 stripped to 32-byte rows)");
    surf_rec(1, 3, 1, rc, 8, 8, 32, 0x41, argb, (unsigned char *)rect, "the 4x4 rectangle a was locked with");
    surf_rec(2, 4, 1, full4, 4, 4, 16, 0x40, m565, (unsigned char *)b565, "surface b, RGB565");
    surf_rec(3, 5, 2, full8, 8, 8, 32, 0x41, argb, (unsigned char *)gdi, "surface c after GetDC/ReleaseDC (read after the DC was released)");
    {   /* a after its rectangle was rewritten, copied into c by the Blt */
        unsigned amodel[64];
        memcpy(amodel, a, sizeof a);
        for (y = S_RECT_T; y < S_RECT_B; y++)
            for (x = S_RECT_L; x < S_RECT_R; x++)
                amodel[y * 8 + x] = rect_pixel(x, y);
        surf_rec(4, 5, 5, full8, 8, 8, 32, 0x41, argb, (unsigned char *)amodel, "surface c bound as a texture after a Blt onto it");
    }
    surf_rec(5, 3, 4, full8, 8, 8, 32, 0x41, argb, (unsigned char *)fill, "surface a read as a BltFast source after a colour fill");

    /* the lines */
    ln = nth_line("T1 surf#3.Unlock(*", 0);
    check_ref(ln, "blob=", 0, "first Unlock of a");
    check_hash_in_line(ln, 0, "first Unlock of a");
    check_ref(nth_line("T1 surf#3.Unlock(*", 1), "blob=", 0, "second, identical Unlock of a (stored once)");
    ln = nth_line("T1 surf#3.Unlock(*", 2);
    check_ref(ln, "blob=", 1, "Unlock of a's rectangle");
    check_hash_in_line(ln, 1, "rectangle Unlock");
    ln = nth_line("T1 surf#4.Unlock(*", 0);
    check_ref(ln, "blob=", 2, "Unlock of b");
    check_hash_in_line(ln, 2, "Unlock of b");
    check(strstr(nth_line("T1 surf#4.Unlock(*", 1), "blob=") == NULL, "a DDLOCK_READONLY Unlock stores nothing");
    check_ref(nth_line("T1 surf#5.ReleaseDC(*", 0), "blob=", 3, "ReleaseDC of c");
    check(strstr(nth_line("T1 surf#5.Blt(*", 0), "blob=") == NULL, "Blt of an unchanged source stores nothing");
    check_ref(nth_line("T1 dev7#1.SetTexture(*", 0), "blob=", 4, "SetTexture of the blitted-to c");
    check(strstr(nth_line("T1 dev7#1.SetTexture(*", 1), "blob=") == NULL, "SetTexture of an unchanged texture stores nothing");
    check_ref(nth_line("T1 surf#5.BltFast(*", 0), "blob=", 5, "BltFast from the filled a");
    expect_count("T1 surf#5.Lock(*", 0);
    expect_count("T1 surf#3.Lock(*", 3);
    expect_count("*blob=*", 7);
    check(g_nlines > 0 && !strcmp(g_lines[g_nlines - 1], "# end: 3 presents"), "the capture ends at the third present");
    check_same_as_off();
}

/* DDT_DATA_MAX too small for the third record: the capture fails loudly and stops. */
static void cycle_data_cap(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    char path[700];
    const char *trace = getenv("DDT_TRACE");
    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    check(g_nlines > 0, "the capped trace could be read");
    expect("# data: *");
    expect("# ERROR: data cap of 400 bytes exceeded*");
    check(g_nlines > 0 && !strncmp(g_lines[g_nlines - 1], "# ERROR: data cap", 17), "the ERROR line ends the trace");
    sprintf(path, "%s.failed", trace ? trace : "");
    check(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, "a .failed marker is written");
    sprintf(path, "%s.done", trace ? trace : "");
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, "no .done marker: the capture is not complete");
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the truncated data file is still well-formed (whole records only)");
    check(g_nrecs == 2 && g_data_len <= 400, "only the 2 records that fit under the cap were written");
}

/* ---- data capture integrity: I/O and allocation failures end the capture loudly; a failed readback is retried -------------- */
static void check_failed_capture(const char *error_glob, const char *what) {
    char path[700], b[200];
    const char *trace = getenv("DDT_TRACE");
    sprintf(b, "%s: the trace says why (%s)", what, error_glob);
    expect(error_glob);
    sprintf(b, "%s: the ERROR line ends the trace", what);
    check(g_nlines > 0 && !strncmp(g_lines[g_nlines - 1], "# ERROR:", 8), b);
    sprintf(path, "%s.failed", trace ? trace : "");
    sprintf(b, "%s: a .failed marker is written", what);
    check(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, b);
    sprintf(path, "%s.done", trace ? trace : "");
    sprintf(b, "%s: no .done marker", what);
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, b);
}

/* DDT_TEST_FAULT=data-write: every write to the data file fails. */
static void cycle_data_write_fail(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    check_failed_capture("# ERROR: data write failed*", "failed data write");
    expect_count("# ERROR: data cap*", 0);
}

/* DDT_TEST_FAULT=data-alloc: the record index cannot grow. */
static void cycle_data_alloc_fail(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    check_failed_capture("# ERROR: data allocation failed*", "failed index allocation");
    expect_count("# ERROR: data cap*", 0);
}

/* DDT_TEST_FAULT=shadow-alloc: the vertex shadow copy cannot be allocated. */
static void cycle_data_shadow_fail(HMODULE plugin, HWND hwnd) {
    ScenarioV r;
    run_scenario_v(plugin, hwnd, &r);
    load_trace_again();
    check_failed_capture("# ERROR: data allocation failed*vertex shadow*", "failed shadow allocation");
    expect_count("# ERROR: data cap*", 0);
}

/* MOCK_FAIL_RO_LOCK=3: the read-back of c at its first SetTexture finds the surface busy.  Its pixels must not be lost: the
 * surface stays dirty and the next use stores them. */
static void cycle_surf_retry(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_s(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(g_nrecs == 6, "the same 6 records as without the failure");
    check(strstr(nth_line("T1 dev7#1.SetTexture(*", 0), "blob=unavailable") != NULL, "the failed read-back is reported as blob=unavailable");
    check_ref(nth_line("T1 dev7#1.SetTexture(*", 1), "blob=", 4, "the next SetTexture of c retries and stores it");
}

/* A Blt-written surface, then a rectangle Unlock: the rectangle is stored but the rest is still unstored. */
static void cycle_surf_partial(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    ScenarioS r;
    ScenarioSFn fn = (ScenarioSFn)GetProcAddress(plugin, "scenario_p");
    unsigned a[64], rect[16], x, y;
    static const unsigned argb[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000};
    static const unsigned full8[4] = {0, 0, 8, 8}, rc[4] = {S_RECT_L, S_RECT_T, S_RECT_R, S_RECT_B};
    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_p");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0 && r.n == 4, "scenario P ran to the end");
    load_trace_again();
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(g_nrecs == 2, "2 records: the rectangle, then the whole surface used as a Blt source");
    if (g_nrecs != 2)
        return;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            a[y * 8 + x] = S_FILL_COLOR;
    for (y = S_RECT_T; y < S_RECT_B; y++)
        for (x = S_RECT_L; x < S_RECT_R; x++) {
            a[y * 8 + x] = rect_pixel(x, y);
            rect[(y - S_RECT_T) * 4 + x - S_RECT_L] = rect_pixel(x, y);
        }
    surf_rec(0, 2, 1, rc, 8, 8, 32, 0x41, argb, (unsigned char *)rect, "the locked rectangle at Unlock");
    surf_rec(1, 2, 4, full8, 8, 8, 32, 0x41, argb, (unsigned char *)a, "the whole surface (fill outside, rectangle inside) as a BltFast source");
    check_ref(nth_line("T1 surf#3.BltFast(*", 0), "blob=", 1, "BltFast from a, after the partial Unlock");
}

/* Textures created and filled before the capture is armed: the arming stores each one once ("# snapshot surf#N blob=..."). */
static void run_scenario_n(HMODULE plugin, HWND hwnd) {
    ScenarioS r;
    ScenarioSFn fn = (ScenarioSFn)GetProcAddress(plugin, "scenario_n");
    memset(&r, 0, sizeof r);
    check(fn != NULL, "fake plugin exports scenario_n");
    if (!fn)
        return;
    check(fn(hwnd, &r) == 0, "scenario N ran to the end");
    load_trace_again();
}

static int snapshot_index(const char *pat) {
    int i;
    for (i = 0; i < g_nlines; i++)
        if (glob(pat, g_lines[i]))
            return i;
    return -1;
}

static void cycle_arm_snapshot(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    unsigned a[64], b[16], x, y;
    unsigned short b565[16];
    static const unsigned argb[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, m565[4] = {0xf800, 0x07e0, 0x001f, 0};
    static const unsigned full8[4] = {0, 0, 8, 8}, full4[4] = {0, 0, 4, 4};
    int armed, s3, s4;

    run_scenario_n(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(g_nrecs == 2, "2 records: the two textures filled before the arming (surf#2 is the 3D target, surf#1 the primary: none)");
    if (g_nrecs != 2)
        return;
    armed = snapshot_index("# armed*");
    s3 = snapshot_index("# snapshot surf#3 blob=@*");
    s4 = snapshot_index("# snapshot surf#4 blob=@*");
    check(armed >= 0 && s3 == armed + 2 && s4 == armed + 4, "after '# armed': a '# snapshot' line for surf#3, then surf#4, in id order, each after its own progress line");
    check(snapshot_index("# snapshot-begin surf#3") == armed + 1 && snapshot_index("# snapshot-begin surf#4") == armed + 3,
          "a '# snapshot-begin surf#N' progress line is written before each read-back, so a crash names the surface");
    expect_count("# snapshot *", 2);
    expect_count("# snapshot-begin *", 2);
    fill_texture((unsigned char *)a, 32);
    (void)b;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            b565[y * 4 + x] = s565_pixel(x, y);
    surf_rec(0, 3, 6, full8, 8, 8, 32, 0x41, argb, (unsigned char *)a, "texture a as it was when the capture was armed (origin 6)");
    surf_rec(1, 4, 6, full4, 4, 4, 16, 0x40, m565, (unsigned char *)b565, "texture b as it was when the capture was armed (origin 6)");
    check_ref(g_lines[s3], "blob=", 0, "the snapshot line of surf#3");
    check_ref(g_lines[s4], "blob=", 1, "the snapshot line of surf#4");
    check(strstr(nth_line("T1 surf#2.Blt(*", 0), "blob=") == NULL, "a Blt from a texture that was snapshotted and not changed stores nothing");
    check_same_as_off();
}

/* The read-back of the first texture fails (a busy surface): its snapshot says so, it stays dirty, and its first use retries. */
static void cycle_arm_snapshot_retry(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_n(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(snapshot_index("# snapshot surf#3 blob=unavailable") >= 0, "the failed read-back of surf#3 is reported as blob=unavailable");
    check(snapshot_index("# snapshot surf#4 blob=@*") >= 0, "the other texture is stored all the same");
    check(g_nrecs == 2, "2 records: b at the arming, a when its first use retried");
    check_ref(nth_line("T1 surf#2.Blt(*", 0), "blob=", 1, "the Blt that reads a retries the read-back and stores it");
    sprintf(path, "%s.failed", trace ? trace : "");
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, "an unavailable read-back is not a failed capture");
}

/* DDT_DATA_MAX is respected by the snapshots: the one that does not fit ends the capture loudly. */
static void cycle_arm_snapshot_cap(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_n(plugin, hwnd);
    check_failed_capture("# ERROR: data cap of 400 bytes exceeded*", "snapshot over the cap");
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path) && g_nrecs == 1 && g_data_len <= 400, "only the record that fits under the cap was written");
}

/* A surface DDraw freed behind the proxy's back (stale wrapper): the read-back must not call through its dead vtable. */
static void cycle_arm_snapshot_dead(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_n(plugin, hwnd);
    check(snapshot_index("# snapshot-begin surf#5") >= 0, "the progress line names the dead surface");
    check(snapshot_index("# snapshot surf#5 blob=unavailable") >= 0, "the dead surface is reported as blob=unavailable, not read");
    check(snapshot_index("# snapshot surf#3 blob=@*") >= 0 && snapshot_index("# snapshot surf#4 blob=@*") >= 0, "the live textures are stored all the same");
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path) && g_nrecs == 2, "2 records: the two live textures");
    expect("# end: 2 presents");
}

/* Default (no DDT_SNAPSHOT, no DDT_DATA_EARLY): arming never locks a live surface and stores nothing before the window. */
static void cycle_arm_default(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_n(plugin, hwnd);
    expect_count("# armed*", 1);
    expect_count("# snapshot*", 0);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path) && g_nrecs == 0, "no record at all: the pre-window Unlocks are not stored, the live surfaces are not read");
    check_same_as_off();
}

/* DDT_DATA_EARLY: the pixels are stored as the game writes them, before the window; the arming only names the records
 * ("# snapshot surf#N blob=@O+L"), in the order they were stored, and never locks a surface. */
static void cycle_arm_early(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    unsigned a[64], x, y;
    unsigned short b565[16];
    static const unsigned argb[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, m565[4] = {0xf800, 0x07e0, 0x001f, 0};
    static const unsigned full8[4] = {0, 0, 8, 8}, full4[4] = {0, 0, 4, 4};
    int armed, s3, s4;

    run_scenario_n(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path), "the data file has the documented header and well-formed records");
    check(g_nrecs == 2, "2 records, written at the game's own Unlock calls before the arming");
    if (g_nrecs != 2)
        return;
    armed = snapshot_index("# armed*");
    s3 = snapshot_index("# snapshot surf#3 blob=@*");
    s4 = snapshot_index("# snapshot surf#4 blob=@*");
    check(armed >= 0 && s3 == armed + 1 && s4 == armed + 2, "right after '# armed': the stored records of surf#3 and surf#4, no progress lines");
    expect_count("# snapshot-begin*", 0);
    fill_texture((unsigned char *)a, 32);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            b565[y * 4 + x] = s565_pixel(x, y);
    surf_rec(0, 3, 1, full8, 8, 8, 32, 0x41, argb, (unsigned char *)a, "texture a, stored at its Unlock (origin 1)");
    surf_rec(1, 4, 1, full4, 4, 4, 16, 0x40, m565, (unsigned char *)b565, "texture b, stored at its Unlock (origin 1)");
    check_ref(g_lines[s3], "blob=", 0, "the line of surf#3");
    check_ref(g_lines[s4], "blob=", 1, "the line of surf#4");
    check_same_as_off();
}

/* A Blt-written surface read as a Blt source before the window (the 2D screen surface, every frame, in the real game) is not
 * stored then: that would fill the cap with superseded full-surface records.  It stays dirty and is stored at its first use
 * inside the window. */
static void cycle_arm_early_blt(HMODULE plugin, HWND hwnd) {
    const char *trace = getenv("DDT_TRACE");
    char path[700];
    run_scenario_n(plugin, hwnd);
    sprintf(path, "%s.data", trace ? trace : "");
    check(load_data(path) && g_nrecs == 2, "only the 2 Unlock records: the Blt-source read-back before the window stored nothing");
    check(g_nrecs < 3 || g_recs[2].p[8] != 3, "no origin-3 record");
}

static void cycle_arm_early_cap(HMODULE plugin, HWND hwnd) {
    run_scenario_n(plugin, hwnd);
    check_failed_capture("# ERROR: data cap of 400 bytes exceeded*", "early records over the cap");
}

/* Data capture off: arming writes no snapshot line and no data file. */
static void cycle_arm_snapshot_off(HMODULE plugin, HWND hwnd) {
    run_scenario_n(plugin, hwnd);
    expect_count("# armed*", 1);
    expect_count("# snapshot*", 0);
    expect_count("*blob=*", 0);
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
    } else if (hwnd && plugin && argc > 1 && !strncmp(argv[1], "data", 4)) {
        load_trace();
        if (!strcmp(argv[1], "data-off"))
            cycle_data_off(plugin, hwnd);
        else if (!strcmp(argv[1], "data-surf-off"))
            cycle_surf_off(plugin, hwnd);
        else if (!strcmp(argv[1], "data-surf"))
            cycle_surf(plugin, hwnd);
        else if (!strcmp(argv[1], "data-cap"))
            cycle_data_cap(plugin, hwnd);
        else if (!strcmp(argv[1], "data-write-fail"))
            cycle_data_write_fail(plugin, hwnd);
        else if (!strcmp(argv[1], "data-alloc-fail"))
            cycle_data_alloc_fail(plugin, hwnd);
        else if (!strcmp(argv[1], "data-shadow-fail"))
            cycle_data_shadow_fail(plugin, hwnd);
        else if (!strcmp(argv[1], "data-surf-retry"))
            cycle_surf_retry(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm"))
            cycle_arm_snapshot(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-retry"))
            cycle_arm_snapshot_retry(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-cap"))
            cycle_arm_snapshot_cap(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-default"))
            cycle_arm_default(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-early"))
            cycle_arm_early(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-early-blt"))
            cycle_arm_early_blt(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-early-cap"))
            cycle_arm_early_cap(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-dead"))
            cycle_arm_snapshot_dead(plugin, hwnd);
        else if (!strcmp(argv[1], "data-arm-off"))
            cycle_arm_snapshot_off(plugin, hwnd);
        else if (!strcmp(argv[1], "data-partial"))
            cycle_surf_partial(plugin, hwnd);
        else
            cycle_data(plugin, hwnd);
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
