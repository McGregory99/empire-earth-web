// The terrain transform (task P2.6.7f): TransformToCamera(GETerrainMesh &, GEViewport &, float time, bool water, bool orthographic),
// slot 67.  The original is 4995 bytes (0x10007297); tests/dx7_rasterizer/terrain_checks.h compares both builds on 33 recorded steps.
//
// What it does, in the order the original does it:
//   1. Nothing at all when the mesh has no visible tile rows.  On a water pass (`water`) also nothing when the ripple speed
//      (GERasterizer field 0x40) is zero or not a number, or the mesh's visible water share (0x17b584) is.
//   2. The ripple phase (a module-wide float, zero when the DLL loads) grows by `time` * the ripple speed.
//   3. For every point of the visible window (columns x0 .. x0 + width + 1, rows y0 .. y0 + height + 1 of the 101 x 101 grid, the
//      row outermost) that the mesh's visibility array marks, the height z comes from the point rows.  sine = (float) sin(row * column +
//      phase).  The terrain vertex is the point (column, row, z) - or, below z = -0.5, (column, row, z - sine * depth) with the depth
//      from field 0x48 - through the viewport's camera inverse, and gets a six-bit outcode: bit 1 depth <= the front clip, bit 2 depth
//      >= the rear clip, then the left, right, top and bottom tests (bits 4, 8, 0x10, 0x20): the viewport's rectangle and clip scale in
//      the orthographic case, otherwise the viewport's left and right planes and a top and bottom plane built from the field of view.
//      Where the water visibility array marks the point there is a water vertex too: (column, row, w) with w = sine * height for a
//      point below sea level and (sine - 1) * height / 2 above it (height: field 0x44).  A water pass does only the points that are
//      below sea level or have water, and recomputes the terrain vertex only where it is below z = -0.5.
//   4. A terrain pass also transforms the mesh's edge points into the edge render vertices.
//   5. Every terrain and water material sorts its polygons: the list at 0x4 (all of them) goes into the list at 0x14 when none of
//      the three vertices has an outcode, into the list at 0x24 when one or two has or when all three have outcodes that share no
//      bit (or all six), and is dropped when all three share a bit.  Each list ends with a zero and its last slot holds its count.
//
// The float arithmetic is the original's x87 code (values kept in extended precision between operations, rounded once when stored),
// written as the same instruction sequences; the sine is the C run-time's.  Differs from the original, deliberately (the original
// reads and writes wherever the mesh says): a window wider than the 101 x 101 grid and a point row, point or material list that does
// not exist are skipped.
#include "null_rasterizer.h"

#include "dx7_math.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" double sin(double);
extern "C" double sqrt(double);

typedef unsigned long ulong;

extern "C" {
float __attribute__((cdecl)) EEWebUMTan(float) __asm__("?UMTan@@YAMM@Z");
float __attribute__((cdecl)) EEWebUMArcTan(float) __asm__("?UMArcTan@@YAMM@Z");
float __attribute__((cdecl)) EEWebUMCos(float) __asm__("?UMCos@@YAMM@Z");
ulong __attribute__((thiscall)) EEWebVisibleTileRows(const void* mesh) __asm__("?GetNumVisibleTileRows@GETerrainMesh@@QBEKXZ");
void __attribute__((thiscall)) EEWebVectorCopy(void* self, const void* other) __asm__("??0GEVector@@QAE@ABV0@@Z");
void __attribute__((thiscall)) EEWebVectorAssign(void* self, const void* other) __asm__("??4GEVector@@QAEAAV0@ABV0@@Z");
void __attribute__((thiscall)) EEWebColorNormalize(void* self) __asm__("?Normalize@GEColor@@QAEXXZ");
void __attribute__((thiscall)) EEWebVectorNormalize(void* self) __asm__("?Normalize@GEVector@@QAEXXZ");
void __attribute__((thiscall)) EEWebTransformationApplyVector(const void* self, const void* input, void* output) __asm__("?Apply@GETransformation@@QBEXABVGEVector@@AAV2@@Z");
unsigned char __attribute__((thiscall)) EEWebPlaneBelow(const void* plane, const void* point) __asm__("?IsPointOnOrBelowPlane@GE3DPlane@@QBE_NABVGE3DPoint@@@Z");

// out = {0, cos(pi/2 - a), cos(a)} with a = arctan(halfHeight * tan(fov / 2) / (halfWidth - 1)): the original's sequence, whose
// intermediate values stay in the x87 registers (the engine's functions leave their results there in extended precision).
// cdecl: viewport, the constants {0.5f, 1.0f, pi/2}, out, and the addresses of UMTan, UMArcTan and UMCos.
void __attribute__((cdecl)) EEWebFrustumNormal(const void* viewport, const float* constants, float* out, void* const* functions);

// out = {s * direction.x, s * direction.y, s * direction.z} with s = sqrt(2 * reach * reach): the sqrt result is used straight from the
// x87 register it comes back in (cdecl: &reach, direction, out, the address of sqrt).
void __attribute__((cdecl)) EEWebCoverReach(const float* reach, const float* direction, float* out, void* squareRoot);
}

__asm__(".text\n"
        ".globl _EEWebCoverReach\n"
        "_EEWebCoverReach:\n"
        ".intel_syntax noprefix\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "mov eax, [ebp+8]\n"
        "fld dword ptr [eax]\n"
        "fmul st, st(0)\n"
        "fadd st, st(0)\n"
        "sub esp, 8\n"
        "fstp qword ptr [esp]\n"
        "call dword ptr [ebp+20]\n"
        "add esp, 8\n"
        "mov eax, [ebp+12]\n"
        "mov ecx, [ebp+16]\n"
        "fld st(0)\n"
        "fmul dword ptr [eax]\n"
        "fstp dword ptr [ecx]\n"
        "fld st(0)\n"
        "fmul dword ptr [eax+4]\n"
        "fstp dword ptr [ecx+4]\n"
        "fmul dword ptr [eax+8]\n"
        "fstp dword ptr [ecx+8]\n"
        "pop ebp\n"
        "ret\n"
        ".att_syntax prefix\n");

