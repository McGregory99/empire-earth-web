/*
 * Joining a game: connections to a server (NPIMConnect and friends), sending and
 * receiving over them, and the bulk-transfer side channel from the client side.
 */
#include "connection.h"

#include <stdlib.h>
#include <string.h>

#include "adapters.h"
#include "cipher.h"
#include "exports.h"
#include "packet.h"
#include "plugin_state.h"
#include "protocol.h"

ConnectionState *connection_create(SOCKET socket, unsigned long transport)
{
    ConnectionState *connection = calloc(1, sizeof *connection);

    if (connection) {
        connection->socket = socket;
        connection->transport = transport;
    }
    return connection;
}

bool transfer_listener_close(void)
{
    /* Forgetting the handle afterwards avoids closing a recycled handle by mistake. */
    if (closesocket(g_plugin.transfer_listener) != 0) {
        report_socket_error_and_acknowledge();
        return false;
    }
    g_plugin.transfer_listener = INVALID_SOCKET;
    return true;
}

/* ------------------------------------------------------------------------ */
/* NPIMConnect                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Create the socket for a connection to the game identified by (address, service, name) and
 * find where it lives.  If games were detected on the LAN, the game must be one of them and its
 * address comes from the discovery reply; otherwise `address` is resolved as a host name or IPv4
 * address and port 33334 is used.
 */
static long resolve_target(const UString *address, const UString *service, const UWideString *name,
                           unsigned long transport, SOCKET *out_socket, struct sockaddr_in *out_address)
{
    SOCKET s;
    long result = NET_OK;

    lock_enter(g_plugin.game_lock);
    s = socket(AF_INET, transport != TRANSPORT_TCP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        report_socket_error_and_acknowledge();
        result = NET_FAILED;
    } else if (g_plugin.detected_games.count > 0) {
        const DetectedGame *match = NULL;

        for (size_t i = 0; i < g_plugin.detected_games.count && !match; i++) {
            const DetectedGame *game = g_plugin.detected_games.items[i];

            if (ustring_equal(address, &game->host) && ustring_equal(service, &game->service) &&
                uwidestring_equal(name, &game->name)) {
                match = game;
            }
        }
        if (match) {
            *out_address = transport == TRANSPORT_TCP ? match->tcp_address : match->udp_address;
            out_address->sin_port = htons(GAME_PORT);
            *out_socket = s;
        } else {
            closesocket(s);
            result = NET_NOT_FOUND;
        }
    } else {
        struct hostent *host = gethostbyname(address->chars);

        if (!host) {
            uint32_t ip = inet_addr(address->chars);

            if (ip != INADDR_NONE) {
                host = gethostbyaddr((const char *)&ip, sizeof ip, AF_INET);
            }
        }
        if (host) {
            *out_socket = s;
            memset(out_address, 0, sizeof *out_address);
            out_address->sin_family = AF_INET;
            out_address->sin_port = htons(GAME_PORT);
            memcpy(&out_address->sin_addr, host->h_addr_list[0], sizeof out_address->sin_addr);
        } else {
            closesocket(s);
            result = NET_NOT_SUPPORTED;
        }
    }
    lock_leave(g_plugin.game_lock);
    return result;
}

long NPIMConnect(const UString *address, const UString *service, const UWideString *name,
                 unsigned long transport, TSEvent *event, NEConnection *connection, unsigned long adapter)
{
    SOCKET s;
    struct sockaddr_in remote, local;
    long result;

    if (transport == 0) {
        return NET_NOT_SUPPORTED;
    }
    adapter_select(adapter);
    result = resolve_target(address, service, name, transport, &s, &remote);
    if (result != NET_OK) {
        return result;
    }
    if (transport == TRANSPORT_TCP && connect(s, (struct sockaddr *)&remote, sizeof remote) != 0) {
        report_socket_error_and_acknowledge();
        closesocket(s);
        return NET_FAILED;
    }

    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    lock_enter(g_plugin.scan_lock);
    /*
     * Bind to an ephemeral port and ask for read notifications.  On a TCP socket that is
     * already connected the bind fails; the original then returns success without creating
     * a connection (and leaves the connected socket open), so only UDP connections are usable.
     */
    if (bind(s, (struct sockaddr *)&local, sizeof local) == 0) {
        ConnectionState *state;

        WSAEventSelect(s, ts_event_handle(event), FD_READ);
        state = connection_create(s, transport);
        if (state) {
            state->remote = remote;
            connection->state = state;
        }
    }
    lock_leave(g_plugin.scan_lock);
    return NET_OK;
}

