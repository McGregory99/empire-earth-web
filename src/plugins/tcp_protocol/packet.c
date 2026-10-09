#include "packet.h"

#include <string.h>

/* Copy a NUL-terminated name into a field of `size` bytes, cutting it to leave room for the terminator. */
static void put_name(uint8_t *field, size_t size, const char *name)
{
    size_t length = strlen(name);

    if (length > size - 1) {
        length = size - 1;
    }
    memcpy(field, name, length);
    field[length] = '\0';
}

/* Read a name from a field of `size` bytes into `text` (the same size), never past the field. */
static void get_name(char *text, const uint8_t *field, size_t size)
{
    memcpy(text, field, size);
    text[size - 1] = '\0';
}

void packet_fill_probe(uint8_t probe[DISCOVERY_PROBE_SIZE], const char *service)
{
    memset(probe, 0, DISCOVERY_PROBE_SIZE);
    probe[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_PROBE;
    put_name(probe + DISCOVERY_PROBE_SERVICE_OFFSET, DISCOVERY_SERVICE_FIELD, service);
}

bool packet_fill_reply(uint8_t reply[DISCOVERY_REPLY_SIZE], const char *service, const char *game_name)
{
    memset(reply, 0, DISCOVERY_REPLY_SIZE);
    reply[PACKET_HEADER_SIZE] = PACKET_DISCOVERY_REPLY;
    put_name(reply + DISCOVERY_REPLY_SERVICE_OFFSET, DISCOVERY_SERVICE_FIELD, service);
    put_name(reply + DISCOVERY_REPLY_NAME_OFFSET, DISCOVERY_GAME_NAME_FIELD, game_name);
    return strlen(game_name) < DISCOVERY_GAME_NAME_FIELD;
}

void packet_read_reply(const uint8_t reply[DISCOVERY_REPLY_SIZE], char service[DISCOVERY_SERVICE_FIELD],
                       char game_name[DISCOVERY_GAME_NAME_FIELD])
{
    get_name(service, reply + DISCOVERY_REPLY_SERVICE_OFFSET, DISCOVERY_SERVICE_FIELD);
    get_name(game_name, reply + DISCOVERY_REPLY_NAME_OFFSET, DISCOVERY_GAME_NAME_FIELD);
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
    return strncmp((const char *)probe + DISCOVERY_PROBE_SERVICE_OFFSET, service, DISCOVERY_SERVICE_FIELD) == 0;
}

bool packet_is_join_request(const uint8_t *datagram, int length)
{
    return length >= PACKET_JOIN_MIN_SIZE &&
           (datagram[PACKET_HEADER_SIZE] == PACKET_JOIN_A || datagram[PACKET_HEADER_SIZE] == PACKET_JOIN_B);
}
