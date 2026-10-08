/*
 * Differential test harness for Empire Earth's network plugin "TCP Protocol.dll".
 *
 * Usage: harness.exe <plugin.dll>
 *
 * The harness loads the given plugin the way the engine does (LoadLibraryEx
 * with an altered search path, then GetProcAddress by name), calls its exports
 * with the same argument conventions as the engine's NEProtocol class, and
 * prints a normalized transcript to stdout: one line per call (or group of
 * related observations) with inputs, return codes and outputs.
 *
 * A raw Winsock "peer" lives inside the harness.  It records the exact bytes
 * the plugin puts on the wire and injects crafted datagrams (including the
 * encrypted game-discovery replies), so wire compatibility is part of the
 * transcript.  Values that legitimately differ from run to run (ephemeral
 * ports, the random header byte of encrypted datagrams, uninitialised padding,
 * adapter names, local addresses) are normalized.
 *
 * The transcript produced for the ORIGINAL DLL is the golden file; a
 * reimplementation passes when it produces the identical transcript.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "low_level_engine.h"

/* ------------------------------------------------------------------------ */
/* Transcript output                                                         */
/* ------------------------------------------------------------------------ */

static void line(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

static const char *hex(const uint8_t *bytes, size_t count)
{
    static char rotating[8][1200];
    static unsigned next;
    char *out = rotating[next++ % 8];
    size_t used = 0;
    if (count == 0) {
        strcpy(out, "(empty)");
        return out;
    }
    for (size_t i = 0; i < count && used + 4 < sizeof rotating[0]; i++) {
        used += (size_t)sprintf(out + used, i ? " %02x" : "%02x", bytes[i]);
    }
    return out;
}

static const char *yesno(int value) { return value ? "yes" : "no"; }

/* Never print a real ephemeral port: it differs on every run. */
static const char *port_text(unsigned port)
{
    static char rotating[8][24];
    static unsigned next;
    char *out = rotating[next++ % 8];
    if (port >= 49152u || (port >= 1024u && port != 33334u && port != 33335u && port < 33000u)) {
        strcpy(out, "<ephemeral>");
    } else {
        sprintf(out, "%u", port);
    }
    return out;
}

/* ------------------------------------------------------------------------ */
/* Plugin under test                                                         */
/* ------------------------------------------------------------------------ */

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

static struct PluginApi {
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
} P;

/* Exports in ordinal order (the original numbers them 1..33 alphabetically). */
static const char *const kExportNames[33] = {
    "NPIMAccept", "NPIMAcceptForXfer", "NPIMActivate", "NPIMAllowNewConnections",
    "NPIMCloseConnection", "NPIMCloseServer", "NPIMConnect", "NPIMConnectForXfer",
    "NPIMDeactivate", "NPIMEnumerateNetworkAdapters", "NPIMGetClientAddress",
    "NPIMGetConfiguration", "NPIMGetDetectedGame", "NPIMGetDetectedGameCount",
    "NPIMGetLocalAddress", "NPIMGetProtocolType", "NPIMGetStatus", "NPIMIsAlive",
    "NPIMPublish", "NPIMReceive", "NPIMReceiveFrom", "NPIMResetClientConnection",
    "NPIMResetXfer", "NPIMSearchForGame", "NPIMSend", "NPIMSendSingle", "NPIMSendTo",
    "NPIMSetConfiguration", "NPIMXferReceive", "NPIMXferSendTo", "PIMGetName",
    "PIMGetType", "PIMIsValid",
};

static HMODULE g_module;

#define BIND(field, name)                                                            \
    do {                                                                             \
        FARPROC proc_ = GetProcAddress(g_module, name);                              \
        memcpy(&P.field, &proc_, sizeof proc_);                                      \
    } while (0)

static int bind_plugin(const char *path)
{
    char full[MAX_PATH];
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) {
        return 0;
    }
    g_module = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_module) {
        return 0;
    }
    BIND(Accept, "NPIMAccept");
    BIND(AcceptForXfer, "NPIMAcceptForXfer");
    BIND(Activate, "NPIMActivate");
    BIND(AllowNewConnections, "NPIMAllowNewConnections");
    BIND(CloseConnection, "NPIMCloseConnection");
    BIND(CloseServer, "NPIMCloseServer");
    BIND(Connect, "NPIMConnect");
    BIND(ConnectForXfer, "NPIMConnectForXfer");
    BIND(Deactivate, "NPIMDeactivate");
    BIND(EnumerateNetworkAdapters, "NPIMEnumerateNetworkAdapters");
    BIND(GetClientAddress, "NPIMGetClientAddress");
    BIND(GetConfiguration, "NPIMGetConfiguration");
    BIND(GetDetectedGame, "NPIMGetDetectedGame");
    BIND(GetDetectedGameCount, "NPIMGetDetectedGameCount");
    BIND(GetLocalAddress, "NPIMGetLocalAddress");
    BIND(GetProtocolType, "NPIMGetProtocolType");
    BIND(GetStatus, "NPIMGetStatus");
    BIND(IsAlive, "NPIMIsAlive");
    BIND(Publish, "NPIMPublish");
    BIND(Receive, "NPIMReceive");
    BIND(ReceiveFrom, "NPIMReceiveFrom");
    BIND(ResetClientConnection, "NPIMResetClientConnection");
    BIND(ResetXfer, "NPIMResetXfer");
    BIND(SearchForGame, "NPIMSearchForGame");
    BIND(Send, "NPIMSend");
    BIND(SendSingle, "NPIMSendSingle");
    BIND(SendTo, "NPIMSendTo");
    BIND(SetConfiguration, "NPIMSetConfiguration");
    BIND(XferReceive, "NPIMXferReceive");
    BIND(XferSendTo, "NPIMXferSendTo");
    BIND(PIMGetName, "PIMGetName");
    BIND(PIMGetType, "PIMGetType");
    BIND(PIMIsValid, "PIMIsValid");
    return 1;
}

/* ------------------------------------------------------------------------ */
/* The plugin's datagram obfuscation, re-implemented on the peer side         */
/* ------------------------------------------------------------------------ */

/*
 * Every datagram of the UDP transport is "header byte + payload" where the
 * payload is XORed with an RC4 keystream.  The key is 7 bytes of a fixed
 * 261-byte table, starting at offset (header - 1); the table itself is the
 * output of the engine's URandom seeded with -1.  The header byte is random
 * per datagram (1..254), so the harness decrypts what it captures and prints
 * the plaintext instead of the raw ciphertext.
 */
#define KEY_TABLE_SIZE 0x105
#define KEY_LENGTH 7

static uint8_t g_key_table[KEY_TABLE_SIZE];

static void init_key_table(void)
{
    URandom generator = { 0xffffffffu, 0x343fdu, 0x80000000u, 0x269ec3u };
    for (size_t i = 0; i < KEY_TABLE_SIZE; i++) {
        g_key_table[i] = (uint8_t)(int)(urandom_generate(&generator) * 255.0f);
    }
}

static void rc4_apply(uint8_t header, uint8_t *data, size_t length)
{
    uint8_t state[256];
    unsigned i, j = 0, k = 0;
    if (header == 0) {
        return;
    }
    for (i = 0; i < 256; i++) {
        state[i] = (uint8_t)i;
    }
    for (i = 0; i < 256; i++) {
        uint8_t tmp;
        j = (j + state[i] + g_key_table[(header - 1u) + k]) & 0xff;
        tmp = state[i]; state[i] = state[j]; state[j] = tmp;
        k = (k + 1) % KEY_LENGTH;
    }
    i = j = 0;
    for (size_t n = 0; n < length; n++) {
        uint8_t tmp;
        i = (i + 1) & 0xff;
        j = (j + state[i]) & 0xff;
        tmp = state[i]; state[i] = state[j]; state[j] = tmp;
        data[n] ^= state[(state[i] + state[j]) & 0xff];
    }
}

/* wire = header || cipher(plain) */
static size_t seal(uint8_t *wire, uint8_t header, const uint8_t *plain, size_t plain_length)
{
    wire[0] = header;
    memcpy(wire + 1, plain, plain_length);
    rc4_apply(header, wire + 1, plain_length);
    return plain_length + 1;
}

/* Decrypt a captured datagram; returns the plaintext length. */
static size_t unseal(uint8_t *plain, const uint8_t *wire, size_t wire_length)
{
    if (wire_length == 0) {
        return 0;
    }
    memcpy(plain, wire + 1, wire_length - 1);
    rc4_apply(wire[0], plain, wire_length - 1);
    return wire_length - 1;
}