/*
 * Closes the connection's socket.  Like the original this also closes the bulk-transfer
 * listener of the published server, so it reports failure whenever that listener is already
 * gone, even though the connection itself was closed.
 */
long NPIMCloseConnection(NEConnection *connection)
{
    ConnectionState *state = connection->state;
    long result = NET_OK;

    if (state && closesocket(state->socket) == 0) {
        free(state);
        connection->state = NULL;
    } else {
        report_socket_error_and_acknowledge();
        result = NET_FAILED;
    }
    if (!transfer_listener_close()) {
        result = NET_FAILED;
    }
    return result;
}

/* ------------------------------------------------------------------------ */
/* Sending and receiving                                                     */
/* ------------------------------------------------------------------------ */

/* Blocks (the socket is not told otherwise) until all bytes are written; any error aborts. */
static long send_stream(const ConnectionState *state, const uint8_t *buffer, unsigned long length)
{
    unsigned long sent = 0;

    while (sent < length) {
        int n = send(state->socket, (const char *)buffer + sent, (int)(length - sent), 0);

        if (n == SOCKET_ERROR) {
            report_socket_error_and_acknowledge();
            return NET_FAILED;
        }
        sent += (unsigned long)n;
    }
    return NET_OK;
}

/* The first byte of `buffer` is reserved for the header byte; the rest is encrypted in place. */
static long send_datagram(const ConnectionState *state, uint8_t *buffer, unsigned long length)
{
    int n;

    datagram_seal(buffer, (int)length);
    n = sendto(state->socket, (const char *)buffer, (int)length, 0, (const struct sockaddr *)&state->remote,
               sizeof state->remote);
    if (n == (int)length) {
        return NET_OK;
    }
    if (n == SOCKET_ERROR) {
        report_socket_error_and_acknowledge();
        return NET_FAILED;
    }
    return NET_NOT_SUPPORTED;
}

long NPIMSend(const NEConnection *connection, uint8_t *buffer, unsigned long length)
{
    const ConnectionState *state = connection->state;

    return state->transport == TRANSPORT_TCP ? send_stream(state, buffer, length)
                                             : send_datagram(state, buffer, length);
}

static bool same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family && a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

/* Shared tail of the receive functions for a failed recv/recvfrom. */
static long receive_error(unsigned long *received)
{
    int error = WSAGetLastError();

    if (error == WSAEWOULDBLOCK) {
        *received = 0;
        return NET_NO_DATA;
    }
    report_native_error((unsigned long)error);
    return NET_FAILED;
}

static long receive_stream(const ConnectionState *state, unsigned long capacity, uint8_t *buffer,
                           unsigned long *received)
{
    int n = recv(state->socket, (char *)buffer, (int)capacity, 0);

    if (n == 0) {
        return NET_NOT_SUPPORTED;  /* the peer closed the connection */
    }
    if (n == SOCKET_ERROR) {
        return receive_error(received);
    }
    *received = (unsigned long)n;
    return NET_OK;
}

/*
 * The next datagram from the connection's peer, decrypted in place.  Datagrams from other
 * senders and game-discovery packets are skipped; if only those were queued, the result is
 * "no data".
 */
static long receive_datagram(const ConnectionState *state, unsigned long capacity, uint8_t *buffer,
                             unsigned long *received)
{
    for (;;) {
        struct sockaddr_in from;
        int from_length = sizeof from;
        int n = recvfrom(state->socket, (char *)buffer, (int)capacity, 0, (struct sockaddr *)&from, &from_length);

        if (n == 0) {
            return NET_NOT_SUPPORTED;
        }
        if (n == SOCKET_ERROR) {
            return receive_error(received);
        }
        datagram_open(buffer, n);
        if (packet_is_probe(buffer, n) || packet_is_reply(buffer, n)) {
            *received = 0;
        } else if (same_peer(&from, &state->remote)) {
            *received = (unsigned long)n;
            return NET_OK;
        }
    }
}

/* TCP: whatever bytes are available.  UDP: see receive_datagram; `*received` includes the header byte. */
long NPIMReceive(const NEConnection *connection, unsigned long capacity, uint8_t *buffer, unsigned long *received)
{
    const ConnectionState *state = connection->state;

    if (!state) {
        return NET_NO_DATA;
    }
    return state->transport == TRANSPORT_TCP ? receive_stream(state, capacity, buffer, received)
                                             : receive_datagram(state, capacity, buffer, received);
}

