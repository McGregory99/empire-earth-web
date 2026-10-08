/*
 * Hosting a game: publishing a server, accepting TCP clients, exchanging datagrams
 * with UDP clients, answering LAN discovery probes and serving the bulk-transfer
 * side channel.
 */
#include "server.h"

#include <stdlib.h>
#include <string.h>

#include "adapters.h"
#include "cipher.h"
#include "discovery.h"
#include "exports.h"
#include "plugin_state.h"
#include "protocol.h"

/* Backlog of the listening sockets: "as many as the stack allows". */
#define LISTEN_BACKLOG 0x7fffffff

/* ------------------------------------------------------------------------ */
/* Server state                                                              */
/* ------------------------------------------------------------------------ */

static ServerState *server_state_create(unsigned long transports, uint8_t internet_game, uint8_t announce_on_lan)
{
    ServerState *state = calloc(1, sizeof *state);

    if (state) {
        state->tcp_socket = INVALID_SOCKET;
        state->udp_socket = INVALID_SOCKET;
        ptr_array_init(&state->clients);
        ptr_array_init(&state->transfers);
        state->transports = transports;
        state->internet_game = internet_game;
        state->announce_on_lan = announce_on_lan;
        ustring_construct(&state->service);
        uwidestring_construct(&state->name);
    }
    return state;
}

/* Free the state of a server.  Sockets are the caller's business. */
static void server_state_destroy(ServerState *state)
{
    for (size_t i = 0; i < state->clients.size; i++) {
        free(state->clients.items[i]);
    }
    for (size_t i = 0; i < state->transfers.size; i++) {
        free(state->transfers.items[i]);
    }
    uwidestring_destruct(&state->name);
    ustring_destruct(&state->service);
    ptr_array_release(&state->transfers);
    ptr_array_release(&state->clients);
    free(state);
}

/* ------------------------------------------------------------------------ */
/* NPIMPublish                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Create the non-blocking socket for one transport, bound to the game port, listening if it is
 * TCP.  Like the original this first restarts Winsock (NPIMDeactivate / NPIMActivate).
 * `*port` is the port to bind (network order) and receives the port actually bound.
 */
static long create_server_socket(unsigned long transport, SOCKET *out_socket, unsigned long *port)
{
    u_long nonblocking = 1;
    struct sockaddr_in address;
    int address_length = sizeof address;
    SOCKET s;

    NPIMDeactivate();
    NPIMActivate();

    s = socket(AF_INET, transport != TRANSPORT_TCP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        report_socket_error();
        *out_socket = INVALID_SOCKET;
        return NET_FAILED;
    }
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = (u_short)*port;
    if (ioctlsocket(s, FIONBIO, &nonblocking) != 0) {
        report_socket_error();
    } else if (bind(s, (struct sockaddr *)&address, sizeof address) != 0) {
        report_socket_error_and_acknowledge();
    } else if (getsockname(s, (struct sockaddr *)&address, &address_length) != 0) {
        report_socket_error();
    } else {
        *port = address.sin_port;
        if (transport != TRANSPORT_TCP || listen(s, LISTEN_BACKLOG) == 0) {
            *out_socket = s;
            return NET_OK;
        }
        report_socket_error();
    }
    closesocket(s);
    *out_socket = INVALID_SOCKET;
    return NET_FAILED;
}

/* Create the transport sockets named in state->transports; false on failure (nothing is closed). */
static bool open_transports(ServerState *state, TSEvent *socket_event)
{
    unsigned long port = htons(GAME_PORT);
    SOCKET s;

    if (state->transports & TRANSPORT_TCP) {
        if (create_server_socket(TRANSPORT_TCP, &s, &port) != NET_OK) {
            return false;
        }
        state->port = port;
        state->accepting = 0;
        state->tcp_socket = s;
        WSAEventSelect(s, ts_event_handle(socket_event), FD_READ);
    }
    if (state->transports & TRANSPORT_UDP) {
        if (create_server_socket(TRANSPORT_UDP, &s, &port) != NET_OK) {
            return false;
        }
        state->port = port;
        state->accepting = 1;
        state->udp_socket = s;
        WSAEventSelect(s, ts_event_handle(socket_event), FD_READ);
    }
    return true;
}

