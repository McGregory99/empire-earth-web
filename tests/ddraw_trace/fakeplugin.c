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
    for (i = 0; i < 6; i++)
        r->hr_blt[i] = IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL);
    IDirectDrawSurface7_Release(back);
    if (clipper) IDirectDrawClipper_Release(clipper);
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_Release(dd);
    return 0;
}
