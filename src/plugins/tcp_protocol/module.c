/*
 * Module identity, activation of the Winsock session, status and configuration.
 */
#include <stdlib.h>

#include "cipher.h"
#include "exports.h"
#include "plugin_state.h"
#include "protocol.h"

/* Lines reported to the engine's assertion hook when a critical section cannot be created. */
#define SOURCE_LINE_GAME_LOCK 0x18e
#define SOURCE_LINE_SCAN_LOCK 0x190
#define SOURCE_LINE_TRANSFER_LOCK 0x194
#define STATUS_STRING "WinSock 2.0"

UString *PIMGetName(void)
{
    return &g_plugin.protocol_name;
}

unsigned long PIMGetType(void)
{
    return PLUGIN_MODULE_TYPE;
}

unsigned char PIMIsValid(void)
{
    return g_plugin.active;
}

unsigned long NPIMGetProtocolType(void)
{
    return PROTOCOL_ID;
}

static void create_lock(TSCriticalSection **lock, unsigned long source_line)
{
    if (tsscheduler_create_critical_section(lock) != 0) {
        es_verify(ENGINE_ASSERT_FILE, source_line, "theResult == keseOK");
    }
}

static void destroy_lock(TSCriticalSection **lock)
{
    if (*lock) {
        tsscheduler_destroy_critical_section(*lock);
        *lock = NULL;
    }
}

/* Start Winsock 2.0, check that sockets work, and set up the helpers the other calls rely on. */
long NPIMActivate(void)
{
    WSADATA wsa;
    SOCKET probe;
    UWideString status;
    long result;

    if (WSAStartup(MAKEWORD(2, 0), &wsa) != 0) {
        report_socket_error();
        return NET_FAILED;
    }
    if (wsa.wVersion != MAKEWORD(2, 0)) {
        return NET_NOT_SUPPORTED;
    }
    probe = socket(AF_INET, SOCK_STREAM, 0);
    if (probe == INVALID_SOCKET) {
        return NET_NOT_SUPPORTED;
    }
    closesocket(probe);

    g_plugin.active = true;
    create_lock(&g_plugin.game_lock, SOURCE_LINE_GAME_LOCK);
    create_lock(&g_plugin.scan_lock, SOURCE_LINE_SCAN_LOCK);
    create_lock(&g_plugin.transfer_lock, SOURCE_LINE_TRANSFER_LOCK);

    g_plugin.random = malloc(sizeof *g_plugin.random);
    if (g_plugin.random) {
        unsigned long now = tm_get_time();

        g_plugin.random->seed = now ? now : 1;
        g_plugin.random->multiplier = 0x343fd;
        g_plugin.random->modulus = 0x80000000u;
        g_plugin.random->increment = 0x269ec3;
    }

    uwidestring_construct_from_chars(&status, STATUS_STRING);
    result = NPIMGetStatus(&status, 0);
    uwidestring_destruct(&status);
    return result;
}

long NPIMDeactivate(void)
{
    if (WSACleanup() != 0) {
        report_socket_error();
        return NET_FAILED;
    }
    destroy_lock(&g_plugin.game_lock);
    destroy_lock(&g_plugin.scan_lock);
    destroy_lock(&g_plugin.transfer_lock);
    cipher_release();
    free(g_plugin.random);
    g_plugin.random = NULL;
    g_plugin.active = false;
    return NET_OK;
}

/*
 * The engine asks for the status "WinSock 2.0" and the plugin answers with a
 * freshly generated key table (so repeated calls are harmless).  The original
 * compares the requested string with a copy of itself, so *any* string is
 * accepted and the "unsupported" status 7 can never be returned; the port
 * argument it computes is not used either.
 */
long NPIMGetStatus(const UWideString *status, unsigned long port)
{
    (void)status;
    (void)port;
    cipher_generate_key_table();
    return g_plugin.key_table ? NET_OK : NET_OUT_OF_MEMORY;
}

/* The protocol has no tunable parameters: hand out a copy of the empty configuration. */
long NPIMGetConfiguration(NEProtocolConfiguration **configuration)
{
    NEProtocolConfiguration *copy = malloc(sizeof *copy);

    if (copy) {
        neprotocolconfiguration_construct_copy(copy, &g_plugin.configuration);
    }
    *configuration = copy;
    return copy ? NET_OK : NET_OUT_OF_MEMORY;
}

/* Only the (empty) configuration we handed out is acceptable. */
long NPIMSetConfiguration(const NEProtocolConfiguration *configuration)
{
    if (configuration->version == g_plugin.configuration.version &&
        configuration->parameterCount == g_plugin.configuration.parameterCount) {
        return NET_OK;
    }
    return NET_NOT_SUPPORTED;
}

/* Entry point of the DLL: set up and tear down the objects that need the engine's constructors. */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved);

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        neprotocolconfiguration_construct(&g_plugin.configuration, 0, 1);
        ustring_construct_from_chars(&g_plugin.protocol_name, PROTOCOL_NAME);
        uwidestring_construct(&g_plugin.local_address);
        break;
    case DLL_PROCESS_DETACH:
        game_list_release(&g_plugin.scan_results);
        game_list_release(&g_plugin.detected_games);
        uwidestring_destruct(&g_plugin.local_address);
        ustring_destruct(&g_plugin.protocol_name);
        neprotocolconfiguration_destruct(&g_plugin.configuration);
        break;
    default:
        break;
    }
    return TRUE;
}
