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
 * fields the original's DX7Rasterizer has (the engine's 0x68 bytes and the plugin's 0x14, ge_layouts.h
 * DX7RasterizerLayout): counter k is the long at byte offset EEWEB_COUNTERS_OFFSET + 4 * k.
 */
#define EEWEB_COUNTERS_OFFSET 0x7c
enum {
    EEWEB_COUNTER_ACTIVATIONS = 0,    /* ActivateRasterizer() calls on this object */
    EEWEB_COUNTER_DEACTIVATIONS = 1,  /* Deactivate() calls on this object */
    EEWEB_COUNTER_SELF_CHECKS = 2,    /* internal self-checks run by PIMIsValid */
    EEWEB_COUNTER_SELF_CHECK_FAILURES = 3,
    EEWEB_COUNTER_DESTROYED = 4,      /* destructor runs of this object */
    EEWEB_COUNTER_LAYOUT_VIOLATIONS = 5,  /* engine objects refused by a layout canary (layout_canaries.h) */
    EEWEB_COUNTER_LAST_VIOLATION = 6,     /* the EEWEB_LAYOUT_* code of the latest refusal */
    EEWEB_COUNTER_SCREENS = 7,            /* screens the rasterizer holds (the length of its screen record list) */
    EEWEB_COUNTER_COUNT = 8
};

/* Results of the null operations. */
#define EEWEB_NOT_SUPPORTED 0x45570002L   /* the other operations that return a status */
#define EEWEB_DRAW_BITMAP_BASE 0x1000L    /* Draw(GEBitmap *, mode)        returns this + mode */
#define EEWEB_LAYOUT_VIOLATION 0x45570003L /* an operation refused an engine object whose layout canary failed */
#define EEWEB_DRAW_LINE_BASE 0x2000L      /* Draw(GELine *, first, second) returns this + first + second */

/* Why a layout canary refused an object (layout_canaries.h): the code is recorded in EEWEB_COUNTER_LAST_VIOLATION. */
enum {
    EEWEB_LAYOUT_OK = 0,
    EEWEB_LAYOUT_NULL = 1,        /* a null pointer where an engine object is required */
    EEWEB_LAYOUT_MISALIGNED = 2,  /* an object address that is not 4-byte aligned */
    EEWEB_LAYOUT_UNREADABLE = 3,  /* memory the plugin would read is not readable */
    EEWEB_LAYOUT_WRONG_CLASS = 4, /* the RTTI of the object does not name the expected class */
    EEWEB_LAYOUT_BAD_FIELD = 5,   /* a field violates an invariant of the class (count, float, flag) */
    EEWEB_LAYOUT_BAD_CHAIN = 6    /* a pointer chain the plugin follows is null or unreadable */
};

/* RTTI names (MSVC type descriptor names) of our class and of the two it derives from; the original's
 * DX7TnLRasterizer derives from DX7Rasterizer and GERasterizer in the same way. */
#define EEWEB_RTTI_NULL ".?AVEEWebNullRasterizer@@"
#define EEWEB_RTTI_ABI ".?AVDX7RasterizerAbi@@"
#define EEWEB_RTTI_GE ".?AVGERasterizer@@"

#endif
