// The indexed polygon draw (task P2.6.7c): Draw(GEPolygonArray &, polygons, GEVertexArray &, vertices, GETexture *, screen, bool),
// slot 13.  It holds the rasterizer's critical section, sets the 3D projection (Set3DMode), sends the identity as the world
// transform and switches NORMALIZENORMALS (render state 0x88) on, binds the texture, copies the first `vertices` vertices of the
// array into the ring (a colour array, 0x24 bytes per vertex, as it is; a standard array, 0x20 bytes, with the colour 0xffffffff
// added at 0x18), draws the polygon array's indices as a triangle list of `polygons` * 3 indices and moves the ring's cursor on by
// the vertices; the screen's scene polygon count (engine state 0x330) grows by `polygons`.  The answer is the unlock's status.
// The last argument (a flag) is not used by the original either.
//
// Draw3DLine(points, texture, start, screen, orthographic) (slot 22) draws a polyline as a line list.  It takes no critical section
// and does not call Set3DMode: it sends the identity as world, the Y/Z swap as view (the engine's up axis is Z) and the screen
// state's orthographic (flag set) or perspective projection as projection, binds the texture, sends a white material (diffuse and
// ambient 1, specular, emissive and power 0) and calls SetShadingColorSource(0).  The points are 16-byte elements of a MSVC 6 vector
// (a point and the length of the segment starting there).  Every segment becomes two vertices at the points transformed by the
// state's camera inverse (0xf0), normal 0, colour 0xffff0000, u 0 and v from `start`, which grows by the length of the segment after
// the first vertex and is wrapped after the second: above -1 and above 1 it loses its integer part (floor by the rounded value), at
// -1 or below (or not a number) it becomes ceil(v) - v.  A line of more than 0x4c6 points is cut to the ring (0x98a vertices);
// one point draws nothing (0 vertices).  tests/dx7_rasterizer/line3d_checks.h compares both builds on 19 recorded steps.  Differs
// deliberately: a vector with no point (the original reads past it), a null vector and a screen without record, device, vertex
// buffer or state are refused (EEWEB_NOT_SUPPORTED); a lock without a pointer answers 1.
//
// tests/dx7_rasterizer/poly_checks.h compares both builds on 17 recorded steps.  Differs from the original, deliberately (the
// original reads past the arrays): a screen without record, device, vertex buffer or engine state, a polygon array with fewer
// polygons or a vertex array with fewer vertices than the draw reads, an array of another kind, more vertices than the ring holds
// and a lock that gave no pointer are refused without a draw (EEWEB_NOT_SUPPORTED, or 1 for the lock).
#include "null_rasterizer.h"

#include "dx7_math.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

typedef unsigned long ulong;

namespace {

const ulong kRingVertices = 0x98a;
const ulong kVertexSize = 0x24;
const ulong kWorldTransform = 1;
const ulong kViewTransform = 2;
const ulong kProjectionTransform = 3;
const ulong kNormalizeNormals = 0x88;

using dx7math::FloatMinusInt;
using dx7math::IntMinusFloat;

// The sum of two floats in extended precision, stored as a float.
inline float AddFloats(float first, float second)
{
    float result;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(result) : "m"(first), "m"(second) : "st");
    return result;
}

using dx7math::RoundToNearest;

// v wrapped the way the original does it.
float WrapCoordinate(float v)
{
    if (v > -1.0f) {
        if (v > 1.0f) {
            long integer = RoundToNearest(v);
            if (static_cast<double>(integer) > static_cast<double>(v)) {
                --integer;
            }
            return FloatMinusInt(v, integer);
        }
        return v;
    }
    long integer = RoundToNearest(v);
    if (static_cast<double>(integer) < static_cast<double>(v)) {
        ++integer;
    }
    return IntMinusFloat(integer, v);
}

} // namespace