static const char *header_text(uint8_t header)
{
    return (header >= 1 && header <= 254) ? "<random 1..254>" : "INVALID";
}

/* ------------------------------------------------------------------------ */
/* Raw Winsock peer                                                          */
/* ------------------------------------------------------------------------ */

static struct sockaddr_in make_addr(const char *ip, unsigned port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = ip ? inet_addr(ip) : htonl(INADDR_ANY);
    return addr;
}

/* UDP socket, optionally bound.  `reuse` lets us share a port with the plugin. */
static SOCKET udp_socket(const char *ip, unsigned port, int reuse)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (reuse) {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
    }
    if (ip || port) {
        struct sockaddr_in addr = make_addr(ip, port);
        if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    return s;
}

static int wait_readable(SOCKET s, int timeout_ms)
{
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET(s, &set);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, &set, NULL, NULL, &tv) > 0;
}

static int udp_receive(SOCKET s, uint8_t *buffer, int capacity, struct sockaddr_in *from, int timeout_ms)
{
    int from_length = sizeof *from;
    if (!wait_readable(s, timeout_ms)) {
        return -1;
    }
    return recvfrom(s, (char *)buffer, capacity, 0, (struct sockaddr *)from, &from_length);
}

static int udp_send(SOCKET s, const void *data, int length, const struct sockaddr_in *to)
{
    return sendto(s, (const char *)data, length, 0, (const struct sockaddr *)to, sizeof *to);
}

