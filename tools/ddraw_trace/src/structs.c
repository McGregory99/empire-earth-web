/* Field-by-field text of the DirectDraw and Direct3D 7 structures.  Floats are written as their IEEE bits,
 * pointers as P or NULL (addresses are not reproducible), flag-gated fields only when their flag is set. */
#include "ddt.h"
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(DDSURFACEDESC2) == 124, "DDSURFACEDESC2");
_Static_assert(sizeof(DDPIXELFORMAT) == 32, "DDPIXELFORMAT");
_Static_assert(sizeof(DDCAPS_DX7) == 380, "DDCAPS_DX7");
_Static_assert(sizeof(D3DDEVICEDESC7) == 236, "D3DDEVICEDESC7");
_Static_assert(sizeof(D3DMATERIAL7) == 68, "D3DMATERIAL7");
_Static_assert(sizeof(D3DLIGHT7) == 104, "D3DLIGHT7");
_Static_assert(sizeof(DDBLTFX) == 100, "DDBLTFX");

enum { FK_HEX, FK_DEC, FK_F32, FK_PTR };

typedef struct FieldDef {
    const char *name;
    unsigned short off;
    unsigned char kind;
} FieldDef;

#define FD(type, member, kind) {#member, (unsigned short)offsetof(type, member), kind}

static void put_value(Buf *b, const void *base, const FieldDef *f) {
    DWORD v;
    memcpy(&v, (const char *)base + f->off, sizeof v);
    switch (f->kind) {
    case FK_DEC: buf_printf(b, "%d", (int)v); break;
    case FK_F32: buf_printf(b, "f32:%08x", (unsigned)v); break;
    case FK_PTR: buf_puts(b, v ? "P" : "NULL"); break;
    default: buf_printf(b, "0x%x", (unsigned)v); break;
    }
}

static void put_fields(Buf *b, const void *base, const FieldDef *f, unsigned n, int *first) {
    unsigned i;
    for (i = 0; i < n; i++) {
        buf_printf(b, "%s%s=", *first ? "" : ", ", f[i].name);
        *first = 0;
        put_value(b, base, &f[i]);
    }
}

static void fmt_fields(Buf *b, const void *base, const FieldDef *f, unsigned n) {
    int first = 1;
    buf_puts(b, "{");
    put_fields(b, base, f, n, &first);
    buf_puts(b, "}");
}

void fmt_fourcc(Buf *b, DWORD v) {
    char c[5];
    int i, printable = 1;
    memcpy(c, &v, 4);
    c[4] = 0;
    for (i = 0; i < 4; i++)
        if (c[i] < 0x20 || c[i] > 0x7e)
            printable = 0;
    if (printable)
        buf_printf(b, "'%s'", c);
    else
        buf_printf(b, "0x%x", (unsigned)v);
}

static void fmt_colorkey(Buf *b, const DDCOLORKEY *k) {
    buf_printf(b, "{0x%x, 0x%x}", (unsigned)k->dwColorSpaceLowValue, (unsigned)k->dwColorSpaceHighValue);
}

static void fmt_ddpf(Buf *b, const DDPIXELFORMAT *pf) {
    const DWORD *w = (const DWORD *)pf; /* layout: size, flags, bitcount, R, G, B, A masks; fourcc shares slot 2 */
    buf_printf(b, "{dwSize=%u, dwFlags=0x%x", (unsigned)w[0], (unsigned)w[1]);
    if (w[1] & DDPF_FOURCC) {
        buf_puts(b, ", dwFourCC=");
        fmt_fourcc(b, w[2]);
    } else {
        buf_printf(b, ", dwRGBBitCount=%u, dwRBitMask=0x%x, dwGBitMask=0x%x, dwBBitMask=0x%x, dwRGBAlphaBitMask=0x%x",
                   (unsigned)w[2], (unsigned)w[3], (unsigned)w[4], (unsigned)w[5], (unsigned)w[6]);
    }
    buf_puts(b, "}");
}

static void fmt_ddscaps2(Buf *b, const DDSCAPS2 *c) {
    const DWORD *w = (const DWORD *)c;
    buf_printf(b, "{dwCaps=0x%x, dwCaps2=0x%x, dwCaps3=0x%x, dwCaps4=0x%x}", (unsigned)w[0], (unsigned)w[1],
               (unsigned)w[2], (unsigned)w[3]);
}

