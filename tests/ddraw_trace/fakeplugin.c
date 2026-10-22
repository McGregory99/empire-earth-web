/* Stands in for DX7HRTnLDisplay.dll: makes DirectDraw/Direct3D 7 calls from a module the proxy attributes. */
#define CINTERFACE
#define COBJMACROS
#include <ddraw.h>
#include <d3d.h>
#include <string.h>
#include "scenario.h"

static HRESULT WINAPI fmt_cb(LPDDPIXELFORMAT pf, LPVOID ctx) {
    (void)pf;
    ++*(int *)ctx;
    return D3DENUMRET_OK;
}

static BOOL WINAPI enum_ex_cb(GUID *guid, LPSTR desc, LPSTR name, LPVOID ctx, HMONITOR mon) {
    (void)guid; (void)desc; (void)name; (void)mon;
    ++*(int *)ctx;
    return TRUE;
}

static BOOL WINAPI enum_cb(GUID *guid, LPSTR desc, LPSTR name, LPVOID ctx) {
    (void)guid; (void)desc; (void)name;
    ++*(int *)ctx;
    return TRUE;
}

__declspec(dllexport) int WINAPI scenario_a(HWND hwnd, ScenarioA *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECT3D7 d3d = NULL, d3d2 = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, target = NULL;
    LPDIRECT3DDEVICE7 dev = NULL;
    DDSURFACEDESC2 sd;

    r->hr_enum_ex = DirectDrawEnumerateExA(enum_ex_cb, &r->enum_ex_callbacks,
                                           DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_DETACHEDSECONDARYDEVICES | DDENUM_NONDISPLAYDEVICES);
    r->hr_enum = DirectDrawEnumerateA(enum_cb, &r->enum_callbacks);
    r->hr_create = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_create))
        return 1;
    r->hr_coop = IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    r->hr_qi = IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    r->hr_qi_again = IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d2);
    r->d3d_first = d3d;
    r->d3d_again = d3d2;

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    r->hr_primary = IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 128;
    sd.dwHeight = 128;
    r->hr_target = IDirectDraw7_CreateSurface(dd, &sd, &target, NULL);

    r->hr_device = IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, target, &dev);
    if (dev) {
        r->hr_begin = IDirect3DDevice7_BeginScene(dev);
        r->hr_clear = IDirect3DDevice7_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1.0f, 0);
        r->hr_end = IDirect3DDevice7_EndScene(dev);
        r->device_release = IDirect3DDevice7_Release(dev);
    }
    if (target) IDirectDrawSurface7_Release(target);
    if (primary) IDirectDrawSurface7_Release(primary);
    if (d3d2) IDirect3D7_Release(d3d2);
    if (d3d) IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    return 0;
}

