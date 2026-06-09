#include "storage.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// Dex layout: a contiguous array of fixed 128-byte slots in a reserved flash
// region. Slot index = (gen-1)*256 + species, so each (gen, species) has a
// dedicated slot and an upsert is just a rewrite of that slot. Flash erases are
// per 4 KB sector (32 slots), so writing a slot is a read-modify-write of its
// sector.
#define SECTOR_SIZE FLASH_SECTOR_SIZE              // 4096
#define SLOT_SIZE 128
#define SLOTS_PER_SECTOR (SECTOR_SIZE / SLOT_SIZE) // 32
#define DEX_SLOTS 512                              // 2 gens * 256 species
#define RESERVED_SECTORS 32                         // 128 KB region (erased on wipe)
#define VAULT_OFFSET (PICO_FLASH_SIZE_BYTES - (RESERVED_SECTORS * SECTOR_SIZE))

// Slot header: ['P','K'][gen][species][len_lo][len_hi][0xFF][0xFF] then payload.
#define HDR_LEN 8
#define MAX_PAYLOAD (SLOT_SIZE - HDR_LEN)          // 120

static const uint8_t MAGIC[2] = {'P', 'K'};

static const uint8_t *slot_ptr(int idx) {
    return (const uint8_t *)(XIP_BASE + VAULT_OFFSET + ((uint32_t)idx * SLOT_SIZE));
}

static bool slot_used(int idx) {
    return memcmp(slot_ptr(idx), MAGIC, sizeof(MAGIC)) == 0;
}

int storage_capacity(void) { return DEX_SLOTS; }

int storage_count(void) {
    int c = 0;
    for (int i = 0; i < DEX_SLOTS; i++)
        if (slot_used(i)) c++;
    return c;
}

// Rewrite one sector with a single slot replaced (or cleared if data == NULL).
static void rewrite_slot(int idx, const uint8_t *header_and_data, uint16_t total) {
    int sector = idx / SLOTS_PER_SECTOR;
    int slot_in_sector = idx % SLOTS_PER_SECTOR;
    uint32_t sector_off = VAULT_OFFSET + (uint32_t)sector * SECTOR_SIZE;

    static uint8_t buf[SECTOR_SIZE];
    memcpy(buf, (const uint8_t *)(XIP_BASE + sector_off), SECTOR_SIZE);
    uint8_t *s = buf + slot_in_sector * SLOT_SIZE;
    memset(s, 0xFF, SLOT_SIZE);
    if (header_and_data) memcpy(s, header_and_data, total);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_off, SECTOR_SIZE);
    flash_range_program(sector_off, buf, SECTOR_SIZE);
    restore_interrupts(ints);
}

bool storage_put_mon(int gen, int species, const uint8_t *data, uint16_t len) {
    if (gen < 1 || gen > 2 || species < 1 || species > 255) return false;
    if (len > MAX_PAYLOAD) return false;
    int idx = (gen - 1) * 256 + species;

    uint8_t slot[SLOT_SIZE];
    memset(slot, 0xFF, sizeof(slot));
    slot[0] = MAGIC[0];
    slot[1] = MAGIC[1];
    slot[2] = (uint8_t)gen;
    slot[3] = (uint8_t)species;
    slot[4] = (uint8_t)(len & 0xFF);
    slot[5] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(slot + HDR_LEN, data, len);
    rewrite_slot(idx, slot, SLOT_SIZE);
    return true;
}

uint16_t storage_read_slot(int index, int *gen, int *species, uint8_t *out, uint16_t out_cap) {
    int seen = 0;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        if (seen == index) {
            const uint8_t *p = slot_ptr(i);
            if (gen) *gen = p[2];
            if (species) *species = p[3];
            uint16_t len = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
            if (len > out_cap) len = out_cap;
            memcpy(out, p + HDR_LEN, len);
            return len;
        }
        seen++;
    }
    return 0;
}

bool storage_delete(int index) {
    int seen = 0;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        if (seen == index) {
            rewrite_slot(i, NULL, 0);  // clear the slot
            return true;
        }
        seen++;
    }
    return false;
}

void storage_wipe(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(VAULT_OFFSET, RESERVED_SECTORS * SECTOR_SIZE);
    restore_interrupts(ints);
}
