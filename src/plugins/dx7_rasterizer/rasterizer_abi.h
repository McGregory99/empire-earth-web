/*
 * The shape of the C++ class this plugin hands to the engine, as a contract that both the
 * plugin (null_rasterizer.h) and its test harness (tests/dx7_rasterizer/harness.c) include.
 * Plain C so that the mingw-built harness can use it.
 *
 * Why this class exists: the real engine calls rasterizer objects through positional vtables
 * built by MSVC 6 (GERasterizer: 78 slots, DX7Rasterizer: 82), so a clang-built plugin must
 * reproduce the Microsoft layout rules.  The class below exercises each of them once:
 *
 *   - pure virtual slots  (the vtable holds the CRT's _purecall),
 *   - an overload pair, which MSVC lays out in REVERSE declaration order at the position of
 *     the first overload (slots 2 and 3 below), unlike the Itanium ABI,
 *   - a virtual destructor at the position where it is declared (slot 5), whose slot is the
 *     "deleting destructor" taking a flags argument (bit 0: also free the memory),
 *   - RTTI reachable at vtable[-1] in the MSVC layout, used by __RTDynamicCast.
 *
 *   slot  method
 *     0   long Activate()                      pure
 *     1   long GetScreenCount() const          pure
 *     2   long Draw(float scale, long mode)    overload declared second
 *     3   long Draw(long mode)                 overload declared first
 *     4   long Describe()                      not overridden by the derived class
 *     5   ~EEWebRasterizerBase()               deleting destructor
 *     6   long Deactivate()
 *     7   long GetCounter(long which) const
 *
 * All methods are __thiscall (object pointer in ECX, callee pops its arguments).
 */
#ifndef EEWEB_RASTERIZER_ABI_H
#define EEWEB_RASTERIZER_ABI_H

enum {
    EEWEB_VSLOT_ACTIVATE = 0,
    EEWEB_VSLOT_GET_SCREEN_COUNT = 1,
    EEWEB_VSLOT_DRAW_SCALED = 2,
    EEWEB_VSLOT_DRAW_PLAIN = 3,
    EEWEB_VSLOT_DESCRIBE = 4,
    EEWEB_VSLOT_DELETING_DESTRUCTOR = 5,
    EEWEB_VSLOT_DEACTIVATE = 6,
    EEWEB_VSLOT_GET_COUNTER = 7
};

/* GetCounter(which): bookkeeping that makes the DLL's internal exercise observable. */
enum {
    EEWEB_COUNTER_ACTIVATIONS = 0,    /* Activate() calls on this object */
    EEWEB_COUNTER_DEACTIVATIONS = 1,  /* Deactivate() calls on this object */
    EEWEB_COUNTER_SELF_CHECKS = 2,    /* internal self-checks run by PIMIsValid */
    EEWEB_COUNTER_SELF_CHECK_FAILURES = 3,
    EEWEB_COUNTER_DESTROYED = 4,      /* EEWebNullRasterizer destructor runs, any object in the DLL */
    EEWEB_COUNTER_BASE_DESTROYED = 5, /* EEWebRasterizerBase destructor runs, any object in the DLL */
    EEWEB_COUNTER_COUNT = 6
};

/* Results of the null implementation. */
#define EEWEB_ACTIVATE_FAILED 0x45570001L /* Activate(): the null rasterizer cannot activate */
#define EEWEB_DRAW_PLAIN_BASE 0x1000L     /* Draw(mode)         returns this + mode */
#define EEWEB_DRAW_SCALED_BASE 0x2000L    /* Draw(scale, mode)  returns this + mode + (long)scale */
#define EEWEB_DESCRIBE_RESULT 7L          /* Describe(), implemented once in the base class */

/* RTTI names (MSVC type descriptor names) of the two classes. */
#define EEWEB_RTTI_BASE ".?AVEEWebRasterizerBase@@"
#define EEWEB_RTTI_NULL ".?AVEEWebNullRasterizer@@"

#endif
