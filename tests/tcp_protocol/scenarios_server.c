/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

/* Publish only some transports and probe what the remaining calls refuse. */
void scenario_single_transport_servers(void)
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

void scenario_udp_server(void)
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
    line("server event before any datagram arrived: %s", event_state(events.activity, 0));

    /* First datagram of a new client registers it (payload type 0x22). */
    memcpy(buffer, hello, sizeof hello);
    P.Send(cli, buffer, sizeof hello);
    Sleep(100);
    line("server event after a datagram arrived: %s", event_state(events.activity, 1000));
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
    event_reset(client_event);
    rc = P.SendTo(server, 0, buffer, sizeof reply);
    line("NPIMSendTo(client 0, 7 bytes) -> %ld", rc);
    line("client event after the server's datagram arrived: %s", event_state(client_event, 1000));
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
        event_reset(events.xfer_data);
        event_reset(events.xfer_accept);
        rc = P.ConnectForXfer(cli, 1, client_event, xclient);
        line("NPIMConnectForXfer(TCP) -> %ld connection=%s", rc, xclient->impl ? "created" : "null");
        Sleep(100);
        line("transfer listener events after a peer connected: accept=%s, read=%s", event_state(events.xfer_accept, 1000),
             event_state(events.xfer_data, 0));
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.AcceptForXfer(server, xserver));
        line("NPIMAcceptForXfer -> %ld connection=%s", rc, xserver->impl ? "created" : "null");
        rc = P.XferSendTo(server, 0, blob, sizeof blob);
        line("NPIMXferSendTo(client 0, reset earlier, no transfer socket) -> %ld", rc);
        rc = P.XferSendTo(server, 99, blob, sizeof blob);
        line("NPIMXferSendTo(client 99) -> %ld", rc);
        unsigned long target = 0;
        event_reset(client_event);
        for (unsigned long i = 1; i < 4; i++) {
            if (P.XferSendTo(server, i, blob, sizeof blob) == 0) {
                target = i;
                break;
            }
        }
        line("NPIMXferSendTo(first client with a transfer socket, 8 bytes) -> index %lu", target);
        Sleep(100);
        line("client event after transfer data arrived: %s", event_state(client_event, 1000));
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
        event_reset(events.activity);
        SOCKET peer = tcp_connect_peer("127.0.0.1", GAME_PORT);
        Sleep(100);
        line("server event after a TCP peer connected: %s", event_state(events.activity, 500));
        POLL_WHILE_PENDING(rc, POLL_DEFAULT_MS, P.Accept(server, tconn));
        line("NPIMAccept(peer connected: %s) -> %ld connection=%s", yesno(peer != INVALID_SOCKET), rc,
             tconn->impl ? "created" : "null");
        if (tconn->impl && peer != INVALID_SOCKET) {
            static const uint8_t out[] = "SERVER-TCP";
            static const uint8_t in[] = { 0xca, 0xfe, 0x00, 0x01 };
            event_reset(events.activity);
            send(peer, (const char *)in, sizeof in, 0);
            Sleep(100);
            line("server event after data arrived on the accepted connection: %s", event_state(events.activity, 500));
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

void scenario_probe_flags(void)
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
void scenario_close_order_quirk(void)
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

