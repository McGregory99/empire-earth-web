/*
 * Binding to the original "Low-Level Engine.dll" (Stainless Steel Studios, 2002).
 *
 * The engine is a C++ library built with MSVC 6.  Plugins such as the TCP
 * protocol only touch it through its exported functions, which we import by
 * their decorated names (see low_level_engine.def) and call with the exact
 * MSVC calling conventions:
 *
 *   - non-static member functions use `thiscall` (object pointer in ECX),
 *   - free functions and static members use `cdecl`.
 *
 * Engine objects are treated as correctly-sized buffers.  We never construct,
 * copy or destroy them by hand: that always goes through the engine's own
 * constructors, assignment operators and destructors, which this header wraps
 * under descriptive C names.  The only layout knowledge we rely on is the
 * handful of fields that the original plugin read inline (character pointer,
 * length, event handle, configuration counters).
 */
#ifndef EE_LOW_LEVEL_ENGINE_H
#define EE_LOW_LEVEL_ENGINE_H

#include <stdint.h>

/* <winsock2.h> must come before <windows.h>; plugins using the engine need both anyway. */
#include <winsock2.h>
#include <windows.h>

/* Bind a C declaration to a decorated engine export. */
#define ENGINE_EXPORT(decorated_name) __asm__("\"" decorated_name "\"")
#define ENGINE_THISCALL __attribute__((thiscall))

/* ------------------------------------------------------------------------ */
/* Engine object types                                                       */
/* ------------------------------------------------------------------------ */

/* class UString: narrow string with a virtual destructor.  16 bytes. */
typedef struct UString {
    const void *vtable;
    char *chars;       /* NUL-terminated character data */
    uint32_t length;   /* characters, excluding the terminator */
    uint32_t capacity;
} UString;

/*
 * class UWideString: despite its name this engine build keeps single-byte
 * characters (the original plugin hands `chars` to strcpy).  20 bytes.
 */
typedef struct UWideString {
    const void *vtable;
    char *chars;
    uint32_t length;
    uint32_t capacity;
    uint32_t reserved;
} UWideString;

/* class NEProtocolConfiguration: a list of tunable protocol parameters. */
typedef struct NEProtocolConfiguration {
    void *parameters;        /* array of parameter records, owned by the engine */
    uint32_t parameterCount;
    uint32_t version;
} NEProtocolConfiguration;

/*
 * class URandom: a linear congruential generator.  The plugin fills in the
 * four fields itself (the constructor was inlined by MSVC) and then calls
 * URandom::Generate, which returns a float in [0, 1).
 */
typedef struct URandom {
    uint32_t seed;
    uint32_t multiplier;
    uint32_t modulus;
    uint32_t increment;
} URandom;

/* Opaque scheduler objects.  A TSEvent starts with its Win32 event HANDLE. */
typedef struct TSEvent TSEvent;
typedef struct TSCriticalSection TSCriticalSection;
typedef struct TSThread TSThread;

static inline HANDLE ts_event_handle(const TSEvent *event)
{
    return *(const HANDLE *)event;
}

/* The sizes the engine uses for these objects; the plugin embeds them in its own structures. */
#define ENGINE_ABI_CHECK(name, condition) typedef char engine_abi_check_##name[(condition) ? 1 : -1]
ENGINE_ABI_CHECK(ustring_is_16_bytes, sizeof(UString) == 16);
ENGINE_ABI_CHECK(uwidestring_is_20_bytes, sizeof(UWideString) == 20);
ENGINE_ABI_CHECK(configuration_is_12_bytes, sizeof(NEProtocolConfiguration) == 12);
ENGINE_ABI_CHECK(urandom_is_16_bytes, sizeof(URandom) == 16);

/* Thread entry point accepted by TSScheduler::CreateThread (cdecl). */
typedef unsigned long (*TSThreadProc)(void *argument);

/* ------------------------------------------------------------------------ */
/* UString                                                                   */
/* ------------------------------------------------------------------------ */

extern UString *ENGINE_THISCALL ustring_construct(UString *self)
    ENGINE_EXPORT("??0UString@@QAE@XZ");