__asm__(".text\n"
        ".globl _EEWebFrustumNormal\n"
        "_EEWebFrustumNormal:\n"
        ".intel_syntax noprefix\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 8\n"
        "mov eax, [ebp+8]\n"
        "mov ecx, [ebp+12]\n"
        "fld dword ptr [eax+0x25c]\n"
        "fmul dword ptr [ecx]\n"
        "push eax\n"
        "fstp dword ptr [esp]\n"
        "mov edx, [ebp+20]\n"
        "call dword ptr [edx]\n"
        "mov eax, [ebp+8]\n"
        "mov ecx, [ebp+12]\n"
        "fmul dword ptr [eax+0x268]\n"
        "fld dword ptr [eax+0x26c]\n"
        "fsub dword ptr [ecx+4]\n"
        "fdivp st(1), st\n"
        "fstp dword ptr [esp]\n"
        "mov edx, [ebp+20]\n"
        "call dword ptr [edx+4]\n"
        "fstp dword ptr [ebp-4]\n"
        "mov ecx, [ebp+12]\n"
        "fld dword ptr [ecx+8]\n"
        "fsub dword ptr [ebp-4]\n"
        "fstp dword ptr [esp]\n"
        "mov edx, [ebp+20]\n"
        "call dword ptr [edx+8]\n"
        "mov eax, [ebp+16]\n"
        "fstp dword ptr [eax+4]\n"
        "fld dword ptr [ebp-4]\n"
        "fstp dword ptr [esp]\n"
        "mov edx, [ebp+20]\n"
        "call dword ptr [edx+8]\n"
        "mov eax, [ebp+16]\n"
        "fstp dword ptr [eax+8]\n"
        "mov dword ptr [eax], 0\n"
        "mov esp, ebp\n"
        "pop ebp\n"
        "ret\n"
        ".att_syntax prefix\n");

namespace {

const ulong kColumns = 101;
const ulong kFrontBit = 1, kRearBit = 2, kLeftBit = 4, kRightBit = 8, kTopBit = 0x10, kBottomBit = 0x20;

// The ripple's phase: the original keeps it in its data section (0x10017ca8), zero when the DLL loads.
float g_wavePhase = 0.0f;

// The x87 status word bits the original's `sahf` moves into the flags.
const unsigned kLess = 0x100;     // C0: below, or not ordered
const unsigned kEqual = 0x4000;   // C3: equal, or not ordered

// phase = phase + time * speed, the sum rounded once.
void AdvancePhase(float time, float speed)
{
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfadds %0\n\tfstps %0" : "+m"(g_wavePhase) : "m"(time), "m"(speed) : "st");
}

// (float) sin((double) (row * column + phase)): the product of the integer row and the float column in extended precision.
float Sine(ulong row, ulong column)
{
    long long wide = static_cast<long long>(row);
    float columnValue = static_cast<float>(static_cast<unsigned long long>(column));
    double argument;
    __asm__ volatile("fildll %1\n\tfmuls %2\n\tfadds %3\n\tfstpl %0" : "=m"(argument) : "m"(wide), "m"(columnValue), "m"(g_wavePhase) : "st");
    return static_cast<float>(sin(argument));
}

// height - sine * depth in extended precision, rounded once.
float Rippled(float height, float sine, float depth)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfsubrs %3\n\tfstps %0" : "=m"(result) : "m"(sine), "m"(depth), "m"(height) : "st");
    return result;
}

// The height of the water: sine * scale below sea level, (sine - 1) * scale * 0.5 above it, one rounding.
float WaterHeight(bool aboveSea, float sine, float scale)
{
    static const float kOne = 1.0f, kHalf = 0.5f;
    float result;
    if (aboveSea) {
        __asm__ volatile("flds %1\n\tfsubs %3\n\tfmuls %2\n\tfmuls %4\n\tfstps %0" : "=m"(result) : "m"(sine), "m"(scale), "m"(kOne), "m"(kHalf) : "st");
    } else {
        __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(sine), "m"(scale) : "st");
    }
    return result;
}

// The status word of the comparison of -(a * b) (the product kept in extended precision) with `value`.
unsigned CompareNegatedProduct(float a, float b, float value)
{
    unsigned short status;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfchs\n\tfcoms %3\n\tfnstsw %0\n\tfstp %%st(0)" : "=a"(status) : "m"(a), "m"(b), "m"(value) : "st");
    return status;
}

struct Plane {
    float point[3];
    float normal[3];
};

struct Transformer {
    GETerrainMeshLayout* mesh;
    const GEViewportLayout* viewport;
    bool orthographic;
    float front, rear;
    Plane left, right, top, bottom;
    float heightOfWater, depthOfRipple;
};

void CopyPlane(Plane& to, const GE3DPlaneLayout& from)
{
    __builtin_memcpy(to.point, &from.point, sizeof to.point);
    EEWebVectorCopy(to.normal, &from.normal);
}

// The outcode of a camera-space position.
ulong Outcode(const Transformer& t, const float* position)
{
    ulong flags = 0;
    if (!(position[1] > t.front)) {
        flags |= kFrontBit;
    }
    if (position[1] >= t.rear) {
        flags |= kRearBit;
    }
    if (t.orthographic) {
        const float* rectangle = reinterpret_cast<const float*>(t.viewport);
        float scale = t.viewport->yClippingPlane;
        if (rectangle[1] >= position[0]) {
            flags |= kLeftBit;
        }
        if (!(rectangle[2] > position[0])) {
            flags |= kRightBit;
        }
        if (CompareNegatedProduct(scale, rectangle[3], position[2]) & (kLess | kEqual)) {
            flags |= kTopBit;
        }
        if (!(CompareNegatedProduct(rectangle[0], scale, position[2]) & kLess)) {
            flags |= kBottomBit;
        }
    } else {
        if (EEWebPlaneBelow(&t.left, position)) {
            flags |= kLeftBit;
        }
        if (EEWebPlaneBelow(&t.right, position)) {
            flags |= kRightBit;
        }
        if (EEWebPlaneBelow(&t.top, position)) {
            flags |= kTopBit;
        }
        if (EEWebPlaneBelow(&t.bottom, position)) {
            flags |= kBottomBit;
        }
    }
    return flags;
}

