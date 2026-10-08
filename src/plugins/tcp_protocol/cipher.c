#include "cipher.h"

#include <stdlib.h>
#include <string.h>

#include "low_level_engine.h"
#include "plugin_state.h"
#include "protocol.h"

/* Scale the generator's [0,1) output to a key byte, or to a header byte in 1..254. */
#define KEY_BYTE_SCALE 255.0f
#define HEADER_SCALE 254.0f
#define HEADER_BIAS 1.0f

static void swap_bytes(uint8_t *a, uint8_t *b)
{
    uint8_t tmp = *a;
    *a = *b;
    *b = tmp;
}

void cipher_generate_key_table(void)
{
    URandom generator = { 0xffffffffu, 0x343fdu, 0x80000000u, 0x269ec3u };

    if (!g_plugin.key_table) {
        g_plugin.key_table = malloc(CIPHER_KEY_TABLE_SIZE);
        if (!g_plugin.key_table) {
            return;
        }
    }
    for (unsigned i = 0; i < CIPHER_KEY_TABLE_SIZE; i++) {
        g_plugin.key_table[i] = (uint8_t)(int)(urandom_generate(&generator) * KEY_BYTE_SCALE);
    }
}

void cipher_release(void)
{
    free(g_plugin.key_table);
    g_plugin.key_table = NULL;
}

uint8_t cipher_random_header(void)
{
    if (!g_plugin.random) {
        return 0;
    }
    return (uint8_t)(int)(urandom_generate(g_plugin.random) * HEADER_SCALE + HEADER_BIAS);
}

/* RC4 with the 7-byte key window selected by `header`; encrypting and decrypting are the same. */
static void rc4_apply(uint8_t header, uint8_t *data, int length)
{
    const uint8_t *key = g_plugin.key_table + (header - 1u);
    uint8_t state[256];
    unsigned i, j = 0, key_index = 0;

    for (i = 0; i < 256; i++) {
        state[i] = (uint8_t)i;
    }
    for (i = 0; i < 256; i++) {
        j = (j + state[i] + key[key_index]) & 0xff;
        swap_bytes(&state[i], &state[j]);
        key_index = (key_index + 1) % CIPHER_KEY_LENGTH;
    }

    i = j = 0;
    for (int n = 0; n < length; n++) {
        i = (i + 1) & 0xff;
        j = (j + state[i]) & 0xff;
        swap_bytes(&state[i], &state[j]);
        data[n] ^= state[(state[i] + state[j]) & 0xff];
    }
}

static void apply_key_stream(uint8_t header, uint8_t *payload, int length)
{
    if (!g_plugin.key_table || header == 0 || length <= 0) {
        return;
    }
    rc4_apply(header, payload, length);
}

uint8_t datagram_seal(uint8_t *datagram, int length)
{
    uint8_t header = cipher_random_header();

    datagram[0] = header;
    apply_key_stream(header, datagram + PACKET_HEADER_SIZE, length - PACKET_HEADER_SIZE);
    return header;
}

void datagram_open(uint8_t *datagram, int length)
{
    apply_key_stream(datagram[0], datagram + PACKET_HEADER_SIZE, length - PACKET_HEADER_SIZE);
}
