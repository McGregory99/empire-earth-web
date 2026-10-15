/*
 * Layout oracle (task P2.5): the engine's own code is the judge of ../../src/engine_abi/ge_layouts.h.
 *
 * Every check builds an object in plain memory through the layout structs, then lets the REAL
 * "Low-Level Engine.dll" read it (an exported accessor) or write it (an exported setter or
 * constructor) and compares what the engine saw with what the layout says.  If a field sat at a
 * different offset than the layout claims, the engine would return or leave something else.
 *
 * The engine is loaded by file name from the current directory and its members are found with
 * GetProcAddress by their MSVC-decorated names; a name that does not resolve fails the run.  Member
 * functions are __thiscall (object in ECX), so the typed call helpers below use that convention.
 */
#ifndef EEWEB_LAYOUT_ORACLE_H
#define EEWEB_LAYOUT_ORACLE_H

#include "ge_layouts.h"

#include <stdint.h>
#include <windows.h>

#define TC __attribute__((thiscall))

typedef float (TC *fn_f_v)(const void *self);
typedef float (TC *fn_f_p)(const void *self, const void *a);
typedef float (TC *fn_f_u)(const void *self, unsigned long a);
typedef float (TC *fn_f_uu)(const void *self, unsigned long a, unsigned long b);
typedef unsigned long (TC *fn_u_v)(const void *self);
typedef unsigned long (TC *fn_u_u)(const void *self, unsigned long a);
typedef unsigned char (TC *fn_b_v)(const void *self);
typedef void *(TC *fn_p_v)(const void *self);
typedef void *(TC *fn_p_u)(const void *self, unsigned long a);
typedef void (TC *fn_v_v)(void *self);
typedef void (TC *fn_v_f)(void *self, float a);
typedef void (TC *fn_v_ff)(void *self, float a, float b);
typedef void (TC *fn_v_fff)(void *self, float a, float b, float c);
typedef void (TC *fn_v_p)(void *self, const void *a);
typedef void (TC *fn_v_u)(void *self, unsigned long a);
typedef void (TC *fn_v_uu)(void *self, unsigned long a, unsigned long b);
typedef void (TC *fn_v_pp)(void *self, void *a, void *b);
typedef void (TC *fn_v_pu)(void *self, const void *a, unsigned long b);
typedef void (TC *fn_v_fu)(void *self, float a, unsigned long b);
typedef void *(TC *fn_p_pp)(void *self, void *a, void *b);
typedef void *(TC *fn_p_uu)(void *self, unsigned long a, unsigned long b);
typedef void *(TC *fn_p_p)(void *self, const void *a);

extern int g_checks;
extern int g_failures;

/* The engine export by decorated name; aborts the run (a failed check) when it is missing. */
FARPROC oracle_export(const char *decorated_name);

void oracle_check(int ok, const char *fmt, ...);

#define CHECK(cond, ...) oracle_check((cond) ? 1 : 0, __VA_ARGS__)
#define CHECK_F(got, want, ...) oracle_check((got) == (want), __VA_ARGS__)

/* One group of checks per source file, run in this order by oracle_main.c. */
void oracle_value_types(void);
void oracle_texture_material_light(void);

/* The address of an exported data member, e.g. the vftable of a class ("??_7GETexture@@6B@"). */
const void *oracle_vftable(const char *class_name);

/* Guard bytes: an object buffer that is `size` bytes long inside `total` bytes of 0xA5; the engine must not touch the rest. */
void oracle_guard_fill(void *buffer, size_t total);
int oracle_guard_intact(const void *buffer, size_t size, size_t total);

#endif
