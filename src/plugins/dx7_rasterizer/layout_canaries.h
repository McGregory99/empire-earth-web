// Layout canaries (task P2.5): checks that run where the plugin first touches an engine object.
//
// The plugin reads private fields of engine objects by offset (src/engine_abi/ge_layouts.h).  If an
// assumption about a layout is wrong, or the pointer is not what the layout says, reading further would
// return garbage silently.  Each check below looks at the object the way the plugin is about to read it
// and refuses it before any field is trusted:
//
//   * the pointer is non-null, 4-byte aligned, and the whole layout is readable memory,
//   * where the class has a vptr, its MSVC RTTI names the expected class (or a class derived from it),
//   * the fields the plugin will use satisfy invariants that hold for every real engine object
//     (flag bytes are 0 or 1, counts match their arrays, floats are finite, string length <= capacity),
//   * every pointer the plugin will follow is non-null and readable for the span it will read.
//
// A check returns EEWEB_LAYOUT_OK (0) or the first EEWEB_LAYOUT_* code that failed (rasterizer_abi.h),
// and on failure writes one line to the debugger (OutputDebugStringA).  It never reads past the first
// failure.  Callers must treat any non-zero result as "do not read this object at all".
#ifndef EEWEB_LAYOUT_CANARIES_H
#define EEWEB_LAYOUT_CANARIES_H

#include "rasterizer_abi.h"

long EEWebCheckTexture(const void *object);
// True when `object` is readable and its vftable's RTTI names `mangledClassName` (e.g. ".?AVDX7Surface@@"): a guard before a dynamic_cast.
bool EEWebObjectIsClass(const void *object, const char *mangledClassName);
long EEWebCheckMaterial(const void *object);
long EEWebCheckModel(const void *object);
long EEWebCheckLight(const void *object);
long EEWebCheckTerrainMesh(const void *object);
long EEWebCheckViewport(const void *object);
long EEWebCheckText(const void *object);

#endif
