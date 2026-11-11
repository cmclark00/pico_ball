// Gen 1 <-> Gen 2 record conversion for cross-generation injection.
//
// Gen 2 species ids are Pokédex numbers; Gen 1 uses its own scrambled internal
// index (Rhydon = 0x01, Bulbasaur = 0x99, ...). The species/type/base-stat tables
// are generated from the pinned trade engine's data (tools/gen_cross_gen.py), so
// they stay consistent with what the host tools and WebUI decode.
#include "cross_gen.h"
#include "cross_gen_tables.h"
#include <string.h>

// Record layouts: [struct][OT name (11)][nickname (11)].
#define G1_STRUCT_LEN 0x2C
#define G2_STRUCT_LEN 0x30
#define NAME_LEN      0x0B
#define G1_REC_LEN    (G1_STRUCT_LEN + 2 * NAME_LEN)   // 66
#define G2_REC_LEN    (G2_STRUCT_LEN + 2 * NAME_LEN)   // 70

#define G1_LAST_MOVE  165   // Struggle; higher ids are Gen 2-only moves
#define BASE_HAPPINESS 70

static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

// The Gen 1/2 stat formula for a non-HP stat:
//   floor(((base + DV) * 2 + floor(ceil(sqrt(stat_exp)) / 4)) * level / 100) + 5
// with the square root capped at 255, exactly as the games (and the engine's
// stat_calculation) do it.
static uint16_t calc_stat(uint8_t base, uint8_t dv, uint16_t stat_exp, uint8_t level) {
    uint32_t root = 0;
    while (root < 255 && root * root < stat_exp) root++;
    uint32_t v = ((uint32_t)base + dv) * 2 + root / 4;
    return (uint16_t)(v * level / 100 + 5);
}

bool conv_g1_to_g2_rec(const uint8_t *g1, uint8_t *g2) {
    uint8_t dex = xg_g1_to_dex[g1[0x00]];
    if (!dex) return false;
    memset(g2, 0, G2_REC_LEN);
    g2[0x00] = dex;
    // g2[0x01] held item: none
    memcpy(g2 + 0x02, g1 + 0x08, 4);            // moves (every Gen 1 move exists in Gen 2)
    memcpy(g2 + 0x06, g1 + 0x0C, 2);            // OT ID
    memcpy(g2 + 0x08, g1 + 0x0E, 3);            // EXP
    memcpy(g2 + 0x0B, g1 + 0x11, 10);           // stat EXP: HP, Atk, Def, Spd, Special
    memcpy(g2 + 0x15, g1 + 0x1B, 2);            // DVs
    memcpy(g2 + 0x17, g1 + 0x1D, 4);            // PP (PP-Up bits share the same format)
    g2[0x1B] = BASE_HAPPINESS;
    // g2[0x1C] Pokérus, g2[0x1D..0x1E] caught data: none
    uint8_t level = g1[0x21];
    g2[0x1F] = level;
    g2[0x20] = g1[0x04];                        // status
    memcpy(g2 + 0x22, g1 + 0x01, 2);            // current HP
    memcpy(g2 + 0x24, g1 + 0x22, 8);            // max HP, Atk, Def, Spd (same base stats)
    // Gen 2 splits Special into Sp. Atk / Sp. Def with their own base stats; both
    // still share Gen 1's single Special DV and stat EXP.
    uint8_t spc_dv = g1[0x1C] & 0x0F;
    uint16_t spc_exp = rd16be(g1 + 0x19);
    wr16be(g2 + 0x2C, calc_stat(xg_g2_spatk[dex], spc_dv, spc_exp, level));
    wr16be(g2 + 0x2E, calc_stat(xg_g2_spdef[dex], spc_dv, spc_exp, level));
    memcpy(g2 + G2_STRUCT_LEN, g1 + G1_STRUCT_LEN, 2 * NAME_LEN);  // OT name + nickname
    return true;
}

bool conv_g2_to_g1_rec(const uint8_t *g2, uint8_t *g1) {
    uint8_t dex = g2[0x00];
    if (dex < 1 || dex > XG_NUM_DEX) return false;
    uint8_t g1s = xg_dex_to_g1[dex];
    if (!g1s) return false;
    memset(g1, 0, G1_REC_LEN);
    g1[0x00] = g1s;
    memcpy(g1 + 0x01, g2 + 0x22, 2);            // current HP
    uint8_t level = g2[0x1F];
    g1[0x03] = level;                           // box level
    g1[0x04] = g2[0x20];                        // status
    g1[0x05] = xg_g1_types[dex][0];
    g1[0x06] = xg_g1_types[dex][1];
    // g1[0x07] catch rate: 0
    // Moves: drop Gen 2-only moves and close the gaps (Gen 1 expects the move
    // list packed from slot 0). A mon left with no move at all can't be used in
    // Gen 1, so refuse it — the real Time Capsule refuses these too.
    int kept = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t mv = g2[0x02 + i];
        if (mv == 0 || mv > G1_LAST_MOVE) continue;
        g1[0x08 + kept] = mv;
        g1[0x1D + kept] = g2[0x17 + i];         // PP (incl. PP-Up bits)
        kept++;
    }
    if (kept == 0) return false;
    memcpy(g1 + 0x0C, g2 + 0x06, 2);            // OT ID
    memcpy(g1 + 0x0E, g2 + 0x08, 3);            // EXP
    memcpy(g1 + 0x11, g2 + 0x0B, 10);           // stat EXP: HP, Atk, Def, Spd, Special
    memcpy(g1 + 0x1B, g2 + 0x15, 2);            // DVs
    g1[0x21] = level;
    memcpy(g1 + 0x22, g2 + 0x24, 8);            // max HP, Atk, Def, Spd (same base stats)
    // Gen 1 has one Special stat with its own base value (not Gen 2's Sp. Atk).
    wr16be(g1 + 0x2A, calc_stat(xg_g1_special[dex], g2[0x16] & 0x0F,
                                rd16be(g2 + 0x13), level));
    memcpy(g1 + G1_STRUCT_LEN, g2 + G2_STRUCT_LEN, 2 * NAME_LEN);  // OT name + nickname
    return true;
}
