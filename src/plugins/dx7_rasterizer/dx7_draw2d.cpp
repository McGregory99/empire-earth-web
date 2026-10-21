// The 2D primitive draws (task P2.6.7b): Draw(GEBitmap *, screen) (slot 21), Draw(GELine *, count, screen) (20), Draw(GEPoint *,
// count, screen) (18) and Draw(GERhombus *, screen) (17).  Each one holds the rasterizer's critical section, sets the 2D projection
// (Set2DMode), binds the texture of the screen (or none), copies the objects' vertices into the screen's ring vertex buffer at the
// record's cursor (LockVB/UnlockVB, P2.6.6), draws them with DrawPrimitiveVB or DrawIndexedPrimitiveVB and moves the cursor on.
//
// tests/dx7_rasterizer/draw2d_checks.h compares both builds on 42 recorded steps (every fake Direct3D call with its arguments, the
// engine and record words that changed, the vertices written into the ring and the texture coordinates left in the arrays).  What
// the original does and this build keeps:
//   - Draw(GEBitmap): the texture is bitmap->textures[screen]; with none the draw answers 9 after the projection set-up and
//     nothing else happens.  With a texture whose frame scale (0x4c) is not 1 (a scale that is not a number counts as 1) the four
//     texture coordinates of the bitmap's vertex array are rewritten first: u = u * scale + frameU, v = frameV + v * scale
//     (extended precision until the store).  The texture is bound (virtual SetTexture), 4 vertices of 0x24 bytes are locked and
//     copied, the ring is unlocked and an indexed triangle list {0,1,2,2,1,3} of 4 vertices and 6 indices is drawn from the cursor,
//     then the cursor grows by 4.  The answer is the unlock's status (0, or 1 when Unlock failed, which also records the error);
//   - Draw(GELine) / Draw(GEPoint): `count` objects of 0xc bytes (vertex array at 0, texture table at 4); the texture, even for
//     the objects after the first, is the first object's textures[screen] (or none, which unbinds); a scaled texture rewrites the
//     coordinate of vertex 0 of every object's array (vertex 0 only); each line copies 2 vertices (0x48 bytes) and then adds 0.001
//     to x and y of both, each point 1 vertex (0x24 bytes); the draw is a line list (type 2) of count * 2 vertices or a point list
//     (type 1) of count vertices; with count 0 the ring is still locked, unlocked and drawn with 0 vertices;
//   - Draw(GERhombus): no texture (SetTexture(0)), the 4 vertices of its array, the same indexed quad as the bitmap;
//   - a draw retries while Direct3D answers DDERR_WASSTILLDRAWING (0x8876021c) and ignores any other error of the draw call.
// Differs from the original, deliberately (the original dereferences them): a null object, a screen that does not exist or has no
// device or vertex buffer, an object without a vertex array or texture table, an array with fewer vertices than the draw reads or
// that is not the 0x24-byte colour kind, a screen index beyond the object's table and a lock that gave no pointer are refused
// without a draw (EEWEB_NOT_SUPPORTED, or the old test constants for a null bitmap and a null line).
#include "null_rasterizer.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
}

typedef unsigned long ulong;

namespace {

const long kBusy = static_cast<long>(0x8876021cUL); // DDERR_WASSTILLDRAWING
const ulong kRingVertices = 0x98a;
const ulong kVertexSize = 0x24;
const unsigned short kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

struct Coordinate {
    float u, v;
};

// The original's x87 code: the product and the sum stay in extended precision, only the store rounds.
inline float ScaleAdd(float value, float scale, float offset)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0" : "=m"(result) : "m"(value), "m"(scale), "m"(offset) : "st");
    return result;
}

inline float Add(float first, float second)
{
    float result;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(result) : "m"(first), "m"(second) : "st");
    return result;
}

// "scale != 1.0" the way the original compares: a scale that is not a number is taken as equal.
inline bool IsRescaled(float scale)
{
    return scale < 1.0f || scale > 1.0f;
}

