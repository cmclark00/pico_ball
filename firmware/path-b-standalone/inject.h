// Standalone Pokémon injection: trade a vaulted mon back into a cartridge
// without a PC. Reverse of capture_run — we send the stored mon and receive
// whatever the player gives away.
#ifndef INJECT_H
#define INJECT_H
#include <stdint.h>
#include "gen_profile.h"

typedef enum {
    GB_INJ_OK = 0,
    GB_INJ_BAD_RECORD,  // rec_len too short for the generation
    GB_INJ_NO_GAME,     // never reached the trade table
    GB_INJ_SECTION,     // a data section failed to sync
    GB_INJ_TIMEOUT,     // selection / commit timed out
    GB_INJ_CANCELLED,   // cartridge sent stop_trade
    GB_INJ_DECLINED,    // cartridge declined the trade
} gb_inject_result_t;

// Trade mon_rec into the cartridge (commits the trade).
// The mon the cartridge gives up is extracted and stored in the dex
// automatically. mon_rec must be at least mon_len + 2*name_len bytes for
// the given generation (struct + OT name + nickname).
gb_inject_result_t inject_run(const gen_profile_t *p,
                               const uint8_t *mon_rec, uint16_t rec_len);

#endif // INJECT_H
