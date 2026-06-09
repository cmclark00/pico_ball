#include "gen_profile.h"
#include "baked_party.h"
#include "baked_party_gen2.h"

// Gen 1 (RBY): 3 sections, all 0xFD-synced.
static const uint16_t g1_lens[3] = {PB_SEC0_LEN, PB_SEC1_LEN, PB_SEC2_LEN};
static const uint8_t g1_start[3] = {0xFD, 0xFD, 0xFD};
static const uint8_t g1_sync[3] = {1, 1, 1};
static const uint8_t g1_pre[3] = {7, 6, 3};
static const gen_profile_t G1 = {
    1, 3, g1_lens, g1_start, g1_sync, g1_pre, pb_sections, 625};

// Gen 2 (GSC): 4 sections; the 4th (mail) uses starter 0x20 and is not synced.
static const uint16_t g2_lens[4] = {PB2_SEC0_LEN, PB2_SEC1_LEN, PB2_SEC2_LEN, PB2_SEC3_LEN};
static const uint8_t g2_start[4] = {0xFD, 0xFD, 0xFD, 0x20};
static const uint8_t g2_sync[4] = {1, 1, 1, 0};
static const uint8_t g2_pre[4] = {7, 6, 3, 5};
static const gen_profile_t G2 = {
    2, 4, g2_lens, g2_start, g2_sync, g2_pre, pb2_sections, 1036};

const gen_profile_t *gen_profile(int gen) { return gen == 2 ? &G2 : &G1; }