static SOCKET tcp_listener(const char *ip, unsigned port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = make_addr(ip, port);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static SOCKET tcp_accept_peer(SOCKET listener, int timeout_ms)
{
    if (!wait_readable(listener, timeout_ms)) {
        return INVALID_SOCKET;
    }
    return accept(listener, NULL, NULL);
}

static SOCKET tcp_connect_peer(const char *ip, unsigned port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = make_addr(ip, port);
    if (s == INVALID_SOCKET) {
        return s;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static int tcp_read(SOCKET s, uint8_t *buffer, int capacity, int timeout_ms)
{
    if (!wait_readable(s, timeout_ms)) {
        return -1;
    }
    return recv(s, (char *)buffer, capacity, 0);
}

/* ------------------------------------------------------------------------ */
/* Small helpers for engine objects and polling                              */
/* ------------------------------------------------------------------------ */

static const char *chars_of(const UString *s) { return (s && s->chars) ? s->chars : "(null)"; }
static const char *wchars_of(const UWideString *s) { return (s && s->chars) ? s->chars : "(null)"; }

static NeHandle *new_handle(void)
{
    NeHandle *handle = calloc(1, sizeof *handle);
    handle->protocol = (void *)(uintptr_t)0x4e455052; /* "NEPR" */
    return handle;
}

static TSEvent *new_event(void)
{
    TSEvent *event = NULL;
    tsscheduler_create_event(&event, 1);
    return event;
}

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

static const char *ipv4_text(const char *text)
{
    unsigned a, b, c, d;
    char tail;
    return (text && sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 &&
            a < 256 && b < 256 && c < 256 && d < 256) ? "dotted-quad" : "NOT-AN-ADDRESS";
}

/* ------------------------------------------------------------------------ */
/* Scenarios                                                                 */
/* ------------------------------------------------------------------------ */

#define GAME_PORT 33334u
#define XFER_PORT 33335u

static void scenario_exports(void)
{
    line("== exports");
    for (int i = 0; i < 33; i++) {
        FARPROC by_name = GetProcAddress(g_module, kExportNames[i]);
        FARPROC by_ordinal = GetProcAddress(g_module, (LPCSTR)(uintptr_t)(i + 1));
        line("export %2d %-28s present=%s by_ordinal=%s", i + 1, kExportNames[i],
             yesno(by_name != NULL), yesno(by_name != NULL && by_name == by_ordinal));
    }
}

static void scenario_identity(void)
{
    line("== identity (before NPIMActivate)");
    UString *name = P.PIMGetName();
    line("PIMGetName -> \"%s\" length=%lu same_object_every_call=%s", chars_of(name),
         name ? (unsigned long)name->length : 0ul, yesno(name != NULL && name == P.PIMGetName()));
    line("PIMGetType -> 0x%08lx", P.PIMGetType());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMGetProtocolType -> %lu", P.GetProtocolType());
}

/* Everything after the activation scenario needs a working Winsock stack inside the plugin. */
static int g_plugin_active;

static void scenario_activation(void)
{
    UWideString good, other;
    uwidestring_construct_from_chars(&good, "WinSock 2.0");
    uwidestring_construct_from_chars(&other, "WinSock 1.1");

    line("== activation");
    long activated = P.Activate();
    g_plugin_active = activated == 0;
    line("NPIMActivate -> %ld", activated);
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMGetStatus(\"WinSock 2.0\", 0) -> %ld", P.GetStatus(&good, 0));
    line("NPIMGetStatus(\"WinSock 2.0\", 0x82360000) -> %ld", P.GetStatus(&good, 0x82360000ul));
    line("NPIMGetStatus(\"WinSock 1.1\", 0) -> %ld", P.GetStatus(&other, 0));
    line("NPIMDeactivate -> %ld", P.Deactivate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMActivate -> %ld", P.Activate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMActivate (already active) -> %ld", P.Activate());
    line("PIMIsValid -> %d", P.PIMIsValid());

    uwidestring_destruct(&good);
    uwidestring_destruct(&other);
}

static void scenario_configuration(void)
{
    NEProtocolConfiguration *config = NULL;
    NEProtocolConfiguration three, versioned, matching;

    line("== configuration");
    long rc = P.GetConfiguration(&config);
    line("NPIMGetConfiguration -> %ld object=%s parameters=%lu version=%lu", rc, config ? "returned" : "null",
         config ? (unsigned long)config->parameterCount : 0ul, config ? (unsigned long)config->version : 0ul);
    line("NPIMSetConfiguration(returned copy) -> %ld", config ? P.SetConfiguration(config) : -1l);

    neprotocolconfiguration_construct(&three, 3, 1);
    line("NPIMSetConfiguration(3 parameters, version 1) -> %ld", P.SetConfiguration(&three));
    neprotocolconfiguration_construct(&versioned, 0, 2);
    line("NPIMSetConfiguration(0 parameters, version 2) -> %ld", P.SetConfiguration(&versioned));
    neprotocolconfiguration_construct(&matching, 0, 1);
    line("NPIMSetConfiguration(0 parameters, version 1) -> %ld", P.SetConfiguration(&matching));

    neprotocolconfiguration_destruct(&three);
    neprotocolconfiguration_destruct(&versioned);
    neprotocolconfiguration_destruct(&matching);
    if (config) {
        neprotocolconfiguration_destruct(config);
        free(config);
    }
}

static void scenario_network_info(void)
{
    WideStringVector adapters = { 0, NULL, NULL, NULL };

    line("== network information");
    unsigned long count = P.EnumerateNetworkAdapters(&adapters);
    unsigned long size = adapters.first ? (unsigned long)(adapters.last - adapters.first) : 0ul;
    int names_ok = 1;
    for (UWideString *it = adapters.first; it && it != adapters.last; it++) {
        names_ok &= it->chars != NULL && it->length > 0;
    }
    line("NPIMEnumerateNetworkAdapters -> count_at_most_10=%s vector_size_equals_count=%s names_non_empty=%s",
         yesno(count <= 10), yesno(size == count), yesno(names_ok));
    unsigned long again = P.EnumerateNetworkAdapters(&adapters);
    unsigned long size_again = adapters.first ? (unsigned long)(adapters.last - adapters.first) : 0ul;
    line("NPIMEnumerateNetworkAdapters (second call, same vector) -> same_count=%s size_not_doubled=%s",
         yesno(again == count), yesno(size_again == again));
    for (UWideString *it = adapters.first; it && it != adapters.last; it++) {
        uwidestring_destruct(it);
    }
    free(adapters.first);

    for (unsigned long adapter = 0; adapter <= 1; adapter++) {
        UWideString *address = P.GetLocalAddress(adapter);
        line("NPIMGetLocalAddress(%lu) -> %s", adapter, address ? ipv4_text(address->chars) : "null");
    }
    UWideString *first = P.GetLocalAddress(0);
    line("NPIMGetLocalAddress returns one shared object: %s", yesno(first != NULL && first == P.GetLocalAddress(1)));
    char first_text[64];
    snprintf(first_text, sizeof first_text, "%s", wchars_of(first));
    UWideString *far_adapter = P.GetLocalAddress(99);
    line("NPIMGetLocalAddress(99) -> %s same_as_adapter_0=%s", far_adapter ? ipv4_text(far_adapter->chars) : "null",
         yesno(far_adapter && strcmp(first_text, wchars_of(far_adapter)) == 0));
}

static void scenario_empty_detected_games(void)
{
    UString address, service;
    UWideString name;
    ustring_construct(&address);
    ustring_construct(&service);
    uwidestring_construct(&name);
    line("== detected games (nothing searched yet)");
    line("NPIMGetDetectedGameCount -> %lu", P.GetDetectedGameCount());
    line("NPIMGetDetectedGame(0) -> %ld", P.GetDetectedGame(0, &address, &service, &name));
    ustring_destruct(&address);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
}

/* NPIMSendSingle: one raw, unencrypted datagram to an arbitrary port. */
static void scenario_send_single(void)
{
    static const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x41, 0x42, 0x43 };
    uint8_t buffer[64];
    struct sockaddr_in from;
    UString local, nowhere;
    SOCKET capture = udp_socket("127.0.0.1", 40101, 1);

    line("== NPIMSendSingle");
    ustring_construct_from_chars(&local, "127.0.0.1");
    long rc = P.SendSingle(&local, 40101, payload, sizeof payload);
    int n = capture == INVALID_SOCKET ? -1 : udp_receive(capture, buffer, sizeof buffer, &from, 2000);
    line("NPIMSendSingle(\"127.0.0.1\", 40101, 8 bytes) -> %ld", rc);
    line("  peer received %d bytes: %s", n, n > 0 ? hex(buffer, (size_t)n) : "-");
    line("  source port %s", n > 0 ? port_text(ntohs(from.sin_port)) : "-");

    ustring_construct_from_chars(&nowhere, "no such host!");
    line("NPIMSendSingle(unresolvable host) -> %ld", P.SendSingle(&nowhere, 40101, payload, sizeof payload));

    ustring_destruct(&local);
    ustring_destruct(&nowhere);
    if (capture != INVALID_SOCKET) {
        closesocket(capture);
    }
}

/* Fabricate and show the first byte plus plaintext of a captured datagram. */
static void show_wire(const char *label, const uint8_t *wire, int length)
{
    uint8_t plain[2048];
    if (length <= 0) {
        line("%s: nothing received", label);
        return;
    }
    size_t plain_length = unseal(plain, wire, (size_t)length);
    uint8_t again[2048];
    size_t again_length = seal(again, wire[0], plain, plain_length);
    line("%s: wire_length=%d header=%s plaintext=%s re_encrypts_to_wire=%s", label, length, header_text(wire[0]),
         hex(plain, plain_length), yesno(again_length == (size_t)length && memcmp(again, wire, again_length) == 0));
}

/*
 * UDP client connection, with the harness acting as the server.  The harness
 * socket shares port 33334 with nothing else, so it sees exactly the bytes
 * the client puts on the wire and can answer with crafted datagrams.
 */
static void scenario_udp_client_wire(void)
{
    UString address, service;
    UWideString name;
    NeHandle *conn = new_handle();
    TSEvent *event = new_event();
    uint8_t buffer[512], wire[512], plain[512];
    unsigned long received = 0xdeadbeefu;
    struct sockaddr_in from, client_addr;
    SOCKET server = udp_socket("127.0.0.1", GAME_PORT, 1);

    line("== UDP client connection against a raw peer");
    ustring_construct_from_chars(&address, "127.0.0.1");
    ustring_construct_from_chars(&service, "ee-test");
    uwidestring_construct_from_chars(&name, "Test Game");

    long rc = P.Connect(&address, &service, &name, 2, event, conn, 0);
    line("NPIMConnect(\"127.0.0.1\", \"ee-test\", \"Test Game\", UDP, adapter 0) -> %ld connection=%s", rc,
         conn->impl ? "created" : "null");
    line("NPIMIsAlive(UDP connection) -> %d", P.IsAlive(conn));

    /* Outgoing: header byte + XOR-stream-encrypted payload, sent to port 33334. */
    static const uint8_t hello[] = { 0x00, 0x22, 'H', 'E', 'L', 'L', 'O', ' ', 'U', 'D', 'P' };
    memcpy(buffer, hello, sizeof hello);
    rc = P.Send(conn, buffer, sizeof hello);
    int n = udp_receive(server, wire, sizeof wire, &from, 2000);
    line("NPIMSend(UDP, 11 bytes) -> %ld", rc);
    show_wire("  peer captured", wire, n);
    line("  caller buffer mutated in place: header_changed=%s payload_encrypted=%s",
         yesno(buffer[0] != 0 && buffer[0] == wire[0]), yesno(n > 0 && memcmp(buffer + 1, wire + 1, sizeof hello - 1) == 0));
    line("  source address %s port %s", n > 0 ? inet_ntoa(from.sin_addr) : "-", n > 0 ? port_text(ntohs(from.sin_port)) : "-");
    client_addr = from;

    /* Incoming: nothing pending yet. */
    received = 0xdeadbeefu;
    rc = P.Receive(conn, sizeof buffer, buffer, &received);
    line("NPIMReceive(nothing pending) -> %ld received=0x%lx", rc, received);

    /* A stranger's datagram, a discovery-reply look-alike and a probe look-alike must all be skipped. */
    SOCKET stranger = udp_socket(NULL, 0, 0);
    static const uint8_t noise[] = { 0x77, 0x01, 0x02, 0x03 };
    uint8_t spoof[0x88];
    memset(plain, 0x00, sizeof plain);
    plain[0] = 0x16;
    size_t spoof_length = seal(spoof, 0x31, plain, 0x87);
    udp_send(stranger, noise, sizeof noise, &client_addr);
    udp_send(server, spoof, (int)spoof_length, &client_addr);   /* right source, but a 0x88/0x16 packet */
    static const uint8_t reply[] = { 0x01, 'R', 'E', 'P', 'L', 'Y' };
    size_t reply_length = seal(wire, 0x5a, reply, sizeof reply);
    udp_send(server, wire, (int)reply_length, &client_addr);
    Sleep(100);
    received = 0xdeadbeefu;
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Receive(conn, sizeof buffer, buffer, &received));
    line("NPIMReceive(stranger + reply-look-alike + real reply) -> %ld received=%lu", rc, received);
    if (rc == 0) {
        line("  header=0x%02x plaintext=%s", buffer[0], hex(buffer + 1, received - 1));
    }
    /* Remaining skipped datagrams do not produce anything further. */
    received = 0xdeadbeefu;
    rc = P.Receive(conn, sizeof buffer, buffer, &received);
    line("NPIMReceive(queue now empty) -> %ld received=%lu", rc, received);
    closesocket(stranger);

    /* A datagram larger than the caller's buffer is an OS-level error, not a short read. */
    n = (int)seal(wire, 0x5a, reply, sizeof reply);
    udp_send(server, wire, n, &client_addr);
    Sleep(80);
    received = 0xdeadbeefu;
    rc = P.Receive(conn, 4, buffer, &received);
    line("NPIMReceive(datagram bigger than the 4-byte buffer) -> %ld", rc);
    received = 0xdeadbeefu;
    rc = P.Receive(conn, sizeof buffer, buffer, &received);
    line("NPIMReceive(afterwards) -> %ld received=0x%lx", rc, received);

    /* Degenerate sends: header byte only, and nothing at all. */
    memset(buffer, 0x11, 4);
    rc = P.Send(conn, buffer, 1);
    n = udp_receive(server, wire, sizeof wire, &from, 1000);
    line("NPIMSend(UDP, 1 byte) -> %ld; peer received %d bytes", rc, n);
    rc = P.Send(conn, buffer, 0);
    n = udp_receive(server, wire, sizeof wire, &from, 1000);
    line("NPIMSend(UDP, 0 bytes) -> %ld; peer received %d bytes", rc, n);

    /* The header byte is random per datagram, within 1..254; every datagram must decrypt. */
    {
        unsigned decrypted = 0, lowest = 255, highest = 0, distinct = 0;
        unsigned char seen[256];
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < 300; i++) {
            uint8_t message[16], expected[15];
            message[0] = 0;
            message[1] = 0x22;
            for (int j = 2; j < 16; j++) {
                message[j] = (uint8_t)(i * 7 + j);
            }
            memcpy(expected, message + 1, sizeof expected);
            P.Send(conn, message, sizeof message);
            n = udp_receive(server, wire, sizeof wire, &from, 1000);
            if (n != (int)sizeof message) {
                break;
            }
            unseal(plain, wire, (size_t)n);
            decrypted += memcmp(plain, expected, sizeof expected) == 0;
            lowest = wire[0] < lowest ? wire[0] : lowest;
            highest = wire[0] > highest ? wire[0] : highest;
            distinct += seen[wire[0]]++ == 0;
        }
        line("NPIMSend x300: decrypted=%u/300 header_range_1_to_254=%s many_distinct_headers=%s", decrypted,
             yesno(lowest >= 1 && highest <= 254), yesno(distinct > 100));
    }

    /* The bulk-transfer connection goes to port 33335 of the server; nobody listens there. */
    {
        NeHandle *refused = new_handle();
        rc = P.ConnectForXfer(conn, 1, event, refused);
        line("NPIMConnectForXfer(TCP, nobody listening) -> %ld connection=%s", rc, refused->impl ? "created" : "null");
        received = 0xdeadbeefu;
        rc = P.XferReceive(sizeof buffer, buffer, &received);
        line("NPIMXferReceive(on the failed transfer socket) -> %ld received=0x%lx", rc, received);
        free(refused);
    }

    rc = P.CloseConnection(conn);
    line("NPIMCloseConnection(UDP) -> %ld connection=%s", rc, conn->impl ? "still set" : "cleared");

    ustring_destruct(&address);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    if (server != INVALID_SOCKET) {
        closesocket(server);
    }
    tsscheduler_destroy_event(event);
    free(conn);
}

