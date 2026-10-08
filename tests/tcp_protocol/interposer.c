/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

#include <iphlpapi.h>

/* ------------------------------------------------------------------------ */
/* A replacement for iphlpapi!GetAdaptersInfo                                  */
/* ------------------------------------------------------------------------ */

/*
 * Wine reports far more adapters than the plugin's fixed-size buffer can hold, so against
 * the real library the plugin never sees an adapter.  To exercise the adapter code the
 * harness redirects the plugin's import of GetAdaptersInfo to a function of its own, which
 * reports `g_fake_adapter_count` adapters.  Adapter n is "Test adapter n"; its address is the
 * n-th address of this host (which maps back to a position in the host's address list), or an
 * invented one when the host has fewer addresses.
 */
int g_fake_adapter_count;

/*
 * Under Wine the address list of this host changes order from one gethostbyname call to the next,
 * which would make the adapter-number to address mapping untestable.  While the fake host is
 * enabled, the plugin's gethostname returns "harness-host" and gethostbyname("harness-host")
 * returns a fixed list of two addresses; any other name is resolved for real.
 */
#define FAKE_HOST_NAME "harness-host"
static int g_fake_host_enabled;
static struct in_addr g_fake_host_addresses[2];
static char *g_fake_host_address_list[3];
static struct hostent g_fake_host;

void fake_host_enable(int on)
{
    g_fake_host_addresses[0].s_addr = inet_addr("10.1.2.3");
    g_fake_host_addresses[1].s_addr = inet_addr("10.1.2.4");
    g_fake_host_address_list[0] = (char *)&g_fake_host_addresses[0];
    g_fake_host_address_list[1] = (char *)&g_fake_host_addresses[1];
    g_fake_host_address_list[2] = NULL;
    g_fake_host.h_name = (char *)FAKE_HOST_NAME;
    g_fake_host.h_aliases = NULL;
    g_fake_host.h_addrtype = AF_INET;
    g_fake_host.h_length = 4;
    g_fake_host.h_addr_list = g_fake_host_address_list;
    g_fake_host_enabled = on;
}

static int WINAPI hook_gethostname(char *name, int length)
{
    if (g_fake_host_enabled) {
        snprintf(name, (size_t)length, "%s", FAKE_HOST_NAME);
        return 0;
    }
    return gethostname(name, length);
}

static struct hostent *WINAPI hook_gethostbyname(const char *name)
{
    return (g_fake_host_enabled && strcmp(name, FAKE_HOST_NAME) == 0) ? &g_fake_host : gethostbyname(name);
}

static DWORD WINAPI fake_get_adapters_info(PIP_ADAPTER_INFO adapters, PULONG size)
{
    ULONG needed = (ULONG)g_fake_adapter_count * sizeof(IP_ADAPTER_INFO);

    if (g_fake_adapter_count <= 0) {
        return ERROR_NO_DATA;
    }
    if (!adapters || *size < needed) {
        *size = needed;
        return ERROR_BUFFER_OVERFLOW;
    }
    for (int i = 0; i < g_fake_adapter_count; i++) {
        IP_ADAPTER_INFO *adapter = &adapters[i];
        char invented[16];
        const char *address;

        memset(adapter, 0, sizeof *adapter);
        snprintf(adapter->Description, sizeof adapter->Description, "Test adapter %d", i + 1);
        snprintf(invented, sizeof invented, "10.9.8.%d", i + 1);
        address = (i < 2 && g_fake_host_enabled) ? inet_ntoa(g_fake_host_addresses[i]) : invented;
        snprintf(adapter->IpAddressList.IpAddress.String, sizeof adapter->IpAddressList.IpAddress.String, "%s", address);
        adapter->Next = i + 1 < g_fake_adapter_count ? &adapters[i + 1] : NULL;
    }
    return NO_ERROR;
}

/*
 * Point the import-table slot of `dll!function` (or of the given ordinal, for binaries that
 * import by ordinal, like the original) in `module` at `replacement`.
 */
