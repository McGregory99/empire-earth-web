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

static DWORD WINAPI fake_get_adapters_info(PIP_ADAPTER_INFO adapters, PULONG size)
{
    char host_name[256];
    struct hostent *host = gethostname(host_name, sizeof host_name) == 0 ? gethostbyname(host_name) : NULL;
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
        int known = 0;

        memset(adapter, 0, sizeof *adapter);
        snprintf(adapter->Description, sizeof adapter->Description, "Test adapter %d", i + 1);
        for (int j = 0; host && host->h_addr_list[j]; j++) {
            known |= j == i;
        }
        snprintf(invented, sizeof invented, "10.9.8.%d", i + 1);
        address = known ? inet_ntoa(*(struct in_addr *)host->h_addr_list[i]) : invented;
        snprintf(adapter->IpAddressList.IpAddress.String, sizeof adapter->IpAddressList.IpAddress.String, "%s", address);
        adapter->Next = i + 1 < g_fake_adapter_count ? &adapters[i + 1] : NULL;
    }
    return NO_ERROR;
}

/* Point the import-table slot of `dll!function` in `module` at `replacement`. */
static int hook_import(HMODULE module, const char *dll, const char *function, void *replacement)
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
            IMAGE_IMPORT_BY_NAME *by_name;
            DWORD old_protection;

            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                continue;
            }
            by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)by_name->Name, function) == 0) {
                VirtualProtect(slots, sizeof *slots, PAGE_READWRITE, &old_protection);
                slots->u1.Function = (DWORD_PTR)replacement;
                VirtualProtect(slots, sizeof *slots, old_protection, &old_protection);
                return 1;
            }
        }
    }
    return 0;
}


void interposer_install(HMODULE plugin)
{
    hook_import(plugin, "iphlpapi.dll", "GetAdaptersInfo", (void *)fake_get_adapters_info);
}
