// The ring vertex buffer of the T&L class (task P2.6.6): LockVB and UnlockVB, the protected helpers every draw uses to write its
// vertices.  The buffer (created by CreateVertexBuffers, P2.6.5) holds 0x98a vertices of 0x24 bytes and is write-only; the screen
// record's cursor (0x28) counts the vertices already written since the last wrap.  The draw slots (P2.6.7+) advance the cursor by
// the vertices they drew; these helpers only decide how to lock.
//
// tests/dx7_rasterizer/ring_checks.h compares both builds on the scripted vertex buffer (the flags and the pointer of every Lock,
// every retry).  What the original does and this build keeps:
//   - LockVB(count, screen, vertices): when fewer than `count` vertices are left in the ring (unsigned arithmetic, so a cursor
//     past the end counts as plenty left) the cursor goes back to 0 and the buffer is locked DISCARDCONTENTS | WRITEONLY (0x2020),
//     otherwise NOOVERWRITE | WRITEONLY (0x1020);
//   - the lock is retried (yielding the processor) while Direct3D answers "surface busy" or while it succeeded without giving a
//     pointer; any other error ends the wait, and LockVB still answers 0: the caller finds out from the pointer, which the lock
//     left as it was;
//   - UnlockVB retries while Direct3D answers "surface busy", records any other error with the engine and answers 1 for it.
// The caller adds cursor * 0x24 to the pointer itself.
//
// Where the original dereferences a screen that has no vertex buffer this build answers 1 and hands back a null pointer.
#include "null_rasterizer.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
}

typedef unsigned long ulong;

namespace {

const ulong kRingVertices = 0x98a;
const long kSurfaceBusy = static_cast<long>(0x887601AEUL); // DDERR_SURFACEBUSY
const ulong kDiscard = 0x2020;                              // DDLOCK_DISCARDCONTENTS | DDLOCK_WRITEONLY
const ulong kNoOverwrite = 0x1020;                          // DDLOCK_NOOVERWRITE | DDLOCK_WRITEONLY

IDirect3DVertexBuffer7* BufferOf(const DX7ScreenRecordLayout* record)
{
    return record ? static_cast<IDirect3DVertexBuffer7*>(record->vertexBuffer) : 0;
}

} // namespace

long EEWebNullRasterizer::LockVB(ulong count, ulong screen, void*& vertices)
{
    DX7ScreenRecordLayout* record = Record(screen);
    if (!BufferOf(record)) {
        vertices = 0;
        return 1;
    }
    ulong flags = kNoOverwrite;
    if (kRingVertices - record->vertexCursor < count) {
        record->vertexCursor = 0;
        flags = kDiscard;
    }
    long error;
    do {
        IDirect3DVertexBuffer7* buffer = BufferOf(record);
        error = buffer->vtbl->Lock(buffer, flags, &vertices, 0);
        if (vertices == 0 || error == kSurfaceBusy) {
            Sleep(0);
        }
    } while ((vertices == 0 && error == 0) || error == kSurfaceBusy);
    return 0;
}

long EEWebNullRasterizer::UnlockVB(ulong screen)
{
    DX7ScreenRecordLayout* record = Record(screen);
    if (!BufferOf(record)) {
        return 1;
    }
    long error;
    for (;;) {
        IDirect3DVertexBuffer7* buffer = BufferOf(record);
        error = buffer->vtbl->Unlock(buffer);
        if (error != kSurfaceBusy) {
            break;
        }
        Sleep(0);
    }
    if (error != 0) {
        ESRecordNativeError(static_cast<ulong>(error));
    }
    return error != 0;
}