__declspec(dllexport) int WINAPI scenario_b(HWND hwnd, ScenarioB *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECT3D7 d3d = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, target = NULL, tex = NULL;
    LPDIRECT3DDEVICE7 dev = NULL;
    LPDIRECT3DVERTEXBUFFER7 vb = NULL;
    LPDIRECTDRAWCLIPPER clipper = NULL;
    LPDIRECTDRAW4 dd4 = NULL;
    DDSURFACEDESC2 sd;
    D3DDEVICEDESC7 caps;
    D3DVERTEXBUFFERDESC vbd, got;
    D3DMATRIX m;
    D3DVIEWPORT7 vp;
    D3DMATERIAL7 mat;
    D3DLIGHT7 light;
    DDCAPS ddcaps;
    DWORD size = 0;
    void *data = NULL;
    int i;

    r->hr_setup = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_setup))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 128;
    sd.dwHeight = 128;
    IDirectDraw7_CreateSurface(dd, &sd, &target, NULL);
    r->hr_setup = IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, target, &dev);
    if (!dev)
        return 1;

    r->hr_caps = IDirect3DDevice7_GetCaps(dev, &caps);
    r->hr_enum_formats = IDirect3DDevice7_EnumTextureFormats(dev, fmt_cb, &r->format_callbacks);

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    sd.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
    sd.dwWidth = TEX_SIZE;
    sd.dwHeight = TEX_SIZE;
    sd.ddpfPixelFormat.dwSize = sizeof sd.ddpfPixelFormat;
    sd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    sd.ddpfPixelFormat.dwRGBBitCount = 32;
    sd.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    sd.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    sd.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    sd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
    r->hr_texture = IDirectDraw7_CreateSurface(dd, &sd, &tex, NULL);

    /* surface Lock/Unlock: the proxy hashes what the plugin wrote */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    r->hr_tex_lock = IDirectDrawSurface7_Lock(tex, NULL, &sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (SUCCEEDED(r->hr_tex_lock)) {
        r->tex_pitch = (DWORD)sd.lPitch;
        fill_texture(sd.lpSurface, (unsigned)sd.lPitch);
    }
    r->hr_tex_unlock = IDirectDrawSurface7_Unlock(tex, NULL);

    r->hr_set_texture = IDirect3DDevice7_SetTexture(dev, 0, tex);
    r->hr_render_state = IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_ZENABLE, TRUE);
    IDirect3DDevice7_GetRenderState(dev, D3DRENDERSTATE_ZENABLE, &r->got_render_state);
    r->hr_stage_state = IDirect3DDevice7_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    memset(&m, 0, sizeof m);
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    m._41 = 0.5f;
    r->hr_transform = IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_WORLD, &m);
    memset(&vp, 0, sizeof vp);
    vp.dwWidth = 128;
    vp.dwHeight = 128;
    vp.dvMaxZ = 1.0f;
    r->hr_viewport = IDirect3DDevice7_SetViewport(dev, &vp);
    memset(&mat, 0, sizeof mat);
    mat.diffuse.r = mat.diffuse.g = mat.diffuse.b = mat.diffuse.a = 1.0f;
    r->hr_material = IDirect3DDevice7_SetMaterial(dev, &mat);
    memset(&light, 0, sizeof light);
    light.dltType = D3DLIGHT_DIRECTIONAL;
    light.dcvDiffuse.r = light.dcvDiffuse.g = light.dcvDiffuse.b = 1.0f;
    light.dvDirection.z = 1.0f;
    r->hr_light = IDirect3DDevice7_SetLight(dev, 0, &light);
    r->hr_light_enable = IDirect3DDevice7_LightEnable(dev, 0, TRUE);

    /* vertex buffer */
    memset(&vbd, 0, sizeof vbd);
    vbd.dwSize = sizeof vbd;
    vbd.dwCaps = D3DVBCAPS_WRITEONLY;
    vbd.dwFVF = VB_FVF;
    vbd.dwNumVertices = VB_VERTICES;
    r->hr_vb_create = IDirect3D7_CreateVertexBuffer(d3d, &vbd, &vb, 0);
    if (vb) {
        r->hr_vb_lock = IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_WRITEONLY, &data, &size);
        if (SUCCEEDED(r->hr_vb_lock))
            fill_vertices(data);
        r->hr_vb_unlock = IDirect3DVertexBuffer7_Unlock(vb);
        IDirect3DDevice7_BeginScene(dev);
        r->hr_draw_vb = IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 0, 3, 0);
        {
            WORD idx[6] = {0, 1, 2, 2, 1, 3};
            unsigned char verts[VB_VERTICES * VB_VERTEX_SIZE];
            fill_vertices(verts);
            r->hr_draw_indexed = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 0, 4, idx, 6, 0);
            r->hr_draw_prim = IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, VB_FVF, verts, 3, 0);
        }
        IDirect3DDevice7_EndScene(dev);
        memset(&got, 0, sizeof got);
        got.dwSize = sizeof got;
        r->hr_vb_desc = IDirect3DVertexBuffer7_GetVertexBufferDesc(vb, &got);
        r->vb_fvf = got.dwFVF;
        r->vb_vertices = got.dwNumVertices;
    }

    /* an interface the proxy does not wrap is handed out as is and keeps working */
    r->hr_clipper = IDirectDraw7_CreateClipper(dd, 0, &clipper, NULL);
    if (clipper) {
        r->hr_clipper_raw = IDirectDrawClipper_SetHWnd(clipper, 0, hwnd);
        r->hr_clipper_use = IDirectDrawSurface7_SetClipper(primary, clipper);
        IDirectDrawSurface7_SetClipper(primary, NULL);
        IDirectDrawClipper_Release(clipper);
    }
    r->hr_qi_dd4 = IDirectDraw7_QueryInterface(dd, &IID_IDirectDraw4, (void **)&dd4);
    if (dd4) {
        memset(&ddcaps, 0, sizeof ddcaps);
        ddcaps.dwSize = sizeof ddcaps;
        r->hr_dd4_caps = IDirectDraw4_GetCaps(dd4, &ddcaps, NULL);
        IDirectDraw4_Release(dd4);
    }

    /* timing-dependent polling collapses to one line */
    for (i = 0; i < 3; i++)
        r->hr_flip_status = IDirectDrawSurface7_GetFlipStatus(primary, DDGFS_CANFLIP);
    for (i = 0; i < 3; i++)
        r->hr_scanline = IDirectDraw7_GetScanLine(dd, &size);

    if (vb) IDirect3DVertexBuffer7_Release(vb);
    if (tex) IDirectDrawSurface7_Release(tex);
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(target);
    IDirectDrawSurface7_Release(primary);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    return 0;
}

