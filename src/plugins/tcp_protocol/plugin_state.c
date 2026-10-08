#include "plugin_state.h"

PluginState g_plugin = { .transfer_listener = INVALID_SOCKET, .transfer_client = INVALID_SOCKET };

void lock_enter(TSCriticalSection *section)
{
    if (section) {
        tscriticalsection_enter(section);
    }
}

void lock_leave(TSCriticalSection *section)
{
    if (section) {
        tscriticalsection_leave(section);
    }
}

void report_native_error(unsigned long error)
{
    es_record_native_error(error);
}

void report_native_error_and_acknowledge(unsigned long error)
{
    es_record_native_error(error);
    es_get_native_error();
}

void report_socket_error(void)
{
    report_native_error((unsigned long)WSAGetLastError());
}

void report_socket_error_and_acknowledge(void)
{
    report_native_error_and_acknowledge((unsigned long)WSAGetLastError());
}
