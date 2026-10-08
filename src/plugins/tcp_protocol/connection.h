/*
 * State behind an NEConnection: one TCP or UDP "connection" to a peer.
 *
 * A TCP connection wraps a connected stream socket.  A UDP connection wraps a
 * datagram socket plus the address of the single peer it talks to; datagrams
 * from anybody else are ignored.
 */
#ifndef TCP_PROTOCOL_CONNECTION_H
#define TCP_PROTOCOL_CONNECTION_H

#include <stdbool.h>
#include <winsock2.h>

typedef struct ConnectionState {
    SOCKET socket;
    struct sockaddr_in remote;  /* the peer (for UDP: where datagrams go and may come from) */
    unsigned long transport;    /* TRANSPORT_TCP or TRANSPORT_UDP */
} ConnectionState;

/* A new connection record for `socket`; NULL when out of memory.  The address starts out zeroed. */
ConnectionState *connection_create(SOCKET socket, unsigned long transport);

/*
 * Close the TCP listener for bulk transfers that NPIMPublish created.  The listener is
 * shared by all servers, and NPIMCloseConnection closes it as well (see there).
 * Returns false (after reporting the error) when there is no open listener.
 */
bool transfer_listener_close(void);

#endif /* TCP_PROTOCOL_CONNECTION_H */
