#include "storage.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

// Exactly two banks: bank 0 = Gen 1's latest party, bank 1 = Gen 2's latest.
// Re-capturing a generation overwrites its bank. We still reserve a larger
// region so a wipe also clears any records left by older firmware.
#define SECTOR_SIZE FLASH_SECTOR_SIZE          // 4096
#define RESERVED_SECTORS 32                     // 128 KB region erased on wipe
#define BANK_COUNT 2
#define VAULT_OFFSET (PICO_FLASH_SIZE_BYTES - (RESERVED_SECTORS * SECTOR_SIZE))

// Record header: ['P','K','V','1'][len_lo][len_hi][gen][0xFF] then payload.
#define HDR_LEN 8
#define MAX_PAYLOAD (SECTOR_SIZE - HDR_LEN)
#define PROG_LEN 1280  // multiple of FLASH_PAGE_SIZE (256); holds HDR + up to 1036 (Gen 2)

static const uint8_t MAGIC[4] = {'P', 'K', 'V', '1'};

static const uint8_t *bank_ptr(int bank) {
    return (const uint8_t *)(XIP_BASE + VAULT_OFFSET + (bank * SECTOR_SIZE));
}

static bool bank_used(int bank) {
    return memcmp(bank_ptr(bank), MAGIC, sizeof(MAGIC)) == 0;
}

int storage_capacity(void) { return BANK_COUNT; }

int storage_count(void) {
    int c = 0;
    for (int b = 0; b < BANK_COUNT; b++)
        if (bank_used(b)) c++;
    return c;
}

// Overwrite the bank for `gen` (1 or 2). Always succeeds (unless len too big).
bool storage_put(int gen, const uint8_t *payload, uint16_t len) {
    if (gen < 1 || gen > BANK_COUNT) return false;
    if (len > MAX_PAYLOAD || (HDR_LEN + len) > PROG_LEN) return false;
    int bank = gen - 1;

    static uint8_t buf[PROG_LEN];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, MAGIC, sizeof(MAGIC));
    buf[4] = (uint8_t)(len & 0xFF);
    buf[5] = (uint8_t)((len >> 8) & 0xFF);
    buf[6] = (uint8_t)gen;
    memcpy(buf + HDR_LEN, payload, len);

    uint32_t off = VAULT_OFFSET + (uint32_t)bank * SECTOR_SIZE;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, SECTOR_SIZE);
    flash_range_program(off, buf, PROG_LEN);
    restore_interrupts(ints);
    return true;
}

bool storage_delete(int index) {
    int seen = 0;
    for (int b = 0; b < BANK_COUNT; b++) {
        if (!bank_used(b)) continue;
        if (seen == index) {
            uint32_t off = VAULT_OFFSET + (uint32_t)b * SECTOR_SIZE;
            uint32_t ints = save_and_disable_interrupts();
            flash_range_erase(off, SECTOR_SIZE);
            restore_interrupts(ints);
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

uint16_t storage_read(int index, uint8_t *out, uint16_t out_cap) {
    int seen = 0;
    for (int b = 0; b < BANK_COUNT; b++) {
        if (!bank_used(b)) continue;
        if (seen == index) {
            const uint8_t *p = bank_ptr(b);
            uint16_t len = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
            if (len > out_cap) len = out_cap;
            memcpy(out, p + HDR_LEN, len);
            return len;
        }
        seen++;
    }
    return 0;
}