void Place(const Transformer& t, float x, float y, float z, GETMRenderVertexLayout& vertex)
{
    float point[3] = {x, y, z};
    EEWebTransformationApplyPoint(&t.viewport->cameraInverse, point, &vertex.position);
    vertex.clipFlags = Outcode(t, &vertex.position.x);
}

// One list of polygons per material: the vector at +0x4 is the input, the ones at +0x14 and +0x24 the result.
void SortMaterials(const StdVectorLayout& materials)
{
    if (!materials.first) {
        return;
    }
    ulong count = static_cast<ulong>((static_cast<char*>(materials.last) - static_cast<char*>(materials.first)) / static_cast<long>(sizeof(GETMMaterialLayout)));
    GETMMaterialLayout* list = static_cast<GETMMaterialLayout*>(materials.first);
    for (ulong index = 0; index < count; index++) {
        GETMMaterialLayout& material = list[index];
        GETMPolygonLayout** polygons = static_cast<GETMPolygonLayout**>(material.unknownVector.first);
        ulong polygonCount = polygons ? static_cast<ulong>((static_cast<char*>(material.unknownVector.last) - static_cast<char*>(material.unknownVector.first)) >> 2) : 0;
        GETMPolygonLayout** inside = static_cast<GETMPolygonLayout**>(material.polygons.first);
        GETMPolygonLayout** overlap = static_cast<GETMPolygonLayout**>(material.overlayPolygons.first);
        if (!inside || !overlap || !material.polygons.last || !material.overlayPolygons.last) {
            continue;
        }
        ulong insideCount = 0, overlapCount = 0;
        for (ulong i = 0; i < polygonCount; i++) {
            GETMPolygonLayout* polygon = polygons[i];
            ulong first = static_cast<GETMRenderVertexLayout*>(polygon->vertices[0])->clipFlags;
            ulong second = static_cast<GETMRenderVertexLayout*>(polygon->vertices[1])->clipFlags;
            ulong third = static_cast<GETMRenderVertexLayout*>(polygon->vertices[2])->clipFlags;
            ulong marked = (first != 0) + (second != 0) + (third != 0);
            if (marked == 0) {
                inside[insideCount++] = polygon;
            } else if (marked != 3 || (first & second & third) == 0 || (first & second & third) == 0x3f) {
                overlap[overlapCount++] = polygon;
            }
        }
        overlap[overlapCount] = 0;
        inside[insideCount] = 0;
        static_cast<ulong*>(material.overlayPolygons.last)[-1] = overlapCount;
        static_cast<ulong*>(material.polygons.last)[-1] = insideCount;
    }
}

} // namespace

void EEWebNullRasterizer::TransformToCamera(GETerrainMesh& meshObject, GEViewport& viewportObject, float time, bool water, bool orthographic)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK || Note(EEWebCheckViewport(&viewportObject)) != EEWEB_LAYOUT_OK) {
        return;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    const GEViewportLayout* viewport = reinterpret_cast<const GEViewportLayout*>(&viewportObject);
    if (mesh->visibleTileWidth > kColumns - 2 || mesh->visibleTileHeight > kColumns - 2) {
        return;
    }
    Transformer t;
    t.mesh = mesh;
    t.viewport = viewport;
    t.orthographic = orthographic;
    t.front = viewport->frontClip;
    t.rear = viewport->rearClip;
    t.heightOfWater = Base()->waterRipple[1];
    t.depthOfRipple = Base()->waterRipple[2];
    CopyPlane(t.left, viewport->leftPlane);
    CopyPlane(t.right, viewport->rightPlane);
    CopyPlane(t.top, viewport->topPlane);
    CopyPlane(t.bottom, viewport->bottomPlane);
    if (EEWebVisibleTileRows(mesh) == 0) {
        return;
    }
    float speed = Base()->waterRipple[0];
    // equal to zero, or not ordered with it (the original's `fcomp; sahf; je`)
    bool stillRipple = !(speed < 0.0f || speed > 0.0f);
    float share = mesh->waterVisiblePercentage;
    bool noWater = !(share < 0.0f || share > 0.0f);
    if ((stillRipple || noWater) && water) {
        return;
    }
    // The top and bottom planes of the frustum: points (0, 0, 0), the normals from the field of view, mirrored.
    static const float kConstants[3] = {0.5f, 1.0f, 1.5707963705062866f};
    void* const functions[3] = {reinterpret_cast<void*>(&EEWebUMTan), reinterpret_cast<void*>(&EEWebUMArcTan), reinterpret_cast<void*>(&EEWebUMCos)};
    float normal[3];
    EEWebFrustumNormal(viewport, kConstants, normal, functions);
    EEWebVectorNormalize(normal);
    __builtin_memset(&t.bottom, 0, sizeof t.bottom.point);
    EEWebVectorAssign(t.bottom.normal, normal);
    normal[2] = -normal[2];
    __builtin_memset(&t.top, 0, sizeof t.top.point);
    EEWebVectorAssign(t.top.normal, normal);

    AdvancePhase(time, speed);

    ulong x0 = mesh->visibleBottomLeftX;
    ulong y0 = mesh->visibleBottomLeftY;
    ulong x1 = x0 + mesh->visibleTileWidth + 2;
    ulong y1 = y0 + mesh->visibleTileHeight + 2;
    for (ulong row = y0; row < y1; row++) {
        for (ulong column = x0; column < x1; column++) {
            ulong index = (column - x0) * kColumns + (row - y0);
            if (!mesh->pointVisibility[index]) {
                continue;
            }
            if (row >= mesh->pointRowCount || !mesh->pointRows[row].points || column < mesh->pointRows[row].firstIndex ||
                column - mesh->pointRows[row].firstIndex >= mesh->pointRows[row].count) {
                continue;
            }
            float z = mesh->pointRows[row].points[column - mesh->pointRows[row].firstIndex].z;
            bool hasWater = mesh->waterPointVisibility[index] != 0;
            if (water && !(z < 0.0f || !(z >= 0.0f)) && !hasWater) {
                continue;
            }
            float sine = Sine(row, column);
            float columnValue = static_cast<float>(column);
            float rowValue = static_cast<float>(row);
            // the terrain vertex: always on a terrain pass, only below z = -0.5 on a water pass
            if (!water || !(z >= -0.5f)) {
                float top = !(z >= -0.5f) ? Rippled(z, sine, t.depthOfRipple) : z;
                Place(t, columnValue, rowValue, top, mesh->renderVertices[index]);
            }
            if (hasWater) {
                Place(t, columnValue, rowValue, WaterHeight(z >= 0.0f, sine, t.heightOfWater), mesh->waterRenderVertices[index]);
            }
        }
    }
    if (!water && mesh->edgeWorldPoints.first) {
        ulong count = static_cast<ulong>((static_cast<char*>(mesh->edgeWorldPoints.last) - static_cast<char*>(mesh->edgeWorldPoints.first)) / 12);
        GE3DPointLayout* points = static_cast<GE3DPointLayout*>(mesh->edgeWorldPoints.first);
        GETMRenderVertexLayout** vertices = static_cast<GETMRenderVertexLayout**>(mesh->edgeRenderVertices.first);
        for (ulong i = 0; vertices && i < count; i++) {
            EEWebTransformationApplyPoint(&viewport->cameraInverse, &points[i], vertices[i]);
        }
    }
    SortMaterials(mesh->materials);
    SortMaterials(mesh->waterMaterials);
}

