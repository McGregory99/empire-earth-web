/*
 * The shape of the C++ object this plugin hands to the engine, as a contract that both the plugin
 * (null_rasterizer.h) and its test harness (tests/dx7_rasterizer/harness.c) include.  Plain C so that
 * the mingw-built harness can use it.
 *
 * The object is a null rasterizer with the vtable of the original's DX7TnLRasterizer: 82 slots, at the
 * positions the generated ../../engine_abi/ge_interfaces.h declares (classes GERasterizer, 78 slots, and
 * DX7RasterizerAbi, 82) and ../../engine_abi/ge_slots.h numbers.  The engine calls rasterizer objects
 * through such positional vtables built by MSVC 6, so a clang-built plugin must reproduce the Microsoft
 * layout rules; the harness calls the slots below as thiscall methods through the table, which pins them
 * on the real class:
 *
 *   - the destructor slot (GERasterizer_Destructor) is the "deleting destructor" taking a flags
 *     argument (bit 0: also free the memory), at the position where the destructor was declared,
 *   - an overload set (the nine Draw overloads) is laid out in REVERSE declaration order at the
 *     position of the first overload, unlike the Itanium ABI,
 *   - the slots DX7Rasterizer adds sit behind the 78 of the engine's GERasterizer,
 *   - RTTI is reachable at vtable[-1], used by __RTDynamicCast.
 *
 * All methods are __thiscall (object pointer in ECX, callee pops its arguments).
 */
#ifndef EEWEB_RASTERIZER_ABI_H
#define EEWEB_RASTERIZER_ABI_H

#include "ge_slots.h" /* generated: the slot indices of the original's vtables */

/*
 * Counters: bookkeeping that makes the DLL's internal exercise observable.  They sit right behind the
 * vptr of the object (a real vtable has no room for a test hook): counter k is the long at byte offset
 * EEWEB_COUNTERS_OFFSET + 4 * k.
 */
#define EEWEB_COUNTERS_OFFSET 4
enum {
    EEWEB_COUNTER_ACTIVATIONS = 0,    /* ActivateRasterizer() calls on this object */
    EEWEB_COUNTER_DEACTIVATIONS = 1,  /* Deactivate() calls on this object */
    EEWEB_COUNTER_SELF_CHECKS = 2,    /* internal self-checks run by PIMIsValid */
    EEWEB_COUNTER_SELF_CHECK_FAILURES = 3,
    EEWEB_COUNTER_DESTROYED = 4,      /* destructor runs of this object */
    EEWEB_COUNTER_COUNT = 5
};

/* Results of the null implementation. */
#define EEWEB_ACTIVATE_FAILED 0x45570001L /* ActivateRasterizer(): the null rasterizer cannot activate */
#define EEWEB_NOT_SUPPORTED 0x45570002L   /* the other operations that return a status */
#define EEWEB_DRAW_BITMAP_BASE 0x1000L    /* Draw(GEBitmap *, mode)        returns this + mode */
#define EEWEB_DRAW_LINE_BASE 0x2000L      /* Draw(GELine *, first, second) returns this + first + second */

/* RTTI names (MSVC type descriptor names) of our class and of the two it derives from; the original's
 * DX7TnLRasterizer derives from DX7Rasterizer and GERasterizer in the same way. */
#define EEWEB_RTTI_NULL ".?AVEEWebNullRasterizer@@"
#define EEWEB_RTTI_ABI ".?AVDX7RasterizerAbi@@"
#define EEWEB_RTTI_GE ".?AVGERasterizer@@"

#endif
