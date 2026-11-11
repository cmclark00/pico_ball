// Standalone Pokémon injection: trade a vaulted mon back into a cartridge
// without a PC. Reverse of capture_run — we send the stored mon and receive
// whatever the player gives away.
#ifndef INJECT_H
#define INJECT_H
#include <stdint.h>
#include <stdbool.h>
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

// What the cartridge traded away in return (valid when inject_run returns
// GB_INJ_OK). `stored` is false if it could not be written to the dex — e.g. the
// vault is full — in which case that Pokémon is gone from the cartridge and was
// NOT saved here either.
typedef struct {
    uint8_t species;   // generation's species id; 0 if it couldn't be identified
    bool stored;
} gb_inject_received_t;

// Trade mon_rec into the cartridge (commits the trade). `flags` are the stored
// MON_FLAG_* bits of mon_rec (a Gen 2 Egg is offered as an Egg).
// The mon the cartridge gives up is extracted and stored in the dex
// automatically; `received` (may be NULL) reports its species and whether the
// store succeeded. mon_rec must be at least mon_len + 2*name_len bytes for the
// given generation (struct + OT name + nickname).
gb_inject_result_t inject_run(const gen_profile_t *p,
                               const uint8_t *mon_rec, uint16_t rec_len, uint8_t flags,
                               gb_inject_received_t *received);

#endif // INJECT_H
