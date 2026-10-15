/* Entry point of the layout oracle: loads the real engine and runs every group of checks. */
#include "oracle.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_checks;
int g_failures;
static HMODULE g_engine;

void oracle_check(int ok, const char *fmt, ...)
{
    va_list args;
    g_checks++;
    if (!ok) {
        g_failures++;
    }
    printf(ok ? "ok   " : "FAIL ");
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

FARPROC oracle_export(const char *decorated_name)
{
    FARPROC address = GetProcAddress(g_engine, decorated_name);
    if (!address) {
        printf("FAIL the engine exports %s\n", decorated_name);
        fflush(stdout);
        exit(100 + (g_failures < 100 ? g_failures : 99));
    }
    return address;
}

const void *oracle_vftable(const char *class_name)
{
    char name[128];
    snprintf(name, sizeof(name), "??_7%s@@6B@", class_name);
    return (const void *)oracle_export(name);
}

void oracle_guard_fill(void *buffer, size_t total)
{
    memset(buffer, 0xA5, total);
}

int oracle_guard_intact(const void *buffer, size_t size, size_t total)
{
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t i;
    for (i = size; i < total; i++) {
        if (bytes[i] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    g_engine = LoadLibraryA("Low-Level Engine.dll");
    if (!g_engine) {
        printf("FAIL cannot load Low-Level Engine.dll (error %lu)\n", GetLastError());
        return 99;
    }
    oracle_value_types();
    oracle_texture_material_light();
    printf("layout oracle: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