/* DDSURFACEDESC2, with its flag-gated members (the union members are read at their fixed offsets). */
static void fmt_ddsd2(Buf *b, const DDSURFACEDESC2 *d) {
    const DWORD *w = (const DWORD *)d;
    DWORD fl = w[1];
    buf_printf(b, "{dwSize=%u, dwFlags=0x%x", (unsigned)w[0], (unsigned)fl);
    if (fl & DDSD_HEIGHT) buf_printf(b, ", dwHeight=%u", (unsigned)w[2]);
    if (fl & DDSD_WIDTH) buf_printf(b, ", dwWidth=%u", (unsigned)w[3]);
    if (fl & DDSD_PITCH) buf_printf(b, ", lPitch=%d", (int)w[4]);
    if (fl & DDSD_LINEARSIZE) buf_printf(b, ", dwLinearSize=%u", (unsigned)w[4]);
    if (fl & DDSD_BACKBUFFERCOUNT) buf_printf(b, ", dwBackBufferCount=%u", (unsigned)w[5]);
    if (fl & DDSD_DEPTH) buf_printf(b, ", dwDepth=%u", (unsigned)w[5]);
    if (fl & DDSD_MIPMAPCOUNT) buf_printf(b, ", dwMipMapCount=%u", (unsigned)w[6]);
    if (fl & DDSD_REFRESHRATE) buf_printf(b, ", dwRefreshRate=%u", (unsigned)w[6]);
    if (fl & DDSD_ALPHABITDEPTH) buf_printf(b, ", dwAlphaBitDepth=%u", (unsigned)w[7]);
    if (fl & DDSD_LPSURFACE) buf_printf(b, ", lpSurface=%s", w[9] ? "P" : "NULL");
    if (fl & DDSD_CKDESTOVERLAY) { buf_puts(b, ", ddckCKDestOverlay="); fmt_colorkey(b, (const DDCOLORKEY *)&w[10]); }
    if (fl & DDSD_CKDESTBLT) { buf_puts(b, ", ddckCKDestBlt="); fmt_colorkey(b, (const DDCOLORKEY *)&w[12]); }
    if (fl & DDSD_CKSRCOVERLAY) { buf_puts(b, ", ddckCKSrcOverlay="); fmt_colorkey(b, (const DDCOLORKEY *)&w[14]); }
    if (fl & DDSD_CKSRCBLT) { buf_puts(b, ", ddckCKSrcBlt="); fmt_colorkey(b, (const DDCOLORKEY *)&w[16]); }
    if (fl & DDSD_PIXELFORMAT) { buf_puts(b, ", ddpfPixelFormat="); fmt_ddpf(b, (const DDPIXELFORMAT *)&w[18]); }
    if (fl & DDSD_FVF) buf_printf(b, ", dwFVF=0x%x", (unsigned)w[18]);
    if (fl & DDSD_CAPS) { buf_puts(b, ", ddsCaps="); fmt_ddscaps2(b, (const DDSCAPS2 *)&w[26]); }
    if (fl & DDSD_TEXTURESTAGE) buf_printf(b, ", dwTextureStage=%u", (unsigned)w[30]);
    buf_puts(b, "}");
}