namespace {

// a * b rounded to a float.
float Scaled(float a, float b)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}

// (a * b) * c with the intermediate product in extended precision, rounded to a float once.
float TripleProduct(float a, float b, float c)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfmuls %3\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}

} // namespace

long EEWebNullRasterizer::DrawTerrainMaterial(GEViewport*, std::vector<GETerrainMesh::GETMPolygon*, std::allocator<GETerrainMesh::GETMPolygon*>>& polygons, ulong screen)
{
    static const float k255 = 255.0f;
    DX7ScreenRecordLayout* record = Record(screen);
    if (!record || !Device(screen) || !record->vertexBuffer) {
        return EEWEB_NOT_SUPPORTED;
    }
    const StdVectorLayout* list = reinterpret_cast<const StdVectorLayout*>(&polygons);
    GETMPolygonLayout* const* entries = static_cast<GETMPolygonLayout* const*>(list->first);
    if (!entries || list->last <= list->first) {
        return EEWEB_NOT_SUPPORTED;
    }
    ulong count = static_cast<const ulong*>(list->last)[-1];
    ulong vertices = count * 3;
    if (vertices == 0) {
        return 0;
    }
    if (count > 0x98a / 3) {
        return EEWEB_NOT_SUPPORTED;
    }
    void* ring = 0;
    LockVB(vertices, screen, ring);
    if (!ring) {
        UnlockVB(screen);
        return 1;
    }
    char* to = static_cast<char*>(ring) + record->vertexCursor * 0x24;
    for (ulong i = 0; i < count && entries[i]; i++, to += 3 * 0x24) {
        const GETMPolygonLayout* polygon = entries[i];
        for (int corner = 0; corner < 3; corner++) {
            const GETMRenderVertexLayout* vertex = static_cast<const GETMRenderVertexLayout*>(polygon->vertices[corner]);
            float* out = reinterpret_cast<float*>(to + corner * 0x24);
            out[0] = vertex->position.x;
            out[1] = vertex->position.y;
            out[2] = vertex->position.z;
            out[3] = out[4] = out[5] = 0.0f;
            ulong alpha = static_cast<ulong>(dx7math::RoundToNearest(Scaled(polygon->alpha[corner], k255)));
            ulong red = static_cast<ulong>(dx7math::RoundToNearest(TripleProduct(vertex->color.r, vertex->intensity, k255)));
            ulong green = static_cast<ulong>(dx7math::RoundToNearest(TripleProduct(vertex->color.g, vertex->intensity, k255)));
            ulong blue = static_cast<ulong>(dx7math::RoundToNearest(TripleProduct(vertex->color.b, vertex->intensity, k255)));
            *reinterpret_cast<ulong*>(out + 6) = ((((alpha << 8) | red) << 8 | green) << 8) | blue;
            out[7] = polygon->uv[corner].x;
            out[8] = polygon->uv[corner].y;
        }
    }
    return UnlockAndDraw(screen, 4, vertices, 0, 0);
}

namespace {

// a + b and a * b, each rounded to a float once (the x87 register operations of the original).
float Add(float a, float b)
{
    float result;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}

float Mul(float a, float b)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}

// a + (b + c) with the inner sum kept in extended precision.
float AddSum(float a, float b, float c)
{
    float result;
    __asm__ volatile("flds %2\n\tfadds %3\n\tfadds %1\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}

// c + a * b with the product kept in extended precision.
float AddProduct(float c, float a, float b)
{
    float result;
    __asm__ volatile("flds %2\n\tfmuls %3\n\tfadds %1\n\tfstps %0" : "=m"(result) : "m"(c), "m"(a), "m"(b) : "st");
    return result;
}

// a * b + c * d with both products kept in extended precision.
float SumOfProducts(float a, float b, float c, float d)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tflds %3\n\tfmuls %4\n\tfaddp\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c), "m"(d) : "st");
    return result;
}

struct MapNode {
    MapNode *left, *parent, *right;
    void* key;
    unsigned long value;
    unsigned char color, isNil;
};

// The next node in order of an MSVC 6 std::map (the library's _Inc).
MapNode* Next(MapNode* node)
{
    if (node->isNil) {
        return node;
    }
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil) {
            node = node->left;
        }
        return node;
    }
    MapNode* up;
    while (!(up = node->parent)->isNil && node == up->right) {
        node = up;
    }
    return up;
}

} // namespace

