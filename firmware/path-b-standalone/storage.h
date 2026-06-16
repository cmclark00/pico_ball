// Persistent dex in the RP2040's own flash: one slot per (generation, species),
// so the vault accumulates a living Pokédex. Capturing a party upserts each
// Pokémon by species — recapturing a species overwrites just that entry, new
// species are added. Survives power-off.
#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
#include <stdbool.h>

// Number of dex entries currently stored.
int storage_count(void);

// Total slots available (shared across all generations).
int storage_capacity(void);

// Upsert one Pokémon into the dex by (gen, species). gen is 1..3; species is the
// generation's internal species id (1..65535; Gen 3 reaches ~411). Returns false
// on invalid args, an over-long record, or a full vault.
bool storage_put_mon(int gen, int species, const uint8_t *data, uint16_t len);

// Read the index-th stored entry (0..count-1). Sets *gen and *species, copies
// the mon bytes into out (capped at out_cap), returns the byte length (0 if none).
uint16_t storage_read_slot(int index, int *gen, int *species, uint8_t *out, uint16_t out_cap);

// Delete the index-th stored entry.
bool storage_delete(int index);

// Erase the whole dex.
void storage_wipe(void);

// Return the logical index (0..count-1) of the most recently stored entry,
// based on the monotonic write counter. Returns -1 if the vault is empty or
// all entries predate the counter (written before this feature was added).
int storage_last_written_index(void);

#endif // STORAGE_H
