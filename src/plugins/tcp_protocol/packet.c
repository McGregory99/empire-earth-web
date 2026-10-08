#include "packet.h"

#include <string.h>

/* Copy a NUL-terminated name into a protocol field, truncating to 64 characters. */
static void put_name(uint8_t *field, const char *name)
{
    size_t length = strlen(name);

    if (length > DISCOVERY_NAME_FIELD - 1) {
        length = DISCOVERY_NAME_FIELD - 1;
    }
    memcpy(field, name, length);
    field[length] = '\0';
}

/* Read a name from a protocol field, never past the field. */
static void get_name(char *text, const uint8_t *field)
{
    memcpy(text, field, DISCOVERY_NAME_FIELD);
    text[DISCOVERY_NAME_FIELD - 1] = '\0';
}

void packet_fill_probe(uint8_t probe[DISCOVERY_PROBE_SIZE], const char *service)
{
    memset(probe, 0, DISCOVERY_PROBE_SIZE);
    probe[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_PROBE;
    put_name(probe + DISCOVERY_PROBE_SERVICE_OFFSET, service);
}

void packet_fill_reply(uint8_t reply[DISCOVERY_REPLY_SIZE], const char *service, const char *game_name)
{
    memset(reply, 0, DISCOVERY_REPLY_SIZE);
    reply[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_REPLY;
    put_name(reply + DISCOVERY_REPLY_SERVICE_OFFSET, service);
    put_name(reply + DISCOVERY_REPLY_NAME_OFFSET, game_name);
}

void packet_read_reply(const uint8_t reply[DISCOVERY_REPLY_SIZE], char service[DISCOVERY_NAME_FIELD],
                       char game_name[DISCOVERY_NAME_FIELD])
{
    get_name(service, reply + DISCOVERY_REPLY_SERVICE_OFFSET);
    get_name(game_name, reply + DISCOVERY_REPLY_NAME_OFFSET);
}

bool packet_is_probe(const uint8_t *datagram, int length)
{
    return length == DISCOVERY_PROBE_SIZE && datagram[PACKET_HEADER_SIZE] == PACKET_DISCOVERY_PROBE;
}

bool packet_is_reply(const uint8_t *datagram, int length)
{
    return length == DISCOVERY_REPLY_SIZE && datagram[PACKET_HEADER_SIZE] == PACKET_DISCOVERY_REPLY;
}

bool packet_probe_asks_for(const uint8_t probe[DISCOVERY_PROBE_SIZE], const char *service)
{
    return strncmp((const char *)probe + DISCOVERY_PROBE_SERVICE_OFFSET, service, DISCOVERY_NAME_FIELD) == 0;
}

bool packet_is_join_request(const uint8_t *datagram, int length)
{
    return length >= PACKET_JOIN_MIN_SIZE &&
           (datagram[PACKET_HEADER_SIZE] == PACKET_JOIN_A || datagram[PACKET_HEADER_SIZE] == PACKET_JOIN_B);
}
