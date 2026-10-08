/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

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

void scenario_discovery(void)
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