/* TCP client connection against a raw listener. */
static void scenario_tcp_client(void)
{
    UString address, service;
    UWideString name;
    NeHandle *conn = new_handle();
    TSEvent *event = new_event();
    uint8_t buffer[256];
    unsigned long received = 0xdeadbeefu;
    SOCKET listener = tcp_listener("127.0.0.1", GAME_PORT);

    line("== TCP client connection against a raw listener");
    ustring_construct_from_chars(&address, "127.0.0.1");
    ustring_construct_from_chars(&service, "ee-test");
    uwidestring_construct_from_chars(&name, "Test Game");

    long rc = P.Connect(&address, &service, &name, 1, event, conn, 0);
    SOCKET peer = listener == INVALID_SOCKET ? INVALID_SOCKET : tcp_accept_peer(listener, 1500);
    line("NPIMConnect(\"127.0.0.1\", TCP) -> %ld connection=%s peer_accepted=%s", rc,
         conn->impl ? "created" : "null", yesno(peer != INVALID_SOCKET));
    if (conn->impl) {
        static const uint8_t hello[] = "TCP-HELLO";
        rc = P.Send(conn, hello, sizeof hello - 1);
        int n = peer == INVALID_SOCKET ? -1 : tcp_read(peer, buffer, sizeof buffer, 2000);
        line("NPIMSend(TCP, 9 bytes) -> %ld; peer read %d bytes: %s", rc, n, n > 0 ? hex(buffer, (size_t)n) : "-");
        line("NPIMIsAlive(TCP) -> %d", P.IsAlive(conn));
        received = 0xdeadbeefu;
        rc = P.Receive(conn, sizeof buffer, buffer, &received);
        line("NPIMReceive(nothing pending) -> %ld received=0x%lx", rc, received);
        if (peer != INVALID_SOCKET) {
            static const uint8_t answer[] = { 'P', 'E', 'E', 'R', 0x00, 0xff };
            send(peer, (const char *)answer, sizeof answer, 0);
        }
        received = 0xdeadbeefu;
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Receive(conn, sizeof buffer, buffer, &received));
        line("NPIMReceive(peer sent 6 bytes) -> %ld received=%lu bytes=%s", rc, received,
             rc == 0 ? hex(buffer, received) : "-");
        if (peer != INVALID_SOCKET) {
            closesocket(peer);
            peer = INVALID_SOCKET;
        }
        Sleep(100);
        line("NPIMIsAlive(after peer closed) -> %d", P.IsAlive(conn));
        received = 0xdeadbeefu;
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Receive(conn, sizeof buffer, buffer, &received));
        line("NPIMReceive(after peer closed) -> %ld", rc);
        rc = P.CloseConnection(conn);
        line("NPIMCloseConnection(TCP) -> %ld connection=%s", rc, conn->impl ? "still set" : "cleared");
    }
    if (peer != INVALID_SOCKET) {
        closesocket(peer);
    }
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }

    /* Calls on a connection object the plugin never filled in. */
    NeHandle empty = { NULL, NULL };
    received = 0xdeadbeefu;
    rc = P.Receive(&empty, sizeof buffer, buffer, &received);
    line("NPIMReceive(empty connection) -> %ld received=0x%lx", rc, received);
    line("NPIMConnect(protocol 0) -> %ld", P.Connect(&address, &service, &name, 0, event, &empty, 0));
    ustring_destruct(&address);
    ustring_construct_from_chars(&address, "no such host!");
    line("NPIMConnect(unresolvable host, UDP) -> %ld", P.Connect(&address, &service, &name, 2, event, &empty, 0));

    ustring_destruct(&address);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    tsscheduler_destroy_event(event);
    free(conn);
}


/* ------------------------------------------------------------------------ */
/* Published servers                                                         */
/* ------------------------------------------------------------------------ */

/* First non-loopback IPv4 address of this host, or NULL. */
static const char *lan_address(void)
{
    static char text[32];
    char host[256];
    struct hostent *entry;
    if (gethostname(host, sizeof host) != 0 || !(entry = gethostbyname(host))) {
        return NULL;
    }
    for (int i = 0; entry->h_addr_list[i]; i++) {
        const char *candidate = inet_ntoa(*(struct in_addr *)entry->h_addr_list[i]);
        if (strncmp(candidate, "127.", 4) != 0) {
            snprintf(text, sizeof text, "%s", candidate);
            return text;
        }
    }
    return NULL;
}

typedef struct ServerEvents {
    TSEvent *activity;   /* socket activity (FD_READ) */
    TSEvent *xfer_data;  /* bulk-transfer listener, FD_READ */
    TSEvent *xfer_accept;/* bulk-transfer listener, FD_ACCEPT */
} ServerEvents;

static ServerEvents new_server_events(void)
{
    ServerEvents events = { new_event(), new_event(), new_event() };
    return events;
}

static void free_server_events(ServerEvents *events)
{
    tsscheduler_destroy_event(events->activity);
    tsscheduler_destroy_event(events->xfer_data);
    tsscheduler_destroy_event(events->xfer_accept);
}

static long publish(NeHandle *server, const char *service_text, const char *name_text, unsigned long protocols,
                    ServerEvents *events, int skip_duplicate_check, int visible)
{
    UString service;
    UWideString name;
    ustring_construct_from_chars(&service, service_text);
    uwidestring_construct_from_chars(&name, name_text);
    long rc = P.Publish(&service, &name, protocols, events->activity, events->xfer_data, events->xfer_accept,
                        server, skip_duplicate_check, visible, 0);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    return rc;
}

/* Publish only some transports and probe what the remaining calls refuse. */
static void scenario_single_transport_servers(void)
{
    uint8_t buffer[64];
    unsigned long received = 0xdeadbeefu, client = 0xfeedf00du;
    static const uint8_t payload[] = { 0x00, 0x01, 0x02 };

    line("== servers publishing a single transport");
    for (unsigned long protocols = 0; protocols <= 2; protocols++) {
        NeHandle *server = new_handle();
        NeHandle *accepted = new_handle();
        ServerEvents events = new_server_events();
        const char *label = protocols == 0 ? "no transport" : protocols == 1 ? "TCP only" : "UDP only";

        long rc = publish(server, "ee-single", "Single", protocols, &events, 1, 1);
        line("NPIMPublish(%s) -> %ld server=%s", label, rc, server->impl ? "created" : "null");
        if (server->impl) {
            rc = P.Accept(server, accepted);
            line("  NPIMAccept -> %ld connection=%s", rc, accepted->impl ? "created" : "null");
            received = 0xdeadbeefu;
            client = 0xfeedf00du;
            rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
            line("  NPIMReceiveFrom -> %ld received=0x%lx client=0x%lx", rc, received, client);
            memcpy(buffer, payload, sizeof payload);
            line("  NPIMSendTo(client 0) -> %ld", P.SendTo(server, 0, buffer, sizeof payload));
            UWideString address;
            uwidestring_construct(&address);
            rc = P.GetClientAddress(server, 0, &address);
            line("  NPIMGetClientAddress(0) -> %ld \"%s\"", rc, wchars_of(&address));
            uwidestring_destruct(&address);
            rc = P.CloseServer(server);
            line("  NPIMCloseServer -> %ld server=%s", rc, server->impl ? "still set" : "cleared");
        }
        free_server_events(&events);
        free(accepted);
        free(server);
    }

    /* A second server cannot bind the ports the first one holds. */
    {
        NeHandle *first = new_handle(), *second = new_handle();
        ServerEvents events = new_server_events();
        long rc = publish(first, "ee-first", "First", 3, &events, 1, 1);
        long rc2 = publish(second, "ee-second", "Second", 3, &events, 1, 1);
        line("NPIMPublish(TCP+UDP) twice -> %ld then %ld (second server=%s)", rc, rc2, second->impl ? "created" : "null");
        rc = P.CloseServer(first);
        line("NPIMCloseServer(first) -> %ld", rc);
        free_server_events(&events);
        free(first);
        free(second);
    }
}