__declspec(dllexport) int WINAPI scenario_c(HWND hwnd, ScenarioC *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL;
    LPDIRECTDRAWCLIPPER clipper = NULL;
    DDSURFACEDESC2 sd;
    int i;

    r->hr_setup = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_setup))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    /* clip to the (hidden) test window so that nothing is drawn on the desktop */
    if (SUCCEEDED(IDirectDraw7_CreateClipper(dd, 0, &clipper, NULL))) {
        IDirectDrawClipper_SetHWnd(clipper, 0, hwnd);
        IDirectDrawSurface7_SetClipper(primary, clipper);
    }
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 64;
    sd.dwHeight = 64;
    IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);
    /* not presents: a sprite blit (the software cursor) and a colour fill of the primary */
    {
        RECT sprite = {10, 10, 42, 42};
        DDBLTFX fx;
        r->hr_partial = IDirectDrawSurface7_Blt(primary, &sprite, back, &sprite, DDBLT_WAIT, NULL);
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillColor = 0x00ff00ff;
        r->hr_fill = IDirectDrawSurface7_Blt(primary, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    }
    /* presents: whole-surface blits from another surface */
    for (i = 0; i < 6; i++)
        r->hr_blt[i] = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL);
    IDirectDrawSurface7_Release(back);
    if (clipper) IDirectDrawClipper_Release(clipper);
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_Release(dd);
    return 0;
}

/* Scenario M: runs against the scripted mock ddraw (mockddraw.c). */
__declspec(dllexport) int WINAPI scenario_m(HWND hwnd, ScenarioM *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL, attached = NULL;
    DDSURFACEDESC2 sd;
    DDSCAPS2 caps;
    int i;

    r->hr_create = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_create))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    r->hr_primary = IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    r->hr_back = IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);
    memset(&caps, 0, sizeof caps);
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    r->hr_attached = IDirectDrawSurface7_GetAttachedSurface(primary, &caps, &attached);
    r->attached = attached;
    r->back = back;

    /* a flip is retried while the driver is busy: the retries are timing, only the success is a frame */
    for (i = 0; i < 3; i++)
        r->hr_flip_first[i] = IDirectDrawSurface7_Flip(primary, NULL, 0);
    r->hr_flip_override = IDirectDrawSurface7_Flip(primary, back, 0);
    r->hr_blt_busy = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_DONOTWAIT, NULL);
    r->hr_blt_ok = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_DONOTWAIT, NULL);
    r->hr_bltfast = IDirectDrawSurface7_BltFast(primary, 1, 2, back, NULL, DDBLTFAST_WAIT);
    r->hr_flip_after_cap = IDirectDrawSurface7_Flip(primary, NULL, 0);
    return 0;
}

