#include "gts.h"
#include <stdio.h>
#include <string.h>

uint8_t gts_last_capture[GTS_PARTY_LEN];
int     gts_capture_count = 0;
uint8_t gts_inject_mon[GTS_PARTY_LEN];
int     gts_inject_valid = 0;

/* ---- base64 -------------------------------------------------------------- */

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;   /* accept urlsafe '-' */
    if (c == '/' || c == '_') return 63;   /* accept urlsafe '_' */
    return -1;                              /* '=', '*', whitespace, etc. */
}

int gts_b64_decode(const char *in, size_t inlen, uint8_t *out, size_t outcap) {
    uint32_t acc = 0;
    int nbits = 0;
    size_t n = 0;
    for (size_t i = 0; i < inlen; i++) {
        int v = b64val(in[i]);
        if (v < 0) continue;               /* skip padding / junk */
        acc = (acc << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (n >= outcap) return -1;
            out[n++] = (uint8_t)((acc >> nbits) & 0xff);
        }
    }
    return (int)n;
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int gts_b64_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap) {
    size_t n = 0;
    for (size_t i = 0; i < inlen; i += 3) {
        uint32_t b0 = in[i];
        uint32_t b1 = (i + 1 < inlen) ? in[i + 1] : 0;
        uint32_t b2 = (i + 2 < inlen) ? in[i + 2] : 0;
        uint32_t trip = (b0 << 16) | (b1 << 8) | b2;
        char c[4] = {
            B64[(trip >> 18) & 0x3f], B64[(trip >> 12) & 0x3f],
            (i + 1 < inlen) ? B64[(trip >> 6) & 0x3f] : '=',
            (i + 2 < inlen) ? B64[trip & 0x3f] : '=',
        };
        if (n + 4 > outcap) return -1;
        for (int k = 0; k < 4; k++) out[n++] = c[k];
    }
    if (n < outcap) out[n] = '\0';
    return (int)n;
}

/* ---- Gen 4 GTS stream cipher --------------------------------------------- *
 * Mirrors IR-GTS decrypt_sce_data / decrypt_pokemon:
 *   state = (state * 0x45 + 0x1111) & 0x7fffffff   (advance, then key)
 *   key   = (state >> 16) & 0xff
 *   plain = cipher ^ key
 * Seed derives from the 4-byte big-endian header:
 *   checksum = be32(header) ^ 0x4a3b2c1d
 *   state0   = checksum | (checksum << 16)         (64-bit before first mask)
 * The encrypted span is 240 bytes; the first 4 decrypted bytes are dropped,
 * leaving the 236-byte party Pokemon.
 */
static void sce_xor(const uint8_t *in, uint8_t *out, size_t n, uint64_t state) {
    for (size_t i = 0; i < n; i++) {
        state = (state * 0x45 + 0x1111) & 0x7fffffffULL;
        uint8_t key = (uint8_t)((state >> 16) & 0xff);
        out[i] = (uint8_t)(in[i] ^ key);
    }
}

int gts_decrypt_pokemon(const uint8_t *blob, size_t len,
                        uint8_t out[GTS_PARTY_LEN]) {
    if (len < GTS_BLOB_LEN) return -1;
    uint32_t header = ((uint32_t)blob[0] << 24) | ((uint32_t)blob[1] << 16) |
                      ((uint32_t)blob[2] << 8)  |  (uint32_t)blob[3];
    uint32_t checksum = header ^ 0x4a3b2c1dU;
    uint64_t state0 = (uint64_t)checksum | ((uint64_t)checksum << 16);

    uint8_t dec[240];
    sce_xor(blob + 4, dec, 240, state0);
    memcpy(out, dec + 4, GTS_PARTY_LEN);   /* drop the 4-byte prefix */
    return 0;
}

/* UNVALIDATED inject direction. Symmetric XOR, so the game recovers exactly
 * what we encrypt as long as the header encodes the same seed. The residual
 * risks are (a) whether the game validates the inbound 16-bit checksum field
 * against content and (b) the trailing GTS metadata appended by result.asp. */
int gts_encrypt_pokemon(const uint8_t mon[GTS_PARTY_LEN],
                        uint8_t out[GTS_BLOB_LEN]) {
    uint8_t plain[240];
    memset(plain, 0, 4);                    /* 4-byte prefix the game strips */
    memcpy(plain + 4, mon, GTS_PARTY_LEN);

    uint32_t c16 = 0;                        /* simple 16-bit sum over payload */
    for (int i = 0; i < 240; i++) c16 += plain[i];
    c16 &= 0xffff;

    uint32_t header = c16 ^ 0x4a3b2c1dU;
    out[0] = (uint8_t)(header >> 24); out[1] = (uint8_t)(header >> 16);
    out[2] = (uint8_t)(header >> 8);  out[3] = (uint8_t)header;

    uint64_t state0 = (uint64_t)c16 | ((uint64_t)c16 << 16);
    sce_xor(plain, out + 4, 240, state0);
    return GTS_BLOB_LEN;
}

/* ---- capture/inject staging ---------------------------------------------- */

void gts_init(void) {
    gts_capture_count = 0;
    gts_inject_valid = 0;
    memset(gts_last_capture, 0, sizeof gts_last_capture);
}

void gts_on_deposit(const uint8_t party236[GTS_PARTY_LEN]) {
    memcpy(gts_last_capture, party236, GTS_PARTY_LEN);
    gts_capture_count++;

    /* Emit the capture as hex over USB stdio so the host (or the existing
     * Web Serial UI) can save it as a .pk4 record, mirroring how Path B
     * surfaces stored records. Species id lives at party offset 0x08. */
    uint16_t species = (uint16_t)party236[0x08] | ((uint16_t)party236[0x09] << 8);
    printf("\nGTS-CAPTURE #%d species=%u len=%d\n", gts_capture_count,
           species, GTS_PARTY_LEN);
    printf("PK4:");
    for (int i = 0; i < GTS_PARTY_LEN; i++) printf("%02x", party236[i]);
    printf("\n");
}