/* The process-wide TCP listener for bulk transfers (port 33335). */
static bool open_transfer_listener(TSEvent *data_event, TSEvent *accept_event)
{
    u_long nonblocking = 1;
    struct sockaddr_in address;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    g_plugin.transfer_listener = s;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(TRANSFER_PORT);
    if (s != INVALID_SOCKET && ioctlsocket(s, FIONBIO, &nonblocking) == 0 &&
        bind(s, (struct sockaddr *)&address, sizeof address) == 0 && listen(s, LISTEN_BACKLOG) == 0) {
        WSAEventSelect(s, ts_event_handle(data_event), FD_READ);
        /* The last WSAEventSelect on a socket wins: in effect only accepts are signalled. */
        WSAEventSelect(s, ts_event_handle(accept_event), FD_ACCEPT);
        return true;
    }
    report_socket_error();
    closesocket(s);
    g_plugin.transfer_listener = INVALID_SOCKET;
    return false;
}

/*
 * With the LAN duplicate check on, scan the LAN first and refuse a game name that is already in
 * use there.  Returns true when the name is taken.
 */
static bool name_in_use_on_lan(const UString *service, const UWideString *name)
{
    UString host, found_service;
    UWideString found_name;
    bool taken = false;

    if (discovery_scan(service) != NET_OK) {
        return false;
    }
    ustring_construct(&host);
    ustring_construct(&found_service);
    uwidestring_construct(&found_name);
    for (unsigned long i = 0, count = NPIMGetDetectedGameCount(); i < count && !taken; i++) {
        NPIMGetDetectedGame(i, &host, &found_service, &found_name);
        taken = uwidestring_equal(&found_name, name) != 0;
    }
    uwidestring_destruct(&found_name);
    ustring_destruct(&found_service);
    ustring_destruct(&host);
    return taken;
}

/*
 * Publish a game on the TCP and/or UDP transport(s) named in `transports`, and open the
 * bulk-transfer listener.  Both Boolean flags are only known by how they are used: unless
 * `internet_game` is set, the LAN is checked for a game of the same name (failing with
 * NET_FAILED if there is one), and a server answers LAN discovery probes only if it is not an
 * internet game and `announce_on_lan` is set.
 *
 * Deviation from the original: when the transfer listener cannot be opened, the original
 * reports failure but leaves the half-built server (and its bound sockets) behind; we
 * dispose of it.
 */
long NPIMPublish(const UString *service, const UWideString *name, unsigned long transports,
                 TSEvent *socket_event, TSEvent *transfer_data_event, TSEvent *transfer_accept_event,
                 NEServer *server, unsigned char internet_game, unsigned char announce_on_lan,
                 unsigned long adapter)
{
    ServerState *state;

    if (internet_game == 0 && name_in_use_on_lan(service, name)) {
        es_get_native_error();
        es_record_native_error(es_get_native_error());
        return NET_FAILED;
    }
    adapter_select(adapter);

    state = server_state_create(transports, internet_game, announce_on_lan);
    if (!state) {
        return NET_OUT_OF_MEMORY;
    }
    if (!open_transports(state, socket_event)) {
        if (state->tcp_socket != INVALID_SOCKET) {
            closesocket(state->tcp_socket);
        }
        if (state->udp_socket != INVALID_SOCKET) {
            closesocket(state->udp_socket);
        }
        server_state_destroy(state);
        return NET_FAILED;
    }
    ustring_assign(&state->service, service);
    uwidestring_assign(&state->name, name);

    if (!open_transfer_listener(transfer_data_event, transfer_accept_event)) {
        if (state->tcp_socket != INVALID_SOCKET) {
            closesocket(state->tcp_socket);
        }
        if (state->udp_socket != INVALID_SOCKET) {
            closesocket(state->udp_socket);
        }
        server_state_destroy(state);
        return NET_FAILED;
    }
    server->state = state;
    return NET_OK;
}