/* Scenario V: vertex-buffer ring and index data against the scripted mock ddraw (DDT_DATA tests). */
__declspec(dllexport) int WINAPI scenario_v(HWND hwnd, ScenarioV *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECT3D7 d3d = NULL;
    LPDIRECT3DDEVICE7 dev = NULL;
    LPDIRECT3DVERTEXBUFFER7 vb = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL;
    D3DVERTEXBUFFERDESC vbd;
    DDSURFACEDESC2 sd;
    void *mem = NULL;
    DWORD size = 0;
    unsigned char app[3 * V_STRIDE];
    WORD idx6[6] = {0, 1, 2, 2, 1, 3}, idx3[3] = {2, 1, 0};
    int i;

    r->hr_setup = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_setup))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);
    IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, back, &dev);
    memset(&vbd, 0, sizeof vbd);
    vbd.dwSize = sizeof vbd;
    vbd.dwCaps = D3DVBCAPS_WRITEONLY;
    vbd.dwFVF = 0x152;
    vbd.dwNumVertices = V_TOTAL;
    r->hr_vb_create = IDirect3D7_CreateVertexBuffer(d3d, &vbd, &vb, 0);
    if (FAILED(r->hr_vb_create))
        return 2;

    /* a ring: DISCARD starts a new generation, NOOVERWRITE appends behind the vertices already drawn */
    r->hr_lock[0] = IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_DISCARDCONTENTS, &mem, &size);
    fill_pattern((unsigned char *)mem + 0 * V_STRIDE, 0, 4, 1);
    r->hr_unlock[0] = IDirect3DVertexBuffer7_Unlock(vb);
    r->hr_draw[0] = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 0, 4, idx6, 6, 0);
    r->hr_lock[1] = IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_NOOVERWRITE, &mem, &size);
    fill_pattern((unsigned char *)mem + 4 * V_STRIDE, 4, 4, 2);
    r->hr_unlock[1] = IDirect3DVertexBuffer7_Unlock(vb);
    r->hr_draw[1] = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 4, 4, idx6, 6, 0);
    r->hr_draw[2] = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 0, 4, idx6, 6, 0);
    r->hr_draw[3] = IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLESTRIP, vb, 4, 4, 0);
    r->hr_lock[2] = IDirect3DVertexBuffer7_Lock(vb, DDLOCK_WAIT | DDLOCK_DISCARDCONTENTS, &mem, &size);
    fill_pattern((unsigned char *)mem + 0 * V_STRIDE, 0, 4, 3);
    r->hr_unlock[2] = IDirect3DVertexBuffer7_Unlock(vb);
    r->hr_draw[4] = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 0, 4, idx6, 6, 0);
    r->hr_draw[5] = IDirect3DDevice7_DrawIndexedPrimitiveVB(dev, D3DPT_TRIANGLELIST, vb, 4, 4, idx6, 6, 0);

    /* draws that read application memory */
    fill_pattern(app, 0, 3, 4);
    r->hr_draw[6] = IDirect3DDevice7_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0x152, app, 3, idx3, 3, 0);
    r->hr_draw[7] = IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, 0x152, app, 3, 0);

    for (i = 0; i < 3; i++)
        r->hr_blt[i] = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL);
    return 0;
}

/* Scenario S: surface data against the scripted mock ddraw. */
static LPDIRECTDRAWSURFACE7 make_surface(LPDIRECTDRAW7 dd, DWORD w, DWORD h, int rgb565) {
    DDSURFACEDESC2 sd;
    LPDIRECTDRAWSURFACE7 s = NULL;
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    sd.dwWidth = w;
    sd.dwHeight = h;
    if (rgb565) {
        sd.dwFlags |= DDSD_PIXELFORMAT;
        sd.ddpfPixelFormat.dwSize = sizeof sd.ddpfPixelFormat;
        sd.ddpfPixelFormat.dwFlags = DDPF_RGB;
        sd.ddpfPixelFormat.dwRGBBitCount = 16;
        sd.ddpfPixelFormat.dwRBitMask = 0xf800;
        sd.ddpfPixelFormat.dwGBitMask = 0x07e0;
        sd.ddpfPixelFormat.dwBBitMask = 0x001f;
    }
    IDirectDraw7_CreateSurface(dd, &sd, &s, NULL);
    return s;
}