static long receive_from_poll(NeHandle *server, uint8_t *buffer, unsigned long capacity, unsigned long *received,
                              unsigned long *client)
{
    long rc;
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.ReceiveFrom(server, capacity, buffer, received, client));
    return rc;
}

/* A UDP client built from the plugin itself talking to the published server. */
static NeHandle *connect_udp_client(const char *service_text, const char *name_text, TSEvent *event)
{
    UString address, service;
    UWideString name;
    NeHandle *conn = new_handle();
    ustring_construct_from_chars(&address, "127.0.0.1");
    ustring_construct_from_chars(&service, service_text);
    uwidestring_construct_from_chars(&name, name_text);
    long rc = P.Connect(&address, &service, &name, 2, event, conn, 0);
    if (rc != 0 || !conn->impl) {
        line("  (client connect failed: %ld)", rc);
    }
    ustring_destruct(&address);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    return conn;
}

static void show_plain(const char *label, long rc, const uint8_t *buffer, unsigned long received, unsigned long client)
{
    if (rc == 0 && received > 0 && received < 0x10000u) {
        line("%s -> %ld received=%lu client=%lu header=%s plaintext=%s", label, rc, received, client,
             header_text(buffer[0]), hex(buffer + 1, received - 1));
    } else {
        line("%s -> %ld received=0x%lx client=0x%lx", label, rc, received, client);
    }
}

