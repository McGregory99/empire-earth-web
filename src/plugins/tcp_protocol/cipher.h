/*
 * Obfuscation of UDP datagrams.
 *
 * A datagram is `header byte || payload`.  The payload is XORed with an RC4
 * key stream.  The RC4 key is 7 consecutive bytes of a fixed 261-byte table,
 * starting at offset (header - 1); a header of 0 leaves the payload in clear.
 * The table is the output of the engine's URandom seeded with -1, so every
 * peer derives the same one.  The header byte itself is picked at random
 * (1..254) for each datagram that we send.
 */
#ifndef TCP_PROTOCOL_CIPHER_H
#define TCP_PROTOCOL_CIPHER_H

#include <stdint.h>

#define CIPHER_KEY_TABLE_SIZE 0x105
#define CIPHER_KEY_LENGTH 7

/* (Re)build the key table; allocates it on first use. */
void cipher_generate_key_table(void);
/* Drop the key table (NPIMDeactivate). */
void cipher_release(void);

/* Pick the random header byte for an outgoing datagram (0 if no generator exists). */
uint8_t cipher_random_header(void);

/*
 * Prepare an outgoing datagram in place: store a fresh random header in
 * datagram[0] and encrypt datagram[1 .. length).  Returns the header.
 */
uint8_t datagram_seal(uint8_t *datagram, int length);

/* Decrypt a received datagram in place, using its header byte datagram[0]. */
void datagram_open(uint8_t *datagram, int length);

#endif /* TCP_PROTOCOL_CIPHER_H */