__declspec(dllexport) int WINAPI scenario_s(HWND hwnd, ScenarioS *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECT3D7 d3d = NULL;
    LPDIRECT3DDEVICE7 dev = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL, a, b, c;
    DDSURFACEDESC2 sd;
    DDBLTFX fx;
    RECT rc = {S_RECT_L, S_RECT_T, S_RECT_R, S_RECT_B};
    unsigned x, y;
    int i;
#define R(call) (r->hr[r->n++] = (call))

    if (FAILED(DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL)))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL); /* surf#1 */
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);    /* surf#2 */
    a = make_surface(dd, 8, 8, 0);                       /* surf#3: 8x8 ARGB, texture */
    b = make_surface(dd, 4, 4, 1);                       /* surf#4: 4x4 RGB565 */
    c = make_surface(dd, 8, 8, 0);                       /* surf#5: GDI text and blit target */
    IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, back, &dev);

    for (i = 0; i < 2; i++) { /* the second pass writes the same pixels: nothing new is stored */
        memset(&sd, 0, sizeof sd);
        sd.dwSize = sizeof sd;
        R(IDirectDrawSurface7_Lock(a, NULL, &sd, DDLOCK_WAIT, NULL));
        fill_texture((unsigned char *)sd.lpSurface, (unsigned)sd.lPitch);
        R(IDirectDrawSurface7_Unlock(a, NULL));
    }
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(a, &rc, &sd, DDLOCK_WAIT, NULL));
    for (y = S_RECT_T; y < S_RECT_B; y++)
        for (x = S_RECT_L; x < S_RECT_R; x++)
            ((unsigned *)((unsigned char *)sd.lpSurface + (y - S_RECT_T) * sd.lPitch))[x - S_RECT_L] = rect_pixel(x, y);
    R(IDirectDrawSurface7_Unlock(a, &rc));

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(b, NULL, &sd, DDLOCK_WAIT, NULL));
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            ((unsigned short *)((unsigned char *)sd.lpSurface + y * sd.lPitch))[x] = s565_pixel(x, y);
    R(IDirectDrawSurface7_Unlock(b, NULL));
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(b, NULL, &sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL)); /* reading changes nothing */
    R(IDirectDrawSurface7_Unlock(b, NULL));

    {   HDC dc = NULL;
        R(IDirectDrawSurface7_GetDC(c, &dc));
        R(IDirectDrawSurface7_ReleaseDC(c, dc)); /* the mock draws its "GDI text" here */
    }
    R(IDirectDrawSurface7_Blt(c, NULL, a, NULL, DDBLT_WAIT, NULL)); /* c now holds a copy of a */
    R(IDirect3DDevice7_SetTexture(dev, 0, c));                      /* c changed by a blit: its pixels are stored now */
    R(IDirect3DDevice7_SetTexture(dev, 0, c));                      /* unchanged since: nothing */
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwFillColor = S_FILL_COLOR;
    R(IDirectDrawSurface7_Blt(a, NULL, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx));
    R(IDirectDrawSurface7_BltFast(c, 0, 0, a, NULL, DDBLTFAST_WAIT)); /* a is a source after a colour fill: its pixels are stored */
    for (i = 0; i < 3; i++)
        R(IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL));
#undef R
    return 0;
}

/* Scenario N: textures created and filled BEFORE the capture is armed (DDT_ARM_FILE).  The arm file is made after the second
 * present; the present that sees it opens the window; inside it a Blt reads texture a as its source.  The back buffer has the
 * 3D device cap, so only a (surf#3) and b (surf#4) are textures to snapshot. */
__declspec(dllexport) int WINAPI scenario_n(HWND hwnd, ScenarioS *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL, a, b;
    DDSURFACEDESC2 sd;
    char arm[MAX_PATH];
    unsigned x, y;
    int i;
#define R(call) (r->hr[r->n++] = (call))

    if (FAILED(DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL)))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL); /* surf#1 */
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);    /* surf#2 */
    a = make_surface(dd, 8, 8, 0);                       /* surf#3: 8x8 ARGB */
    b = make_surface(dd, 4, 4, 1);                       /* surf#4: 4x4 RGB565 */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(a, NULL, &sd, DDLOCK_WAIT, NULL));
    fill_texture((unsigned char *)sd.lpSurface, (unsigned)sd.lPitch);
    R(IDirectDrawSurface7_Unlock(a, NULL));
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(b, NULL, &sd, DDLOCK_WAIT, NULL));
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            ((unsigned short *)((unsigned char *)sd.lpSurface + y * sd.lPitch))[x] = s565_pixel(x, y);
    R(IDirectDrawSurface7_Unlock(b, NULL));
    if (GetEnvironmentVariableA("N_BLT", arm, sizeof arm)) { /* a surface written by a Blt and then read as a Blt source, before the window */
        R(IDirectDrawSurface7_Blt(b, NULL, a, NULL, DDBLT_WAIT, NULL));
        R(IDirectDrawSurface7_Blt(back, NULL, b, NULL, DDBLT_WAIT, NULL));
    }
    if (GetEnvironmentVariableA("N_DEAD", arm, sizeof arm)) { /* surf#5: freed by DDraw behind the proxy's back (see the mock) */
        LPDIRECTDRAWSURFACE7 dead = make_surface(dd, 13, 13, 0);
        IDirectDrawSurface7_Release(dead);
    }
    for (i = 0; i < 5; i++) {
        if (i == 2 && GetEnvironmentVariableA("DDT_ARM_FILE", arm, sizeof arm)) {
            HANDLE h = CreateFileA(arm, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        if (i == 3)
            R(IDirectDrawSurface7_Blt(back, NULL, a, NULL, DDBLT_WAIT, NULL)); /* inside the window: a is a Blt source */
        R(IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL));
    }
#undef R
    return 0;
}

/* Scenario P: a surface changed by a Blt, then partly rewritten through a rectangle Lock; it is read as a Blt source afterwards.
 * The partial Unlock stores only the rectangle, so the Blt-written pixels outside it still have to be stored when it is used. */
