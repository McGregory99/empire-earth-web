/*
 * Shared declarations of the differential test harness for "TCP Protocol.dll".
 *
 * The harness is split by concern:
 *   transcript.c       printing the normalized transcript
 *   plugin_api.c       loading the plugin under test and binding its 33 exports
 *   interposer.c       import-table hooks: replacement GetAdaptersInfo, fault injection, engine error log
 *   wire.c             the datagram obfuscation, re-implemented for the peer side
 *   peer.c             raw Winsock sockets that play the other end of every conversation
 *   support.c          small helpers around engine objects, events, published servers
 *   scenarios_*.c      the scenarios themselves, listed in harness.c
 */
#ifndef TCP_PROTOCOL_HARNESS_H
#define TCP_PROTOCOL_HARNESS_H

#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "low_level_engine.h"

#define GAME_PORT 33334u
#define XFER_PORT 33335u

/* ---- transcript.c --------------------------------------------------------- */

void line(const char *format, ...);
const char *hex(const uint8_t *bytes, size_t count);
const char *port_text(unsigned port);
const char *chars_of(const UString *s);
const char *wchars_of(const UWideString *s);
const char *ipv4_text(const char *text);

static inline const char *yesno(int value)
{
    return value ? "yes" : "no";
}

/* ---- plugin_api.c -------------------------------------------------------- */

/* NEServer / NEConnection as the engine allocates them: 8-byte wrappers. */
typedef struct NeHandle {
    void *impl;      /* filled in by the plugin */
    void *protocol;  /* owning NEProtocol (unused by the plugin) */
} NeHandle;

/* The MSVC 6 std::vector<UWideString> the engine passes to the plugin. */
typedef struct WideStringVector {
    uint32_t allocator;
    UWideString *first;
    UWideString *last;
    UWideString *end;
} WideStringVector;

struct PluginApi {
    long (*Accept)(NeHandle *server, NeHandle *out);
    long (*AcceptForXfer)(NeHandle *server, NeHandle *out);
    long (*Activate)(void);
    void (*AllowNewConnections)(NeHandle *server, int allow);
    long (*CloseConnection)(NeHandle *connection);
    long (*CloseServer)(NeHandle *server);
    long (*Connect)(const UString *address, const UString *service, const UWideString *name,
                    unsigned long protocol, TSEvent *event, NeHandle *out, unsigned long adapter);
    long (*ConnectForXfer)(NeHandle *connection, unsigned long protocol, TSEvent *event, NeHandle *out);
    long (*Deactivate)(void);
    unsigned long (*EnumerateNetworkAdapters)(WideStringVector *out);
    long (*GetClientAddress)(NeHandle *server, unsigned long index, UWideString *out);
    long (*GetConfiguration)(NEProtocolConfiguration **out);
    long (*GetDetectedGame)(unsigned long index, UString *address, UString *service, UWideString *name);
    unsigned long (*GetDetectedGameCount)(void);
    UWideString *(*GetLocalAddress)(unsigned long adapter);
    unsigned long (*GetProtocolType)(void);
    long (*GetStatus)(const UWideString *status, unsigned long port);
    unsigned char (*IsAlive)(NeHandle *connection);
    long (*Publish)(const UString *service, const UWideString *name, unsigned long protocols,
                    TSEvent *event_a, TSEvent *event_b, TSEvent *event_c, NeHandle *out,
                    int flag_a, int flag_b, unsigned long adapter);
    long (*Receive)(NeHandle *connection, unsigned long capacity, uint8_t *buffer, unsigned long *received);
    long (*ReceiveFrom)(NeHandle *server, unsigned long capacity, uint8_t *buffer,
                        unsigned long *received, unsigned long *client);
    void (*ResetClientConnection)(NeHandle *server, unsigned long client);
    void (*ResetXfer)(NeHandle *server);
    long (*SearchForGame)(const UString *unused, const UString *service, int start);
    long (*Send)(NeHandle *connection, const uint8_t *buffer, unsigned long length);
    long (*SendSingle)(const UString *address, unsigned long port, const uint8_t *buffer, unsigned long length);
    long (*SendTo)(NeHandle *server, unsigned long client, const uint8_t *buffer, unsigned long length);
    long (*SetConfiguration)(NEProtocolConfiguration *configuration);
    long (*XferReceive)(unsigned long capacity, uint8_t *buffer, unsigned long *received);
    long (*XferSendTo)(NeHandle *server, unsigned long client, const uint8_t *buffer, unsigned long length);
    UString *(*PIMGetName)(void);
    unsigned long (*PIMGetType)(void);
    unsigned char (*PIMIsValid)(void);
};
extern struct PluginApi P;


/* Exports in ordinal order (the original numbers them 1..33 alphabetically). */
extern const char *const kExportNames[33];
extern HMODULE g_module;

/* Load the plugin by path and bind its exports; false if it cannot be loaded. */
int bind_plugin(const char *path);

/* ---- interposer.c --------------------------------------------------------- */

/* How many adapters the replacement GetAdaptersInfo reports (0 = none). */
extern int g_fake_adapter_count;
/*
 * Give the plugin a fixed host name and address list; see interposer.c.  Mode 1: 10.1.2.3 and
 * 10.1.2.4.  Mode 2: 0.0.0.0 and 10.1.2.4.  Mode 0: the real host.
 */
