// Persistent vault in the RP2040's own flash. Two banks — one per generation
// (Gen 1, Gen 2) — in a reserved region near the top of flash. Re-capturing a
// generation overwrites its bank. Survives power-off; this is what makes the
// board a carry-around vault.
#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
#include <stdbool.h>

// Number of banks currently occupied (0..2).
int storage_count(void);

// Total banks (2).
int storage_capacity(void);

// Store this generation's latest party, overwriting that generation's bank.
// gen is 1 or 2. Returns false only if gen/len is invalid.
bool storage_put(int gen, const uint8_t *payload, uint16_t len);

// Read record `index` (0..count-1) into `out` (>= 800 bytes). Returns the
// payload length, or 0 if not present.
uint16_t storage_read(int index, uint8_t *out, uint16_t out_cap);

// Delete the record at `index` (0..count-1). Returns false if not present.
bool storage_delete(int index);

// Erase the whole vault.
void storage_wipe(void);

#endif // STORAGE_H