__declspec(dllexport) int WINAPI scenario_p(HWND hwnd, ScenarioS *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, a, c;
    DDSURFACEDESC2 sd;
    DDBLTFX fx;
    RECT rc = {S_RECT_L, S_RECT_T, S_RECT_R, S_RECT_B};
    unsigned x, y;
#define R(call) (r->hr[r->n++] = (call))

    if (FAILED(DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL)))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL); /* surf#1 */
    a = make_surface(dd, 8, 8, 0);                       /* surf#2 */
    c = make_surface(dd, 8, 8, 0);                       /* surf#3 */
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwFillColor = S_FILL_COLOR;
    R(IDirectDrawSurface7_Blt(a, NULL, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx));
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(a, &rc, &sd, DDLOCK_WAIT, NULL));
    for (y = S_RECT_T; y < S_RECT_B; y++)
        for (x = S_RECT_L; x < S_RECT_R; x++)
            ((unsigned *)((unsigned char *)sd.lpSurface + (y - S_RECT_T) * sd.lPitch))[x - S_RECT_L] = rect_pixel(x, y);
    R(IDirectDrawSurface7_Unlock(a, &rc));
    R(IDirectDrawSurface7_BltFast(c, 0, 0, a, NULL, DDBLTFAST_WAIT)); /* a is a source: the whole surface is stored now */
#undef R
    return 0;
}

/* Scenario O: the same pixels reach the data file under different origins.  a is filled and unlocked (origin 1); a Blt copies it
 * into c, a Blt copies c back into a (a is written again, with the pixels it already had), and a is read as a Blt source:
 * the pixels of a are the ones the Unlock stored. */
__declspec(dllexport) int WINAPI scenario_o(HWND hwnd, ScenarioS *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, a, c;
    DDSURFACEDESC2 sd;
#define R(call) (r->hr[r->n++] = (call))

    if (FAILED(DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL)))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL); /* surf#1 */
    a = make_surface(dd, 8, 8, 0);                       /* surf#2 */
    c = make_surface(dd, 8, 8, 0);                       /* surf#3 */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    R(IDirectDrawSurface7_Lock(a, NULL, &sd, DDLOCK_WAIT, NULL));
    fill_texture((unsigned char *)sd.lpSurface, (unsigned)sd.lPitch);
    R(IDirectDrawSurface7_Unlock(a, NULL));                           /* origin 1: stored */
    R(IDirectDrawSurface7_Blt(c, NULL, a, NULL, DDBLT_WAIT, NULL));   /* a is clean: nothing; c is now written */
    R(IDirectDrawSurface7_Blt(a, NULL, c, NULL, DDBLT_WAIT, NULL));   /* c is read as a source (origin 3); a is written */
    R(IDirectDrawSurface7_Blt(c, NULL, a, NULL, DDBLT_WAIT, NULL));   /* a is read as a source (origin 3): same pixels as the Unlock */
#undef R
    return 0;
}

__declspec(dllexport) int WINAPI scenario_r(HWND hwnd, ScenarioR *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, back = NULL;
    LPDIRECTDRAWCLIPPER clipper = NULL;
    DDSURFACEDESC2 sd;
    char arm[MAX_PATH];
    int i;

    r->hr_setup = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_setup))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);
    if (SUCCEEDED(IDirectDraw7_CreateClipper(dd, 0, &clipper, NULL))) {
        IDirectDrawClipper_SetHWnd(clipper, 0, hwnd);
        IDirectDrawSurface7_SetClipper(primary, clipper);
    }
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 64;
    sd.dwHeight = 64;
    IDirectDraw7_CreateSurface(dd, &sd, &back, NULL);
    for (i = 0; i < 8; i++) {
        if (i == 2 && GetEnvironmentVariableA("DDT_ARM_FILE", arm, sizeof arm)) {
            HANDLE h = CreateFileA(arm, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                r->armed_file_made = 1;
            }
        }
        r->hr_blt[i] = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL);
    }
    IDirectDrawSurface7_Release(back);
    if (clipper) IDirectDrawClipper_Release(clipper);
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_Release(dd);
    return 0;
}

static HRESULT WINAPI enum_att_cb(LPDIRECTDRAWSURFACE7 s, LPDDSURFACEDESC2 d, LPVOID ctx) {
    ScenarioD *r = ctx;
    (void)d;
    r->enum_seen = s; /* must be a wrapper: calling a method through it must still work */
    r->enum_callbacks++;
    return DDENUMRET_OK;
}

