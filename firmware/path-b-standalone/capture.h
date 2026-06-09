// On-device Gen 1/2 trade-capture FSM. Presents a fixed throwaway party, records
// the cartridge's raw trade-block bytes, and never commits a trade.
#ifndef CAPTURE_H
#define CAPTURE_H
#include <stdint.h>
#include <stdbool.h>
#include "gen_profile.h"

typedef enum {
    GB_CAP_OK = 0,
    GB_CAP_NO_GAME,    // never reached the trade table
    GB_CAP_HANDSHAKE,  // handshake/sync timed out
    GB_CAP_SECTION,    // a data section failed to sync
} gb_capture_result_t;

// Run a capture for the given generation profile. On GB_CAP_OK, `out` (>=
// profile->record_len bytes) holds the cartridge's concatenated section bytes.
gb_capture_result_t capture_run(const gen_profile_t *p, uint8_t *out);

// Split a captured raw record into individual Pokémon (struct + OT + nickname
// each). Applies the patch list, then copies up to 6 mons into data[]/lens[]
// and their species into species[]. Returns the party count (0 on bad data).
int extract_mons(const gen_profile_t *p, uint8_t *raw,
                 uint8_t species[6], uint8_t data[6][DEX_MON_MAX], uint16_t lens[6]);

#endif // CAPTURE_H