static const FieldDef ddcaps_fields[] = {
    FD(DDCAPS_DX7, dwSize, FK_DEC), FD(DDCAPS_DX7, dwCaps, FK_HEX), FD(DDCAPS_DX7, dwCaps2, FK_HEX),
    FD(DDCAPS_DX7, dwCKeyCaps, FK_HEX), FD(DDCAPS_DX7, dwFXCaps, FK_HEX), FD(DDCAPS_DX7, dwFXAlphaCaps, FK_HEX),
    FD(DDCAPS_DX7, dwPalCaps, FK_HEX), FD(DDCAPS_DX7, dwSVCaps, FK_HEX),
    FD(DDCAPS_DX7, dwAlphaBltConstBitDepths, FK_HEX), FD(DDCAPS_DX7, dwAlphaBltPixelBitDepths, FK_HEX),
    FD(DDCAPS_DX7, dwAlphaBltSurfaceBitDepths, FK_HEX), FD(DDCAPS_DX7, dwAlphaOverlayConstBitDepths, FK_HEX),
    FD(DDCAPS_DX7, dwAlphaOverlayPixelBitDepths, FK_HEX), FD(DDCAPS_DX7, dwAlphaOverlaySurfaceBitDepths, FK_HEX),
    FD(DDCAPS_DX7, dwZBufferBitDepths, FK_HEX), FD(DDCAPS_DX7, dwVidMemTotal, FK_DEC),
    FD(DDCAPS_DX7, dwVidMemFree, FK_DEC), FD(DDCAPS_DX7, dwMaxVisibleOverlays, FK_DEC),
    FD(DDCAPS_DX7, dwCurrVisibleOverlays, FK_DEC), FD(DDCAPS_DX7, dwNumFourCCCodes, FK_DEC),
    FD(DDCAPS_DX7, dwAlignBoundarySrc, FK_DEC), FD(DDCAPS_DX7, dwAlignSizeSrc, FK_DEC),
    FD(DDCAPS_DX7, dwAlignBoundaryDest, FK_DEC), FD(DDCAPS_DX7, dwAlignSizeDest, FK_DEC),
    FD(DDCAPS_DX7, dwAlignStrideAlign, FK_DEC), FD(DDCAPS_DX7, dwMinOverlayStretch, FK_DEC),
    FD(DDCAPS_DX7, dwMaxOverlayStretch, FK_DEC), FD(DDCAPS_DX7, dwMinLiveVideoStretch, FK_DEC),
    FD(DDCAPS_DX7, dwMaxLiveVideoStretch, FK_DEC), FD(DDCAPS_DX7, dwMinHwCodecStretch, FK_DEC),
    FD(DDCAPS_DX7, dwMaxHwCodecStretch, FK_DEC), FD(DDCAPS_DX7, dwSVBCaps, FK_HEX),
    FD(DDCAPS_DX7, dwSVBCKeyCaps, FK_HEX), FD(DDCAPS_DX7, dwSVBFXCaps, FK_HEX), FD(DDCAPS_DX7, dwVSBCaps, FK_HEX),
    FD(DDCAPS_DX7, dwVSBCKeyCaps, FK_HEX), FD(DDCAPS_DX7, dwVSBFXCaps, FK_HEX), FD(DDCAPS_DX7, dwSSBCaps, FK_HEX),
    FD(DDCAPS_DX7, dwSSBCKeyCaps, FK_HEX), FD(DDCAPS_DX7, dwSSBFXCaps, FK_HEX),
    FD(DDCAPS_DX7, dwMaxVideoPorts, FK_DEC), FD(DDCAPS_DX7, dwCurrVideoPorts, FK_DEC),
    FD(DDCAPS_DX7, dwSVBCaps2, FK_HEX), FD(DDCAPS_DX7, dwNLVBCaps, FK_HEX), FD(DDCAPS_DX7, dwNLVBCaps2, FK_HEX),
    FD(DDCAPS_DX7, dwNLVBCKeyCaps, FK_HEX), FD(DDCAPS_DX7, dwNLVBFXCaps, FK_HEX),
};

static void fmt_ddcaps(Buf *b, const DDCAPS_DX7 *c) {
    int first = 1;
    buf_puts(b, "{");
    put_fields(b, c, ddcaps_fields, sizeof ddcaps_fields / sizeof *ddcaps_fields, &first);
    /* the ROP tables are long bit sets: a hash of all of them is enough to compare */
    buf_printf(b, ", rops_hash=%08x", (unsigned)fnv1a64(c->dwRops, sizeof c->dwRops) ^
                   (unsigned)fnv1a64(c->dwSVBRops, sizeof c->dwSVBRops) ^ (unsigned)fnv1a64(c->dwVSBRops, sizeof c->dwVSBRops) ^
                   (unsigned)fnv1a64(c->dwSSBRops, sizeof c->dwSSBRops) ^ (unsigned)fnv1a64(c->dwNLVBRops, sizeof c->dwNLVBRops));
    buf_puts(b, ", ddsCaps=");
    fmt_ddscaps2(b, &c->ddsCaps);
    buf_puts(b, "}");
}

