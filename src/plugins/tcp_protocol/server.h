/*
 * State behind an NEServer: a published game.
 *
 * A server listens on a TCP socket and/or a UDP socket, both on port 33334.
 * TCP clients are handed out one by one by NPIMAccept.  UDP "clients" are
 * identified by the address their first datagram came from; each gets a client
 * index, and a TCP side channel (port 33335) can be attached to a client for
 * bulk data.
 */
#ifndef TCP_PROTOCOL_SERVER_H
#define TCP_PROTOCOL_SERVER_H

#include <stdint.h>
#include <winsock2.h>

#include "connection.h"
#include "low_level_engine.h"
#include "ptr_array.h"

typedef struct ServerState {
    SOCKET tcp_socket;          /* listening socket, or INVALID_SOCKET */
    SOCKET udp_socket;          /* datagram socket, or INVALID_SOCKET */
    PtrArray clients;           /* struct sockaddr_in *, indexed by client number */
    PtrArray transfers;         /* ConnectionState *, parallel to `clients` (NULL = no side channel) */
    unsigned long transports;   /* TRANSPORT_TCP | TRANSPORT_UDP */
    UString service;            /* the service name this game advertises */
    UWideString name;           /* the game's display name */
    unsigned long port;         /* network-order port the last transport bound to */
    /*
     * Flags kept as bytes because the original compares them with 0 and 1 (NPIMAllowNewConnections
     * stores whatever byte the engine passes).  `accepting`: new UDP clients may register, and
     * discovery probes are answered only when it is exactly 1.
     */
    uint8_t accepting;
    /* The two Boolean arguments of NPIMPublish; their exact meaning is inferred from their use. */
    uint8_t internet_game;      /* non-zero: neither check the LAN for duplicate names nor answer probes */
    uint8_t announce_on_lan;    /* must be exactly 1 for discovery probes to be answered */
} ServerState;

#endif /* TCP_PROTOCOL_SERVER_H */