char* DataOf(GEVertexArray* array)
{
    return *reinterpret_cast<char**>(reinterpret_cast<char*>(array) + 4);
}

// u = u * scale + frameU, v = frameV + v * scale, for one vertex.
void Rescale(GEVertexArray* array, ulong vertex, const GETextureLayout* texture)
{
    Coordinate coordinate = {0.0f, 0.0f};
    array->GetTextureCoordinate(reinterpret_cast<GETextureCoordinate&>(coordinate), vertex);
    coordinate.u = ScaleAdd(coordinate.u, texture->frameUVSize, texture->frameTopLeft.u);
    coordinate.v = ScaleAdd(coordinate.v, texture->frameUVSize, texture->frameTopLeft.v);
    array->SetTextureCoordinate(reinterpret_cast<const GETextureCoordinate&>(coordinate), vertex);
}

// A vertex array the draws can read `vertices` vertices of: the colour kind (0x24 bytes per vertex) with enough of them.
bool CanRead(GEVertexArray* array, ulong vertices)
{
    return array && array->GetType() == 1 && array->GetSize() >= vertices;
}

} // namespace

long EEWebNullRasterizer::UnlockAndDraw(ulong screen, ulong primitive, ulong vertices, const unsigned short* indices, ulong indexCount)
{
    long unlocked = UnlockVB(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    long error;
    do {
        IDirect3DDevice7* device = Device(screen);
        IDirect3DVertexBuffer7* buffer = static_cast<IDirect3DVertexBuffer7*>(record->vertexBuffer);
        if (indices) {
            error = device->vtbl->DrawIndexedPrimitiveVB(device, primitive, buffer, record->vertexCursor, vertices, indices, indexCount, 0);
        } else {
            error = device->vtbl->DrawPrimitiveVB(device, primitive, buffer, record->vertexCursor, vertices, 0);
        }
        if (error == kBusy) {
            Sleep(0);
        }
    } while (error == kBusy);
    record->vertexCursor += vertices;
    return unlocked;
}

long EEWebNullRasterizer::Draw(GEBitmap* bitmap, ulong screen)
{
    if (!bitmap) {
        return EEWEB_DRAW_BITMAP_BASE + static_cast<long>(screen);
    }
    GEBitmapLayout* object = reinterpret_cast<GEBitmapLayout*>(bitmap);
    DX7ScreenRecordLayout* record = Record(screen);
    GEVertexArray* array = reinterpret_cast<GEVertexArray*>(object->vertexArray);
    if (!record || !Device(screen) || !record->vertexBuffer || !object->textures || screen >= object->screenCount || !CanRead(array, 4)) {
        return EEWEB_NOT_SUPPORTED;
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set2DMode(screen);
    GETextureLayout* texture = object->textures[screen];
    long result = 9;
    if (texture) {
        if (IsRescaled(texture->frameUVSize)) {
            for (ulong i = 0; i < 4; i++) {
                Rescale(array, i, texture);
            }
        }
        SetTexture(reinterpret_cast<GETexture*>(texture), screen);
        void* ring = 0;
        LockVB(4, screen, ring);
        if (!ring) {
            UnlockVB(screen);
            result = 1;
        } else {
            __builtin_memcpy(static_cast<char*>(ring) + record->vertexCursor * kVertexSize, DataOf(array), 0x90);
            result = UnlockAndDraw(screen, 4, 4, kQuadIndices, 6);
        }
    }
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}

long EEWebNullRasterizer::Draw(GELine* lines, ulong count, ulong screen)
{
    if (!lines) {
        return EEWEB_DRAW_LINE_BASE + static_cast<long>(count + screen);
    }
    GELineLayout* objects = reinterpret_cast<GELineLayout*>(lines);
    DX7ScreenRecordLayout* record = Record(screen);
    if (!record || !Device(screen) || !record->vertexBuffer || !objects->textures || screen >= objects->screenCount || count > kRingVertices / 2) {
        return EEWEB_NOT_SUPPORTED;
    }
    for (ulong i = 0; i < count; i++) {
        if (!CanRead(reinterpret_cast<GEVertexArray*>(objects[i].vertexArray), 2)) {
            return EEWEB_NOT_SUPPORTED;
        }
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set2DMode(screen);
    GETextureLayout* texture = objects->textures[screen];
    SetTexture(reinterpret_cast<GETexture*>(texture), screen);
    void* ring = 0;
    LockVB(count * 2, screen, ring);
    long result = 1;
    if (!ring) {
        UnlockVB(screen);
    } else {
        char* to = static_cast<char*>(ring) + record->vertexCursor * kVertexSize;
        for (ulong i = 0; i < count; i++) {
            GEVertexArray* array = reinterpret_cast<GEVertexArray*>(objects[i].vertexArray);
            if (texture && IsRescaled(texture->frameUVSize)) {
                Rescale(array, 0, texture);
            }
            __builtin_memcpy(to, DataOf(array), 0x48);
            float* first = reinterpret_cast<float*>(to);
            float* second = reinterpret_cast<float*>(to + kVertexSize);
            first[0] = Add(0.001f, first[0]);
            first[1] = Add(0.001f, first[1]);
            second[0] = Add(0.001f, second[0]);
            second[1] = Add(0.001f, second[1]);
            to += 2 * kVertexSize;
        }
        result = UnlockAndDraw(screen, 2, count * 2, 0, 0);
    }
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}

long EEWebNullRasterizer::Draw(GEPoint* points, ulong count, ulong screen)
{
    if (!points) {
        return EEWEB_NOT_SUPPORTED;
    }
    GEPointLayout* objects = reinterpret_cast<GEPointLayout*>(points);
    DX7ScreenRecordLayout* record = Record(screen);
    if (!record || !Device(screen) || !record->vertexBuffer || !objects->textures || screen >= objects->screenCount || count > kRingVertices) {
        return EEWEB_NOT_SUPPORTED;
    }
    for (ulong i = 0; i < count; i++) {
        if (!CanRead(reinterpret_cast<GEVertexArray*>(objects[i].vertexArray), 1)) {
            return EEWEB_NOT_SUPPORTED;
        }
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set2DMode(screen);
    GETextureLayout* texture = objects->textures[screen];
    SetTexture(reinterpret_cast<GETexture*>(texture), screen);
    void* ring = 0;
    LockVB(count, screen, ring);
    long result = 1;
    if (!ring) {
        UnlockVB(screen);
    } else {
        char* to = static_cast<char*>(ring) + record->vertexCursor * kVertexSize;
        for (ulong i = 0; i < count; i++) {
            GEVertexArray* array = reinterpret_cast<GEVertexArray*>(objects[i].vertexArray);
            if (texture && IsRescaled(texture->frameUVSize)) {
                Rescale(array, 0, texture);
            }
            __builtin_memcpy(to, DataOf(array), kVertexSize);
            to += kVertexSize;
        }
        result = UnlockAndDraw(screen, 1, count, 0, 0);
    }
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}

long EEWebNullRasterizer::Draw(GERhombus* rhombus, ulong screen)
{
    if (!rhombus) {
        return EEWEB_NOT_SUPPORTED;
    }
    GERhombusLayout* object = reinterpret_cast<GERhombusLayout*>(rhombus);
    DX7ScreenRecordLayout* record = Record(screen);
    GEVertexArray* array = reinterpret_cast<GEVertexArray*>(object->vertexArray);
    if (!record || !Device(screen) || !record->vertexBuffer || !CanRead(array, 4)) {
        return EEWEB_NOT_SUPPORTED;
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set2DMode(screen);
    SetTexture(0, screen);
    void* ring = 0;
    LockVB(4, screen, ring);
    long result = 1;
    if (!ring) {
        UnlockVB(screen);
    } else {
        __builtin_memcpy(static_cast<char*>(ring) + record->vertexCursor * kVertexSize, DataOf(array), 0x90);
        result = UnlockAndDraw(screen, 4, 4, kQuadIndices, 6);
    }
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}
