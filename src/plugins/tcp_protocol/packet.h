/*
 * Layout of the datagrams the plugin builds and recognises (see protocol.h for the
 * sizes and type bytes).  All functions work on *decrypted* datagrams; sealing and
 * opening them is the job of cipher.h.
 */
#ifndef TCP_PROTOCOL_PACKET_H
#define TCP_PROTOCOL_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

/* "Is anybody hosting `service` here?" -- header byte, type 0x15, service name field. */
void packet_fill_probe(uint8_t probe[DISCOVERY_PROBE_SIZE], const char *service);
/*
 * "Yes: `service`, called `game_name`" -- header byte, type 0x16, service field, game-name field.
 * A service name is cut to 64 characters.  A game name must fit its 69-byte field (68 characters
 * and the terminator); returns false, with the reply unusable, if it does not.
 */
bool packet_fill_reply(uint8_t reply[DISCOVERY_REPLY_SIZE], const char *service, const char *game_name);
/* Copy the (always terminated) service and game name out of a reply. */
void packet_read_reply(const uint8_t reply[DISCOVERY_REPLY_SIZE], char service[DISCOVERY_SERVICE_FIELD],
                       char game_name[DISCOVERY_GAME_NAME_FIELD]);

bool packet_is_probe(const uint8_t *datagram, int length);
bool packet_is_reply(const uint8_t *datagram, int length);
/* Does this probe ask for `service`? */
bool packet_probe_asks_for(const uint8_t probe[DISCOVERY_PROBE_SIZE], const char *service);
/* Is this the first datagram a new client sends to register with a server? */
bool packet_is_join_request(const uint8_t *datagram, int length);

#endif /* TCP_PROTOCOL_PACKET_H */
