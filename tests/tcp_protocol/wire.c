/* Part of the differential test harness for "TCP Protocol.dll"; see harness.h. */
#include "harness.h"

/* ------------------------------------------------------------------------ */
/* The plugin's datagram obfuscation, re-implemented on the peer side         */
/* ------------------------------------------------------------------------ */

/*
 * Every datagram of the UDP transport is "header byte + payload" where the
 * payload is XORed with an RC4 keystream.  The key is 7 bytes of a fixed
 * 261-byte table, starting at offset (header - 1); the table itself is the
 * output of the engine's URandom seeded with -1.  The header byte is random
 * per datagram (1..254), so the harness decrypts what it captures and prints
 * the plaintext instead of the raw ciphertext.
 */
#define KEY_TABLE_SIZE 0x105
#define KEY_LENGTH 7

static uint8_t g_key_table[KEY_TABLE_SIZE];

void init_key_table(void)
{
    URandom generator = { 0xffffffffu, 0x343fdu, 0x80000000u, 0x269ec3u };
    for (size_t i = 0; i < KEY_TABLE_SIZE; i++) {
        g_key_table[i] = (uint8_t)(int)(urandom_generate(&generator) * 255.0f);
    }
}

static void rc4_apply(uint8_t header, uint8_t *data, size_t length)
{
    uint8_t state[256];
    unsigned i, j = 0, k = 0;
    if (header == 0) {
        return;
    }
    for (i = 0; i < 256; i++) {
        state[i] = (uint8_t)i;
    }
    for (i = 0; i < 256; i++) {
        uint8_t tmp;
        j = (j + state[i] + g_key_table[(header - 1u) + k]) & 0xff;
        tmp = state[i]; state[i] = state[j]; state[j] = tmp;
        k = (k + 1) % KEY_LENGTH;
    }
    i = j = 0;
    for (size_t n = 0; n < length; n++) {
        uint8_t tmp;
        i = (i + 1) & 0xff;
        j = (j + state[i]) & 0xff;
        tmp = state[i]; state[i] = state[j]; state[j] = tmp;
        data[n] ^= state[(state[i] + state[j]) & 0xff];
    }
}

/* wire = header || cipher(plain) */
size_t seal(uint8_t *wire, uint8_t header, const uint8_t *plain, size_t plain_length)
{
    wire[0] = header;
    memcpy(wire + 1, plain, plain_length);
    rc4_apply(header, wire + 1, plain_length);
    return plain_length + 1;
}

/* Decrypt a captured datagram; returns the plaintext length. */
size_t unseal(uint8_t *plain, const uint8_t *wire, size_t wire_length)
{
    if (wire_length == 0) {
        return 0;
    }
    memcpy(plain, wire + 1, wire_length - 1);
    rc4_apply(wire[0], plain, wire_length - 1);
    return wire_length - 1;
}

const char *header_text(uint8_t header)
{
    return (header >= 1 && header <= 254) ? "<random 1..254>" : "INVALID";
}


/* Fabricate and show the first byte plus plaintext of a captured datagram. */
void show_wire(const char *label, const uint8_t *wire, int length)
{
    uint8_t plain[2048];
    if (length <= 0) {
        line("%s: nothing received", label);
        return;
    }
    size_t plain_length = unseal(plain, wire, (size_t)length);
    uint8_t again[2048];
    size_t again_length = seal(again, wire[0], plain, plain_length);
    line("%s: wire_length=%d header=%s plaintext=%s re_encrypts_to_wire=%s", label, length, header_text(wire[0]),
         hex(plain, plain_length), yesno(again_length == (size_t)length && memcmp(again, wire, again_length) == 0));
}

