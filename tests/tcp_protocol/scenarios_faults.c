/*
 * Error paths.  A healthy loopback never makes a socket call fail, so the harness arms
 * faults in the plugin's Winsock imports (see interposer.c) and records, for each, the
 * status the plugin returns and which error reports it makes to the engine.
 */
#include "harness.h"

#define SOCKET_FAILED (-1) /* SOCKET_ERROR and INVALID_SOCKET */

/* "fault used; engine: record(10065) acknowledge" */
static const char *report(void)
{
    static char text[640];
    const char *fault = fault_state();

    snprintf(text, sizeof text, "%s; engine: %s", fault, engine_errors());
    return text;
}

static long connect_to_loopback(NeHandle *connection, TSEvent *event, unsigned long transport)
{
    UString address, service;
    UWideString name;
    long rc;

    ustring_construct_from_chars(&address, "127.0.0.1");
    ustring_construct_from_chars(&service, "ee-fault");
    uwidestring_construct_from_chars(&name, "Fault Game");
    rc = P.Connect(&address, &service, &name, transport, event, connection, 0);
    ustring_destruct(&address);
    ustring_destruct(&service);
    uwidestring_destruct(&name);
    return rc;
}

/* ------------------------------------------------------------------------ */

static void activation_faults(void)
{
    long rc;
    int valid;

    line("-- activation");
    engine_errors();
    fault_inject("WSACleanup", 0, 1, SOCKET_FAILED, WSANOTINITIALISED);
    rc = P.Deactivate();
    valid = P.PIMIsValid();
    line("WSACleanup fails: NPIMDeactivate -> %ld, PIMIsValid %d; %s", rc, valid, report());
    rc = P.Deactivate();
    valid = P.PIMIsValid();
    line("NPIMDeactivate -> %ld, PIMIsValid %d", rc, valid);

    fault_inject("WSAStartup", 0, 1, WSASYSNOTREADY, WSASYSNOTREADY);
    rc = P.Activate();
    valid = P.PIMIsValid();
    line("WSAStartup fails: NPIMActivate -> %ld, PIMIsValid %d; %s", rc, valid, report());
    fault_inject("WSAStartup", 0, 1, FAULT_BAD_VERSION, 0);
    rc = P.Activate();
    valid = P.PIMIsValid();
    line("WSAStartup reports version 1.1: NPIMActivate -> %ld, PIMIsValid %d; %s", rc, valid, report());
    fault_inject("socket", 0, 1, SOCKET_FAILED, WSAEMFILE);
    rc = P.Activate();
    valid = P.PIMIsValid();
    line("no stream socket can be created: NPIMActivate -> %ld, PIMIsValid %d; %s", rc, valid, report());
    rc = P.Activate();
    valid = P.PIMIsValid();
    line("NPIMActivate -> %ld, PIMIsValid %d", rc, valid);
}

/* ------------------------------------------------------------------------ */