long EEWebNullRasterizer::Draw(GETerrainMesh& meshObject, ulong screen, bool orthographic)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device || !record->vertexBuffer) {
        return EEWEB_NOT_SUPPORTED;
    }
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 1, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    long result = device->vtbl->SetTransform(device, 2, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, 3, matrix);
    if (EEWebVisibleTileRows(mesh) != 0) {
        MapNode* head = static_cast<MapNode*>(mesh->materialMap.head);
        if (head && head->left) {
            ulong materialCount = mesh->materials.first ? static_cast<ulong>((static_cast<char*>(mesh->materials.last) - static_cast<char*>(mesh->materials.first)) /
                                                                             static_cast<long>(sizeof(GETMMaterialLayout)))
                                                        : 0;
            GETMMaterialLayout* materials = static_cast<GETMMaterialLayout*>(mesh->materials.first);
            ulong visited = 0;
            for (MapNode* node = head->left; node != head && visited < 4096; node = Next(node), visited++) {
                if (node->value >= materialCount) {
                    continue;
                }
                GETMMaterialLayout* material = materials + node->value;
                if (!material->polygons.last || !material->overlayPolygons.last) {
                    continue;
                }
                state->scenePolyCount += static_cast<const ulong*>(material->polygons.last)[-1];
                state->scenePolyCount += static_cast<const ulong*>(material->overlayPolygons.last)[-1];
                SetTexture(static_cast<GETexture*>(node->key), screen);
                device->vtbl->SetRenderState(device, 0x88, 0);
                DrawTerrainMaterial(reinterpret_cast<GEViewport*>(state), *reinterpret_cast<std::vector<GETerrainMesh::GETMPolygon*, std::allocator<GETerrainMesh::GETMPolygon*>>*>(&material->polygons),
                                    screen);
                device->vtbl->SetRenderState(device, 0x88, 1);
                result = DrawTerrainMaterial(reinterpret_cast<GEViewport*>(state),
                                             *reinterpret_cast<std::vector<GETerrainMesh::GETMPolygon*, std::allocator<GETerrainMesh::GETMPolygon*>>*>(&material->overlayPolygons), screen);
            }
        }
        record->sceneDrawn = 1;
        return result;
    }
    // Nothing of the terrain is visible: a black cover of four triangles from the camera.
    static const float kSide = 1000.0f, kAhead = 7.071066856384277f;
    float forward[3] = {0.0f, 1.0f, 0.0f}, right[3] = {1.0f, 0.0f, 0.0f}, v1[3], v2[3];
    EEWebTransformationApplyVector(&state->viewport.camera, forward, v1);
    v1[2] = 0.0f;
    EEWebVectorNormalize(v1);
    EEWebTransformationApplyVector(&state->viewport.camera, right, v2);
    v2[2] = 0.0f;
    EEWebVectorNormalize(v2);
    const float cx = state->viewport.camera.m[3], cy = state->viewport.camera.m[7];
    float a[6][3];
    const float negative = -kSide;
    // the camera's xy plus or minus 1000 to the right (z: the bare product)
    a[0][0] = Add(Mul(negative, v2[0]), cx);
    a[0][1] = Add(Mul(v2[1], negative), cy);
    a[0][2] = Mul(negative, v2[2]);
    a[1][0] = Add(Mul(v2[0], kSide), cx);
    a[1][1] = Add(Mul(v2[1], kSide), cy);
    a[1][2] = Mul(kSide, v2[2]);
    // a point ahead of the camera
    float ahead[3] = {Mul(v1[0], kAhead), Mul(v1[1], kAhead), 0.0f};
    float forwardPoint[3];
    forwardPoint[0] = Add(ahead[0], cx);
    forwardPoint[1] = Add(ahead[1], cy);
    forwardPoint[2] = Mul(kAhead, v1[2]);
    a[2][0] = Add(Mul(negative, v2[0]), forwardPoint[0]);
    a[2][1] = Add(Mul(negative, v2[1]), forwardPoint[1]);
    a[2][2] = SumOfProducts(negative, v2[2], kAhead, v1[2]);
    a[3][0] = Add(Mul(kSide, v2[0]), forwardPoint[0]);
    a[3][1] = Add(Mul(kSide, v2[1]), forwardPoint[1]);
    a[3][2] = Add(Mul(kSide, v2[2]), forwardPoint[2]);
    // a point at the rear plane's reach ahead of the camera
    float far[3];
    EEWebCoverReach(&state->viewport.rearClip, v1, far, reinterpret_cast<void*>(&sqrt));
    float farY = Add(far[1], cy);
    float farZ = far[2];
    a[4][0] = AddSum(Mul(negative, v2[0]), far[0], cx);
    a[4][1] = Add(Mul(negative, v2[1]), farY);
    a[4][2] = AddProduct(farZ, negative, v2[2]);
    a[5][0] = AddSum(Mul(v2[0], kSide), far[0], cx);
    a[5][1] = Add(Mul(v2[1], kSide), farY);
    a[5][2] = Add(Mul(kSide, v2[2]), farZ);
    float b[6][3];
    for (int i = 0; i < 6; i++) {
        EEWebTransformationApplyPoint(&state->viewport.cameraInverse, a[i], b[i]);
    }
    state->scenePolyCount += 4;
    SetTexture(0, screen);
    device->vtbl->SetRenderState(device, 0x88, 1);
    void* ring = 0;
    LockVB(12, screen, ring);
    if (!ring) {
        UnlockVB(screen);
        return 1;
    }
    static const int kOrder[12] = {2, 3, 0, 3, 1, 0, 4, 5, 2, 5, 3, 2};
    float* to = reinterpret_cast<float*>(static_cast<char*>(ring) + record->vertexCursor * 0x24);
    for (int i = 0; i < 12; i++, to += 9) {
        to[0] = b[kOrder[i]][0];
        to[1] = b[kOrder[i]][1];
        to[2] = b[kOrder[i]][2];
        to[3] = to[4] = to[5] = 0.0f;
        *reinterpret_cast<ulong*>(to + 6) = 0xff000000UL;
        to[7] = to[8] = 0.0f;
    }
    UnlockAndDraw(screen, 4, 12, 0, 0);
    return 0;
}

