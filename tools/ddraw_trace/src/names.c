/* Symbolic names: HRESULTs, GUIDs and the Direct3D 7 enumerations that the trace prints by name. */
#include "ddt.h"
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <string.h>

typedef struct { DWORD v; const char *n; } NameEntry;

static const NameEntry hr_table[] = {
    {(DWORD)S_OK, "DD_OK"},
    {(DWORD)S_FALSE, "S_FALSE"},
    {(DWORD)E_FAIL, "E_FAIL"},
    {(DWORD)E_NOINTERFACE, "E_NOINTERFACE"},
    {(DWORD)E_INVALIDARG, "E_INVALIDARG"},
    {(DWORD)E_OUTOFMEMORY, "E_OUTOFMEMORY"},
    {(DWORD)E_NOTIMPL, "E_NOTIMPL"},
    {(DWORD)E_POINTER, "E_POINTER"},
    {(DWORD)E_UNEXPECTED, "E_UNEXPECTED"},
    {(DWORD)CLASS_E_NOAGGREGATION, "CLASS_E_NOAGGREGATION"},
#define DDT_HR(n) {(DWORD)n, #n},
#define DDT_RS(n)
#define DDT_TSS(n)
#define DDT_XFORM(n)
#define DDT_PT(n)
#define DDT_LIGHT(n)
#include "names.inc"
#undef DDT_HR
#undef DDT_RS
#undef DDT_TSS
#undef DDT_XFORM
#undef DDT_PT
#undef DDT_LIGHT
};

#define DDT_HR(n)
#define DDT_RS(n) {(DWORD)D3DRENDERSTATE_##n, #n},
#define DDT_TSS(n)
#define DDT_XFORM(n)
#define DDT_PT(n)
#define DDT_LIGHT(n)
static const NameEntry rs_table[] = {
#include "names.inc"
};
#undef DDT_RS
#define DDT_RS(n)
#undef DDT_TSS
#define DDT_TSS(n) {(DWORD)D3DTSS_##n, #n},
static const NameEntry tss_table[] = {
#include "names.inc"
};
#undef DDT_TSS
#define DDT_TSS(n)
#undef DDT_XFORM
#define DDT_XFORM(n) {(DWORD)D3DTRANSFORMSTATE_##n, #n},
static const NameEntry xform_table[] = {
#include "names.inc"
};
#undef DDT_XFORM
#define DDT_XFORM(n)
#undef DDT_PT
#define DDT_PT(n) {(DWORD)D3DPT_##n, #n},
static const NameEntry pt_table[] = {
#include "names.inc"
};
#undef DDT_PT
#define DDT_PT(n)
#undef DDT_LIGHT
#define DDT_LIGHT(n) {(DWORD)D3DLIGHT_##n, #n},
static const NameEntry light_table[] = {
#include "names.inc"
};

static const char *lookup(const NameEntry *t, size_t n, DWORD v) {
    size_t i;
    for (i = 0; i < n; i++)
        if (t[i].v == v)
            return t[i].n;
    return NULL;
}

#define LOOKUP(fn, table) \
    const char *fn(DWORD v) { return lookup(table, sizeof table / sizeof *table, v); }
LOOKUP(rs_name, rs_table)
LOOKUP(tss_name, tss_table)
LOOKUP(xform_name, xform_table)
LOOKUP(pt_name, pt_table)
LOOKUP(light_name, light_table)

const char *hr_name(HRESULT hr, char tmp[16]) {
    const char *n = lookup(hr_table, sizeof hr_table / sizeof *hr_table, (DWORD)hr);
    if (n)
        return n;
    snprintf(tmp, 16, "0x%08x", (unsigned)hr);
    return tmp;
}

static const struct { const GUID *g; const char *n; } known_guids[] = {
    {&IID_IUnknown, "IID_IUnknown"},
    {&IID_IDirectDraw, "IID_IDirectDraw"},
    {&IID_IDirectDraw2, "IID_IDirectDraw2"},
    {&IID_IDirectDraw4, "IID_IDirectDraw4"},
    {&IID_IDirectDraw7, "IID_IDirectDraw7"},
    {&IID_IDirectDrawSurface, "IID_IDirectDrawSurface"},
    {&IID_IDirectDrawSurface2, "IID_IDirectDrawSurface2"},
    {&IID_IDirectDrawSurface3, "IID_IDirectDrawSurface3"},
    {&IID_IDirectDrawSurface4, "IID_IDirectDrawSurface4"},
    {&IID_IDirectDrawSurface7, "IID_IDirectDrawSurface7"},
    {&IID_IDirectDrawClipper, "IID_IDirectDrawClipper"},
    {&IID_IDirectDrawPalette, "IID_IDirectDrawPalette"},
    {&IID_IDirectDrawColorControl, "IID_IDirectDrawColorControl"},
    {&IID_IDirectDrawGammaControl, "IID_IDirectDrawGammaControl"},
    {&IID_IDirect3D, "IID_IDirect3D"},
    {&IID_IDirect3D2, "IID_IDirect3D2"},
    {&IID_IDirect3D3, "IID_IDirect3D3"},
    {&IID_IDirect3D7, "IID_IDirect3D7"},
    {&IID_IDirect3DDevice7, "IID_IDirect3DDevice7"},
    {&IID_IDirect3DVertexBuffer7, "IID_IDirect3DVertexBuffer7"},
    {&IID_IDirect3DRGBDevice, "IID_IDirect3DRGBDevice"},
    {&IID_IDirect3DHALDevice, "IID_IDirect3DHALDevice"},
    {&IID_IDirect3DMMXDevice, "IID_IDirect3DMMXDevice"},
    {&IID_IDirect3DRampDevice, "IID_IDirect3DRampDevice"},
    {&IID_IDirect3DRefDevice, "IID_IDirect3DRefDevice"},
    {&IID_IDirect3DTnLHalDevice, "IID_IDirect3DTnLHalDevice"},
};

const char *guid_name(const GUID *g, char tmp[48]) {
    size_t i;
    if (!g)
        return "NULL";
    for (i = 0; i < sizeof known_guids / sizeof *known_guids; i++)
        if (!memcmp(g, known_guids[i].g, sizeof(GUID)))
            return known_guids[i].n;
    snprintf(tmp, 48, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned)g->Data1, g->Data2, g->Data3,
             g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return tmp;
}
