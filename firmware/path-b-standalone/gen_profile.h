// Per-generation capture profile: the section layout + handshake the trade FSM
// needs for Gen 1 (RBY) vs Gen 2 (GSC). Lets one firmware capture both.
#ifndef GEN_PROFILE_H
#define GEN_PROFILE_H
#include <stdint.h>

// Largest record we store: Gen 2 = 10+444+197+385 = 1036 bytes.
#define GB_CAPTURE_MAX 1036

typedef struct {
    int gen;                       // 1 or 2
    int n_sections;                // 3 (RBY) or 4 (GSC, incl. mail)
    const uint16_t *lens;          // per-section byte counts
    const uint8_t *starters;       // per-section preamble byte (0xFD, or 0x20 mail)
    const uint8_t *syncs;          // per-section: 1 = send NO_INPUT to sync, 0 = send starter
    const uint8_t *preamble;       // per-section preamble length (only section 0 bounded)
    const uint8_t *const *baked;   // per-section bytes we stream to the cartridge
    uint16_t record_len;           // sum of lens
} gen_profile_t;

const gen_profile_t *gen_profile(int gen);

#endif // GEN_PROFILE_H