void fake_host_enable(int mode);

/* A datagram the plugin sent, recorded by the interposer. */
struct SentDatagram {
    uint8_t data[2048];
    int length;
    struct sockaddr_in to;
};
const struct SentDatagram *last_sent_datagram(void);
/* The first datagram sent to the broadcast address (the probe of a LAN scan) since forget_sent_datagrams(). */
const struct SentDatagram *first_broadcast_datagram(void);
void forget_sent_datagrams(void);
/* While on, the plugin's sendto is recorded but nothing is sent; it reports success. */
void swallow_sendto(int on);
/* Redirect the plugin's imports (adapters, Winsock, engine error reporting) to the harness. */
void interposer_install(HMODULE plugin);

/* Special results for fault_inject: a send that reports one byte less than requested, and a WSAStartup that succeeds
 * but reports version 1.1. */
#define FAULT_SHORT (-2)
#define FAULT_BAD_VERSION (-3)
/*
 * Make `count` calls of the Winsock function `function` that the plugin makes (after skipping `skip`
 * calls) fail with `result` and Winsock error `error`.
 */
void fault_inject(const char *function, int skip, int count, long result, int error);
/* "fault used" / "fault NOT REACHED"; disarms the fault. */
const char *fault_state(void);
/* The error reports the plugin made to the engine since the last call, e.g. "record(10065) acknowledge". */
const char *engine_errors(void);

/* ---- wire.c ------------------------------------------------------------- */

void init_key_table(void);
/* wire = header || cipher(plain); returns the wire length. */
size_t seal(uint8_t *wire, uint8_t header, const uint8_t *plain, size_t plain_length);
/* Decrypt a captured datagram; returns the plaintext length. */
size_t unseal(uint8_t *plain, const uint8_t *wire, size_t wire_length);
const char *header_text(uint8_t header);
/* Print the decrypted content of a captured datagram and whether it re-encrypts to the same bytes. */
void show_wire(const char *label, const uint8_t *wire, int length);

/* ---- peer.c ------------------------------------------------------------- */

struct sockaddr_in make_addr(const char *ip, unsigned port);
SOCKET udp_socket(const char *ip, unsigned port, int reuse);
int wait_readable(SOCKET s, int timeout_ms);
int udp_receive(SOCKET s, uint8_t *buffer, int capacity, struct sockaddr_in *from, int timeout_ms);
int udp_send(SOCKET s, const void *data, int length, const struct sockaddr_in *to);
SOCKET tcp_listener(const char *ip, unsigned port);
SOCKET tcp_accept_peer(SOCKET listener, int timeout_ms);
SOCKET tcp_connect_peer(const char *ip, unsigned port);
int tcp_read(SOCKET s, uint8_t *buffer, int capacity, int timeout_ms);

/* ---- support.c ---------------------------------------------------------- */

NeHandle *new_handle(void);
TSEvent *new_event(void);

#define POLL_DEFAULT_MS 3000

/* Retry `call` while it reports "nothing yet" (status 7) for up to timeout_ms. */
#define POLL_WHILE_PENDING(rc, timeout_ms, call)                                  \
    do {                                                                          \
        DWORD started_ = GetTickCount();                                          \
        for (;;) {                                                                \
            (rc) = (call);                                                        \
            if ((rc) != 7 || (int)(GetTickCount() - started_) >= (timeout_ms)) {  \
                break;                                                            \
            }                                                                     \
            Sleep(5);                                                             \
        }                                                                         \
    } while (0)

/*
 * The plugin associates the Win32 events the engine passes in with its sockets (WSAEventSelect);
 * the engine sleeps on them.  These helpers observe whether an event fires.
 */
void event_reset(TSEvent *event);
const char *event_state(TSEvent *event, int wait_ms);

/* First non-loopback IPv4 address of this host, or NULL. */
const char *lan_address(void);

typedef struct ServerEvents {
    TSEvent *activity;    /* socket activity (FD_READ) */
    TSEvent *xfer_data;   /* bulk-transfer listener, FD_READ */
    TSEvent *xfer_accept; /* bulk-transfer listener, FD_ACCEPT */
} ServerEvents;

ServerEvents new_server_events(void);
void free_server_events(ServerEvents *events);
long publish(NeHandle *server, const char *service_text, const char *name_text, unsigned long protocols,
             ServerEvents *events, int skip_duplicate_check, int visible);

/* ---- scenarios -------------------------------------------------------------- */

/* Everything after the activation scenario needs a working Winsock stack inside the plugin. */
extern int g_plugin_active;

void scenario_exports(void);
void scenario_identity(void);
void scenario_activation(void);
void scenario_configuration(void);
void scenario_network_info(void);
void scenario_adapters(void);
void scenario_empty_detected_games(void);
void scenario_send_single(void);
void scenario_udp_client_wire(void);
void scenario_tcp_client(void);
void scenario_single_transport_servers(void);
void scenario_udp_server(void);
void scenario_probe_flags(void);
void scenario_discovery(void);
void scenario_close_order_quirk(void);
void scenario_faults(void);
/* These leave the game port bound; harness.c runs each in a process of its own. */
void scenario_leak_scan_sendto(void);
void scenario_leak_scan_ioctl(void);
void scenario_leak_transfer_listener(void);

#endif /* TCP_PROTOCOL_HARNESS_H */