extern UString *ENGINE_THISCALL ustring_construct_with_capacity(UString *self, unsigned long capacity)
    ENGINE_EXPORT("??0UString@@QAE@K@Z");
extern UString *ENGINE_THISCALL ustring_construct_from_chars(UString *self, const char *chars)
    ENGINE_EXPORT("??0UString@@QAE@PBD@Z");
extern UString *ENGINE_THISCALL ustring_construct_copy(UString *self, const UString *other)
    ENGINE_EXPORT("??0UString@@QAE@ABV0@@Z");
extern void ENGINE_THISCALL ustring_destruct(UString *self)
    ENGINE_EXPORT("??1UString@@UAE@XZ");
extern UString *ENGINE_THISCALL ustring_assign(UString *self, const UString *other)
    ENGINE_EXPORT("??4UString@@QAEAAV0@ABV0@@Z");
extern UString *ENGINE_THISCALL ustring_assign_chars(UString *self, const char *chars)
    ENGINE_EXPORT("??4UString@@QAEAAV0@PBD@Z");
/* Returns non-zero when both strings hold the same text (low byte only). */
extern unsigned char ustring_equal(const UString *a, const UString *b)
    ENGINE_EXPORT("??8@YA_NABVUString@@0@Z");

/* ------------------------------------------------------------------------ */
/* UWideString                                                               */
/* ------------------------------------------------------------------------ */

extern UWideString *ENGINE_THISCALL uwidestring_construct(UWideString *self)
    ENGINE_EXPORT("??0UWideString@@QAE@XZ");
extern UWideString *ENGINE_THISCALL uwidestring_construct_with_capacity(UWideString *self, unsigned long capacity)
    ENGINE_EXPORT("??0UWideString@@QAE@K@Z");
extern UWideString *ENGINE_THISCALL uwidestring_construct_from_chars(UWideString *self, const char *chars)
    ENGINE_EXPORT("??0UWideString@@QAE@PBD@Z");
extern UWideString *ENGINE_THISCALL uwidestring_construct_copy(UWideString *self, const UWideString *other)
    ENGINE_EXPORT("??0UWideString@@QAE@ABV0@@Z");
extern void ENGINE_THISCALL uwidestring_destruct(UWideString *self)
    ENGINE_EXPORT("??1UWideString@@UAE@XZ");
extern UWideString *ENGINE_THISCALL uwidestring_assign(UWideString *self, const UWideString *other)
    ENGINE_EXPORT("??4UWideString@@QAEAAV0@ABV0@@Z");
extern UWideString *ENGINE_THISCALL uwidestring_assign_chars(UWideString *self, const char *chars)
    ENGINE_EXPORT("??4UWideString@@QAEAAV0@PBD@Z");
extern unsigned char uwidestring_equal(const UWideString *a, const UWideString *b)
    ENGINE_EXPORT("??8@YA_NABVUWideString@@0@Z");

/* ------------------------------------------------------------------------ */
/* NEProtocolConfiguration                                                   */
/* ------------------------------------------------------------------------ */

extern NEProtocolConfiguration *ENGINE_THISCALL neprotocolconfiguration_construct(
    NEProtocolConfiguration *self, unsigned long parameter_count, unsigned long version)
    ENGINE_EXPORT("??0NEProtocolConfiguration@@QAE@KK@Z");
extern NEProtocolConfiguration *ENGINE_THISCALL neprotocolconfiguration_construct_copy(
    NEProtocolConfiguration *self, const NEProtocolConfiguration *other)
    ENGINE_EXPORT("??0NEProtocolConfiguration@@QAE@ABV0@@Z");
extern void ENGINE_THISCALL neprotocolconfiguration_destruct(NEProtocolConfiguration *self)
    ENGINE_EXPORT("??1NEProtocolConfiguration@@QAE@XZ");

/* ------------------------------------------------------------------------ */
/* TSScheduler: critical sections, events and threads                        */
/* ------------------------------------------------------------------------ */

