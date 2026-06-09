// Persistent vault in the RP2040's own flash. One captured trade record per 4 KB
// sector, in a reserved region near the top of flash. Survives power-off; this
// is what makes the board a carry-around vault.
#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
#include <stdbool.h>

// Number of captures currently stored.
int storage_count(void);

// Maximum captures that fit.
int storage_capacity(void);

// Append one record. Returns false if the vault is full or len is too big.
bool storage_append(const uint8_t *payload, uint16_t len);

// Read record `index` (0..count-1) into `out` (>= 800 bytes). Returns the
// payload length, or 0 if not present.
uint16_t storage_read(int index, uint8_t *out, uint16_t out_cap);

// Delete the record at `index` (0..count-1). Returns false if not present.
bool storage_delete(int index);

// Erase the whole vault.
void storage_wipe(void);

#endif // STORAGE_H
