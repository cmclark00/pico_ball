#include "storage.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// Dex layout: a contiguous array of fixed 128-byte slots in a reserved flash
// region. Slots are keyed by (gen, species) via a linear upsert — recapturing a
// species rewrites its slot, new species take the first free slot. This is
// generation-agnostic (Gen 3 species exceed 255, so the old idx=(gen-1)*256+
// species scheme with a 1-byte species no longer works). Flash erases are per
// 4 KB sector (32 slots), so writing a slot is a read-modify-write of its sector.
#define SECTOR_SIZE FLASH_SECTOR_SIZE              // 4096
#define SLOT_SIZE 128
#define SLOTS_PER_SECTOR (SECTOR_SIZE / SLOT_SIZE) // 32
#define RESERVED_SECTORS 32                         // 128 KB region (erased on wipe)
#define DEX_SLOTS (RESERVED_SECTORS * SLOTS_PER_SECTOR)  // 1024 slots
#define VAULT_OFFSET (PICO_FLASH_SIZE_BYTES - (RESERVED_SECTORS * SECTOR_SIZE))

// Slot header (8 B): ['P','3'][gen][species_lo][species_hi][len][cnt_lo][cnt_hi]
// then payload. species is 16-bit (Gen 3 internal ids reach ~411); len is one
// byte (records are <=120). Bytes [6..7] are a monotonic 16-bit write counter.
// The 'P','3' magic supersedes the old 'P','K' format — old slots read as unused
// (wipe once after upgrading).
#define HDR_LEN 8
#define MAX_PAYLOAD (SLOT_SIZE - HDR_LEN)          // 120
#define NO_COUNTER 0xFFFF

static const uint8_t MAGIC[2] = {'P', '3'};

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

// Scan all used slots and return the maximum write counter (0 if none > 0).
static uint16_t max_counter(void) {
    uint16_t mx = 0;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        const uint8_t *p = slot_ptr(i);
        uint16_t cnt = (uint16_t)p[6] | ((uint16_t)p[7] << 8);
        if (cnt != NO_COUNTER && cnt > mx) mx = cnt;
    }
    return mx;
}

bool storage_put_mon(int gen, int species, const uint8_t *data, uint16_t len) {
    if (gen < 1 || gen > 3 || species < 1 || species > 0xFFFF) return false;
    if (len > MAX_PAYLOAD) return false;

    // Find an existing (gen, species) slot to overwrite, else the first free one.
    int idx = -1, free_idx = -1;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) { if (free_idx < 0) free_idx = i; continue; }
        const uint8_t *p = slot_ptr(i);
        int sp = (int)p[3] | ((int)p[4] << 8);
        if (p[2] == gen && sp == species) { idx = i; break; }
    }
    if (idx < 0) idx = free_idx;
    if (idx < 0) return false;  // vault full

    uint16_t cnt = max_counter() + 1;
    if (cnt == NO_COUNTER) cnt = 1;  // skip reserved sentinel value

    uint8_t slot[SLOT_SIZE];
    memset(slot, 0xFF, sizeof(slot));
    slot[0] = MAGIC[0];
    slot[1] = MAGIC[1];
    slot[2] = (uint8_t)gen;
    slot[3] = (uint8_t)(species & 0xFF);
    slot[4] = (uint8_t)((species >> 8) & 0xFF);
    slot[5] = (uint8_t)len;
    slot[6] = (uint8_t)(cnt & 0xFF);
    slot[7] = (uint8_t)((cnt >> 8) & 0xFF);
    memcpy(slot + HDR_LEN, data, len);
    rewrite_slot(idx, slot, SLOT_SIZE);
    return true;
}

int storage_last_written_index(void) {
    uint16_t best_cnt = 0;
    int best_physical = -1;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        const uint8_t *p = slot_ptr(i);
        uint16_t cnt = (uint16_t)p[6] | ((uint16_t)p[7] << 8);
        if (cnt == NO_COUNTER) continue;
        if (cnt > best_cnt) { best_cnt = cnt; best_physical = i; }
    }
    if (best_physical < 0) return -1;
    // Convert physical slot index to logical (nth used slot).
    int logical = 0;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        if (i == best_physical) return logical;
        logical++;
    }
    return -1;
}

uint16_t storage_read_slot(int index, int *gen, int *species, uint8_t *out, uint16_t out_cap) {
    int seen = 0;
    for (int i = 0; i < DEX_SLOTS; i++) {
        if (!slot_used(i)) continue;
        if (seen == index) {
            const uint8_t *p = slot_ptr(i);
            if (gen) *gen = p[2];
            if (species) *species = (int)p[3] | ((int)p[4] << 8);
            uint16_t len = p[5];
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