long EEWebNullRasterizer::DrawWater(GETerrainMesh& meshObject, ulong screen, bool orthographic)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device || !record->vertexBuffer) {
        return EEWEB_NOT_SUPPORTED;
    }
    if (EEWebVisibleTileRows(mesh) == 0) {
        return 0;
    }
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 1, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    long result = device->vtbl->SetTransform(device, 2, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, 3, matrix);
    GETMMaterialLayout* materials = static_cast<GETMMaterialLayout*>(mesh->waterMaterials.first);
    ulong count = materials ? static_cast<ulong>((static_cast<char*>(mesh->waterMaterials.last) - reinterpret_cast<char*>(materials)) / static_cast<long>(sizeof(GETMMaterialLayout))) : 0;
    typedef std::vector<GETerrainMesh::GETMPolygon*, std::allocator<GETerrainMesh::GETMPolygon*>> PolygonList;
    for (ulong index = 0; index < count; index++) {
        GETMMaterialLayout* material = materials + index;
        if (!material->polygons.last || !material->overlayPolygons.last) {
            continue;
        }
        state->scenePolyCount += static_cast<const ulong*>(material->polygons.last)[-1];
        state->scenePolyCount += static_cast<const ulong*>(material->overlayPolygons.last)[-1];
        SetTexture(reinterpret_cast<GETexture*>(material->texture), screen);
        device->vtbl->SetRenderState(device, 0x88, 0);
        DrawTerrainMaterial(reinterpret_cast<GEViewport*>(state), *reinterpret_cast<PolygonList*>(&material->polygons), screen);
        device->vtbl->SetRenderState(device, 0x88, 1);
        result = DrawTerrainMaterial(reinterpret_cast<GEViewport*>(state), *reinterpret_cast<PolygonList*>(&material->overlayPolygons), screen);
    }
    record->sceneDrawn = 1;
    return result;
}

long EEWebNullRasterizer::DrawTerrainGrid(GETerrainMesh& meshObject, ulong screen, bool orthographic)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device || !record->vertexBuffer || mesh->visibleTileWidth > kColumns - 2 || mesh->visibleTileHeight > kColumns - 2) {
        return EEWEB_NOT_SUPPORTED;
    }
    long result = 0;
    if (EEWebVisibleTileRows(mesh) == 0) {
        return result;
    }
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 1, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 2, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, 3, matrix);
    const ulong x0 = mesh->visibleBottomLeftX, y0 = mesh->visibleBottomLeftY;
    const ulong xEnd = x0 + mesh->visibleTileWidth + 1, yEnd = y0 + mesh->visibleTileHeight + 1;
    for (ulong y = y0; y < yEnd; y++) {
        for (ulong x = x0; x < xEnd; x++) {
            ulong index = (x - x0) * kColumns + (y - y0);
            if (!mesh->pointVisibility[index] || !mesh->pointVisibility[index + kColumns] || !mesh->pointVisibility[index + 1]) {
                continue;
            }
            void* ring = 0;
            LockVB(4, screen, ring);
            if (!ring) {
                UnlockVB(screen);
                return 1;
            }
            const GETMRenderVertexLayout* corner = &mesh->renderVertices[index];
            const GE3DPointLayout* points[4] = {&corner->position, &mesh->renderVertices[index + 1].position, &corner->position, &mesh->renderVertices[index + kColumns].position};
            float* to = reinterpret_cast<float*>(static_cast<char*>(ring) + record->vertexCursor * 0x24);
            for (int i = 0; i < 4; i++, to += 9) {
                to[0] = points[i]->x;
                to[1] = points[i]->y;
                to[2] = points[i]->z;
                to[3] = to[4] = to[5] = 0.0f;
                *reinterpret_cast<ulong*>(to + 6) = 0xffff0000UL;
                to[7] = to[8] = 0.0f;
            }
            result = UnlockAndDraw(screen, 2, 4, 0, 0);
        }
    }
    return result;
}

