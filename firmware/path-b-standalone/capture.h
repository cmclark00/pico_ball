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

#endif // CAPTURE_H
