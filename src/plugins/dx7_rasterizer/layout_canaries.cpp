#include "layout_canaries.h"

#include "ge_layouts.h"

extern "C" {
__declspec(dllimport) int __stdcall IsBadReadPtr(const void *, unsigned long);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *);
}

namespace {

typedef unsigned long ulong;

// ---- logging ------------------------------------------------------------------------------------

const char *ReasonName(long code)
{
    switch (code) {
    case EEWEB_LAYOUT_NULL: return "null pointer";
    case EEWEB_LAYOUT_MISALIGNED: return "misaligned";
    case EEWEB_LAYOUT_UNREADABLE: return "unreadable";
    case EEWEB_LAYOUT_WRONG_CLASS: return "wrong class";
    case EEWEB_LAYOUT_BAD_FIELD: return "field out of range";
    case EEWEB_LAYOUT_BAD_CHAIN: return "broken pointer chain";
    default: return "unknown";
    }
}

long Fail(const char *what, const void *object, long code)
{
    char line[160];
    unsigned n = 0;
    const char *prefix = "EEWeb layout canary: ";
    for (; *prefix && n < sizeof line - 1; ++prefix) line[n++] = *prefix;
    for (const char *p = what; *p && n < 100; ++p) line[n++] = *p;
    line[n++] = ' ';
    line[n++] = '0';
    line[n++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) line[n++] = "0123456789abcdef"[(reinterpret_cast<ulong>(object) >> shift) & 15];
    line[n++] = ':';
    line[n++] = ' ';
    for (const char *p = ReasonName(code); *p && n < sizeof line - 2; ++p) line[n++] = *p;
    line[n++] = '\n';
    line[n] = 0;
    OutputDebugStringA(line);
    return code;
}

// ---- building blocks ----------------------------------------------------------------------------

bool Readable(const void *p, ulong bytes)
{
    return p && !IsBadReadPtr(p, bytes);
}

// Pointer is non-null, aligned and readable for `bytes`; the code to return otherwise.
long ObjectProblem(const void *p, ulong bytes)
{
    if (!p) return EEWEB_LAYOUT_NULL;
    if (reinterpret_cast<ulong>(p) & 3) return EEWEB_LAYOUT_MISALIGNED;
    if (IsBadReadPtr(p, bytes)) return EEWEB_LAYOUT_UNREADABLE;
    return EEWEB_LAYOUT_OK;
}

bool Finite(float f)
{
    uint32_t bits;
    __builtin_memcpy(&bits, &f, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}

bool SameName(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// MSVC RTTI (x86): vftable[-1] is the complete-object locator {signature, offset, cdOffset,
// typeDescriptor, classHierarchy}; a type descriptor is {vftable, spare, char name[]}; the class
// hierarchy is {signature, attributes, baseCount, baseArray} and every base entry starts with a type
// descriptor pointer.  True when the object's class, or any base of it, is named `mangled`
// (".?AVGETexture@@").
bool IsClass(const void *object, const char *mangled)
{
    const ulong *vftable = *static_cast<const ulong *const *>(object);
    if (!Readable(vftable ? vftable - 1 : 0, 8)) return false;
    const ulong *locator = reinterpret_cast<const ulong *>(vftable[-1]);
    if (!Readable(locator, 20) || locator[0] != 0) return false;
    const ulong *descriptor = reinterpret_cast<const ulong *>(locator[3]);
    if (Readable(descriptor, 12) && SameName(reinterpret_cast<const char *>(descriptor + 2), mangled)) return true;
    const ulong *hierarchy = reinterpret_cast<const ulong *>(locator[4]);
    if (!Readable(hierarchy, 16)) return false;
    ulong count = hierarchy[2];
    const ulong *bases = reinterpret_cast<const ulong *>(hierarchy[3]);
    if (count > 32 || !Readable(bases, count * 4)) return false;
    for (ulong i = 0; i < count; ++i) {
        const ulong *base = reinterpret_cast<const ulong *>(bases[i]);
        if (!Readable(base, 4)) return false;
        descriptor = reinterpret_cast<const ulong *>(base[0]);
        if (Readable(descriptor, 12) && SameName(reinterpret_cast<const char *>(descriptor + 2), mangled)) return true;
    }
    return false;
}

// The common prefix of every check: object, alignment, readable layout, class.
long Begin(const void *object, ulong size, const char *name, const char *mangled)
{
    long problem = ObjectProblem(object, size);
    if (problem) return Fail(name, object, problem);
    if (mangled && !IsClass(object, mangled)) return Fail(name, object, EEWEB_LAYOUT_WRONG_CLASS);
    return EEWEB_LAYOUT_OK;
}

bool IsFlag(uint8_t b) { return b <= 1; }

} // namespace

long EEWebCheckTexture(const void *object)
{
    long problem = Begin(object, sizeof(GETextureLayout), "GETexture", ".?AVGETexture@@");
    if (problem) return problem;
    const GETextureLayout *t = static_cast<const GETextureLayout *>(object);
    if (!IsFlag(t->isCompressed) || !IsFlag(t->isPromoted) || !IsFlag(t->hasChanged) || !IsFlag(t->createMIPMaps)) {
        return Fail("GETexture", object, EEWEB_LAYOUT_BAD_FIELD);
    }
    // SetTexture follows stage.texture -> baseTexture -> surface and reads the DirectDraw surface at +0x48.
    const GETextureLayout *stage = t->stage.texture;
    if (ObjectProblem(stage, sizeof(GETextureLayout))) return Fail("GETexture", object, EEWEB_LAYOUT_BAD_CHAIN);
    const GETextureLayout *base = stage->baseTexture;
    if (ObjectProblem(base, sizeof(GETextureLayout))) return Fail("GETexture", object, EEWEB_LAYOUT_BAD_CHAIN);
    if (ObjectProblem(base->surface, 0x4c)) return Fail("GETexture", object, EEWEB_LAYOUT_BAD_CHAIN);
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckMaterial(const void *object)
{
    long problem = Begin(object, sizeof(GEMaterialLayout), "GEMaterial", ".?AVGEMaterial@@");
    if (problem) return problem;
    const GEMaterialLayout *m = static_cast<const GEMaterialLayout *>(object);
    if (!Finite(m->opacity) || !Finite(m->color.r) || !Finite(m->color.g) || !Finite(m->color.b)) {
        return Fail("GEMaterial", object, EEWEB_LAYOUT_BAD_FIELD);
    }
    if ((m->textureCount && !Readable(m->textures, m->textureCount * 4)) ||
        (m->lodCount && (!Readable(m->polygonStart, m->lodCount * 4) || !Readable(m->polygonLength, m->lodCount * 4)))) {
        return Fail("GEMaterial", object, EEWEB_LAYOUT_BAD_CHAIN);
    }
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckModel(const void *object)
{
    long problem = Begin(object, sizeof(GEModelLayout), "GEModel", ".?AVGEModel@@");
    if (problem) return problem;
    const GEModelLayout *m = static_cast<const GEModelLayout *>(object);
    if (m->lodCount == 0 || m->lodCount > 64 || !Finite(m->graphicsScale) || !Finite(m->opacity)) {
        return Fail("GEModel", object, EEWEB_LAYOUT_BAD_FIELD);
    }
    if (!Readable(m->polygonArrays, m->lodCount * 4) || !Readable(m->vertexArrays, m->lodCount * 4) ||
        (m->subModelCount && !Readable(m->subModels, m->subModelCount * 4)) ||
        (m->materialCount && !Readable(m->materials, m->materialCount * 4))) {
        return Fail("GEModel", object, EEWEB_LAYOUT_BAD_CHAIN);
    }
    for (ulong i = 0; i < m->materialCount; ++i) {
        long inner = EEWebCheckMaterial(m->materials[i]);
        if (inner) return inner;
    }
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckLight(const void *object)
{
    long problem = Begin(object, sizeof(GELightLayout), "GELight", 0);
    if (problem) return problem;
    const float *floats = static_cast<const float *>(object);
    for (unsigned i = 0; i < sizeof(GELightLayout) / 4; ++i) {
        // 0x70 is the style word, 0x5c..0x6c are derived values the constructor leaves unset
        if (i == 0x70 / 4 || (i >= 0x5c / 4 && i <= 0x6c / 4)) continue;
        if (!Finite(floats[i])) return Fail("GELight", object, EEWEB_LAYOUT_BAD_FIELD);
    }
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckTerrainMesh(const void *object)
{
    long problem = Begin(object, sizeof(GETerrainMeshLayout), "GETerrainMesh", ".?AVGETerrainMesh@@");
    if (problem) return problem;
    const GETerrainMeshLayout *m = static_cast<const GETerrainMeshLayout *>(object);
    if ((m->pointRowCount && !Readable(m->pointRows, m->pointRowCount * sizeof(GETMPointRowLayout))) ||
        (m->tileRowCount && !Readable(m->tileRows, m->tileRowCount * sizeof(GETMTileRowLayout)))) {
        return Fail("GETerrainMesh", object, EEWEB_LAYOUT_BAD_CHAIN);
    }
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckViewport(const void *object)
{
    long problem = Begin(object, sizeof(GEViewportLayout), "GEViewport", 0);
    if (problem) return problem;
    const GEViewportLayout *v = static_cast<const GEViewportLayout *>(object);
    if (!Finite(v->fieldOfView) || !Finite(v->frontClip) || !Finite(v->rearClip) || !Finite(v->halfWidth) || !Finite(v->halfHeight) ||
        !Finite(v->yClippingPlane) || !Finite(v->depthNear) || !Finite(v->depthFar)) {
        return Fail("GEViewport", object, EEWEB_LAYOUT_BAD_FIELD);
    }
    return EEWEB_LAYOUT_OK;
}

long EEWebCheckText(const void *object)
{
    long problem = Begin(object, sizeof(GETextLayout), "GEText", 0);
    if (problem) return problem;
    const GETextLayout *t = static_cast<const GETextLayout *>(object);
    if (t->text.length > t->text.capacity || !IsFlag(t->isTransparent)) return Fail("GEText", object, EEWEB_LAYOUT_BAD_FIELD);
    if (t->text.length && !Readable(t->text.chars, t->text.length + 1)) return Fail("GEText", object, EEWEB_LAYOUT_BAD_CHAIN);
    if (ObjectProblem(t->font, sizeof(GEFontLayout))) return Fail("GEText", object, EEWEB_LAYOUT_BAD_CHAIN);
    return EEWEB_LAYOUT_OK;
}