__declspec(dllexport) int WINAPI scenario_d(HWND hwnd, ScenarioD *r) {
    LPDIRECTDRAW7 dd = NULL;
    LPDIRECT3D7 d3d = NULL;
    LPDIRECTDRAWSURFACE7 primary = NULL, target = NULL, target2 = NULL, mip = NULL, lvl1 = NULL, lvl1b = NULL, tex = NULL,
                         src = NULL, dst = NULL, got = NULL, bad = NULL;
    LPDIRECT3DDEVICE7 dev = NULL;
    DDSURFACEDESC2 sd;
    DDSCAPS2 caps;
    RECT rect = {LOCK_RECT_L, LOCK_RECT_T, LOCK_RECT_R, LOCK_RECT_B};
    DWORD total = 0, avail = 0;
    unsigned x, y;

    r->hr_setup = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_setup))
        return 1;
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(dd, &sd, &primary, NULL);                      /* surf#6 */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 128;
    sd.dwHeight = 128;
    IDirectDraw7_CreateSurface(dd, &sd, &target, NULL);                       /* surf#7 */
    IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, target, &dev);      /* dev7#3 */
    sd.dwWidth = 64;
    sd.dwHeight = 64;
    IDirectDraw7_CreateSurface(dd, &sd, &target2, NULL);                      /* surf#8 */
    r->target2 = target2;

    /* render target switch and read-back: the result must be the same wrapper */
    r->hr_srt = IDirect3DDevice7_SetRenderTarget(dev, target2, 0);
    r->hr_get_rt = IDirect3DDevice7_GetRenderTarget(dev, &got);
    r->got_rt = got;
    got = NULL;

    /* a mipmapped texture: its levels are surfaces attached to it */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT;
    sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
    sd.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
    sd.dwWidth = 16;
    sd.dwHeight = 16;
    sd.dwMipMapCount = 3;
    sd.ddpfPixelFormat.dwSize = sizeof sd.ddpfPixelFormat;
    sd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    sd.ddpfPixelFormat.dwRGBBitCount = 32;
    sd.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    sd.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    sd.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    sd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
    IDirectDraw7_CreateSurface(dd, &sd, &mip, NULL);                          /* surf#9 */
    memset(&caps, 0, sizeof caps);
    caps.dwCaps = DDSCAPS_MIPMAP;
    r->hr_att1 = IDirectDrawSurface7_GetAttachedSurface(mip, &caps, &lvl1);   /* surf#10 */
    r->hr_att2 = IDirectDrawSurface7_GetAttachedSurface(mip, &caps, &lvl1b);  /* the same one again */
    r->att1 = lvl1;
    r->att2 = lvl1b;
    r->hr_enum_att = IDirectDrawSurface7_EnumAttachedSurfaces(mip, r, enum_att_cb);
    if (r->enum_seen)
        IDirectDrawSurface7_GetCaps((LPDIRECTDRAWSURFACE7)r->enum_seen, &caps); /* a call through what the callback was given */
    memset(&caps, 0, sizeof caps);
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    r->hr_not_found = IDirectDrawSurface7_GetAttachedSurface(target, &caps, &got); /* fails, output untouched */
    r->bad_surface = got;

    /* a texture on the device and back */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    sd.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
    sd.dwWidth = TEX_SIZE;
    sd.dwHeight = TEX_SIZE;
    sd.ddpfPixelFormat.dwSize = sizeof sd.ddpfPixelFormat;
    sd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    sd.ddpfPixelFormat.dwRGBBitCount = 32;
    sd.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    sd.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    sd.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    sd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
    IDirectDraw7_CreateSurface(dd, &sd, &tex, NULL);                          /* surf#11 */
    r->tex = tex;
    IDirect3DDevice7_SetTexture(dev, 0, tex);
    got = NULL;
    r->hr_get_tex = IDirect3DDevice7_GetTexture(dev, 0, &got);
    r->got_tex = got;

    /* BltFast between two plain surfaces */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    sd.dwWidth = 16;
    sd.dwHeight = 16;
    IDirectDraw7_CreateSurface(dd, &sd, &src, NULL);                          /* surf#12 */
    IDirectDraw7_CreateSurface(dd, &sd, &dst, NULL);                          /* surf#13 */
    {
        RECT part = {0, 0, 8, 8};
        r->hr_bltfast = IDirectDrawSurface7_BltFast(dst, 1, 2, src, &part, DDBLTFAST_WAIT);
    }

    /* an inner failure: the real call fails and leaves the output alone */
    memset(&sd, 0, sizeof sd);
    sd.dwFlags = DDSD_CAPS; /* dwSize left 0 */
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    r->hr_bad_create = IDirectDraw7_CreateSurface(dd, &sd, &bad, NULL);

    /* a partial-rectangle lock; a second lock of the same surface fails and must not disturb the first */
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    r->hr_lock1 = IDirectDrawSurface7_Lock(tex, &rect, &sd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (SUCCEEDED(r->hr_lock1)) {
        DDSURFACEDESC2 sd2;
        for (y = LOCK_RECT_T; y < LOCK_RECT_B; y++)
            for (x = LOCK_RECT_L; x < LOCK_RECT_R; x++)
                ((unsigned *)((char *)sd.lpSurface + (y - LOCK_RECT_T) * (unsigned)sd.lPitch))[x - LOCK_RECT_L] = rect_pixel(x, y);
        memset(&sd2, 0, sizeof sd2);
        sd2.dwSize = sizeof sd2;
        r->hr_lock2_busy = IDirectDrawSurface7_Lock(tex, NULL, &sd2, DDLOCK_WAIT, NULL);
    }
    r->hr_unlock = IDirectDrawSurface7_Unlock(tex, &rect);

    /* an input structure of a Get* method: the capabilities asked for */
    memset(&caps, 0, sizeof caps);
    caps.dwCaps = DDSCAPS_TEXTURE;
    r->hr_vidmem = IDirectDraw7_GetAvailableVidMem(dd, &caps, &total, &avail);

    if (bad) IDirectDrawSurface7_Release(bad);
    IDirectDrawSurface7_Release(dst);
    IDirectDrawSurface7_Release(src);
    if (got) IDirectDrawSurface7_Release(got);
    IDirectDrawSurface7_Release(tex);
    if (lvl1b) IDirectDrawSurface7_Release(lvl1b);
    if (lvl1) IDirectDrawSurface7_Release(lvl1);
    IDirectDrawSurface7_Release(mip);
    IDirectDrawSurface7_Release(target2);
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(target);
    IDirectDrawSurface7_Release(primary);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    return 0;
}

