/*
 * Game names at the limits of the discovery datagrams.  The name field of a reply is 69 bytes
 * wide (the 65-byte field of the service name plus the 4 spare bytes of the datagram), so a
 * name of up to 68 characters travels intact and terminated.  The original builds a reply with
 * strcpy into a 69-byte stack buffer that is followed by the sockaddr it then sends to: a name
 * of 69 or more characters overwrites that address, sendto fails and no reply goes out.
 */
#include "harness.h"

#define NAME_FIELD_SIZE 69
#define REPLY_NAME_OFFSET 0x42 /* in the decrypted payload, i.e. wire offset 0x43 */

/* A name of `length` characters: a, b, ..., z, a, ... so that any truncation is visible. */
static void make_name(char *name, unsigned length)
{
    for (unsigned i = 0; i < length; i++) {
        name[i] = (char)('a' + i % 26);
    }
    name[length] = '\0';
}

/* Ask a published game for its reply over loopback and describe the name in the reply. */
static void reply_with_name(unsigned length)
{
    char name[128];
    uint8_t probe_plain[0x42], wire[512], plain[512], buffer[512];
    unsigned long received = 0, client = 0;
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    struct sockaddr_in server_addr = make_addr("127.0.0.1", GAME_PORT), from;

    make_name(name, length);
    long rc = publish(server, "ee-long", name, 2, &events, 0, 1);

    SOCKET asker = udp_socket(NULL, 0, 0);
    memset(probe_plain, 0, sizeof probe_plain);
    probe_plain[0] = 0x15;
    strcpy((char *)probe_plain + 1, "ee-long");
    size_t n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
    udp_send(asker, wire, (int)n, &server_addr);
    Sleep(100);
    SOCKET capture = udp_socket("127.0.0.1", GAME_PORT, 1);
    engine_errors();
    long probe_rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &client);
    const char *errors = engine_errors();
    int got = capture == INVALID_SOCKET ? -1 : udp_receive(capture, wire, sizeof wire, &from, 700);

    if (got > 0) {
        int chars = 0;
        unseal(plain, wire, (size_t)got);
        while (chars < NAME_FIELD_SIZE && plain[REPLY_NAME_OFFSET + chars]) {
            chars++;
        }
        line("%u-character name: NPIMPublish -> %ld, NPIMReceiveFrom(probe) -> %ld; reply of %d bytes with %d name "
             "characters, %s, equal to the name's start: %s; engine: %s",
             length, rc, probe_rc, got, chars, chars < NAME_FIELD_SIZE ? "terminated" : "not terminated",
             yesno(memcmp(plain + REPLY_NAME_OFFSET, name, (size_t)chars) == 0), errors);
    } else {
        line("%u-character name: NPIMPublish -> %ld, NPIMReceiveFrom(probe) -> %ld; no reply; engine: %s", length, rc,
             probe_rc, errors);
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

/*
 * A game of this name advertises itself on the LAN: is its name detected intact, and does
 * publishing a game of the same name (but not one that differs in the last character) get refused?
 */
static void detect_and_compare_name(unsigned length)
{
    char name[128], other[128];
    UString nothing, service, host, found_service;
    UWideString found_name;
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();
    Feeder feeder;
    unsigned long count = 0;
    long rc;

    make_name(name, length);
    make_name(other, length);
    other[length - 1] = other[length - 1] == 'z' ? 'y' : 'z';
    ustring_construct_from_chars(&nothing, "");
    ustring_construct_from_chars(&service, "ee-long");
    ustring_construct(&host);
    ustring_construct(&found_service);
    uwidestring_construct(&found_name);

    feeder_start(&feeder, "ee-long", name);
    P.SearchForGame(&nothing, &service, 1);
    for (DWORD started = GetTickCount(); GetTickCount() - started < 10000;) {
        count = P.GetDetectedGameCount();
        if (count > 0) {
            break;
        }
        Sleep(25);
    }
    rc = P.GetDetectedGame(0, &host, &found_service, &found_name);
    line("%u-character name advertised on the LAN: NPIMGetDetectedGame -> %ld, name of %lu characters, intact: %s", length,
         rc, (unsigned long)strlen(wchars_of(&found_name)), yesno(strcmp(wchars_of(&found_name), name) == 0));
    P.SearchForGame(&nothing, &service, 0);

    rc = publish(server, "ee-long", name, 2, &events, 0, 1);
    line("  publishing a game of that name, duplicate check on -> %ld", rc);
    if (server->impl) {
        P.CloseServer(server);
    }
    rc = publish(server, "ee-long", other, 2, &events, 0, 1);
    line("  publishing a game whose name differs in the last character -> %ld", rc);
    if (server->impl) {
        P.CloseServer(server);
    }
    feeder_stop(&feeder);
    P.SearchForGame(&nothing, &service, 0); /* forget the games the scans above detected */

    uwidestring_destruct(&found_name);
    ustring_destruct(&found_service);
    ustring_destruct(&host);
    ustring_destruct(&service);
    ustring_destruct(&nothing);
    free_server_events(&events);
    free(server);
}

void scenario_long_names(void)
{
    static const unsigned replies[] = { 64, 65, 68, 69, 70, 72, 84 };
    static const unsigned lan[] = { 64, 65, 68 };

    line("== game names at the limit of the discovery datagrams");
    for (size_t i = 0; i < sizeof replies / sizeof replies[0]; i++) {
        reply_with_name(replies[i]);
    }
    if (!broadcast_available()) {
        line("SKIPPED: UDP broadcast is not available on this host");
        return;
    }
    for (size_t i = 0; i < sizeof lan / sizeof lan[0]; i++) {
        detect_and_compare_name(lan[i]);
    }
}
