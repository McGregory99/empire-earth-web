/* Configuration, lazy loading of the next ddraw in the chain, DllMain. */
#include "ddt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DdtConfig g_cfg;

static CRITICAL_SECTION g_init_cs;
static volatile LONG g_inited;
static HMODULE g_self, g_next;

static void cfg_set(const char *key, const char *value) {
    if (!strcmp(key, "DDT_NEXT"))
        lstrcpynA(g_cfg.next_name, value, sizeof g_cfg.next_name);
    else if (!strcmp(key, "DDT_TRACE"))
        lstrcpynA(g_cfg.trace_path, value, sizeof g_cfg.trace_path);
    else if (!strcmp(key, "DDT_MODULES"))
        lstrcpynA(g_cfg.modules, value, sizeof g_cfg.modules);
    else if (!strcmp(key, "DDT_MAX_PRESENTS"))
        g_cfg.max_presents = (unsigned)strtoul(value, NULL, 0);
    else if (!strcmp(key, "DDT_ARM_FILE"))
        lstrcpynA(g_cfg.arm_file, value, sizeof g_cfg.arm_file);
    else if (!strcmp(key, "DDT_DATA"))
        lstrcpynA(g_cfg.data_prefix, value, sizeof g_cfg.data_prefix);
    else if (!strcmp(key, "DDT_DATA_MAX")) {
        unsigned long v = strtoul(value, NULL, 0);
        g_cfg.data_max = v > 0xFFFFFF00ul ? 0xFFFFFF00u : (unsigned)v; /* references are 32-bit offsets */
    } else if (!strcmp(key, "DDT_FLUSH"))
        g_cfg.flush_each = atoi(value) != 0;
}

static const char *const cfg_keys[] = {"DDT_NEXT", "DDT_TRACE", "DDT_MODULES", "DDT_MAX_PRESENTS", "DDT_FLUSH", "DDT_ARM_FILE", "DDT_DATA", "DDT_DATA_MAX"};

/* ddraw_trace.cfg next to the proxy (KEY=VALUE per line), then the environment, which wins. */
static void cfg_load(void) {
    char path[MAX_PATH], line[1024], *slash;
    FILE *f;
    unsigned i;

    lstrcpyA(g_cfg.next_name, "ddraw_next.dll");
    g_cfg.data_max = 512u << 20; /* generous but bounded */
    lstrcpyA(g_cfg.modules, "DX7HRTnLDisplay.dll,DX7HRDisplay.dll");
    GetModuleFileNameA(g_self, path, sizeof path);
    slash = strrchr(path, '\\');
    if (slash) {
        lstrcpyA(slash + 1, "ddraw_trace.cfg");
        f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof line, f)) {
                char *eq = strchr(line, '=');
                size_t n;
                if (!eq || line[0] == '#')
                    continue;
                *eq = 0;
                n = strlen(eq + 1);
                while (n && (eq[n] == '\n' || eq[n] == '\r'))
                    eq[n--] = 0;
                cfg_set(line, eq + 1);
            }
            fclose(f);
        }
    }
    for (i = 0; i < sizeof cfg_keys / sizeof *cfg_keys; i++) {
        char v[1024];
        DWORD n = GetEnvironmentVariableA(cfg_keys[i], v, sizeof v);
        if (n > 0 && n < sizeof v)
            cfg_set(cfg_keys[i], v);
    }
}

void ddt_init(void) {
    if (g_inited)
        return;
    EnterCriticalSection(&g_init_cs);
    if (!g_inited) {
        cfg_load();
        dispatch_init();
        g_inited = 1;
    }
    LeaveCriticalSection(&g_init_cs);
}

HMODULE ddt_next_module(void) {
    ddt_init();
    if (g_next)
        return g_next;
    EnterCriticalSection(&g_init_cs);
    if (!g_next) {
        char path[MAX_PATH], *slash;
        const char *name = g_cfg.next_name;
        int absolute = (name[0] && name[1] == ':') || name[0] == '\\';
        if (absolute) {
            lstrcpynA(path, name, sizeof path);
        } else {
            GetModuleFileNameA(g_self, path, sizeof path);
            slash = strrchr(path, '\\');
            if (slash)
                lstrcpynA(slash + 1, name, (int)(sizeof path - (size_t)(slash + 1 - path)));
        }
        g_next = LoadLibraryA(path);
    }
    LeaveCriticalSection(&g_init_cs);
    return g_next;
}

FARPROC ddt_next_proc(const char *name) {
    HMODULE m = ddt_next_module();
    return m ? GetProcAddress(m, name) : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_init_cs);
    } else if (reason == DLL_PROCESS_DETACH) {
        ddt_shutdown();
    }
    return TRUE;
}
