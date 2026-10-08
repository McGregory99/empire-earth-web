/*
 * Constants of the TCP/IP network protocol plugin: status codes, ports and the
 * layout of the datagrams it exchanges.
 */
#ifndef TCP_PROTOCOL_PROTOCOL_H
#define TCP_PROTOCOL_PROTOCOL_H

/* Status codes returned to the engine (the original's "kese*" values). */
typedef enum NetStatus {
    NET_OK = 0,
    NET_FAILED = 1,         /* an OS error occurred; it was recorded with the engine */
    NET_NOT_FOUND = 3,      /* unknown client or no matching detected game */
    NET_NO_DATA = 7,        /* nothing to receive right now (the call would block) */
    NET_OUT_OF_MEMORY = 8,
    NET_NOT_SUPPORTED = 9,  /* wrong transport, bad arguments or the peer closed the connection */
    NET_OUT_OF_RANGE = 10,  /* client or game index does not exist */
    NET_REFUSED = 13        /* peer not allowed to open a bulk-transfer connection */
} NetStatus;

/* Ports.  Game traffic (TCP listener and UDP datagrams) shares one port number. */
#define GAME_PORT 33334u      /* 0x8236 */
#define TRANSFER_PORT 33335u  /* 0x8237: TCP side channel for bulk data (map transfers) */

/* Transports.  NPIMPublish takes them as a bit mask, connections store one value. */
#define TRANSPORT_TCP 1u
#define TRANSPORT_UDP 2u

/*
 * Every UDP datagram starts with a "header" byte followed by the payload,
 * which is XORed with an RC4 key stream whose key is derived from the header
 * byte (see cipher.h).  A header of 0 means "not encrypted".
 *
 * First payload byte of the datagrams the plugin itself understands:
 */
#define PACKET_HEADER_SIZE 1
#define PACKET_DISCOVERY_PROBE 0x15  /* "is a game of this service hosted here?" */
#define PACKET_DISCOVERY_REPLY 0x16  /* "yes: service, game name" */
#define PACKET_JOIN_A 0x22           /* a first datagram of these types ('"' and '7') ... */
#define PACKET_JOIN_B 0x37           /* ... registers a new client with a server */
#define PACKET_JOIN_MIN_SIZE 4       /* ... provided it is at least this long, header included */

/* Game-discovery datagrams have fixed sizes; names are NUL-terminated within their field. */
#define DISCOVERY_NAME_FIELD 65  /* up to 64 characters plus the terminator */
#define DISCOVERY_PROBE_SIZE (PACKET_HEADER_SIZE + 1 + DISCOVERY_NAME_FIELD)                          /* 67 */
#define DISCOVERY_REPLY_SIZE (PACKET_HEADER_SIZE + 1 + DISCOVERY_NAME_FIELD + DISCOVERY_NAME_FIELD + 4) /* 136 */

#define DISCOVERY_REPLY_SERVICE_OFFSET 2
#define DISCOVERY_REPLY_NAME_OFFSET (DISCOVERY_REPLY_SERVICE_OFFSET + DISCOVERY_NAME_FIELD)

/* How long a LAN scan listens for replies. */
#define DISCOVERY_LISTEN_MS 1000u

/* The status string accepted by NPIMGetStatus, and the module identity. */
#define PROTOCOL_NAME "TCP/IP Protocol"
#define PROTOCOL_ID 1u
#define PLUGIN_MODULE_TYPE 0x6e74776bu /* the characters "ntwk" */

#endif /* TCP_PROTOCOL_PROTOCOL_H */