/* The Create* functions return an engine status (0 = success). */
extern long tsscheduler_create_critical_section(TSCriticalSection **out)
    ENGINE_EXPORT("?CreateCriticalSection@TSScheduler@@SAJAAPAVTSCriticalSection@@@Z");
extern void tsscheduler_destroy_critical_section(TSCriticalSection *section)
    ENGINE_EXPORT("?DestroyCriticalSection@TSScheduler@@SAXPAVTSCriticalSection@@@Z");
extern void ENGINE_THISCALL tscriticalsection_enter(TSCriticalSection *self)
    ENGINE_EXPORT("?Enter@TSCriticalSection@@QAEXXZ");
extern void ENGINE_THISCALL tscriticalsection_leave(TSCriticalSection *self)
    ENGINE_EXPORT("?Leave@TSCriticalSection@@QAEXXZ");

extern long tsscheduler_create_event(TSEvent **out, unsigned char manual_reset)
    ENGINE_EXPORT("?CreateEventA@TSScheduler@@SAJAAPAVTSEvent@@_N@Z");
extern void tsscheduler_destroy_event(TSEvent *event)
    ENGINE_EXPORT("?DestroyEvent@TSScheduler@@SAXPAVTSEvent@@@Z");
extern long ENGINE_THISCALL tsevent_set(TSEvent *self)
    ENGINE_EXPORT("?Set@TSEvent@@QAEJXZ");
extern long ENGINE_THISCALL tsevent_clear(TSEvent *self)
    ENGINE_EXPORT("?Clear@TSEvent@@QAEJXZ");
extern long ENGINE_THISCALL tsevent_wait(TSEvent *self, unsigned long timeout_ms)
    ENGINE_EXPORT("?Wait@TSEvent@@QAEJK@Z");

/* The meaning of the last two parameters is not known; the plugin always passes 0 and false. */
extern long tsscheduler_create_thread(TSThread **out, TSThreadProc procedure, void *argument,
                                      unsigned long option_a, unsigned char option_b)
    ENGINE_EXPORT("?CreateThread@TSScheduler@@SAJAAPAVTSThread@@P6AKPAX@Z1K_N@Z");
extern long tsscheduler_destroy_thread(TSThread *thread)
    ENGINE_EXPORT("?DestroyThread@TSScheduler@@SAJPAVTSThread@@@Z");

/* ------------------------------------------------------------------------ */
/* URandom, time, error reporting, floating-point control                    */
/* ------------------------------------------------------------------------ */

extern float ENGINE_THISCALL urandom_generate(URandom *self)
    ENGINE_EXPORT("?Generate@URandom@@QAEMXZ");
extern unsigned long tm_get_time(void)
    ENGINE_EXPORT("?TMGetTime@@YAKXZ");

/* Remember / fetch the last native (OS) error, in the engine's error log. */
extern unsigned long es_record_native_error(unsigned long native_error)
    ENGINE_EXPORT("?ESRecordNativeError@@YAKK@Z");
extern unsigned long es_get_native_error(void)
    ENGINE_EXPORT("?ESGetNativeError@@YAKXZ");
/* Assertion hook: reports `expression` failed at `file`:`line`. */
extern void es_verify(const char *file, unsigned long line, const char *expression)
    ENGINE_EXPORT("?ESVerify@@YAXPBDK0@Z");

extern void um_set_floating_point_precision(unsigned long precision)
    ENGINE_EXPORT("?UMSetFloatingPointPrecision@@YAXK@Z");
extern unsigned long um_get_floating_point_precision(void)
    ENGINE_EXPORT("?UMGetFloatingPointPrecision@@YAKXZ");
extern void um_set_floating_point_rounding_mode(unsigned long mode)
    ENGINE_EXPORT("?UMSetFloatingPointRoundingMode@@YAXK@Z");
extern unsigned long um_get_floating_point_rounding_mode(void)
    ENGINE_EXPORT("?UMGetFloatingPointRoundingMode@@YAKXZ");

#endif /* EE_LOW_LEVEL_ENGINE_H */