static void scenario_udp_server(void)
{
    static const uint8_t hello[] = { 0x00, 0x22, 'H', 'E', 'L', 'L', 'O' };
    static const uint8_t reply[] = { 0x00, 0x01, 'R', 'E', 'P', 'L', 'Y' };
    uint8_t buffer[512], wire[512], plain[512];
    unsigned long received, client;
    struct sockaddr_in scratch;
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    TSEvent *client_event = new_event();

    line("== published server (TCP + UDP) with a plugin UDP client");
    long rc = publish(server, "ee-test", "Test Game", 3, &events, 0, 1);
    line("NPIMPublish(\"ee-test\", \"Test Game\", TCP+UDP, duplicate check on) -> %ld server=%s", rc,
         server->impl ? "created" : "null");
    NeHandle *cli = connect_udp_client("ee-test", "Test Game", client_event);

    /* First datagram of a new client registers it (payload type 0x22). */
    memcpy(buffer, hello, sizeof hello);
    P.Send(cli, buffer, sizeof hello);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(first datagram of a client)", rc, buffer, received, client);

    UWideString address;
    uwidestring_construct(&address);
    rc = P.GetClientAddress(server, 0, &address);
    line("NPIMGetClientAddress(0) -> %ld \"%s\"", rc, wchars_of(&address));
    rc = P.GetClientAddress(server, 7, &address);
    line("NPIMGetClientAddress(7) -> %ld \"%s\"", rc, wchars_of(&address));
    uwidestring_destruct(&address);

    /* Server -> client. */
    memcpy(buffer, reply, sizeof reply);
    rc = P.SendTo(server, 0, buffer, sizeof reply);
    line("NPIMSendTo(client 0, 7 bytes) -> %ld", rc);
    received = 0xdeadbeefu;
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Receive(cli, sizeof buffer, buffer, &received));
    show_plain("NPIMReceive(client side)", rc, buffer, received, 0);
    memcpy(buffer, reply, sizeof reply);
    line("NPIMSendTo(client 9) -> %ld", P.SendTo(server, 9, buffer, sizeof reply));

    /* Nothing pending. */
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    line("NPIMReceiveFrom(nothing pending) -> %ld received=%lu client=0x%lx", rc, received, client);

    /* A known client keeps its index; resetting it makes the next hello a new client. */
    memcpy(buffer, hello, sizeof hello);
    P.Send(cli, buffer, sizeof hello);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(second datagram, same client)", rc, buffer, received, client);
    P.ResetClientConnection(server, 0);
    P.ResetClientConnection(server, 99);
    memcpy(buffer, hello, sizeof hello);
    P.Send(cli, buffer, sizeof hello);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(after NPIMResetClientConnection(0))", rc, buffer, received, client);

    /* Unknown senders: only payload types 0x22 / 0x37 of at least 4 bytes may join, and only while accepting. */
    SOCKET stranger = udp_socket(NULL, 0, 0), stranger2 = udp_socket(NULL, 0, 0);
    struct sockaddr_in server_addr = make_addr("127.0.0.1", GAME_PORT);
    static const uint8_t join37[] = { 0x37, 0x01, 0x02, 0x03, 0x04 };
    static const uint8_t other[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    static const uint8_t tiny[] = { 0x22, 0x01 };
    static const uint8_t small[] = { 0x22, 0x01, 0x02 };

    P.AllowNewConnections(server, 0);
    size_t n = seal(wire, 0x21, join37, sizeof join37);
    udp_send(stranger, wire, (int)n, &server_addr);
    Sleep(80);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    line("NPIMReceiveFrom(new sender while connections are refused) -> %ld received=0x%lx client=0x%lx", rc, received, client);

    P.AllowNewConnections(server, 1);
    n = seal(wire, 0x21, other, sizeof other);
    udp_send(stranger2, wire, (int)n, &server_addr);
    Sleep(80);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    line("NPIMReceiveFrom(new sender, payload type 0x01) -> %ld received=0x%lx client=0x%lx", rc, received, client);
    n = seal(wire, 0x21, tiny, sizeof tiny);
    udp_send(stranger2, wire, (int)n, &server_addr);
    Sleep(80);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    line("NPIMReceiveFrom(new sender, datagram of 3 bytes, type 0x22) -> %ld received=0x%lx client=0x%lx", rc, received, client);
    n = seal(wire, 0x21, small, sizeof small);
    udp_send(stranger2, wire, (int)n, &server_addr);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(new sender, datagram of 4 bytes, type 0x22)", rc, buffer, received, client);
    n = seal(wire, 0x21, join37, sizeof join37);
    udp_send(stranger, wire, (int)n, &server_addr);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(new sender, payload type 0x37)", rc, buffer, received, client);
    line("  (header byte 0x21 preserved: %s)", yesno(rc == 0 && buffer[0] == 0x21));

    /* Header byte 0 disables the keystream: the payload travels in clear. */
    n = seal(wire, 0x00, join37, sizeof join37);
    udp_send(stranger, wire, (int)n, &server_addr);
    received = 0xdeadbeefu; client = 0xfeedf00du;
    rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
    show_plain("NPIMReceiveFrom(header byte 0, clear payload)", rc, buffer, received, client);

    /* Every header value selects another window of the key table: decrypt across the whole table. */
    {
        unsigned total = 0, correct = 0;
        char failures[256] = "";
        static const uint8_t body[] = { 0x01, 'k', 'e', 'y', 't', 'a', 'b', 'l', 'e' };
        for (unsigned h = 1; h <= 255; h += (h < 253 ? 7 : 1)) {
            n = seal(wire, (uint8_t)h, body, sizeof body);
            udp_send(stranger, wire, (int)n, &server_addr);
            received = 0xdeadbeefu; client = 0xfeedf00du;
            rc = receive_from_poll(server, buffer, sizeof buffer, &received, &client);
            total++;
            if (rc == 0 && received == sizeof body + 1 && client == 3 && memcmp(buffer + 1, body, sizeof body) == 0) {
                correct++;
            } else if (strlen(failures) < 200) {
                sprintf(failures + strlen(failures), " %u", h);
            }
        }
        line("NPIMReceiveFrom over %u header values (whole key table): decrypted correctly=%u failures:%s", total, correct,
             failures[0] ? failures : " none");
    }

    /* Datagrams of a realistic size in both directions. */
    {
        static uint8_t large[1400], big_wire[2048], big_buffer[2048], big_plain[2048];
        large[0] = 0x01;
        for (size_t i = 1; i < sizeof large; i++) {
            large[i] = (uint8_t)(i * 31 + 7);
        }
        n = seal(big_wire, 0x66, large, sizeof large);
        udp_send(stranger, big_wire, (int)n, &server_addr);
        received = 0xdeadbeefu; client = 0xfeedf00du;
        rc = receive_from_poll(server, big_buffer, sizeof big_buffer, &received, &client);
        line("NPIMReceiveFrom(1400-byte payload) -> %ld received=%lu client=%lu intact=%s", rc, received, client,
             yesno(rc == 0 && received == sizeof large + 1 && memcmp(big_buffer + 1, large, sizeof large) == 0));
        memcpy(big_buffer + 1, large, sizeof large);
        rc = P.SendTo(server, client, big_buffer, sizeof large + 1);
        int got = udp_receive(stranger, big_wire, sizeof big_wire, &scratch, 2000);
        size_t m = got > 0 ? unseal(big_plain, big_wire, (size_t)got) : 0;
        line("NPIMSendTo(1400-byte payload) -> %ld; peer received %d bytes intact=%s", rc, got,
             yesno(m == sizeof large && memcmp(big_plain, large, sizeof large) == 0));
    }

    /* Server pushes to the stranger's index, the stranger's socket sees the encrypted datagram. */
    memcpy(buffer, reply, sizeof reply);
    rc = P.SendTo(server, client, buffer, sizeof reply);
    n = (size_t)udp_receive(stranger, wire, sizeof wire, &scratch, 2000);
    line("NPIMSendTo(to the registered stranger) -> %ld", rc);
    show_wire("  stranger captured", wire, (int)n);

    /* Game-discovery probe: answered with the 0x88-byte, type 0x16 reply (sent to port 33334 of the asker). */
    static const char *const probes[] = { "ee-test", "another-service" };
    for (unsigned p = 0; p < 2; p++) {
        uint8_t probe_plain[0x42];
        memset(probe_plain, 0, sizeof probe_plain);
        probe_plain[0] = 0x15;
        strcpy((char *)probe_plain + 1, probes[p]);
        n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
        udp_send(stranger2, wire, (int)n, &server_addr);
        Sleep(100);
        SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
        received = 0xdeadbeefu; client = 0xfeedf00du;
        rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
        line("NPIMReceiveFrom(probe for \"%s\") -> %ld received=0x%lx client=0x%lx", probes[p], rc, received, client);
        struct sockaddr_in from;
        int got = capture == INVALID_SOCKET ? -1 : udp_receive(capture, wire, sizeof wire, &from, p == 0 ? 2000 : 400);
        if (got > 0) {
            size_t plain_length = unseal(plain, wire, (size_t)got);
            line("  reply: wire_length=%d header=%s type=0x%02x service=\"%s\" name=\"%s\" (padding ignored)", got,
                 header_text(wire[0]), plain[0], (const char *)plain + 1, plain_length > 0x42 ? (const char *)plain + 0x42 : "");
        } else {
            line("  reply: none");
        }
        if (capture != INVALID_SOCKET) {
            closesocket(capture);
        }
    }
    P.AllowNewConnections(server, 0);
    {
        uint8_t probe_plain[0x42];
        memset(probe_plain, 0, sizeof probe_plain);
        probe_plain[0] = 0x15;
        strcpy((char *)probe_plain + 1, "ee-test");
        n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
        udp_send(stranger2, wire, (int)n, &server_addr);
        Sleep(100);
        SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
        rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
        int got = capture == INVALID_SOCKET ? -1 : udp_receive(capture, wire, sizeof wire, &scratch, 400);
        line("NPIMReceiveFrom(probe while connections are refused) -> %ld; reply %s", rc, got > 0 ? "sent" : "none");
        if (capture != INVALID_SOCKET) {
            closesocket(capture);
        }
    }
    P.AllowNewConnections(server, 1);
    closesocket(stranger);
    closesocket(stranger2);

    /* Bulk-transfer side channel (TCP port 33335), matched to a registered client by address. */
    NeHandle *xclient = new_handle(), *xserver = new_handle(), *tconn = new_handle();
    uint8_t xbuffer[64];
    unsigned long xreceived = 0xdeadbeefu;
    {
        static const uint8_t blob[] = { 'X', 'F', 'E', 'R', 0x00, 0x01, 0x02, 0x03 };

        rc = P.AcceptForXfer(server, xserver);
        line("NPIMAcceptForXfer(nothing pending) -> %ld connection=%s", rc, xserver->impl ? "created" : "null");
        rc = P.ConnectForXfer(cli, 2, client_event, xclient);
        line("NPIMConnectForXfer(UDP) -> %ld", rc);
        NeHandle none = { NULL, NULL };
        rc = P.ConnectForXfer(&none, 1, client_event, xclient);
        line("NPIMConnectForXfer(connection without state) -> %ld", rc);
        rc = P.ConnectForXfer(cli, 1, client_event, xclient);
        line("NPIMConnectForXfer(TCP) -> %ld connection=%s", rc, xclient->impl ? "created" : "null");
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.AcceptForXfer(server, xserver));
        line("NPIMAcceptForXfer -> %ld connection=%s", rc, xserver->impl ? "created" : "null");
        rc = P.XferSendTo(server, 0, blob, sizeof blob);
        line("NPIMXferSendTo(client 0, reset earlier, no transfer socket) -> %ld", rc);
        rc = P.XferSendTo(server, 99, blob, sizeof blob);
        line("NPIMXferSendTo(client 99) -> %ld", rc);
        unsigned long target = 0;
        for (unsigned long i = 1; i < 4; i++) {
            if (P.XferSendTo(server, i, blob, sizeof blob) == 0) {
                target = i;
                break;
            }
        }
        line("NPIMXferSendTo(first client with a transfer socket, 8 bytes) -> index %lu", target);
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.XferReceive(sizeof xbuffer, xbuffer, &xreceived));
        line("NPIMXferReceive -> %ld received=%lu bytes=%s", rc, xreceived, rc == 0 ? hex(xbuffer, xreceived) : "-");
        xreceived = 0xdeadbeefu;
        rc = P.XferReceive(sizeof xbuffer, xbuffer, &xreceived);
        line("NPIMXferReceive(nothing pending) -> %ld received=0x%lx", rc, xreceived);
        {
            const char *lan = lan_address();
            if (lan) {
                NeHandle *outsider_conn = new_handle();
                SOCKET outsider = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in local = make_addr(lan, 0), remote = make_addr(lan, XFER_PORT);
                bind(outsider, (struct sockaddr *)&local, sizeof local);
                int connected = connect(outsider, (struct sockaddr *)&remote, sizeof remote) == 0;
                POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.AcceptForXfer(server, outsider_conn));
                line("NPIMAcceptForXfer(peer %s from an address no client uses) -> %ld connection=%s",
                     connected ? "connected" : "not connected", rc, outsider_conn->impl ? "created" : "null");
                closesocket(outsider);
                free(outsider_conn);
            } else {
                line("NPIMAcceptForXfer(unregistered address): skipped, no non-loopback address");
            }
        }
        P.ResetXfer(server);
        rc = P.XferSendTo(server, target, blob, sizeof blob);
        line("NPIMXferSendTo(after NPIMResetXfer) -> %ld", rc);
    }

    /* TCP side of the server: accept a raw peer and exchange bytes in clear. */
    {
        rc = P.Accept(server, tconn);
        line("NPIMAccept(nothing pending) -> %ld connection=%s", rc, tconn->impl ? "created" : "null");
        SOCKET peer = tcp_connect_peer("127.0.0.1", GAME_PORT);
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Accept(server, tconn));
        line("NPIMAccept(peer connected: %s) -> %ld connection=%s", yesno(peer != INVALID_SOCKET), rc,
             tconn->impl ? "created" : "null");
        if (tconn->impl && peer != INVALID_SOCKET) {
            static const uint8_t out[] = "SERVER-TCP";
            static const uint8_t in[] = { 0xca, 0xfe, 0x00, 0x01 };
            send(peer, (const char *)in, sizeof in, 0);
            received = 0xdeadbeefu;
            POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Receive(tconn, sizeof buffer, buffer, &received));
            line("NPIMReceive(TCP) -> %ld received=%lu bytes=%s", rc, received, rc == 0 ? hex(buffer, received) : "-");
            rc = P.Send(tconn, out, sizeof out - 1);
            int got = tcp_read(peer, buffer, sizeof buffer, 2000);
            line("NPIMSend(TCP, 10 bytes) -> %ld; peer read %d bytes: %s", rc, got, got > 0 ? hex(buffer, (size_t)got) : "-");
            line("NPIMSend(TCP, 0 bytes) -> %ld", P.Send(tconn, out, 0));
            line("NPIMIsAlive(accepted TCP) -> %d", P.IsAlive(tconn));
            closesocket(peer);
            peer = INVALID_SOCKET;
            Sleep(100);
            line("NPIMIsAlive(after peer closed) -> %d", P.IsAlive(tconn));
            received = 0xdeadbeefu;
            rc = P.Receive(tconn, sizeof buffer, buffer, &received);
            line("NPIMReceive(after peer closed) -> %ld", rc);
        }
        if (peer != INVALID_SOCKET) {
            closesocket(peer);
        }
    }

    /*
     * Closing the server first succeeds.  (Closing any connection first would
     * make NPIMCloseServer fail, see scenario_close_order_quirk.)
     */
    rc = P.CloseServer(server);
    line("NPIMCloseServer -> %ld server=%s", rc, server->impl ? "still set" : "cleared");

    /* The server owns (and has now freed) its end of the transfer connection; only the client end is ours to close. */
    rc = P.CloseConnection(xclient);
    line("NPIMCloseConnection(transfer connection, client side) -> %ld connection=%s", rc, xclient->impl ? "still set" : "cleared");
    rc = P.CloseConnection(tconn);
    line("NPIMCloseConnection(accepted TCP) -> %ld connection=%s", rc, tconn->impl ? "still set" : "cleared");
    rc = P.CloseConnection(cli);
    line("NPIMCloseConnection(UDP client) -> %ld connection=%s", rc, cli->impl ? "still set" : "cleared");
    free(xclient);
    free(xserver);   /* the engine-side wrapper only; the plugin freed the state with the server */
    free(tconn);
    free(cli);
    free(server);
    free_server_events(&events);
    tsscheduler_destroy_event(client_event);
}

