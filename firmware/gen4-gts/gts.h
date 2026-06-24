/*
 * Gen 4 GTS payload codec + capture/inject staging for pico-ball.
 *
 * The cipher is the documented Gen-IV GTS stream cipher (an LCG keystream,
 * multiplier 0x45 / addend 0x1111, masked to 31 bits), mirrored from the
 * IR-GTS reference tool. Decrypt (deposit -> capture) is exercised by the
 * game on every upload and is the primary, well-grounded path. Encrypt
 * (result.asp -> inject) is implemented but flagged UNVALIDATED: the trailing
 * GTS metadata block and the inbound checksum check still need an emulator
 * pass before trusting it against a real cartridge.
 */
#ifndef PICO_BALL_GTS_H
#define PICO_BALL_GTS_H

#include <stdint.h>
#include <stddef.h>

#define GTS_PARTY_LEN   236   /* Gen 4 party-format Pokemon */
#define GTS_BLOB_LEN    244   /* 4-byte header + 240 encrypted */

/* Decrypt a base64-decoded GTS 'data' blob into a 236-byte party Pokemon.
 * Returns 0 on success, negative on bad length. */
int gts_decrypt_pokemon(const uint8_t *blob, size_t len,
                        uint8_t out[GTS_PARTY_LEN]);

/* Build the encrypted 244-byte packet for a withdraw/inject. UNVALIDATED. */
int gts_encrypt_pokemon(const uint8_t mon[GTS_PARTY_LEN],
                        uint8_t out[GTS_BLOB_LEN]);

/* Standard-alphabet base64 (also accepts urlsafe -_ and '*' padding). */
int gts_b64_decode(const char *in, size_t inlen, uint8_t *out, size_t outcap);
int gts_b64_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap);

/* Session capture state (most recent deposit). */
extern uint8_t gts_last_capture[GTS_PARTY_LEN];
extern int     gts_capture_count;

/* A Pokemon staged for the next result.asp inject (0 = nothing staged). */
extern uint8_t gts_inject_mon[GTS_PARTY_LEN];
extern int     gts_inject_valid;

void gts_init(void);
/* HTTP layer calls this when a deposit (post.asp) arrives. */
void gts_on_deposit(const uint8_t party236[GTS_PARTY_LEN]);

#endif /* PICO_BALL_GTS_H */
