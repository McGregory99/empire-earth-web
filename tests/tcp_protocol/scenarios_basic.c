/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

void scenario_exports(void)
{
    line("== exports");
    for (int i = 0; i < 33; i++) {
        FARPROC by_name = GetProcAddress(g_module, kExportNames[i]);
        FARPROC by_ordinal = GetProcAddress(g_module, (LPCSTR)(uintptr_t)(i + 1));
        line("export %2d %-28s present=%s by_ordinal=%s", i + 1, kExportNames[i],
             yesno(by_name != NULL), yesno(by_name != NULL && by_name == by_ordinal));
    }
}

void scenario_identity(void)
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
int g_plugin_active;

void scenario_activation(void)
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
    {
        UWideString unrelated, empty;
        uwidestring_construct_from_chars(&unrelated, "Something else entirely");
        uwidestring_construct_from_chars(&empty, "");
        line("NPIMGetStatus(\"Something else entirely\", 0) -> %ld", P.GetStatus(&unrelated, 0));
        line("NPIMGetStatus(\"\", 0) -> %ld", P.GetStatus(&empty, 0));
        uwidestring_destruct(&unrelated);
        uwidestring_destruct(&empty);
    }
    line("NPIMDeactivate -> %ld", P.Deactivate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMActivate -> %ld", P.Activate());
    line("PIMIsValid -> %d", P.PIMIsValid());
    line("NPIMActivate (already active) -> %ld", P.Activate());
    line("PIMIsValid -> %d", P.PIMIsValid());

    uwidestring_destruct(&good);
    uwidestring_destruct(&other);
}

void scenario_configuration(void)
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

void scenario_network_info(void)
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

/* The plugin's adapter handling, fed by the harness's own GetAdaptersInfo and host address list (see interposer.c). */
void scenario_adapters(void)
{
    static const int counts[] = { 3, 10, 1, 0 };
    static const unsigned long numbers[] = { 0, 1, 2, 3, 4, 99 };
    static const uint8_t payload[] = { 1, 2, 3 };
    WideStringVector adapters = { 0, NULL, NULL, NULL };
    UString destination;

    line("== network adapters (GetAdaptersInfo and the host's addresses replaced by the harness)");
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        g_fake_adapter_count = counts[i];
        unsigned long count = P.EnumerateNetworkAdapters(&adapters);
        unsigned long size = adapters.first ? (unsigned long)(adapters.last - adapters.first) : 0ul;
        const char *first = size ? wchars_of(&adapters.first[0]) : "-";
        const char *last = size ? wchars_of(&adapters.first[size - 1]) : "-";

        line("%d adapter(s) present: NPIMEnumerateNetworkAdapters -> %lu, vector holds %lu, first \"%s\", last \"%s\"",
             counts[i], count, size, first, last);
    }
    for (UWideString *it = adapters.first; it && it != adapters.last; it++) {
        uwidestring_destruct(it);
    }
    free(adapters.first);

    /*
     * The host has two addresses, 10.1.2.3 and 10.1.2.4.  Adapters 1 and 2 have those addresses,
     * adapter 3 an address that is not on the list; there is no adapter 4.  The plugin maps an
     * adapter number to the position of its address in the host's list (0 if it is not there).
     */
    fake_host_enable(1);
    g_fake_adapter_count = 3;
    for (size_t i = 0; i < sizeof numbers / sizeof numbers[0]; i++) {
        UWideString *address = P.GetLocalAddress(numbers[i]);

        line("NPIMGetLocalAddress(%lu) -> %s", numbers[i], wchars_of(address));
    }

    /* NPIMSendSingle indexes the *destination's* address list with that position. */
    ustring_construct_from_chars(&destination, "harness-host");
    swallow_sendto(1);
    for (size_t i = 1; i <= 3; i++) {
        P.GetLocalAddress(i);
        forget_sent_datagrams();
        long rc = P.SendSingle(&destination, 40103, payload, sizeof payload);
        const struct SentDatagram *sent = last_sent_datagram();

        line("after selecting adapter %zu: NPIMSendSingle(\"harness-host\", 40103) -> %ld, sent %d bytes to %s:%u", i, rc,
             sent->length, inet_ntoa(sent->to.sin_addr), ntohs(sent->to.sin_port));
    }
    swallow_sendto(0);
    ustring_destruct(&destination);

    /* A host whose address is 0.0.0.0 makes the plugin ask the OS which local address routes to the outside. */
    fake_host_enable(2);
    {
        UWideString *address = P.GetLocalAddress(0);
        char routed[32] = "0.0.0.0";
        struct sockaddr_in remote = make_addr("128.127.126.125", 7), local;
        int local_length = sizeof local;
        SOCKET probe = socket(AF_INET, SOCK_DGRAM, 0);

        if (probe != INVALID_SOCKET) {
            if (connect(probe, (struct sockaddr *)&remote, sizeof remote) == 0 &&
                getsockname(probe, (struct sockaddr *)&local, &local_length) == 0) {
                snprintf(routed, sizeof routed, "%s", inet_ntoa(local.sin_addr));
            }
            closesocket(probe);
        }
        line("NPIMGetLocalAddress(0) on a host whose address is 0.0.0.0 -> the address that routes outside: %s",
             yesno(strcmp(wchars_of(address), routed) == 0));
    }
    fake_host_enable(0);
    g_fake_adapter_count = 0;
}

void scenario_empty_detected_games(void)
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
void scenario_send_single(void)
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