/*
 * A server only answers LAN discovery probes when it was published with
 * flag_a == false and flag_b == true (and is accepting connections).
 */
static void probe_matrix_case(unsigned flag_a, unsigned flag_b)
{
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    uint8_t probe_plain[0x42], wire[512], plain[512], buffer[512];
    unsigned long received = 0, client = 0;
    struct sockaddr_in server_addr = make_addr("127.0.0.1", GAME_PORT), from;

    long rc = publish(server, "ee-flags", "Flag Game", 2, &events, (int)flag_a, (int)flag_b);
    SOCKET asker = udp_socket(NULL, 0, 0);
    memset(probe_plain, 0, sizeof probe_plain);
    probe_plain[0] = 0x15;
    strcpy((char *)probe_plain + 1, "ee-flags");
    size_t n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
    udp_send(asker, wire, (int)n, &server_addr);
    Sleep(100);
    SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
    long probe_rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    int got = capture == INVALID_SOCKET ? -1 : udp_receive(capture, wire, sizeof wire, &from, 500);
    line("NPIMPublish(UDP, flags %u/%u) -> %ld; probe answered with NPIMReceiveFrom -> %ld; reply %s", flag_a, flag_b,
         rc, probe_rc, got > 0 ? "sent" : "none");
    if (got > 0) {
        unseal(plain, wire, (size_t)got);
        line("  reply: wire_length=%d type=0x%02x service=\"%s\" name=\"%s\"", got, plain[0],
             (const char *)plain + 1, (const char *)plain + 0x42);
    }
    if (capture != INVALID_SOCKET) {
        closesocket(capture);
    }
    closesocket(asker);
    rc = P.CloseServer(server);
    line("  NPIMCloseServer -> %ld", rc);
    free_server_events(&events);
    free(server);
}

/* Service and game names of the maximum length (64 characters) fit the 65-byte packet fields. */
static void probe_long_names(void)
{
    char service[65], name[65];
    uint8_t probe_plain[0x42], wire[512], plain[512], buffer[512];
    unsigned long received = 0, client = 0;
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    struct sockaddr_in server_addr = make_addr("127.0.0.1", GAME_PORT), from;

    memset(service, 'S', 64);
    service[64] = '\0';
    memset(name, 'n', 64);
    name[64] = '\0';
    long rc = publish(server, service, name, 2, &events, 0, 1);
    SOCKET asker = udp_socket(NULL, 0, 0);
    memset(probe_plain, 0, sizeof probe_plain);
    probe_plain[0] = 0x15;
    memcpy(probe_plain + 1, service, 65);
    size_t n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
    udp_send(asker, wire, (int)n, &server_addr);
    Sleep(100);
    SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
    P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    int got = capture == INVALID_SOCKET ? -1 : udp_receive(capture, wire, sizeof wire, &from, 1000);
    if (got == 0x88) {
        unseal(plain, wire, (size_t)got);
        line("NPIMPublish(64-character service and name) -> %ld; reply: wire_length=%d type=0x%02x service_intact=%s name_intact=%s",
             rc, got, plain[0], yesno(strcmp((const char *)plain + 1, service) == 0), yesno(strcmp((const char *)plain + 0x42, name) == 0));
    } else {
        line("NPIMPublish(64-character service and name) -> %ld; reply: %d bytes", rc, got);
    }
    if (capture != INVALID_SOCKET) {
        closesocket(capture);
    }
    closesocket(asker);
    line("  NPIMCloseServer -> %ld", P.CloseServer(server));
    free_server_events(&events);
    free(server);
}

static void scenario_probe_flags(void)
{
    line("== discovery probe replies depend on the publish flags");
    probe_matrix_case(1, 1);
    probe_matrix_case(0, 0);
    probe_matrix_case(0, 1);
    probe_long_names();
}

/*
 * NPIMCloseConnection also closes the bulk-transfer listener (a quirk of the
 * original).  After that NPIMCloseServer cannot close that listener any more,
 * reports failure and leaves the server open.  This scenario goes last since it
 * leaks a server that keeps ports 33334/33335 busy.
 */
static void scenario_close_order_quirk(void)
{
    NeHandle *server = new_handle(), *accepted = new_handle();
    ServerEvents events = new_server_events();

    line("== closing a connection before closing the server");
    long rc = publish(server, "ee-quirk", "Quirk", 1, &events, 1, 1);
    line("NPIMPublish(TCP) -> %ld", rc);
    SOCKET peer = tcp_connect_peer("127.0.0.1", GAME_PORT);
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Accept(server, accepted));
    line("NPIMAccept -> %ld connection=%s", rc, accepted->impl ? "created" : "null");
    rc = P.CloseConnection(accepted);
    line("NPIMCloseConnection -> %ld connection=%s", rc, accepted->impl ? "still set" : "cleared");
    rc = P.CloseServer(server);
    line("NPIMCloseServer -> %ld server=%s", rc, server->impl ? "still set" : "cleared");
    rc = P.CloseServer(server);
    line("NPIMCloseServer (again) -> %ld server=%s", rc, server->impl ? "still set" : "cleared");
    if (peer != INVALID_SOCKET) {
        closesocket(peer);
    }
    free_server_events(&events);
}

/* ------------------------------------------------------------------------ */
/* LAN game discovery                                                        */
/* ------------------------------------------------------------------------ */

typedef struct Feeder {
    volatile LONG running;
    uint8_t datagram[0x88];
    HANDLE thread;
} Feeder;

/* Keep answering "a game of service S named N lives here" to port 33334 on this host. */
static DWORD WINAPI feeder_main(LPVOID argument)
{
    Feeder *feeder = argument;
    SOCKET s = udp_socket(NULL, 0, 0);
    struct sockaddr_in target = make_addr("127.0.0.1", GAME_PORT);
    while (feeder->running) {
        udp_send(s, feeder->datagram, sizeof feeder->datagram, &target);
        Sleep(60);
    }
    closesocket(s);
    return 0;
}

static void feeder_start(Feeder *feeder, const char *service, const char *name)
{
    uint8_t plain[0x87];
    memset(plain, 0, sizeof plain);
    plain[0] = 0x16;
    strcpy((char *)plain + 1, service);
    strcpy((char *)plain + 0x42, name);
    seal(feeder->datagram, 0x37, plain, sizeof plain);
    feeder->running = 1;
    feeder->thread = CreateThread(NULL, 0, feeder_main, feeder, 0, NULL);
}

static void feeder_stop(Feeder *feeder)
{
    feeder->running = 0;
    WaitForSingleObject(feeder->thread, 3000);
    CloseHandle(feeder->thread);
}

static int broadcast_available(void)
{
    SOCKET s = udp_socket(NULL, 0, 0);
    BOOL on = TRUE;
    struct sockaddr_in to = make_addr("255.255.255.255", 9);
    int ok;
    if (s == INVALID_SOCKET) {
        return 0;
    }
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
    ok = udp_send(s, "x", 1, &to) == 1;
    closesocket(s);
    return ok;
}