static void client_faults(void)
{
    static const uint8_t payload[] = { 0x00, 0x22, 'F', 'A', 'U', 'L', 'T' };
    NeHandle *connection = new_handle(), *transfer = new_handle();
    TSEvent *event = new_event();
    uint8_t buffer[64];
    unsigned long received;
    UString address;
    long rc;

    line("-- connections");
    engine_errors();
    fault_inject("socket", 0, 1, SOCKET_FAILED, WSAEMFILE);
    rc = connect_to_loopback(connection, event, 2);
    line("socket fails: NPIMConnect(UDP) -> %ld connection=%s; %s", rc, connection->impl ? "created" : "null", report());
    fault_inject("connect", 0, 1, SOCKET_FAILED, WSAECONNREFUSED);
    rc = connect_to_loopback(connection, event, 1);
    line("connect fails: NPIMConnect(TCP) -> %ld connection=%s; %s", rc, connection->impl ? "created" : "null", report());
    fault_inject("bind", 0, 1, SOCKET_FAILED, WSAEADDRINUSE);
    rc = connect_to_loopback(connection, event, 2);
    line("bind fails: NPIMConnect(UDP) -> %ld connection=%s; %s", rc, connection->impl ? "created" : "null", report());

    rc = connect_to_loopback(connection, event, 2);
    line("NPIMConnect(UDP) -> %ld connection=%s", rc, connection->impl ? "created" : "null");

    memcpy(buffer, payload, sizeof payload);
    fault_inject("sendto", 0, 1, SOCKET_FAILED, WSAEHOSTUNREACH);
    rc = P.Send(connection, buffer, sizeof payload);
    line("sendto fails: NPIMSend(UDP) -> %ld; %s", rc, report());
    fault_inject("sendto", 0, 1, FAULT_SHORT, 0);
    rc = P.Send(connection, buffer, sizeof payload);
    line("sendto sends too few bytes: NPIMSend(UDP) -> %ld; %s", rc, report());

    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.Receive(connection, sizeof buffer, buffer, &received);
    line("recvfrom fails: NPIMReceive(UDP) -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.Receive(connection, sizeof buffer, buffer, &received);
    line("recvfrom would block: NPIMReceive(UDP) -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, 0, 0);
    rc = P.Receive(connection, sizeof buffer, buffer, &received);
    line("recvfrom returns nothing: NPIMReceive(UDP) -> %ld received=0x%lx; %s", rc, received, report());

    /* Datagrams to an arbitrary address. */
    ustring_construct_from_chars(&address, "127.0.0.1");
    fault_inject("socket", 0, 1, SOCKET_FAILED, WSAEMFILE);
    rc = P.SendSingle(&address, 40102, payload, sizeof payload);
    line("socket fails: NPIMSendSingle -> %ld; %s", rc, report());
    fault_inject("bind", 0, 1, SOCKET_FAILED, WSAEADDRINUSE);
    rc = P.SendSingle(&address, 40102, payload, sizeof payload);
    line("bind fails: NPIMSendSingle -> %ld; %s", rc, report());
    fault_inject("sendto", 0, 1, SOCKET_FAILED, WSAEHOSTUNREACH);
    rc = P.SendSingle(&address, 40102, payload, sizeof payload);
    line("sendto fails: NPIMSendSingle -> %ld; %s", rc, report());
    fault_inject("sendto", 0, 1, FAULT_SHORT, 0);
    rc = P.SendSingle(&address, 40102, payload, sizeof payload);
    line("sendto sends too few bytes: NPIMSendSingle -> %ld; %s", rc, report());
    ustring_destruct(&address);

    /* The bulk-transfer connection. */
    fault_inject("socket", 0, 1, SOCKET_FAILED, WSAEMFILE);
    rc = P.ConnectForXfer(connection, 1, event, transfer);
    line("socket fails: NPIMConnectForXfer -> %ld connection=%s; %s", rc, transfer->impl ? "created" : "null", report());
    fault_inject("connect", 0, 1, SOCKET_FAILED, WSAECONNREFUSED);
    rc = P.ConnectForXfer(connection, 1, event, transfer);
    line("connect fails: NPIMConnectForXfer -> %ld connection=%s; %s", rc, transfer->impl ? "created" : "null", report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.XferReceive(sizeof buffer, buffer, &received);
    line("recv fails: NPIMXferReceive -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.XferReceive(sizeof buffer, buffer, &received);
    line("recv would block: NPIMXferReceive -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, 0, 0);
    rc = P.XferReceive(sizeof buffer, buffer, &received);
    line("recv returns nothing: NPIMXferReceive -> %ld received=0x%lx; %s", rc, received, report());

    /*
     * Closing: NPIMCloseConnection closes the connection's socket and the shared transfer
     * listener.  Fail both closes (the connection stays), then only the second.
     */
    fault_inject("closesocket", 0, 2, SOCKET_FAILED, WSAENOTSOCK);
    rc = P.CloseConnection(connection);
    line("neither socket can be closed: NPIMCloseConnection -> %ld connection=%s; %s", rc,
         connection->impl ? "still set" : "cleared", report());
    fault_inject("closesocket", 1, 1, SOCKET_FAILED, WSAENOTSOCK);
    rc = P.CloseConnection(connection);
    line("the transfer listener cannot be closed: NPIMCloseConnection -> %ld connection=%s; %s", rc,
         connection->impl ? "still set" : "cleared", report());

    event_reset(event);
    tsscheduler_destroy_event(event);
    free(connection);
    free(transfer);
}

/* ------------------------------------------------------------------------ */

static void publish_faults(void)
{
    static const struct {
        const char *label;
        unsigned long transports;
        const char *function;
        int skip;
        int error;
    } cases[] = {
        /* Each transport first restarts Winsock, which creates (and closes) a probe socket. */
        { "TCP socket", 1, "socket", 1, WSAEMFILE },
        { "TCP non-blocking mode", 1, "ioctlsocket", 0, WSAENOTSOCK },
        { "TCP bind", 1, "bind", 0, WSAEADDRINUSE },
        { "TCP getsockname", 1, "getsockname", 0, WSAENOTSOCK },
        { "TCP listen", 1, "listen", 0, WSAEINVAL },
        { "UDP socket", 2, "socket", 1, WSAEMFILE },
        { "UDP bind", 2, "bind", 0, WSAEADDRINUSE },
        { "UDP after TCP: socket", 3, "socket", 3, WSAEMFILE },
        { "UDP after TCP: bind", 3, "bind", 1, WSAEADDRINUSE },
    };
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();

    line("-- publishing");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        engine_errors();
        fault_inject(cases[i].function, cases[i].skip, 1, SOCKET_FAILED, cases[i].error);
        long rc = publish(server, "ee-fault", "Fault Game", cases[i].transports, &events, 1, 1);
        line("%s fails: NPIMPublish -> %ld; %s", cases[i].label, rc, report());
    }
    free_server_events(&events);
    free(server);
}

/* The LAN scan that publishing starts (unless the game is an internet game) fails without failing the publish. */
static void scan_faults(void)
{
    static const struct {
        const char *label;
        const char *function;
        int skip;
        int error;
    } cases[] = {
        { "broadcast socket", "socket", 0, WSAEMFILE },
        { "SO_BROADCAST", "setsockopt", 0, WSAENOBUFS },
        { "reply socket", "socket", 1, WSAEMFILE },
        { "reply socket bind", "bind", 0, WSAEADDRINUSE },
        /* Failures after the reply socket is bound leave it bound; see the isolated scenarios below. */
    };
    NeHandle *server = new_handle();
    ServerEvents events = new_server_events();

    line("-- LAN scan while publishing");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        engine_errors();
        fault_inject(cases[i].function, cases[i].skip, 1, SOCKET_FAILED, cases[i].error);
        long rc = publish(server, "ee-fault", "Fault Game", 2, &events, 0, 1);
        const char *state = report();

        line("%s fails: NPIMPublish(UDP, duplicate check on) -> %ld server=%s; %s", cases[i].label, rc,
             server->impl ? "created" : "null", state);
        if (server->impl) {
            rc = P.CloseServer(server);
            line("  NPIMCloseServer -> %ld", rc);
        }
    }
    free_server_events(&events);
    free(server);
}

/* ------------------------------------------------------------------------ */

static void server_faults(void)
{
    static const uint8_t hello[] = { 0x00, 0x22, 'H', 'I' };
    static const uint8_t blob[] = { 'B', 'L', 'O', 'B' };
    NeHandle *server = new_handle(), *accepted = new_handle(), *client = new_handle(), *xclient = new_handle(),
             *xserver = new_handle();
    ServerEvents events = new_server_events();
    TSEvent *client_event = new_event();
    uint8_t buffer[512], wire[512];
    unsigned long received, index = 0;
    struct sockaddr_in server_addr = make_addr("127.0.0.1", GAME_PORT);
    long rc;

    line("-- server");
    rc = publish(server, "ee-fault", "Fault Game", 3, &events, 1, 1);
    line("NPIMPublish(TCP+UDP) -> %ld", rc);
    rc = connect_to_loopback(client, client_event, 2);
    memcpy(buffer, hello, sizeof hello);
    P.Send(client, buffer, sizeof hello);
    received = 0;
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.ReceiveFrom(server, sizeof buffer, buffer, &received, &index));
    line("a client registers: NPIMReceiveFrom -> %ld client=%lu", rc, index);

    /* TCP side. */
    engine_errors();
    fault_inject("accept", 0, 1, SOCKET_FAILED, WSAENOTSOCK);
    rc = P.Accept(server, accepted);
    line("accept fails: NPIMAccept -> %ld; %s", rc, report());
    fault_inject("accept", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.Accept(server, accepted);
    line("accept would block: NPIMAccept -> %ld; %s", rc, report());
    SOCKET peer = tcp_connect_peer("127.0.0.1", GAME_PORT);
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Accept(server, accepted));
    line("NPIMAccept(peer connected: %s) -> %ld connection=%s", yesno(peer != INVALID_SOCKET), rc,
         accepted->impl ? "created" : "null");

    fault_inject("send", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.Send(accepted, blob, sizeof blob);
    line("send fails: NPIMSend(TCP) -> %ld; %s", rc, report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.Receive(accepted, sizeof buffer, buffer, &received);
    line("recv fails: NPIMReceive(TCP) -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.Receive(accepted, sizeof buffer, buffer, &received);
    line("recv would block: NPIMReceive(TCP) -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recv", 0, 1, 0, 0);
    rc = P.Receive(accepted, sizeof buffer, buffer, &received);
    line("recv returns nothing: NPIMReceive(TCP) -> %ld received=0x%lx; %s", rc, received, report());
    {
        static const struct {
            const char *label;
            long result;
            int error;
        } peeks[] = {
            { "a byte is waiting", 1, 0 },
            { "the peer closed", 0, 0 },
            { "the call would block", SOCKET_FAILED, WSAEWOULDBLOCK },
            { "the connection was reset", SOCKET_FAILED, WSAECONNRESET },
        };
        for (size_t i = 0; i < sizeof peeks / sizeof peeks[0]; i++) {
            fault_inject("recv", 0, 1, peeks[i].result, peeks[i].error);
            int alive = P.IsAlive(accepted);
            line("%s: NPIMIsAlive(TCP) -> %d; %s", peeks[i].label, alive, report());
        }
    }

    /* UDP side. */
    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &index);
    line("recvfrom fails: NPIMReceiveFrom -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &index);
    line("recvfrom would block: NPIMReceiveFrom -> %ld received=0x%lx; %s", rc, received, report());
    received = 0xdeadbeefu;
    fault_inject("recvfrom", 0, 1, 0, 0);
    rc = P.ReceiveFrom(server, sizeof buffer, buffer, &received, &index);
    line("recvfrom returns nothing: NPIMReceiveFrom -> %ld received=0x%lx; %s", rc, received, report());
    memcpy(buffer, hello, sizeof hello);
    fault_inject("sendto", 0, 1, SOCKET_FAILED, WSAEHOSTUNREACH);
    rc = P.SendTo(server, 0, buffer, sizeof hello);
    line("sendto fails: NPIMSendTo -> %ld; %s", rc, report());
    fault_inject("sendto", 0, 1, FAULT_SHORT, 0);
    rc = P.SendTo(server, 0, buffer, sizeof hello);
    line("sendto sends too few bytes: NPIMSendTo -> %ld; %s", rc, report());

    /* Bulk transfers. */
    fault_inject("accept", 0, 1, SOCKET_FAILED, WSAENOTSOCK);
    rc = P.AcceptForXfer(server, xserver);
    line("accept fails: NPIMAcceptForXfer -> %ld; %s", rc, report());
    fault_inject("accept", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.AcceptForXfer(server, xserver);
    line("accept would block: NPIMAcceptForXfer -> %ld; %s", rc, report());
    rc = P.ConnectForXfer(client, 1, client_event, xclient);
    POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.AcceptForXfer(server, xserver));
    line("NPIMAcceptForXfer -> %ld connection=%s", rc, xserver->impl ? "created" : "null");
    fault_inject("send", 0, 1, SOCKET_FAILED, WSAEWOULDBLOCK);
    rc = P.XferSendTo(server, 0, blob, sizeof blob);
    received = 0xdeadbeefu;
    unsigned long xreceived = 0xdeadbeefu;
    unsigned long transfer_rc;
    POLL_WHILE_PENDING(transfer_rc, POLL_DEFAULT_MS, P.XferReceive(sizeof buffer, buffer, &xreceived));
    line("send would block once: NPIMXferSendTo -> %ld; peer got %lu bytes (status %lu); %s", rc, xreceived, transfer_rc,
         report());
    fault_inject("send", 0, 1, SOCKET_FAILED, WSAECONNRESET);
    rc = P.XferSendTo(server, 0, blob, sizeof blob);
    line("send fails: NPIMXferSendTo -> %ld; %s", rc, report());

    /* The probe reply of a discoverable server. */
    {
        NeHandle *visible = new_handle();
        ServerEvents visible_events = new_server_events();
        uint8_t probe_plain[0x42];
        SOCKET asker;
        size_t n;

        rc = P.CloseServer(server);
        line("NPIMCloseServer -> %ld", rc);
        rc = publish(visible, "ee-fault", "Fault Game", 2, &visible_events, 0, 1);
        line("NPIMPublish(UDP, discoverable) -> %ld", rc);
        asker = udp_socket(NULL, 0, 0);
        memset(probe_plain, 0, sizeof probe_plain);
        probe_plain[0] = 0x15;
        strcpy((char *)probe_plain + 1, "ee-fault");
        for (int round = 0; round < 2; round++) {
            n = seal(wire, 0x42, probe_plain, sizeof probe_plain);
            udp_send(asker, wire, (int)n, &server_addr);
            Sleep(100);
            engine_errors();
            fault_inject(round == 0 ? "socket" : "sendto", 0, 1, SOCKET_FAILED, round == 0 ? WSAEMFILE : WSAEHOSTUNREACH);
            received = 0xdeadbeefu;
            rc = P.ReceiveFrom(visible, sizeof buffer, buffer, &received, &index);
            line("%s fails while answering a probe: NPIMReceiveFrom -> %ld; %s", round == 0 ? "socket" : "sendto", rc,
                 report());
        }
        closesocket(asker);

        /* Failing to close: every close fails, the server stays; then one close fails (after the first). */
        fault_inject("closesocket", 0, 2, SOCKET_FAILED, WSAENOTSOCK);
        rc = P.CloseServer(visible);
        line("the sockets cannot be closed: NPIMCloseServer -> %ld server=%s; %s", rc,
             visible->impl ? "still set" : "cleared", report());
        fault_inject("closesocket", 1, 1, SOCKET_FAILED, WSAENOTSOCK);
        rc = P.CloseServer(visible);
        line("the transfer listener cannot be closed: NPIMCloseServer -> %ld server=%s; %s", rc,
             visible->impl ? "still set" : "cleared", report());
        free_server_events(&visible_events);
        free(visible);
    }

    if (peer != INVALID_SOCKET) {
        closesocket(peer);
    }
    free_server_events(&events);
    free(server);
    free(accepted);
    free(client);
    free(xclient);
    free(xserver);
    tsscheduler_destroy_event(client_event);
}

/* ------------------------------------------------------------------------ */
/* Failures that leave the game port bound                                    */
/* ------------------------------------------------------------------------ */

/*
 * Two defects of the original keep the game port bound for the rest of the process after a
 * failure, so that every later publish fails with "address in use".  They are kept in the
 * reimplementation for identical behavior.  Each scenario below runs in a process of its own
 * (see harness.c), because the damage cannot be undone.
 */

/* A scan whose probe cannot be sent (or whose reply socket cannot be made non-blocking) never closes its bound reply socket. */
static void scan_leak(const char *what, const char *function, int error)
{
    NeHandle *first = new_handle(), *second = new_handle();
    ServerEvents events = new_server_events();
    long rc;

    fault_inject(function, 0, 1, SOCKET_FAILED, error);
    rc = publish(first, "ee-fault", "Fault Game", 2, &events, 0, 1);
    line("%s fails during the LAN scan: NPIMPublish(UDP, duplicate check on) -> %ld; %s", what, rc, report());
    rc = publish(second, "ee-fault", "Fault Game", 2, &events, 1, 1);
    line("a second NPIMPublish(UDP) -> %ld server=%s; %s", rc, second->impl ? "created" : "null", report());
    free_server_events(&events);
    free(first);
    free(second);
}

void scenario_leak_scan_sendto(void)
{
    line("== a failed LAN scan keeps the game port (probe not sent)");
    scan_leak("sendto", "sendto", WSAEHOSTUNREACH);
}

void scenario_leak_scan_ioctl(void)
{
    line("== a failed LAN scan keeps the game port (reply socket not made non-blocking)");
    scan_leak("ioctlsocket", "ioctlsocket", WSAENOTSOCK);
}

/* When the bulk-transfer listener cannot be opened, the server that was just built stays behind. */
void scenario_leak_transfer_listener(void)
{
    NeHandle *first = new_handle(), *second = new_handle();
    ServerEvents events = new_server_events();
    long rc;

    line("== a failed publish keeps the game port (bulk-transfer listener cannot be opened)");
    /* The TCP and UDP sockets bind first (two binds), then the transfer listener. */
    fault_inject("bind", 2, 1, SOCKET_FAILED, WSAEADDRINUSE);
    rc = publish(first, "ee-fault", "Fault Game", 3, &events, 1, 1);
    line("transfer listener bind fails: NPIMPublish(TCP+UDP) -> %ld; %s", rc, report());
    rc = publish(second, "ee-fault", "Fault Game", 3, &events, 1, 1);
    line("a second NPIMPublish(TCP+UDP) -> %ld server=%s; %s", rc, second->impl ? "created" : "null", report());
    free_server_events(&events);
    free(first);
    free(second);
}

void scenario_faults(void)
{
    line("== error paths (faults injected into the plugin's Winsock calls)");
    activation_faults();
    client_faults();
    publish_faults();
    scan_faults();
    server_faults();
}