static const FieldDef primcaps_fields[] = {
    FD(D3DPRIMCAPS, dwSize, FK_DEC), FD(D3DPRIMCAPS, dwMiscCaps, FK_HEX), FD(D3DPRIMCAPS, dwRasterCaps, FK_HEX),
    FD(D3DPRIMCAPS, dwZCmpCaps, FK_HEX), FD(D3DPRIMCAPS, dwSrcBlendCaps, FK_HEX),
    FD(D3DPRIMCAPS, dwDestBlendCaps, FK_HEX), FD(D3DPRIMCAPS, dwAlphaCmpCaps, FK_HEX),
    FD(D3DPRIMCAPS, dwShadeCaps, FK_HEX), FD(D3DPRIMCAPS, dwTextureCaps, FK_HEX),
    FD(D3DPRIMCAPS, dwTextureFilterCaps, FK_HEX), FD(D3DPRIMCAPS, dwTextureBlendCaps, FK_HEX),
    FD(D3DPRIMCAPS, dwTextureAddressCaps, FK_HEX), FD(D3DPRIMCAPS, dwStippleWidth, FK_DEC),
    FD(D3DPRIMCAPS, dwStippleHeight, FK_DEC),
};

static const FieldDef devdesc_head[] = {
    FD(D3DDEVICEDESC7, dwDevCaps, FK_HEX),
};
static const FieldDef devdesc_tail[] = {
    FD(D3DDEVICEDESC7, dwDeviceRenderBitDepth, FK_HEX), FD(D3DDEVICEDESC7, dwDeviceZBufferBitDepth, FK_HEX),
    FD(D3DDEVICEDESC7, dwMinTextureWidth, FK_DEC), FD(D3DDEVICEDESC7, dwMinTextureHeight, FK_DEC),
    FD(D3DDEVICEDESC7, dwMaxTextureWidth, FK_DEC), FD(D3DDEVICEDESC7, dwMaxTextureHeight, FK_DEC),
    FD(D3DDEVICEDESC7, dwMaxTextureRepeat, FK_DEC), FD(D3DDEVICEDESC7, dwMaxTextureAspectRatio, FK_DEC),
    FD(D3DDEVICEDESC7, dwMaxAnisotropy, FK_DEC), FD(D3DDEVICEDESC7, dvGuardBandLeft, FK_F32),
    FD(D3DDEVICEDESC7, dvGuardBandTop, FK_F32), FD(D3DDEVICEDESC7, dvGuardBandRight, FK_F32),
    FD(D3DDEVICEDESC7, dvGuardBandBottom, FK_F32), FD(D3DDEVICEDESC7, dvExtentsAdjust, FK_F32),
    FD(D3DDEVICEDESC7, dwStencilCaps, FK_HEX), FD(D3DDEVICEDESC7, dwFVFCaps, FK_HEX),
    FD(D3DDEVICEDESC7, dwTextureOpCaps, FK_HEX), FD(D3DDEVICEDESC7, dwMaxActiveLights, FK_DEC),
    FD(D3DDEVICEDESC7, dvMaxVertexW, FK_F32), FD(D3DDEVICEDESC7, dwVertexProcessingCaps, FK_HEX),
};

static void fmt_devdesc7(Buf *b, const D3DDEVICEDESC7 *d) {
    int first = 1;
    char tmp[48];
    buf_puts(b, "{");
    put_fields(b, d, devdesc_head, 1, &first);
    buf_puts(b, ", dpcLineCaps=");
    fmt_fields(b, &d->dpcLineCaps, primcaps_fields, sizeof primcaps_fields / sizeof *primcaps_fields);
    buf_puts(b, ", dpcTriCaps=");
    fmt_fields(b, &d->dpcTriCaps, primcaps_fields, sizeof primcaps_fields / sizeof *primcaps_fields);
    first = 0;
    put_fields(b, d, devdesc_tail, sizeof devdesc_tail / sizeof *devdesc_tail, &first);
    buf_printf(b, ", wMaxTextureBlendStages=%u, wMaxSimultaneousTextures=%u, deviceGUID=%s, wMaxUserClipPlanes=%u, "
                  "wMaxVertexBlendMatrices=%u",
               d->wMaxTextureBlendStages, d->wMaxSimultaneousTextures, guid_name(&d->deviceGUID, tmp),
               d->wMaxUserClipPlanes, d->wMaxVertexBlendMatrices);
    buf_puts(b, "}");
}

static const FieldDef viewport_fields[] = {
    FD(D3DVIEWPORT7, dwX, FK_DEC), FD(D3DVIEWPORT7, dwY, FK_DEC), FD(D3DVIEWPORT7, dwWidth, FK_DEC),
    FD(D3DVIEWPORT7, dwHeight, FK_DEC), FD(D3DVIEWPORT7, dvMinZ, FK_F32), FD(D3DVIEWPORT7, dvMaxZ, FK_F32),
};