/*
 * Close the server's sockets and the bulk-transfer listener and free its state.  If any close
 * fails (for instance the listener was already closed by NPIMCloseConnection) the server stays open.
 */
long NPIMCloseServer(NEServer *server)
{
    ServerState *state = server->state;
    long result = NET_OK;

    if ((state->transports & TRANSPORT_TCP) && closesocket(state->tcp_socket) != 0) {
        report_socket_error_and_acknowledge();
        result = NET_FAILED;
    }
    if ((state->transports & TRANSPORT_UDP) && closesocket(state->udp_socket) != 0) {
        report_socket_error_and_acknowledge();
        result = NET_FAILED;
    }
    if (!transfer_listener_close()) {
        result = NET_FAILED;
    }
    if (result == NET_OK) {
        server_state_destroy(state);
        server->state = NULL;
    }
    return result;
}

void NPIMAllowNewConnections(NEServer *server, unsigned char allow)
{
    server->state->accepting = allow;
}

/* ------------------------------------------------------------------------ */
/* TCP clients                                                               */
/* ------------------------------------------------------------------------ */

long NPIMAccept(const NEServer *server, NEConnection *connection)
{
    const ServerState *state = server->state;
    ConnectionState *client;
    SOCKET s;

    if (!(state->transports & TRANSPORT_TCP)) {
        return NET_NOT_SUPPORTED;
    }
    s = accept(state->tcp_socket, NULL, NULL);
    if (s == INVALID_SOCKET) {
        int error = WSAGetLastError();

        report_native_error((unsigned long)error);
        return error == WSAEWOULDBLOCK ? NET_NO_DATA : NET_FAILED;
    }
    client = connection_create(s, TRANSPORT_TCP);
    if (!client) {
        closesocket(s);
        return NET_OUT_OF_MEMORY;
    }
    connection->state = client;
    return NET_OK;
}

/* ------------------------------------------------------------------------ */
/* UDP clients                                                               */
/* ------------------------------------------------------------------------ */

static size_t find_client(const ServerState *state, const struct sockaddr_in *address)
{
    size_t i;

    for (i = 0; i < state->clients.size; i++) {
        const struct sockaddr_in *client = state->clients.items[i];

        if (client->sin_family == address->sin_family && client->sin_addr.s_addr == address->sin_addr.s_addr &&
            client->sin_port == address->sin_port) {
            break;
        }
    }
    return i;
}

static void put_name(uint8_t *field, const char *name)
{
    size_t length = strlen(name);

    if (length > DISCOVERY_NAME_FIELD - 1) {
        length = DISCOVERY_NAME_FIELD - 1;
    }
    memcpy(field, name, length);
    field[length] = '\0';
}

/*
 * Answer a discovery probe (already decrypted) with the service and game name, but only if it asks
 * for our service and we are an accepting, non-internet, LAN-announced game.  The reply goes to
 * the asker's address on the game port, from a throw-away socket.
 */
static void answer_probe(const ServerState *state, const uint8_t *probe, const struct sockaddr_in *asker)
{
    uint8_t reply[DISCOVERY_REPLY_SIZE] = { 0 };
    struct sockaddr_in destination;
    SOCKET s;

    if (strncmp((const char *)probe + PACKET_HEADER_SIZE + 1, state->service.chars, DISCOVERY_NAME_FIELD) != 0 ||
        state->accepting != 1 || state->internet_game != 0 || state->announce_on_lan != 1) {
        return;
    }
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        report_socket_error_and_acknowledge();
        return;
    }
    reply[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_REPLY;
    put_name(reply + DISCOVERY_REPLY_SERVICE_OFFSET, state->service.chars);
    put_name(reply + DISCOVERY_REPLY_NAME_OFFSET, state->name.chars);
    datagram_seal(reply, sizeof reply);

    destination = *asker;
    destination.sin_port = htons(GAME_PORT);
    if (sendto(s, (const char *)reply, sizeof reply, 0, (struct sockaddr *)&destination, sizeof destination) ==
        SOCKET_ERROR) {
        report_socket_error_and_acknowledge();
    }
    closesocket(s);
}

