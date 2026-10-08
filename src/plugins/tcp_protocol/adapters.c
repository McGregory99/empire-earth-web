#include "adapters.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "exports.h"
#include "plugin_state.h"
#include "protocol.h"

#include <iphlpapi.h>

/* GetAdaptersInfo gets a fixed buffer, as in the original: hosts with more adapters report none. */
#define ADAPTER_BUFFER_SIZE 0x1900
#define MAX_ENUMERATED_ADAPTERS 10
#define HOST_NAME_BUFFER_SIZE 0x400

/* A well-known unreachable-but-routable address, used to ask the OS which interface it would pick. */
#define ROUTE_PROBE_ADDRESS "128.127.126.125"
#define ROUTE_PROBE_PORT 7

/* Fetch the adapter list; returns NULL when it does not fit the buffer or the query fails. */
static IP_ADAPTER_INFO *query_adapters(void)
{
    ULONG size = ADAPTER_BUFFER_SIZE;
    IP_ADAPTER_INFO *adapters = malloc(ADAPTER_BUFFER_SIZE);

    if (adapters && GetAdaptersInfo(adapters, &size) != NO_ERROR) {
        free(adapters);
        adapters = NULL;
    }
    return adapters;
}

/* Look up where `ip` sits in the address list of this host; 0 if it is not there. */
static unsigned long position_in_host_addresses(uint32_t ip)
{
    char host_name[HOST_NAME_BUFFER_SIZE];
    struct hostent *host;
    unsigned long position = 0;

    if (gethostname(host_name, sizeof host_name) == SOCKET_ERROR || !(host = gethostbyname(host_name))) {
        return 0;
    }
    while (host->h_addr_list[position]) {
        uint32_t candidate = 0;

        memcpy(&candidate, host->h_addr_list[position], sizeof candidate);
        if (candidate == ip) {
            return position;
        }
        position++;
    }
    return 0;
}

static unsigned long adapter_host_index_of(unsigned long adapter)
{
    IP_ADAPTER_INFO *adapters;
    const IP_ADAPTER_INFO *selected;
    unsigned long position = 0;

    if (adapter == 0 || !(adapters = query_adapters())) {
        return 0;
    }
    selected = adapters;
    for (unsigned long number = 1; selected && number < adapter; number++) {
        selected = selected->Next;
    }
    if (selected) {
        uint32_t ip = inet_addr(selected->IpAddressList.IpAddress.String);

        if (ip != 0) {
            position = position_in_host_addresses(ip);
        }
    }
    free(adapters);
    return position;
}

unsigned long adapter_select(unsigned long adapter)
{
    g_plugin.adapter_argument = adapter;
    g_plugin.adapter_host_index = adapter_host_index_of(adapter);
    return g_plugin.adapter_host_index;
}

/* ------------------------------------------------------------------------ */
/* NPIMEnumerateNetworkAdapters: fill the engine's std::vector<UWideString>   */
/* ------------------------------------------------------------------------ */

static void wide_vector_clear(WideStringVector *vector)
{
    for (UWideString *it = vector->first; it && it != vector->last; it++) {
        uwidestring_destruct(it);
    }
    vector->last = vector->first;
}

/* Append a copy of `value`, growing the storage like the MSVC 6 vector the engine uses. */
static bool wide_vector_push_back(WideStringVector *vector, const UWideString *value)
{
    size_t size = vector->first ? (size_t)(vector->last - vector->first) : 0;

    if (vector->last == vector->end_of_storage) {
        size_t capacity = vector->first ? (size_t)(vector->end_of_storage - vector->first) : 0;
        size_t grown = capacity + capacity / 2;
        UWideString *storage;

        if (grown < size + 1) {
            grown = size + 1;
        }
        storage = malloc(grown * sizeof *storage);
        if (!storage) {
            return false;
        }
        for (size_t i = 0; i < size; i++) {
            uwidestring_construct_copy(&storage[i], &vector->first[i]);
            uwidestring_destruct(&vector->first[i]);
        }
        free(vector->first);
        vector->first = storage;
        vector->last = storage + size;
        vector->end_of_storage = storage + grown;
    }
    uwidestring_construct_copy(vector->last, value);
    vector->last++;
    return true;
}

unsigned long NPIMEnumerateNetworkAdapters(WideStringVector *out)
{
    IP_ADAPTER_INFO *adapters = query_adapters();
    unsigned long count = 0;

    wide_vector_clear(out);
    for (const IP_ADAPTER_INFO *adapter = adapters; adapter && count < MAX_ENUMERATED_ADAPTERS;
         adapter = adapter->Next, count++) {
        UWideString description;

        uwidestring_construct_from_chars(&description, adapter->Description);
        wide_vector_push_back(out, &description);
        uwidestring_destruct(&description);
    }
    free(adapters);
    return count;
}

/* ------------------------------------------------------------------------ */
/* NPIMGetLocalAddress                                                       */
/* ------------------------------------------------------------------------ */

/* The local address the OS would use to reach the outside world, or 0. */
static uint32_t routed_local_address(void)
{
    struct sockaddr_in remote, local;
    int local_length = sizeof local;
    uint32_t address = 0;
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, 0);

    if (probe == INVALID_SOCKET) {
        return 0;
    }
    memset(&remote, 0, sizeof remote);
    memset(&local, 0, sizeof local);
    remote.sin_family = AF_INET;
    remote.sin_port = htons(ROUTE_PROBE_PORT);
    remote.sin_addr.s_addr = inet_addr(ROUTE_PROBE_ADDRESS);
    if (connect(probe, (struct sockaddr *)&remote, sizeof remote) != SOCKET_ERROR &&
        getsockname(probe, (struct sockaddr *)&local, &local_length) == 0) {
        address = local.sin_addr.s_addr;
    }
    closesocket(probe);
    return address;
}

UWideString *NPIMGetLocalAddress(unsigned long adapter)
{
    struct in_addr address;
    char host_name[HOST_NAME_BUFFER_SIZE];
    struct hostent *host;

    address.s_addr = 0;
    adapter_select(adapter);
    if (gethostname(host_name, sizeof host_name) != SOCKET_ERROR && (host = gethostbyname(host_name)) &&
        host->h_addr_list[g_plugin.adapter_host_index]) {
        memcpy(&address, host->h_addr_list[g_plugin.adapter_host_index], sizeof address);
    }
    if (address.s_addr == 0) {
        address.s_addr = routed_local_address();
    }
    uwidestring_assign_chars(&g_plugin.local_address, inet_ntoa(address));
    return &g_plugin.local_address;
}