static void put_color(Buf *b, const char *name, const DWORD *w) {
    buf_printf(b, "%s={f32:%08x, f32:%08x, f32:%08x, f32:%08x}", name, (unsigned)w[0], (unsigned)w[1], (unsigned)w[2],
               (unsigned)w[3]);
}

static void fmt_material(Buf *b, const D3DMATERIAL7 *m) {
    const DWORD *w = (const DWORD *)m;
    buf_puts(b, "{");
    put_color(b, "diffuse", w);
    buf_puts(b, ", ");
    put_color(b, "ambient", w + 4);
    buf_puts(b, ", ");
    put_color(b, "specular", w + 8);
    buf_puts(b, ", ");
    put_color(b, "emissive", w + 12);
    buf_printf(b, ", power=f32:%08x}", (unsigned)w[16]);
}

static void fmt_light(Buf *b, const D3DLIGHT7 *l) {
    const DWORD *w = (const DWORD *)l;
    const char *tn = light_name(w[0]);
    if (tn)
        buf_printf(b, "{dltType=%s, ", tn);
    else
        buf_printf(b, "{dltType=%u, ", (unsigned)w[0]);
    put_color(b, "diffuse", w + 1);
    buf_puts(b, ", ");
    put_color(b, "specular", w + 5);
    buf_puts(b, ", ");
    put_color(b, "ambient", w + 9);
    buf_printf(b, ", position={f32:%08x, f32:%08x, f32:%08x}, direction={f32:%08x, f32:%08x, f32:%08x}", (unsigned)w[13],
               (unsigned)w[14], (unsigned)w[15], (unsigned)w[16], (unsigned)w[17], (unsigned)w[18]);
    buf_printf(b, ", range=f32:%08x, falloff=f32:%08x, atten=[f32:%08x, f32:%08x, f32:%08x], theta=f32:%08x, phi=f32:%08x}",
               (unsigned)w[19], (unsigned)w[20], (unsigned)w[21], (unsigned)w[22], (unsigned)w[23], (unsigned)w[24],
               (unsigned)w[25]);
}

static void fmt_matrix(Buf *b, const D3DMATRIX *m) {
    const DWORD *w = (const DWORD *)m;
    int i;
    buf_puts(b, "{f32[16]=");
    for (i = 0; i < 16; i++)
        buf_printf(b, "%s%08x", i ? " " : "", (unsigned)w[i]);
    buf_puts(b, "}");
}

static const FieldDef vbdesc_fields[] = {
    FD(D3DVERTEXBUFFERDESC, dwSize, FK_DEC), FD(D3DVERTEXBUFFERDESC, dwCaps, FK_HEX),
    FD(D3DVERTEXBUFFERDESC, dwFVF, FK_HEX), FD(D3DVERTEXBUFFERDESC, dwNumVertices, FK_DEC),
};

static const FieldDef clipstatus_fields[] = {
    FD(D3DCLIPSTATUS, dwFlags, FK_HEX), FD(D3DCLIPSTATUS, dwStatus, FK_HEX), FD(D3DCLIPSTATUS, minx, FK_F32),
    FD(D3DCLIPSTATUS, maxx, FK_F32), FD(D3DCLIPSTATUS, miny, FK_F32), FD(D3DCLIPSTATUS, maxy, FK_F32),
    FD(D3DCLIPSTATUS, minz, FK_F32), FD(D3DCLIPSTATUS, maxz, FK_F32),
};

static void fmt_bltfx(Buf *b, const DDBLTFX *fx) {
    const DWORD *w = (const DWORD *)fx;
    /* size, DDFX, ROP, DDROP, rotation, z-op, z low, z high, z base dest, ... fill value at word 20 */
    buf_printf(b, "{dwSize=%u, dwDDFX=0x%x, dwROP=0x%x, dwDDROP=0x%x, dwFillColor=0x%x, ddckDestColorkey=", (unsigned)w[0],
               (unsigned)w[1], (unsigned)w[2], (unsigned)w[3], (unsigned)w[20]);
    fmt_colorkey(b, &fx->ddckDestColorkey);
    buf_puts(b, ", ddckSrcColorkey=");
    fmt_colorkey(b, &fx->ddckSrcColorkey);
    buf_puts(b, "}");
}