/* A TCP connection is alive while the peer has not closed it; UDP has no such notion. */
unsigned char NPIMIsAlive(const NEConnection *connection)
{
    const ConnectionState *state = connection->state;
    char peeked;
    int n;

    if (!state || state->transport != TRANSPORT_TCP) {
        return state != NULL;
    }
    n = recv(state->socket, &peeked, 1, MSG_PEEK);
    if (n == 1) {
        return 1;
    }
    if (n == 0) {
        return 0;
    }
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

/*
 * Send one raw datagram (no header byte, not encrypted) to `address`:`port` from a throw-away
 * socket.  The destination is taken from the address list of `address` at the position of the
 * adapter that was selected last (the original indexes the *destination's* list with the local
 * adapter's position, which only works for single-address hosts).
 */
long NPIMSendSingle(const UString *address, unsigned short port, const uint8_t *buffer, unsigned long length)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    struct hostent *host;
    struct sockaddr_in destination, local;
    long result = NET_OK;

    if (s == INVALID_SOCKET) {
        return NET_OK;
    }
    host = gethostbyname(address->chars);
    if (!host) {
        uint32_t ip = inet_addr(address->chars);

        if (ip != INADDR_NONE) {
            host = gethostbyaddr((const char *)&ip, sizeof ip, AF_INET);
        }
    }
    if (!host) {
        result = NET_NOT_SUPPORTED;
    } else {
        const char *chosen = host->h_addr_list[g_plugin.adapter_host_index];
        int sent;

        memset(&destination, 0, sizeof destination);
        destination.sin_family = AF_INET;
        destination.sin_port = htons(port);
        memcpy(&destination.sin_addr, chosen ? chosen : host->h_addr_list[0], sizeof destination.sin_addr);

        memset(&local, 0, sizeof local);
        local.sin_family = AF_INET;
        if (bind(s, (struct sockaddr *)&local, sizeof local) == 0) {
            sent = sendto(s, (const char *)buffer, (int)length, 0, (struct sockaddr *)&destination, sizeof destination);
            if (sent == (int)length) {
                closesocket(s);
                return NET_OK;
            }
            result = sent == SOCKET_ERROR ? NET_FAILED : NET_NOT_SUPPORTED;
        } else {
            result = NET_FAILED;
        }
        if (result == NET_FAILED) {
            report_socket_error_and_acknowledge();
        }
    }
    closesocket(s);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Bulk transfers, client side                                               */
/* ------------------------------------------------------------------------ */

/*
 * Open the TCP side channel (port 33335) to the host of `server_connection`.  There is a
 * single such channel per process: its socket is remembered for NPIMXferReceive, even when the
 * connect fails (the half-open socket is then what NPIMXferReceive reads from).
 */
long NPIMConnectForXfer(const NEConnection *server_connection, unsigned long transport, TSEvent *event,
                        NEConnection *connection)
{
    const ConnectionState *server = server_connection->state;

    if (transport != TRANSPORT_TCP || !server) {
        return NET_REFUSED;
    }
    g_plugin.transfer_client = socket(AF_INET, SOCK_STREAM, 0);
    if (g_plugin.transfer_client != INVALID_SOCKET) {
        struct sockaddr_in address;

        memset(&address, 0, sizeof address);
        address.sin_family = AF_INET;
        address.sin_port = htons(TRANSFER_PORT);
        address.sin_addr = server->remote.sin_addr;
        if (connect(g_plugin.transfer_client, (struct sockaddr *)&address, sizeof address) != SOCKET_ERROR) {
            ConnectionState *state = connection_create(g_plugin.transfer_client, TRANSPORT_TCP);

            if (state) {
                state->remote = address;
                connection->state = state;
            }
            WSAEventSelect(g_plugin.transfer_client, ts_event_handle(event), FD_READ);
            return NET_OK;
        }
    }
    report_socket_error_and_acknowledge();
    return NET_FAILED;
}

long NPIMXferReceive(unsigned long capacity, uint8_t *buffer, unsigned long *received)
{
    int n = recv(g_plugin.transfer_client, (char *)buffer, (int)capacity, 0);

    if (n == 0) {
        return NET_NOT_SUPPORTED;
    }
    if (n == SOCKET_ERROR) {
        return receive_error(received);
    }
    *received = (unsigned long)n;
    return NET_OK;
}