long EEWebNullRasterizer::DrawTerrainHighlight(GETerrainMesh& meshObject, U2DRectangle<long> const& rectangleObject, ulong screen, bool orthographic, GEColor const& color,
                                               float alpha)
{
    static const float k255 = 255.0f;
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device || !record->vertexBuffer || mesh->visibleTileWidth > kColumns - 2 || mesh->visibleTileHeight > kColumns - 2) {
        return EEWEB_NOT_SUPPORTED;
    }
    const long* rectangle = reinterpret_cast<const long*>(&rectangleObject);
    long across = rectangle[2] - rectangle[1];
    long down = rectangle[0] - rectangle[3];
    long remaining = (down < 0 ? -down + 1 : down + 1) * (across < 0 ? -across + 1 : across + 1);
    if (EEWebVisibleTileRows(mesh) == 0) {
        return 0;
    }
    const GEColorLayout* rgb = reinterpret_cast<const GEColorLayout*>(&color);
    ulong a = static_cast<ulong>(dx7math::RoundToNearest(Scaled(alpha, k255)));
    ulong r = static_cast<ulong>(dx7math::RoundToNearest(Scaled(rgb->r, k255)));
    ulong g = static_cast<ulong>(dx7math::RoundToNearest(Scaled(rgb->g, k255)));
    ulong b = static_cast<ulong>(dx7math::RoundToNearest(Scaled(rgb->b, k255)));
    const ulong packed = ((((a << 8) | r) << 8 | g) << 8) | b;
    SetTexture(0, screen);
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 1, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 2, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, 3, matrix);
    const ulong x0 = mesh->visibleBottomLeftX, y0 = mesh->visibleBottomLeftY;
    const ulong xLimit = x0 + mesh->visibleTileWidth + 1, yLimit = y0 + mesh->visibleTileHeight + 1;
    const ulong kBatch = 0x197, kBatchVertices = 0x98a;
    void* ring = 0;
    LockVB(remaining > static_cast<long>(kBatch) ? kBatchVertices : static_cast<ulong>(remaining) * 6, screen, ring);
    if (!ring) {
        UnlockVB(screen);
        return 1;
    }
    float* to = reinterpret_cast<float*>(static_cast<char*>(ring) + record->vertexCursor * 0x24);
    ulong tiles = 0;
    const GETMTileRowLayout* tileRows = mesh->tileRows;
    for (ulong row = static_cast<ulong>(rectangle[3]); row <= static_cast<ulong>(rectangle[0]) && row != 0xffffffffUL; row++) {
        for (ulong column = static_cast<ulong>(rectangle[1]); column <= static_cast<ulong>(rectangle[2]) && column != 0xffffffffUL; column++) {
            remaining--;
            if (row >= mesh->tileRowCount || !tileRows || column - tileRows[row].firstIndex >= tileRows[row].count || column < x0 || column > xLimit || row < y0 || row > yLimit) {
                continue;
            }
            ulong index = (column - x0) * kColumns + (row - y0);
            if (index + 102 >= kColumns * kColumns) {
                continue;
            }
            if (!mesh->pointVisibility[index] || !mesh->pointVisibility[index + kColumns] || !mesh->pointVisibility[index + 1] || !mesh->pointVisibility[index + kColumns + 1]) {
                continue;
            }
            const GE3DPointLayout* corners[6] = {&mesh->renderVertices[index].position,
                                                 &mesh->renderVertices[index + 1].position,
                                                 &mesh->renderVertices[index + kColumns].position,
                                                 &mesh->renderVertices[index + 1].position,
                                                 &mesh->renderVertices[index + kColumns + 1].position,
                                                 &mesh->renderVertices[index + kColumns].position};
            for (int i = 0; i < 6; i++, to += 9) {
                to[0] = corners[i]->x;
                to[1] = corners[i]->y;
                to[2] = corners[i]->z;
                to[3] = to[4] = to[5] = 0.0f;
                *reinterpret_cast<ulong*>(to + 6) = packed;
                to[7] = to[8] = 0.0f;
            }
            tiles++;
            if (tiles == kBatch && remaining > 0) {
                UnlockAndDraw(screen, 4, kBatchVertices, 0, 0);
                tiles = 0;
                ring = 0;
                LockVB(remaining > static_cast<long>(kBatch) ? kBatchVertices : static_cast<ulong>(remaining) * 6, screen, ring);
                if (!ring) {
                    UnlockVB(screen);
                    return 1;
                }
                to = reinterpret_cast<float*>(static_cast<char*>(ring) + record->vertexCursor * 0x24);
            }
        }
    }
    if (tiles > 0) {
        return UnlockAndDraw(screen, 4, tiles * 6, 0, 0);
    }
    return UnlockVB(screen);
}

long EEWebNullRasterizer::DrawGridLines(GETerrainMesh& meshObject, std::vector<std::pair<U2DPoint<long>, U2DPoint<long>>, std::allocator<std::pair<U2DPoint<long>, U2DPoint<long>>>>& linesObject,
                                        GETexture* texture, float v, ulong screen, bool orthographic)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !state || !device || !record->vertexBuffer || mesh->visibleTileWidth > kColumns - 2 || mesh->visibleTileHeight > kColumns - 2) {
        return EEWEB_NOT_SUPPORTED;
    }
    const StdVectorLayout* list = reinterpret_cast<const StdVectorLayout*>(&linesObject);
    const char* first = static_cast<const char*>(list->first);
    ulong count = first ? static_cast<ulong>((static_cast<const char*>(list->last) - first) >> 4) : 0;
    ulong vertices = count * 2;
    const float vCoordinate = Add(v, 1.0f);
    if (EEWebVisibleTileRows(mesh) == 0) {
        return 0;
    }
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 1, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, 2, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, 3, matrix);
    SetTexture(texture, screen);
    float material[17] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    device->vtbl->SetMaterial(device, material);
    SetShadingColorSource(0, screen);
    const ulong x0 = mesh->visibleBottomLeftX, y0 = mesh->visibleBottomLeftY;
    const ulong xLimit = x0 + mesh->visibleTileWidth + 1, yLimit = y0 + mesh->visibleTileHeight + 1;
    if (vertices > 0x98a) {
        vertices = 0x98a;
        count = 0x4c5;
    }
    void* ring = 0;
    LockVB(vertices, screen, ring);
    if (!ring) {
        UnlockVB(screen);
        return 1;
    }
    float* to = reinterpret_cast<float*>(static_cast<char*>(ring) + record->vertexCursor * 0x24);
    ulong written = 0;
    for (ulong i = 0; i < count; i++) {
        const long* line = reinterpret_cast<const long*>(first + i * 16);
        const ulong x[2] = {static_cast<ulong>(line[0]), static_cast<ulong>(line[2])};
        const ulong y[2] = {static_cast<ulong>(line[1]), static_cast<ulong>(line[3])};
        bool drawable = true;
        for (int end = 0; end < 2 && drawable; end++) {
            drawable = x[end] >= x0 && x[end] < xLimit && y[end] >= y0 && y[end] < yLimit && y[end] < mesh->pointRowCount && mesh->pointRows[y[end]].points &&
                       x[end] - mesh->pointRows[y[end]].firstIndex < mesh->pointRows[y[end]].count && mesh->pointVisibility[(x[end] - x0) * kColumns + (y[end] - y0)];
        }
        if (!drawable) {
            continue;
        }
        for (int end = 0; end < 2; end++, to += 9) {
            const GETMPointRowLayout& row = mesh->pointRows[y[end]];
            float height = row.points[x[end] - row.firstIndex].z;
            if (!(height >= 0.0f)) {
                height = 0.0f;
            }
            float point[3] = {static_cast<float>(x[end]), static_cast<float>(y[end]), height};
            float transformed[3];
            EEWebTransformationApplyPoint(&state->viewport.cameraInverse, point, transformed);
            to[0] = transformed[0];
            to[1] = transformed[1];
            to[2] = transformed[2];
            to[3] = to[4] = to[5] = 0.0f;
            *reinterpret_cast<ulong*>(to + 6) = 0xffff0000UL;
            to[7] = 0.0f;
            to[8] = end == 0 ? v : vCoordinate;
        }
        written++;
    }
    long status;
    if (written > 0) {
        status = UnlockAndDraw(screen, 2, written * 2, 0, 0);
    } else {
        status = UnlockVB(screen);
    }
    record->vertexCursor += vertices - written * 2;
    return status;
}

