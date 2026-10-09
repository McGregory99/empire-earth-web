/* Stands in for DX7HRTnLDisplay.dll: makes DirectDraw/Direct3D 7 calls from a module the proxy attributes. */
#define CINTERFACE
#define COBJMACROS
#include <ddraw.h>
#include <d3d.h>
#include <string.h>
#include "scenario.h"

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
