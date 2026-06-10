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

    // Mail layout (Gen 2 only; 0 for Gen 1). Mail lives in the last section; a
    // mon carries mail when its held item is a Mail item (see gp_is_mail_item).
    uint16_t mail_pos;             // first mail entry within the mail section (0x00)
    uint8_t  mail_len;             // bytes per mail entry (0x21), 0 = no mail
    uint16_t mail_sender_pos;      // first mail-sender entry (0xC6)
    uint8_t  mail_sender_len;      // bytes per sender entry (0x0E)
} gen_profile_t;

// Gen 2 Mail items (from gsc/ids_mail.bin): Flower Mail + the 0xB5..0xBD mails.
static inline int gp_is_mail_item(uint8_t item) {
    return item == 0x9E || (item >= 0xB5 && item <= 0xBD);
}

// Per-mon record we store in the dex = struct + OT name + nickname (+ mail +
// sender when the mon holds Mail). Gen 2 is the larger; fits MAX_PAYLOAD (120).
#define DEX_MON_MAX (0x30 + 2 * 0x0B + 0x21 + 0x0E)  // 117

// Max section-1 size across generations (Gen 2 = 444 > Gen 1 = 418).
#define GB_SEC1_MAX 444
// Patch-list section size (same for Gen 1 and Gen 2).
#define GB_SEC2_LEN 197
// Mail section size (Gen 2 only = 385; Gen 1 has no mail section).
#define GB_SEC3_MAX 385

const gen_profile_t *gen_profile(int gen);

#endif // GEN_PROFILE_H
