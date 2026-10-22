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

#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" double sin(double);

typedef unsigned long ulong;

extern "C" {
float __attribute__((cdecl)) EEWebUMTan(float) __asm__("?UMTan@@YAMM@Z");
float __attribute__((cdecl)) EEWebUMArcTan(float) __asm__("?UMArcTan@@YAMM@Z");
float __attribute__((cdecl)) EEWebUMCos(float) __asm__("?UMCos@@YAMM@Z");
ulong __attribute__((thiscall)) EEWebVisibleTileRows(const void* mesh) __asm__("?GetNumVisibleTileRows@GETerrainMesh@@QBEKXZ");
void __attribute__((thiscall)) EEWebVectorCopy(void* self, const void* other) __asm__("??0GEVector@@QAE@ABV0@@Z");
void __attribute__((thiscall)) EEWebVectorAssign(void* self, const void* other) __asm__("??4GEVector@@QAEAAV0@ABV0@@Z");
void __attribute__((thiscall)) EEWebVectorNormalize(void* self) __asm__("?Normalize@GEVector@@QAEXXZ");
unsigned char __attribute__((thiscall)) EEWebPlaneBelow(const void* plane, const void* point) __asm__("?IsPointOnOrBelowPlane@GE3DPlane@@QBE_NABVGE3DPoint@@@Z");

// out = {0, cos(pi/2 - a), cos(a)} with a = arctan(halfHeight * tan(fov / 2) / (halfWidth - 1)): the original's sequence, whose
// intermediate values stay in the x87 registers (the engine's functions leave their results there in extended precision).
// cdecl: viewport, the constants {0.5f, 1.0f, pi/2}, out, and the addresses of UMTan, UMArcTan and UMCos.
void __attribute__((cdecl)) EEWebFrustumNormal(const void* viewport, const float* constants, float* out, void* const* functions);
}

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
