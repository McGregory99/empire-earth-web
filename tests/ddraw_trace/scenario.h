/* Shared between the harness (an exe, whose calls the proxy must NOT trace) and the fake plugin DLL
 * (named in DDT_MODULES, whose calls the proxy must trace). */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <windows.h>

/* Results of the calls the plugin made, so that the harness can check the proxy passed them through. */
typedef struct ScenarioA {
    HRESULT hr_enum_ex, hr_enum, hr_create, hr_coop, hr_qi, hr_qi_again;
    HRESULT hr_primary, hr_target, hr_device, hr_begin, hr_clear, hr_end;
    int enum_ex_callbacks, enum_callbacks;
    void *d3d_first, *d3d_again; /* both queries must give the same interface pointer */
    ULONG device_release;
} ScenarioA;

typedef int(WINAPI *ScenarioAFn)(HWND hwnd, ScenarioA *out);

#endif