/*
 * Receive the next datagram of any client.  The datagram is decrypted in place; `*received`
 * is its size including the header byte and `*client` the index of the sender.  A datagram from
 * an unknown sender registers a new client, but only if the server accepts new connections and
 * the payload starts with a "join" type byte; anything else is dropped (status 7).  Discovery
 * probes are answered here and also reported as status 7.
 *
 * When a new client's datagram arrives while the server refuses new connections (or the sender
 * port is 0) the call returns success without touching `*received` and `*client`, as the
 * original does.
 */
long NPIMReceiveFrom(const NEServer *server, unsigned long capacity, uint8_t *buffer, unsigned long *received,
                     unsigned long *client)
{
    ServerState *state = server->state;
    struct sockaddr_in from, *registered;
    int from_length = sizeof from;
    int n;
    size_t index;
    uint8_t type;

    if (!(state->transports & TRANSPORT_UDP)) {
        return NET_NOT_SUPPORTED;
    }
    n = recvfrom(state->udp_socket, (char *)buffer, (int)capacity, 0, (struct sockaddr *)&from, &from_length);
    if (n == 0) {
        return NET_NOT_SUPPORTED;
    }
    if (n == SOCKET_ERROR) {
        int error = WSAGetLastError();

        if (error == WSAEWOULDBLOCK) {
            *received = 0;
            return NET_NO_DATA;
        }
        report_native_error((unsigned long)error);
        return NET_FAILED;
    }
    datagram_open(buffer, n);
    type = buffer[PACKET_HEADER_SIZE];

    if (n == DISCOVERY_PROBE_SIZE && type == PACKET_DISCOVERY_PROBE) {
        *received = 0;
        answer_probe(state, buffer, &from);
        return NET_NO_DATA;
    }

    index = find_client(state, &from);
    if (index < state->clients.size) {
        *client = (unsigned long)index;
        *received = (unsigned long)n;
        return NET_OK;
    }
    if (n < PACKET_JOIN_MIN_SIZE || (type != PACKET_JOIN_A && type != PACKET_JOIN_B)) {
        *received = 0;
        return NET_NO_DATA;
    }
    if (state->accepting == 0 || from.sin_port == 0) {
        return NET_OK;
    }
    registered = malloc(sizeof *registered);
    if (!registered) {
        return NET_OUT_OF_MEMORY;
    }
    *registered = from;
    ptr_array_push(&state->clients, registered);
    ptr_array_push(&state->transfers, NULL);
    *client = (unsigned long)(state->clients.size - 1);
    *received = (unsigned long)n;
    return NET_OK;
}

/* Encrypt `buffer` in place (its first byte is reserved for the header) and send it to a client. */
long NPIMSendTo(const NEServer *server, unsigned long client, uint8_t *buffer, unsigned long length)
{
    const ServerState *state = server->state;
    int n;

    if (!(state->transports & TRANSPORT_UDP)) {
        return NET_NOT_SUPPORTED;
    }
    if (client >= state->clients.size) {
        return NET_OUT_OF_RANGE;
    }
    datagram_seal(buffer, (int)length);
    n = sendto(state->udp_socket, (const char *)buffer, (int)length, 0,
               (const struct sockaddr *)state->clients.items[client], sizeof(struct sockaddr_in));
    if (n == (int)length) {
        return NET_OK;
    }
    if (n == SOCKET_ERROR) {
        report_socket_error_and_acknowledge();
        return NET_FAILED;
    }
    return NET_NOT_SUPPORTED;
}

