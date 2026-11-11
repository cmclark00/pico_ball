// Persistent vault in the RP2040's own flash: one slot per individual Pokémon.
// Storing a mon that is already in the vault (same generation, species, trainer
// ID and DVs — see mon_identity.h) updates its slot, so recapturing after
// levelling up refreshes the entry; any other Pokémon gets a slot of its own and
// is never overwritten. Survives power-off.
#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
#include <stdbool.h>
#include "mon_identity.h"

// Number of dex entries currently stored.
int storage_count(void);

// Total slots available (shared across all generations).
int storage_capacity(void);

// Store one Pokémon, updating its existing slot if this individual is already in
// the vault. gen is 1..3; species is the generation's internal species id
// (1..65535; Gen 3 reaches ~411); flags are MON_FLAG_* bits. Returns false on
// invalid args, an over-long record, or a full vault.
bool storage_put_mon(int gen, int species, uint8_t flags,
                     const uint8_t *data, uint16_t len);

// Read the index-th stored entry (0..count-1). Sets *gen and *species, copies
// the mon bytes into out (capped at out_cap), returns the byte length (0 if none).
uint16_t storage_read_slot(int index, int *gen, int *species, uint8_t *out, uint16_t out_cap);

// MON_FLAG_* bits of the index-th stored entry (0 if none).
uint8_t storage_slot_flags(int index);

// A slot id names a stored entry for as long as it exists: unlike its index
// (its position in the dump), it does not shift when other entries are added or
// deleted. Both return -1 if there is no such entry.
int storage_slot_id(int index);          // index -> id
int storage_index_of_id(int id);         // id -> current index

// Delete the index-th stored entry.
bool storage_delete(int index);

// Erase the whole dex.
void storage_wipe(void);

// Return the logical index (0..count-1) of the most recently stored entry,
// based on the monotonic write counter. Returns -1 if the vault is empty or
// all entries predate the counter (written before this feature was added).
int storage_last_written_index(void);

#endif // STORAGE_H
