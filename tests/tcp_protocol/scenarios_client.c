/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

/*
 * UDP client connection, with the harness acting as the server.  The harness
 * socket shares port 33334 with nothing else, so it sees exactly the bytes
 * the client puts on the wire and can answer with crafted datagrams.
 */
void scenario_udp_client_wire(void)
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
    line("connection event before any datagram arrived: %s", event_state(event, 0));

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
    event_reset(event);
    udp_send(stranger, noise, sizeof noise, &client_addr);
    udp_send(server, spoof, (int)spoof_length, &client_addr);   /* right source, but a 0x88/0x16 packet */
    static const uint8_t reply[] = { 0x01, 'R', 'E', 'P', 'L', 'Y' };
    size_t reply_length = seal(wire, 0x5a, reply, sizeof reply);
    udp_send(server, wire, (int)reply_length, &client_addr);
    Sleep(100);
    line("connection event after datagrams arrived: %s", event_state(event, 1000));
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
        for (int i = 0; i < 2000; i++) {
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
        line("NPIMSend x2000: decrypted=%u/2000 header_range_1_to_254=%s many_distinct_headers=%s", decrypted,
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
void scenario_tcp_client(void)
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


