/*
 * Process-wide state of the plugin.
 *
 * The original kept all of this in file-scope globals; they are gathered here
 * in one struct so each piece can be documented.  The engine loads the plugin
 * once and calls it from several threads (game threads plus the LAN searcher
 * thread this plugin starts itself), hence the critical sections.
 */
#ifndef TCP_PROTOCOL_PLUGIN_STATE_H
#define TCP_PROTOCOL_PLUGIN_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <winsock2.h>

#include "game_list.h"
#include "low_level_engine.h"

typedef struct PluginState {
    /* Set by NPIMActivate, cleared by NPIMDeactivate; what PIMIsValid reports. */
    bool active;

    /* Guards the published list of detected games and the resolving of connect targets. */
    TSCriticalSection *game_lock;
    /* Held for the whole duration of a LAN scan and while a new client socket is bound. */
    TSCriticalSection *scan_lock;
    /* Serialises the bulk-transfer accept and send calls. */
    TSCriticalSection *transfer_lock;

    /* Source of the random header bytes of outgoing datagrams. */
    URandom *random;
    /* Fixed table the RC4 keys are cut from (see cipher.h). */
    uint8_t *key_table;

    /*
     * The adapter index of the latest NPIMConnect / NPIMPublish / NPIMGetLocalAddress
     * call and where that adapter's address sits in the host's address list.
     * NPIMSendSingle reuses the index (on the *destination's* address list).
     */
    unsigned long adapter_argument;
    unsigned long adapter_host_index;

    /* Bulk-transfer sockets: the listener of the published server, the client end of a transfer. */
    SOCKET transfer_listener;
    SOCKET transfer_client;

    GameList detected_games;  /* what NPIMGetDetectedGame reports */
    GameList scan_results;    /* filled while a scan listens */

    /* The background thread that scans the LAN once a search has been started. */
    struct {
        bool running;
        TSEvent *exit_event;      /* set to ask the thread to stop */
        TSEvent *finished_event;  /* set by the thread when it is done */
        TSThread *thread;
    } searcher;

    /* Identity and (empty) configuration, constructed when the DLL is loaded. */
    NEProtocolConfiguration configuration;
    UString protocol_name;
    UWideString local_address;  /* storage handed out by NPIMGetLocalAddress */
} PluginState;

extern PluginState g_plugin;

/* Enter / leave a critical section that may not exist (before activation). */
void lock_enter(TSCriticalSection *section);
void lock_leave(TSCriticalSection *section);

/* Report an OS error to the engine's error log. */
void report_native_error(unsigned long error);
/* Same, and let the engine acknowledge it (most call sites of the original do both). */
void report_native_error_and_acknowledge(unsigned long error);
/* Report the error of the Winsock call that just failed. */
void report_socket_error(void);
void report_socket_error_and_acknowledge(void);

#endif /* TCP_PROTOCOL_PLUGIN_STATE_H */