static int hook_import(HMODULE module, const char *dll, const char *function, unsigned ordinal, void *replacement)
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    for (IMAGE_IMPORT_DESCRIPTOR *entry = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory.VirtualAddress); entry->Name;
         entry++) {
        IMAGE_THUNK_DATA *names, *slots;

        if (_stricmp((const char *)(base + entry->Name), dll) != 0) {
            continue;
        }
        names = (IMAGE_THUNK_DATA *)(base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
        slots = (IMAGE_THUNK_DATA *)(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            int matches;
            DWORD old_protection;

            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                matches = ordinal != 0 && (names->u1.Ordinal & 0xffff) == ordinal;
            } else {
                matches = strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name,
                                 function) == 0;
            }
            if (matches) {
                VirtualProtect(slots, sizeof *slots, PAGE_READWRITE, &old_protection);
                slots->u1.Function = (DWORD_PTR)replacement;
                VirtualProtect(slots, sizeof *slots, old_protection, &old_protection);
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Fault injection and the engine error log                                   */
/* ------------------------------------------------------------------------ */

/*
 * The plugin's Winsock imports go through wrappers that normally call the real function.  A
 * scenario can arm one fault: the next calls of a named function (after `skip` calls) fail
 * with the given result and Winsock error.  This reaches the error paths that a healthy
 * loopback never takes.  The wrappers also work for the original, which imports Winsock by
 * ordinal, and for our DLL, which imports by name.
 */
static struct {
    const char *function;  /* NULL when no fault is armed */
    int skip;
    int remaining;
    long result;
    int error;
    int used;
} g_fault;

/* Declared below with the rest of the engine log. */
static char g_engine_log[512];

void fault_inject(const char *function, int skip, int count, long result, int error)
{
    g_engine_log[0] = '\0';  /* the report that follows should only show what the faulted call caused */
    g_fault.function = function;
    g_fault.skip = skip;
    g_fault.remaining = count;
    g_fault.result = result;
    g_fault.error = error;
    g_fault.used = 0;
}

const char *fault_state(void)
{
    const char *state = !g_fault.function ? "no fault armed"
                        : !g_fault.used   ? "fault NOT REACHED"
                        : g_fault.remaining ? "fault partly used"
                                            : "fault used";

    g_fault.function = NULL;
    return state;
}

static int fault_hit(const char *function)
{
    if (!g_fault.function || strcmp(g_fault.function, function) != 0) {
        return 0;
    }
    if (g_fault.skip > 0) {
        g_fault.skip--;
        return 0;
    }
    if (g_fault.remaining == 0) {
        return 0;
    }
    g_fault.remaining--;
    g_fault.used++;
    WSASetLastError(g_fault.error);
    return 1;
}

#define FAULTED(name) fault_hit(name)

static SOCKET WINAPI hook_accept(SOCKET s, struct sockaddr *address, int *length)
{
    return FAULTED("accept") ? (SOCKET)g_fault.result : accept(s, address, length);
}

static int WINAPI hook_bind(SOCKET s, const struct sockaddr *address, int length)
{
    return FAULTED("bind") ? (int)g_fault.result : bind(s, address, length);
}

static int WINAPI hook_closesocket(SOCKET s)
{
    return FAULTED("closesocket") ? (int)g_fault.result : closesocket(s);
}

static int WINAPI hook_connect(SOCKET s, const struct sockaddr *address, int length)
{
    return FAULTED("connect") ? (int)g_fault.result : connect(s, address, length);
}

static int WINAPI hook_getsockname(SOCKET s, struct sockaddr *address, int *length)
{
    return FAULTED("getsockname") ? (int)g_fault.result : getsockname(s, address, length);
}

static int WINAPI hook_ioctlsocket(SOCKET s, long command, u_long *argument)
{
    return FAULTED("ioctlsocket") ? (int)g_fault.result : ioctlsocket(s, command, argument);
}

static int WINAPI hook_listen(SOCKET s, int backlog)
{
    return FAULTED("listen") ? (int)g_fault.result : listen(s, backlog);
}

static int WINAPI hook_recv(SOCKET s, char *buffer, int length, int flags)
{
    return FAULTED("recv") ? (int)g_fault.result : recv(s, buffer, length, flags);
}

static int WINAPI hook_recvfrom(SOCKET s, char *buffer, int length, int flags, struct sockaddr *from, int *from_length)
{
    return FAULTED("recvfrom") ? (int)g_fault.result : recvfrom(s, buffer, length, flags, from, from_length);
}

static int WINAPI hook_send(SOCKET s, const char *buffer, int length, int flags)
{
    if (FAULTED("send")) {
        return g_fault.result == FAULT_SHORT ? length - 1 : (int)g_fault.result;
    }
    return send(s, buffer, length, flags);
}

/* The datagrams the plugin sends, as far as scenarios want to look at them. */
static struct SentDatagram g_last_sent;
static struct SentDatagram g_first_broadcast;
static int g_swallow_sendto;

const struct SentDatagram *last_sent_datagram(void)
{
    return &g_last_sent;
}

const struct SentDatagram *first_broadcast_datagram(void)
{
    return &g_first_broadcast;
}

void forget_sent_datagrams(void)
{
    memset(&g_last_sent, 0, sizeof g_last_sent);
    memset(&g_first_broadcast, 0, sizeof g_first_broadcast);
}

void swallow_sendto(int on)
{
    g_swallow_sendto = on;
}

static void note_sent(struct SentDatagram *record, const char *buffer, int length, const struct sockaddr *to)
{
    record->length = length < (int)sizeof record->data ? length : (int)sizeof record->data;
    memcpy(record->data, buffer, (size_t)record->length);
    memcpy(&record->to, to, sizeof record->to);
}

static int WINAPI hook_sendto(SOCKET s, const char *buffer, int length, int flags, const struct sockaddr *to,
                              int to_length)
{
    note_sent(&g_last_sent, buffer, length, to);
    if (g_first_broadcast.length == 0 && ((const struct sockaddr_in *)to)->sin_addr.s_addr == htonl(INADDR_BROADCAST)) {
        note_sent(&g_first_broadcast, buffer, length, to);
    }
    if (g_swallow_sendto) {
        return length;
    }
    if (FAULTED("sendto")) {
        return g_fault.result == FAULT_SHORT ? length - 1 : (int)g_fault.result;
    }
    return sendto(s, buffer, length, flags, to, to_length);
}

static int WINAPI hook_setsockopt(SOCKET s, int level, int name, const char *value, int length)
{
    return FAULTED("setsockopt") ? (int)g_fault.result : setsockopt(s, level, name, value, length);
}

static SOCKET WINAPI hook_socket(int family, int type, int protocol)
{
    return FAULTED("socket") ? (SOCKET)g_fault.result : socket(family, type, protocol);
}

static int WINAPI hook_WSAStartup(WORD version, LPWSADATA data)
{
    int faulted = FAULTED("WSAStartup");
    int result;

    if (faulted && g_fault.result != FAULT_BAD_VERSION) {
        return (int)g_fault.result;
    }
    result = WSAStartup(version, data);
    if (faulted) {
        data->wVersion = 0x0101;
    }
    return result;
}

static int WINAPI hook_WSACleanup(void)
{
    return FAULTED("WSACleanup") ? (int)g_fault.result : WSACleanup();
}

/* What the plugin told the engine about errors, since the last call of engine_errors(). */
static void engine_log(const char *format, unsigned long value)
{
    size_t used = strlen(g_engine_log);

    if (used + 40 < sizeof g_engine_log) {
        snprintf(g_engine_log + used, sizeof g_engine_log - used, format, value);
    }
}

static void engine_log_text(const char *text)
{
    size_t used = strlen(g_engine_log);

    if (used + strlen(text) + 1 < sizeof g_engine_log) {
        strcpy(g_engine_log + used, text);
    }
}

const char *engine_errors(void)
{
    static char taken[512];

    snprintf(taken, sizeof taken, "%s", g_engine_log[0] ? g_engine_log : "no errors reported");
    g_engine_log[0] = '\0';
    return taken;
}

static unsigned long hook_es_record_native_error(unsigned long error)
{
    engine_log("record(%lu) ", error);
    return es_record_native_error(error);
}

static unsigned long hook_es_get_native_error(void)
{
    engine_log_text("acknowledge ");
    return es_get_native_error();
}

static void hook_es_verify(const char *file, unsigned long line, const char *expression)
{
    engine_log("assertion-at-line-%lu ", line);
    es_verify(file, line, expression);
}

static const struct {
    const char *name;
    unsigned ordinal;  /* the classic Winsock ordinal the original imports it by */
    void *hook;
} kWinsockHooks[] = {
    { "accept", 1, hook_accept },           { "bind", 2, hook_bind },
    { "closesocket", 3, hook_closesocket }, { "connect", 4, hook_connect },
    { "getsockname", 6, hook_getsockname }, { "ioctlsocket", 10, hook_ioctlsocket },
    { "listen", 13, hook_listen },          { "recv", 16, hook_recv },
    { "recvfrom", 17, hook_recvfrom },      { "send", 19, hook_send },
    { "sendto", 20, hook_sendto },          { "setsockopt", 21, hook_setsockopt },
    { "socket", 23, hook_socket },          { "WSAStartup", 115, hook_WSAStartup },
    { "WSACleanup", 116, hook_WSACleanup }, { "gethostbyname", 52, hook_gethostbyname },
    { "gethostname", 57, hook_gethostname },
};

void interposer_install(HMODULE plugin)
{
    hook_import(plugin, "iphlpapi.dll", "GetAdaptersInfo", 0, (void *)fake_get_adapters_info);
    for (size_t i = 0; i < sizeof kWinsockHooks / sizeof kWinsockHooks[0]; i++) {
        hook_import(plugin, "ws2_32.dll", kWinsockHooks[i].name, kWinsockHooks[i].ordinal, kWinsockHooks[i].hook);
    }
    hook_import(plugin, "Low-Level Engine.dll", "?ESRecordNativeError@@YAKK@Z", 0, (void *)hook_es_record_native_error);
    hook_import(plugin, "Low-Level Engine.dll", "?ESGetNativeError@@YAKXZ", 0, (void *)hook_es_get_native_error);
    hook_import(plugin, "Low-Level Engine.dll", "?ESVerify@@YAXPBDK0@Z", 0, (void *)hook_es_verify);
}
