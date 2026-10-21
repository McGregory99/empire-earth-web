// The indexed polygon draw (task P2.6.7c): Draw(GEPolygonArray &, polygons, GEVertexArray &, vertices, GETexture *, screen, bool),
// slot 13.  It holds the rasterizer's critical section, sets the 3D projection (Set3DMode), sends the identity as the world
// transform and switches NORMALIZENORMALS (render state 0x88) on, binds the texture, copies the first `vertices` vertices of the
// array into the ring (a colour array, 0x24 bytes per vertex, as it is; a standard array, 0x20 bytes, with the colour 0xffffffff
// added at 0x18), draws the polygon array's indices as a triangle list of `polygons` * 3 indices and moves the ring's cursor on by
// the vertices; the screen's scene polygon count (engine state 0x330) grows by `polygons`.  The answer is the unlock's status.
// The last argument (a flag) is not used by the original either.
//
// tests/dx7_rasterizer/poly_checks.h compares both builds on 17 recorded steps.  Differs from the original, deliberately (the
// original reads past the arrays): a screen without record, device, vertex buffer or engine state, a polygon array with fewer
// polygons or a vertex array with fewer vertices than the draw reads, an array of another kind, more vertices than the ring holds
// and a lock that gave no pointer are refused without a draw (EEWEB_NOT_SUPPORTED, or 1 for the lock).
#include "null_rasterizer.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

typedef unsigned long ulong;

namespace {

const ulong kRingVertices = 0x98a;
const ulong kVertexSize = 0x24;
const ulong kWorldTransform = 1;
const ulong kNormalizeNormals = 0x88;

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