static void scenario_discovery(void)
{
    UString nothing, service, address, game_service;
    UWideString game_name;
    Feeder feeder;
    NeHandle *conn = new_handle();
    NeHandle *refused = new_handle();
    TSEvent *event = new_event();
    uint8_t buffer[256];

    line("== LAN game discovery");
    if (!broadcast_available()) {
        line("SKIPPED: UDP broadcast is not available on this host");
        return;
    }
    ustring_construct_from_chars(&nothing, "");
    ustring_construct_from_chars(&service, "ee-test");
    ustring_construct(&address);
    ustring_construct(&game_service);
    uwidestring_construct(&game_name);

    line("NPIMSearchForGame(empty service name, start) -> %ld", P.SearchForGame(&nothing, &nothing, 1));
    line("NPIMSearchForGame(stop while not searching) -> %ld", P.SearchForGame(&nothing, &service, 0));

    feeder_start(&feeder, "ee-test", "Remote Game");
    line("NPIMSearchForGame(\"ee-test\", start) -> %ld", P.SearchForGame(&nothing, &service, 1));
    line("NPIMSearchForGame(\"ee-test\", start again) -> %ld", P.SearchForGame(&nothing, &service, 1));
    unsigned long count = 0;
    for (DWORD started = GetTickCount(); GetTickCount() - started < 10000;) {
        count = P.GetDetectedGameCount();
        if (count > 0) {
            break;
        }
        Sleep(25);
    }
    line("detected games after the first scan: %lu", count);
    long rc = P.GetDetectedGame(0, &address, &game_service, &game_name);
    line("NPIMGetDetectedGame(0) -> %ld host=\"%s\" service=\"%s\" name=\"%s\"", rc, chars_of(&address),
         chars_of(&game_service), wchars_of(&game_name));
    line("NPIMGetDetectedGame(1) -> %ld", P.GetDetectedGame(1, &address, &game_service, &game_name));

    /* Joining a game that was detected uses the address learnt from its reply. */
    UString host, svc;
    UWideString name, other_name;
    ustring_construct_from_chars(&host, "127.0.0.1");
    ustring_construct_from_chars(&svc, "ee-test");
    uwidestring_construct_from_chars(&name, "Remote Game");
    uwidestring_construct_from_chars(&other_name, "Some other game");
    rc = P.Connect(&host, &svc, &other_name, 2, event, refused, 0);
    line("NPIMConnect(not a detected game) -> %ld connection=%s", rc, refused->impl ? "created" : "null");

    SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
    rc = P.Connect(&host, &svc, &name, 2, event, conn, 0);
    line("NPIMConnect(detected game, UDP) -> %ld connection=%s", rc, conn->impl ? "created" : "null");
    if (conn->impl) {
        static const uint8_t ping[] = { 0x00, 0x22, 'P', 'I', 'N', 'G' };
        uint8_t wire[64];
        struct sockaddr_in from;
        memcpy(buffer, ping, sizeof ping);
        P.Send(conn, buffer, sizeof ping);
        /* The feeder's replies reach this socket too: wait for the datagram of the expected size. */
        int got = -1;
        for (DWORD started = GetTickCount(); capture != INVALID_SOCKET && GetTickCount() - started < 3000;) {
            got = udp_receive(capture, wire, sizeof wire, &from, 500);
            if (got == (int)(sizeof ping)) {
                break;
            }
            got = -1;
        }
        show_wire("  datagram reached 127.0.0.1:33334", wire, got);
        /*
         * The connection is deliberately not closed: NPIMCloseConnection also closes the
         * plugin's remembered bulk-transfer listener, which is a stale handle by now and
         * may have been recycled by one of the sockets in use here.
         */
    }
    if (capture != INVALID_SOCKET) {
        closesocket(capture);
    }
    SOCKET listener = tcp_listener("127.0.0.1", GAME_PORT);
    NeHandle *tcp = new_handle();
    rc = P.Connect(&host, &svc, &name, 1, event, tcp, 0);
    SOCKET peer = listener == INVALID_SOCKET ? INVALID_SOCKET : tcp_accept_peer(listener, 500);
    line("NPIMConnect(detected game, TCP) -> %ld connection=%s peer_accepted=%s", rc, tcp->impl ? "created" : "null",
         yesno(peer != INVALID_SOCKET));
    if (peer != INVALID_SOCKET) {
        closesocket(peer);
    }
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    free(tcp);

    rc = P.SearchForGame(&nothing, &service, 0);
    line("NPIMSearchForGame(stop) -> %ld; detected games afterwards: %lu", rc, P.GetDetectedGameCount());

    /* A stopped search can be started again and finds the game again. */
    line("NPIMSearchForGame(\"ee-test\", start again after stop) -> %ld", P.SearchForGame(&nothing, &service, 1));
    count = 0;
    for (DWORD started = GetTickCount(); GetTickCount() - started < 10000;) {
        count = P.GetDetectedGameCount();
        if (count > 0) {
            break;
        }
        Sleep(25);
    }
    line("detected games after restarting the search: %lu", count);
    rc = P.SearchForGame(&nothing, &service, 0);
    line("NPIMSearchForGame(stop) -> %ld; detected games afterwards: %lu", rc, P.GetDetectedGameCount());

    /* Publishing with the duplicate check scans the LAN first and refuses a name that is already in use. */
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    rc = publish(server, "ee-test", "Remote Game", 3, &events, 0, 1);
    line("NPIMPublish(name already in use on the LAN, duplicate check on) -> %ld server=%s", rc, server->impl ? "created" : "null");
    rc = publish(server, "ee-test", "Fresh Game", 3, &events, 0, 1);
    line("NPIMPublish(unused name, duplicate check on) -> %ld server=%s", rc, server->impl ? "created" : "null");
    line("detected games kept from the scan: %lu", P.GetDetectedGameCount());
    feeder_stop(&feeder);
    if (server->impl) {
        rc = P.CloseServer(server);
        line("NPIMCloseServer -> %ld", rc);
    }
    P.SearchForGame(&nothing, &service, 0);

    free(server);
    free(conn);
    free(refused);
    free_server_events(&events);
    tsscheduler_destroy_event(event);
    ustring_destruct(&nothing);
    ustring_destruct(&service);
    ustring_destruct(&address);
    ustring_destruct(&game_service);
    uwidestring_destruct(&game_name);
    ustring_destruct(&host);
    ustring_destruct(&svc);
    uwidestring_destruct(&name);
    uwidestring_destruct(&other_name);
}

/* ------------------------------------------------------------------------ */

static const struct Scenario {
    const char *name;
    void (*run)(void);
} kScenarios[] = {
    { "exports", scenario_exports },
    { "identity", scenario_identity },
    { "activation", scenario_activation },
    { "configuration", scenario_configuration },
    { "network", scenario_network_info },
    { "detected-empty", scenario_empty_detected_games },
    { "send-single", scenario_send_single },
    { "udp-client", scenario_udp_client_wire },
    { "tcp-client", scenario_tcp_client },
    { "single-transport", scenario_single_transport_servers },
    { "udp-server", scenario_udp_server },
    { "probe-flags", scenario_probe_flags },
    { "discovery", scenario_discovery },
    { "close-order", scenario_close_order_quirk },
};

/* Optional extra arguments restrict the run to the named scenarios (debugging aid). */
static int scenario_selected(const char *name, int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static DWORD WINAPI watchdog_main(LPVOID argument)
{
    (void)argument;
    Sleep(180000);
    line("WATCHDOG: harness took too long, aborting");
    ExitProcess(3);
    return 0;
}

int main(int argc, char **argv)
{
    WSADATA wsa;

    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 2) {
        fprintf(stderr, "usage: harness <path to TCP Protocol.dll>\n");
        return 2;
    }
    CreateThread(NULL, 0, watchdog_main, NULL, 0, NULL);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (!LoadLibraryA("Low-Level Engine.dll") || !bind_plugin(argv[1])) {
        line("FATAL: cannot load plugin %s (error %lu)", argv[1], GetLastError());
        return 2;
    }
    init_key_table();

    for (size_t i = 0; i < sizeof kScenarios / sizeof kScenarios[0]; i++) {
        if (argc != 2 && !scenario_selected(kScenarios[i].name, argc, argv)) {
            continue;
        }
        if (i > 2 && !g_plugin_active) {
            line("SKIPPED %s: the plugin did not activate", kScenarios[i].name);
            continue;
        }
        kScenarios[i].run();
    }

    line("NPIMDeactivate -> %ld", P.Deactivate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("== done");
    return 0;
}