typedef struct TCtx {
    LPDIRECTDRAW7 dd;
    HANDLE to_worker, to_main;
    ScenarioT *r;
} TCtx;

static DWORD WINAPI t_worker(LPVOID p) {
    TCtx *c = p;
    DWORD total, avail;
    int i;
    DDSCAPS2 caps;
    memset(&caps, 0, sizeof caps);
    caps.dwCaps = DDSCAPS_TEXTURE;
    for (i = 0; i < 3; i++) {
        WaitForSingleObject(c->to_worker, 10000);
        IDirectDraw7_GetAvailableVidMem(c->dd, &caps, &total, &avail); /* not a polling call */
        c->r->thread_calls++;
        SetEvent(c->to_main);
    }
    return 0;
}

__declspec(dllexport) int WINAPI scenario_t(HWND hwnd, ScenarioT *r) {
    TCtx c;
    LPDIRECTDRAWSURFACE7 primary = NULL;
    DDSURFACEDESC2 sd;
    HANDLE th;
    int i;

    memset(&c, 0, sizeof c);
    c.r = r;
    r->hr_create = DirectDrawCreateEx(NULL, (void **)&c.dd, &IID_IDirectDraw7, NULL);
    if (FAILED(r->hr_create))
        return 1;
    IDirectDraw7_SetCooperativeLevel(c.dd, hwnd, DDSCL_NORMAL | DDSCL_MULTITHREADED);
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDraw7_CreateSurface(c.dd, &sd, &primary, NULL); /* surf#14 */
    c.to_worker = CreateEventA(NULL, FALSE, FALSE, NULL);
    c.to_main = CreateEventA(NULL, FALSE, FALSE, NULL);
    th = CreateThread(NULL, 0, t_worker, &c, 0, NULL);
    for (i = 0; i < 3; i++) {
        /* a poll, then the other thread makes a different call, then the next poll */
        r->hr_flip_status[i] = IDirectDrawSurface7_GetFlipStatus(primary, DDGFS_CANFLIP);
        SetEvent(c.to_worker);
        WaitForSingleObject(c.to_main, 10000);
    }
    WaitForSingleObject(th, 10000);
    CloseHandle(th);
    CloseHandle(c.to_worker);
    CloseHandle(c.to_main);
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_Release(c.dd);
    return 0;
}