static void fmt_devident2(Buf *b, const DDDEVICEIDENTIFIER2 *d) {
    char tmp[48];
    buf_printf(b, "{szDriver=\"%.511s\", szDescription=\"%.511s\", liDriverVersion=0x%x%08x, dwVendorId=0x%x, "
                  "dwDeviceId=0x%x, dwSubSysId=0x%x, dwRevision=0x%x, guidDeviceIdentifier=%s, dwWHQLLevel=%u}",
               d->szDriver, d->szDescription, (unsigned)d->liDriverVersion.HighPart, (unsigned)d->liDriverVersion.LowPart,
               (unsigned)d->dwVendorId, (unsigned)d->dwDeviceId, (unsigned)d->dwSubSysId, (unsigned)d->dwRevision,
               guid_name(&d->guidDeviceIdentifier, tmp), (unsigned)d->dwWHQLLevel);
}

static size_t struct_size(int st) {
    switch (st) {
    case ST_DDSD2: return sizeof(DDSURFACEDESC2);
    case ST_DDPF: return sizeof(DDPIXELFORMAT);
    case ST_DDSCAPS2: return sizeof(DDSCAPS2);
    case ST_DDCAPS: return sizeof(DDCAPS_DX7);
    case ST_DEVDESC7: return sizeof(D3DDEVICEDESC7);
    case ST_VIEWPORT: return sizeof(D3DVIEWPORT7);
    case ST_MATERIAL: return sizeof(D3DMATERIAL7);
    case ST_LIGHT: return sizeof(D3DLIGHT7);
    case ST_MATRIX: return sizeof(D3DMATRIX);
    case ST_RECT: return sizeof(RECT);
    case ST_POINT: return sizeof(POINT);
    case ST_D3DRECT: return sizeof(D3DRECT);
    case ST_VBDESC: return sizeof(D3DVERTEXBUFFERDESC);
    case ST_BLTFX: return sizeof(DDBLTFX);
    case ST_COLORKEY: return sizeof(DDCOLORKEY);
    case ST_CLIPSTATUS: return sizeof(D3DCLIPSTATUS);
    case ST_DEVIDENT2: return sizeof(DDDEVICEIDENTIFIER2);
    default: return 4;
    }
}

void fmt_struct(Buf *b, int st, const void *p) {
    if (!p) {
        buf_puts(b, "NULL");
        return;
    }
    if (IsBadReadPtr(p, struct_size(st))) {
        buf_puts(b, "BAD");
        return;
    }
    switch (st) {
    case ST_DDSD2: fmt_ddsd2(b, p); break;
    case ST_DDPF: fmt_ddpf(b, p); break;
    case ST_DDSCAPS2: fmt_ddscaps2(b, p); break;
    case ST_DDCAPS: fmt_ddcaps(b, p); break;
    case ST_DEVDESC7: fmt_devdesc7(b, p); break;
    case ST_VIEWPORT: fmt_fields(b, p, viewport_fields, sizeof viewport_fields / sizeof *viewport_fields); break;
    case ST_MATERIAL: fmt_material(b, p); break;
    case ST_LIGHT: fmt_light(b, p); break;
    case ST_MATRIX: fmt_matrix(b, p); break;
    case ST_RECT: {
        const RECT *r = p;
        buf_printf(b, "{%d,%d,%d,%d}", (int)r->left, (int)r->top, (int)r->right, (int)r->bottom);
        break;
    }
    case ST_POINT: {
        const POINT *q = p;
        buf_printf(b, "{%d,%d}", (int)q->x, (int)q->y);
        break;
    }
    case ST_D3DRECT: {
        const D3DRECT *r = p;
        buf_printf(b, "{%d,%d,%d,%d}", (int)r->x1, (int)r->y1, (int)r->x2, (int)r->y2);
        break;
    }
    case ST_VBDESC: fmt_fields(b, p, vbdesc_fields, sizeof vbdesc_fields / sizeof *vbdesc_fields); break;
    case ST_BLTFX: fmt_bltfx(b, p); break;
    case ST_COLORKEY: fmt_colorkey(b, p); break;
    case ST_CLIPSTATUS: fmt_fields(b, p, clipstatus_fields, sizeof clipstatus_fields / sizeof *clipstatus_fields); break;
    case ST_DEVIDENT2: fmt_devident2(b, p); break;
    default: buf_puts(b, "?"); break;
    }
}
