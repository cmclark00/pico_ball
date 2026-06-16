// On-device GBA multiboot — streaming port of webui/multiboot.js (verified there
// byte-identical to multiboot.py). The RP2040 can't hold the 254 KB scrambled
// image in RAM, so we compute the rolling CRC in one pass over flash, then
// scramble each word on the fly during the send. Every protocol word is one
// full-duplex SIO32 exchange (gb_link_swap32), which is exactly what the host's
// reconfigurable firmware does per 4-byte group; the host-side config/drain
// steps are firmware<->PC bookkeeping and don't exist on-device.
#include "gen3_multiboot.h"
#include "gb_link.h"
#include "baked_gen3_mb.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define MB_HANDSHAKE_TRIES 8000

static uint32_t g_pacing_us = 36;

static inline uint32_t word_at(uint32_t byte_off) {
    return (uint32_t)gen3_mb_image[byte_off] |
           ((uint32_t)gen3_mb_image[byte_off + 1] << 8) |
           ((uint32_t)gen3_mb_image[byte_off + 2] << 16) |
           ((uint32_t)gen3_mb_image[byte_off + 3] << 24);
}

// One paced SIO32 exchange.
static inline uint32_t mb_swap(uint32_t out) {
    uint32_t r = gb_link_swap32(out);
    busy_wait_us(g_pacing_us);
    return r;
}

bool gen3_multiboot(uint32_t pacing_us) {
    g_pacing_us = pacing_us;
    const uint32_t fsize = gen3_mb_fsize;
    const uint32_t nwords = (fsize - 0xC0) >> 2;

    // Pass 1: rolling CRC over the raw image words (streamed from flash).
    uint32_t crcC = 0xC387;
    for (uint32_t i = 0xC0; i < fsize; i += 4) {
        uint32_t tmp = word_at(i);
        for (int b = 0; b < 32; b++) {
            uint32_t bit = (crcC ^ tmp) & 1;
            crcC = bit ? ((crcC >> 1) ^ 0xC37B) : (crcC >> 1);
            tmp >>= 1;
        }
    }

    // Handshake.
    uint32_t recv = 0, tries = 0;
    do {
        recv = mb_swap(0x6202);
        if (++tries > MB_HANDSHAKE_TRIES) {
            printf("MB: no handshake (last 0x%08lx)\n", (unsigned long)recv);
            return false;
        }
    } while ((recv >> 16) != 0x7202);
    mb_swap(0x6102);

    // 0xC0-byte header (96 halfwords).
    for (int i = 0; i < 96; i++)
        mb_swap(gen3_mb_image[i * 2] | (gen3_mb_image[i * 2 + 1] << 8));

    mb_swap(0x6200);
    mb_swap(0x6200);
    // Host sends 0x63D1 twice (draining the first reply); the GBA's reply to the
    // first arrives during the second exchange (one-word SIO pipeline lag). Each
    // full-duplex swap32 returns its own exchange's reply, so two swaps suffice:
    // the first reply is the (drained) junk, the second is the handshake token.
    mb_swap(0x63D1);
    uint32_t token = mb_swap(0x63D1);
    if (((token >> 24) & 0xFF) != 0x73) {
        printf("MB: bad token 0x%08lx\n", (unsigned long)token);
        return false;
    }

    uint32_t crcA = (token >> 16) & 0xFF;
    uint32_t seed = (0xFFFF00D1u | (crcA << 8)) & 0xFFFFFFFFu;
    crcA = (crcA + 0xF) & 0xFF;

    mb_swap(0x6400 | crcA);
    token = mb_swap((fsize - 0x190) >> 2);
    uint32_t crcB = (token >> 16) & 0xFF;
    printf("MB: handshake ok, pacing=%luus crcA=0x%lx crcB=0x%lx, sending %lu words...\n",
           (unsigned long)g_pacing_us, (unsigned long)crcA, (unsigned long)crcB,
           (unsigned long)nwords);

    // Stream the scrambled image (scramble each word on the fly).
    for (uint32_t k = 0; k < nwords; k++) {
        uint32_t off = 0xC0 + k * 4;
        uint32_t dat = word_at(off) ^ ((0xFE000000u - off)) ^ 0x43202F2Fu;
        seed = (seed * 0x6F646573u + 1u);      // uint32 wraps == &0xFFFFFFFF
        mb_swap(dat ^ seed);
    }

    // Final CRC fold + confirm.
    uint32_t tmp = (0xFFFF0000u | (crcB << 8) | crcA);
    for (int b = 0; b < 32; b++) {
        uint32_t bit = (crcC ^ tmp) & 1;
        crcC = bit ? ((crcC >> 1) ^ 0xC37B) : (crcC >> 1);
        tmp >>= 1;
    }

    mb_swap(0x0065);
    tries = 0;
    do {
        recv = mb_swap(0x0065);
        if (tries < 4) printf("MB: final-CRC reply 0x%08lx\n", (unsigned long)recv);
        if (++tries > MB_HANDSHAKE_TRIES) {
            printf("MB: no final-CRC ready after %lu tries (last 0x%08lx)\n",
                   (unsigned long)tries, (unsigned long)recv);
            return false;
        }
    } while (((recv >> 16) & 0xFFFF) != 0x0075);

    mb_swap(0x0066);
    mb_swap(crcC & 0xFFFF);
    return true;
}
