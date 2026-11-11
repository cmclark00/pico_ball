// On-device Gen 1/2 -> Gen 3 conversion, by running the Pokemon Community
// Conversion Standard (PCCS, MIT, GearsProgress — the library Poke Transporter GB
// converts with) on the board itself. It is the same code and the same call the
// host converter (tools/pccs_convert.cpp) makes, so both produce the same kind of
// Pal-Park-style recreation: species, level, moves, nickname and OT are kept;
// PID / IVs / nature / ability are generated to match.
//
// PCCS is vendored at build time, not redistributed here. When it isn't present
// the firmware links pccs_stub.c instead and pccs_available() is false.
#ifndef PCCS_BRIDGE_H
#define PCCS_BRIDGE_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCCS_BOX_LEN 80

// True if this firmware was built with PCCS.
bool pccs_available(void);

// Convert a stored Gen 1 or Gen 2 record ([party struct][OT name 11][nickname 11],
// as kept in the vault) into an 80-byte encrypted Gen 3 box record. Returns false
// if PCCS isn't built in, `gen` isn't 1 or 2, the record is too short, or PCCS
// rejects the Pokémon.
bool pccs_convert_record(int gen, const uint8_t *rec, uint16_t rec_len,
                         uint8_t box[PCCS_BOX_LEN]);

#ifdef __cplusplus
}
#endif

#endif // PCCS_BRIDGE_H
