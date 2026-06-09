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

    // Party-block layout within section 1 (used to split a party into mons).
    uint16_t count_pos;            // party size byte (0x0B)
    uint16_t mon_pos;              // first Pokémon struct (0x13 / 0x15)
    uint8_t  mon_len;              // bytes per Pokémon struct (0x2C / 0x30)
    uint16_t ot_pos;               // first OT name (0x11B / 0x135)
    uint16_t nick_pos;             // first nickname (0x15D / 0x177)
    uint8_t  name_len;             // OT/nickname length (0x0B)
} gen_profile_t;

// Per-mon record we store in the dex = struct + OT name + nickname.
#define DEX_MON_MAX (0x30 + 2 * 0x0B)  // 70 (Gen 2 is the larger)

const gen_profile_t *gen_profile(int gen);

#endif // GEN_PROFILE_H