long NPIMGetClientAddress(const NEServer *server, unsigned long client, UWideString *address)
{
    const ServerState *state = server->state;
    long result = NET_OK;
    const char *text = "";

    if (client >= state->clients.size) {
        result = NET_NOT_FOUND;
    } else {
        text = inet_ntoa(((const struct sockaddr_in *)state->clients.items[client])->sin_addr);
    }
    uwidestring_assign_chars(address, text);
    return result;
}

/* Forget the address of a client (port 0 matches no sender), so that it can register again. */
void NPIMResetClientConnection(const NEServer *server, unsigned long client)
{
    const ServerState *state = server->state;

    if (client < state->clients.size) {
        ((struct sockaddr_in *)state->clients.items[client])->sin_port = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Bulk-transfer side channel                                                */
/* ------------------------------------------------------------------------ */

/*
 * Accept a connection on the bulk-transfer listener and attach it to the first registered UDP
 * client with the same IP address (clients whose port was reset are skipped).  Strangers are
 * disconnected and refused.
 */
long NPIMAcceptForXfer(const NEServer *server, NEConnection *connection)
{
    ServerState *state = server->state;
    struct sockaddr_in peer;
    int peer_length = sizeof peer;
    long result;
    SOCKET s;

    lock_enter(g_plugin.transfer_lock);
    s = accept(g_plugin.transfer_listener, (struct sockaddr *)&peer, &peer_length);
    if (s == INVALID_SOCKET) {
        int error = WSAGetLastError();

        report_native_error((unsigned long)error);
        result = error == WSAEWOULDBLOCK ? NET_NO_DATA : NET_FAILED;
    } else {
        size_t index;

        for (index = 0; index < state->clients.size; index++) {
            const struct sockaddr_in *client = state->clients.items[index];

            if (client->sin_family == peer.sin_family && client->sin_port != 0 &&
                client->sin_addr.s_addr == peer.sin_addr.s_addr) {
                break;
            }
        }
        if (index == state->clients.size) {
            closesocket(s);
            result = NET_REFUSED;
        } else {
            ConnectionState *transfer = connection_create(s, TRANSPORT_TCP);

            if (!transfer) {
                closesocket(s);
                result = NET_OUT_OF_MEMORY;
            } else {
                connection->state = transfer;
                ptr_array_store(&state->transfers, index, transfer);
                result = NET_OK;
            }
        }
    }
    lock_leave(g_plugin.transfer_lock);
    return result;
}

/* Send `length` bytes to the client's transfer connection, waiting while the socket is full. */
long NPIMXferSendTo(const NEServer *server, unsigned long client, const uint8_t *buffer, unsigned long length)
{
    const ServerState *state = server->state;
    const ConnectionState *transfer;
    long result = NET_OK;

    lock_enter(g_plugin.transfer_lock);
    transfer = client < state->transfers.size ? state->transfers.items[client] : NULL;
    if (!transfer) {
        result = NET_OUT_OF_RANGE;
    } else {
        unsigned long sent = 0;

        while (sent < length) {
            int n = send(transfer->socket, (const char *)buffer + sent, (int)(length - sent), 0);

            if (n != SOCKET_ERROR) {
                sent += (unsigned long)n;
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                Sleep(1);
            } else {
                result = NET_FAILED;
                break;
            }
        }
    }
    lock_leave(g_plugin.transfer_lock);
    return result;
}

/* Dispose of all transfer connection records.  (Their sockets are not closed.) */
void NPIMResetXfer(const NEServer *server)
{
    ServerState *state = server->state;

    for (size_t i = 0; i < state->transfers.size; i++) {
        free(state->transfers.items[i]);
    }
    ptr_array_clear(&state->transfers);
}