long EEWebNullRasterizer::Draw(GEPolygonArray& polygons, ulong polygonCount, GEVertexArray& vertices, ulong vertexCount, GETexture* texture, ulong screen, bool)
{
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    const unsigned short* indices = *reinterpret_cast<const unsigned short* const*>(reinterpret_cast<const char*>(&polygons) + 4);
    ulong kind = vertices.GetType();
    if (!record || !state || !device || !record->vertexBuffer || !indices || vertexCount > kRingVertices || polygons.GetSize() < polygonCount ||
        vertices.GetSize() < vertexCount || (kind != 0 && kind != 1)) {
        return EEWEB_NOT_SUPPORTED;
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set3DMode(screen);
    GETransformationLayout identity;
    EEWebTransformationConstruct(&identity);
    float matrix[16];
    ConvertToD3DMatrix(identity, matrix);
    device->vtbl->SetTransform(device, kWorldTransform, matrix);
    device->vtbl->SetRenderState(device, kNormalizeNormals, 1);
    SetTexture(texture, screen);
    void* ring = 0;
    LockVB(vertexCount, screen, ring);
    long result = 1;
    if (!ring) {
        UnlockVB(screen);
    } else {
        char* to = static_cast<char*>(ring) + record->vertexCursor * kVertexSize;
        const char* from = *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(&vertices) + 4);
        if (kind == 1) {
            __builtin_memcpy(to, from, vertexCount * kVertexSize);
        } else {
            for (ulong i = 0; i < vertexCount; i++) {
                const unsigned long* in = reinterpret_cast<const unsigned long*>(from + i * 0x20);
                unsigned long* out = reinterpret_cast<unsigned long*>(to + i * kVertexSize);
                for (int word = 0; word < 6; word++) {
                    out[word] = in[word];
                }
                out[6] = 0xffffffffUL;
                out[7] = in[6];
                out[8] = in[7];
            }
        }
        result = UnlockAndDraw(screen, 4, vertexCount, indices, polygonCount * 3);
        state->scenePolyCount += polygonCount;
    }
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}

long EEWebNullRasterizer::Draw3DLine(std::vector<std::pair<GE3DPoint, float>, std::allocator<std::pair<GE3DPoint, float>>>& points, GETexture* texture, float start,
                                     ulong screen, bool orthographic)
{
    DX7ScreenRecordLayout* record = Record(screen);
    GERScreenStateLayout* state = State(screen);
    IDirect3DDevice7* device = Device(screen);
    const char* first = *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(&points) + 4);
    const char* last = *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(&points) + 8);
    if (!record || !state || !device || !record->vertexBuffer || !first || last <= first) {
        return EEWEB_NOT_SUPPORTED;
    }
    ulong count = static_cast<ulong>(last - first) >> 4;
    GETransformationLayout transformation;
    EEWebTransformationConstruct(&transformation);
    float matrix[16];
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, kWorldTransform, matrix);
    transformation.m[5] = 0.0f;
    transformation.m[6] = 1.0f;
    transformation.m[9] = 1.0f;
    transformation.m[10] = 0.0f;
    ConvertToD3DMatrix(transformation, matrix);
    device->vtbl->SetTransform(device, kViewTransform, matrix);
    ConvertToD3DMatrix(orthographic ? state->viewport.orthographicProjection : state->viewport.perspectiveProjection, matrix);
    device->vtbl->SetTransform(device, kProjectionTransform, matrix);
    SetTexture(texture, screen);
    float material[17] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    device->vtbl->SetMaterial(device, material);
    SetShadingColorSource(0, screen);
    ulong vertices = count * 2 - 2;
    if (vertices > kRingVertices) {
        vertices = kRingVertices;
        count = 0x4c6;
    }
    void* ring = 0;
    LockVB(vertices, screen, ring);
    if (!ring) {
        UnlockVB(screen);
        return 1;
    }
    char* to = static_cast<char*>(ring) + record->vertexCursor * kVertexSize;
    float v = start;
    for (ulong i = 1; i < count; i++) {
        for (int end = 0; end < 2; end++) {
            const char* point = first + (i - 1 + end) * 16;
            float transformed[3];
            EEWebTransformationApplyPoint(&state->viewport.cameraInverse, point, transformed);
            float* vertex = reinterpret_cast<float*>(to);
            vertex[0] = transformed[0];
            vertex[1] = transformed[1];
            vertex[2] = transformed[2];
            vertex[3] = vertex[4] = vertex[5] = 0.0f;
            *reinterpret_cast<unsigned long*>(to + 0x18) = 0xffff0000UL;
            vertex[7] = 0.0f;
            vertex[8] = v;
            to += kVertexSize;
            if (end == 0) {
                v = AddFloats(v, *reinterpret_cast<const float*>(first + i * 16 - 4));
            }
        }
        v = WrapCoordinate(v);
    }
    return UnlockAndDraw(screen, 2, vertices, 0, 0);
}