namespace {

struct LightEntry {
    float color[3];
    float direction[3];
};

// (dz * nz + dy * ny) + nx * dx with the sums in extended precision.  `positive` is the original's comparison of that extended value
// with zero (above it, and ordered); the result is the float the original stores and multiplies by.
float DotWithNormal(const float* direction, const float* normal, bool* positive)
{
    static const float kZero = 0.0f;
    float stored;
    unsigned short status;
    __asm__ volatile("flds %2\n\tfmuls %3\n\tflds %4\n\tfmuls %5\n\tfaddp\n\tflds %6\n\tfmuls %7\n\tfaddp\n\tfsts %0\n\tfcomps %8\n\tfnstsw %1"
                     : "=m"(stored), "=a"(status)
                     : "m"(direction[2]), "m"(normal[2]), "m"(direction[1]), "m"(normal[1]), "m"(normal[0]), "m"(direction[0]), "m"(kZero)
                     : "st");
    *positive = (status & 0x4100) == 0;
    return stored;
}

} // namespace

void EEWebNullRasterizer::Light(GETerrainMesh& meshObject, ulong screen)
{
    if (Note(EEWebCheckTerrainMesh(&meshObject)) != EEWEB_LAYOUT_OK) {
        return;
    }
    GETerrainMeshLayout* mesh = reinterpret_cast<GETerrainMeshLayout*>(&meshObject);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!record || !device || mesh->visibleTileWidth > kColumns - 2 || mesh->visibleTileHeight > kColumns - 2 || EEWebVisibleTileRows(mesh) == 0) {
        return;
    }
    // the directional lights the device holds, as {colour, minus the direction}
    LightEntry entries[8];
    ulong count = 0;
    for (ulong id = 0; id < 8; id++) {
        if (!LightTaken(id)) {
            continue;
        }
        float light[26] = {};
        if (device->vtbl->GetLight(device, id, light) != 0) {
            continue; // the original goes on with whatever the buffer held
        }
        if (reinterpret_cast<const ulong*>(light)[0] != 3) {
            continue;
        }
        const float* diffuse = light + 1;
        const float* direction = light + 16;
        bool colored = diffuse[0] < 0.0f || diffuse[0] > 0.0f || diffuse[1] < 0.0f || diffuse[1] > 0.0f || diffuse[2] < 0.0f || diffuse[2] > 0.0f;
        if (!colored) {
            continue;
        }
        LightEntry& entry = entries[count++];
        entry.color[0] = diffuse[0];
        entry.color[1] = diffuse[1];
        entry.color[2] = diffuse[2];
        entry.direction[0] = -direction[0];
        entry.direction[1] = -direction[1];
        entry.direction[2] = -direction[2];
    }
    // the colour of the sun: the ambient colour plus every light that shines from above
    const GEColorLayout ambient = Base()->computedAmbientColor;
    float sun[3] = {ambient.r, ambient.g, ambient.b};
    for (ulong i = 0; i < count; i++) {
        const float vertical = entries[i].direction[2];
        if (vertical > 0.0f) {
            sun[0] = AddProduct(sun[0], entries[i].color[0], vertical);
            sun[1] = AddProduct(sun[1], entries[i].color[1], vertical);
            sun[2] = AddProduct(sun[2], entries[i].color[2], vertical);
        }
    }
    EEWebColorNormalize(sun);
    const ulong x0 = mesh->visibleBottomLeftX, y0 = mesh->visibleBottomLeftY;
    const ulong xEnd = x0 + mesh->visibleTileWidth + 2, yEnd = y0 + mesh->visibleTileHeight + 2;
    for (ulong y = y0; y < yEnd; y++) {
        for (ulong x = x0; x < xEnd; x++) {
            const ulong index = (x - x0) * kColumns + (y - y0);
            if (!mesh->pointVisibility[index] || y >= mesh->pointRowCount || !mesh->pointRows[y].points || x - mesh->pointRows[y].firstIndex >= mesh->pointRows[y].count) {
                continue;
            }
            const GETMPointLayout& point = mesh->pointRows[y].points[x - mesh->pointRows[y].firstIndex];
            if (!point.normal) {
                continue;
            }
            const float* normal = &point.normal->x;
            float color[3] = {ambient.r, ambient.g, ambient.b};
            for (ulong i = 0; i < count; i++) {
                bool positive;
                const float dot = DotWithNormal(entries[i].direction, normal, &positive);
                if (positive) {
                    color[0] = AddProduct(color[0], entries[i].color[0], dot);
                    color[1] = AddProduct(color[1], entries[i].color[1], dot);
                    color[2] = AddProduct(color[2], entries[i].color[2], dot);
                }
            }
            EEWebColorNormalize(color);
            const GEColorLayout& table = mesh->colorTable[point.colorIndex];
            GEColorLayout& lit = mesh->renderVertices[index].color;
            lit.r = Mul(table.r, color[0]);
            lit.g = Mul(table.g, color[1]);
            lit.b = Mul(table.b, color[2]);
            if (mesh->waterPointVisibility[index]) {
                GEColorLayout& water = mesh->waterRenderVertices[index].color;
                water.r = Mul(mesh->waterColor.r, sun[0]);
                water.g = Mul(mesh->waterColor.g, sun[1]);
                water.b = Mul(mesh->waterColor.b, sun[2]);
            }
        }
    }
}
