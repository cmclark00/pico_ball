#include "gen3_party.h"
#include "gen3_stats_tables.h"
#include <string.h>

// Box record layout.
#define PID_POS       0
#define OTID_POS      4
#define CHECKSUM_POS  28
#define ENC_POS       32
#define ENC_LEN       48
#define SUB_LEN       12
// Party-only tail.
#define STATUS_POS    80   // u32
#define LEVEL_POS     84
#define MAIL_POS      85
#define CUR_HP_POS    86
#define STATS_POS     88   // max HP, Atk, Def, Speed, Sp. Atk, Sp. Def (u16 each)
#define NO_MAIL       0xFF

#define IV_EGG_BIT    (1u << 30)

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// The four 12-byte substructures (Growth, Attacks, EVs, Misc) are stored in one
// of 24 orders chosen by PID % 24.
static const char SUB_ORDER[24][5] = {
    "GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME",
    "AEGM", "AEMG", "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG",
    "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG",
};

static const uint8_t *substruct(const uint8_t *dec, uint32_t pid, char which) {
    const char *order = SUB_ORDER[pid % 24];
    for (int i = 0; i < 4; i++)
        if (order[i] == which) return dec + i * SUB_LEN;
    return dec;
}

static uint8_t level_for_exp(uint16_t species, uint32_t exp) {
    const uint32_t *curve = g3s_exp[g3s_exp_group[species]];
    uint8_t level = 1;
    while (level < G3S_NUM_LEVELS && exp >= curve[level]) level++;   // curve[level] = exp for level+1
    return level;
}

bool gen3_party_from_box(const uint8_t box[GEN3_BOX_LEN], uint8_t party[GEN3_PARTY_LEN]) {
    uint32_t pid = rd32(box + PID_POS);
    uint32_t key = pid ^ rd32(box + OTID_POS);

    // Decrypt the substructures and verify the record's checksum.
    uint8_t dec[ENC_LEN];
    uint16_t sum = 0;
    for (int i = 0; i < ENC_LEN; i += 4) {
        uint32_t w = rd32(box + ENC_POS + i) ^ key;
        dec[i] = (uint8_t)w; dec[i + 1] = (uint8_t)(w >> 8);
        dec[i + 2] = (uint8_t)(w >> 16); dec[i + 3] = (uint8_t)(w >> 24);
        sum = (uint16_t)(sum + (w & 0xFFFF) + (w >> 16));
    }
    if (sum != rd16(box + CHECKSUM_POS)) return false;

    const uint8_t *growth = substruct(dec, pid, 'G');
    const uint8_t *evs = substruct(dec, pid, 'E');
    const uint8_t *misc = substruct(dec, pid, 'M');

    uint16_t species = rd16(growth);
    if (species < 1 || species > G3S_LAST_SPECIES) return false;
    uint32_t iv_word = rd32(misc + 4);
    if (iv_word & IV_EGG_BIT) return false;

    uint8_t level = level_for_exp(species, rd32(growth + 4));

    // Nature (PID % 25) raises one of Atk/Def/Speed/Sp.Atk/Sp.Def by 10% and
    // lowers another by 10%; when both are the same stat it does nothing.
    uint32_t nature = pid % 25;
    int up = (int)(nature / 5) + 1, down = (int)(nature % 5) + 1;   // 1-based stat index

    uint16_t stats[6];
    for (int s = 0; s < 6; s++) {
        uint32_t iv = (iv_word >> (5 * s)) & 0x1F;
        uint32_t n = (2u * g3s_base[species][s] + iv + evs[s] / 4u) * level / 100u;
        if (s == 0) {
            n += level + 10u;
        } else {
            n += 5u;
            if (up != down) {
                if (s == up) n = n * 110u / 100u;
                else if (s == down) n = n * 90u / 100u;
            }
        }
        stats[s] = (uint16_t)n;
    }

    memcpy(party, box, GEN3_BOX_LEN);
    memset(party + STATUS_POS, 0, 4);
    party[LEVEL_POS] = level;
    party[MAIL_POS] = NO_MAIL;
    wr16(party + CUR_HP_POS, stats[0]);
    for (int s = 0; s < 6; s++) wr16(party + STATS_POS + 2 * s, stats[s]);
    return true;
}
