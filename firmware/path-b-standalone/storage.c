#include "storage.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define SECTOR_SIZE FLASH_SECTOR_SIZE          // 4096
#define VAULT_SECTORS 32                        // 128 KB reserved
#define VAULT_OFFSET (PICO_FLASH_SIZE_BYTES - (VAULT_SECTORS * SECTOR_SIZE))

// Record layout in a sector: ['P','K','V','1'][len_lo][len_hi][0xFF..] payload
#define HDR_LEN 8
#define MAX_PAYLOAD (SECTOR_SIZE - HDR_LEN)
#define PROG_LEN 1280  // multiple of FLASH_PAGE_SIZE (256); holds HDR + up to 1036 (Gen 2)

static const uint8_t MAGIC[4] = {'P', 'K', 'V', '1'};

static const uint8_t *sector_ptr(int i) {
    return (const uint8_t *)(XIP_BASE + VAULT_OFFSET + (i * SECTOR_SIZE));
}

static bool sector_used(int i) {
    return memcmp(sector_ptr(i), MAGIC, sizeof(MAGIC)) == 0;
}

int storage_capacity(void) { return VAULT_SECTORS; }

int storage_count(void) {
    int c = 0;
    for (int i = 0; i < VAULT_SECTORS; i++)
        if (sector_used(i)) c++;
    return c;
}

bool storage_append(const uint8_t *payload, uint16_t len) {
    if (len > MAX_PAYLOAD || (HDR_LEN + len) > PROG_LEN) return false;

    int slot = -1;
    for (int i = 0; i < VAULT_SECTORS; i++) {
        if (!sector_used(i)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;  // vault full

    static uint8_t buf[PROG_LEN];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, MAGIC, sizeof(MAGIC));
    buf[4] = (uint8_t)(len & 0xFF);
    buf[5] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(buf + HDR_LEN, payload, len);

    uint32_t off = VAULT_OFFSET + (uint32_t)slot * SECTOR_SIZE;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, SECTOR_SIZE);
    flash_range_program(off, buf, PROG_LEN);
    restore_interrupts(ints);
    return true;
}

bool storage_delete(int index) {
    int seen = 0;
    for (int i = 0; i < VAULT_SECTORS; i++) {
        if (!sector_used(i)) continue;
        if (seen == index) {
            uint32_t off = VAULT_OFFSET + (uint32_t)i * SECTOR_SIZE;
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
    flash_range_erase(VAULT_OFFSET, VAULT_SECTORS * SECTOR_SIZE);
    restore_interrupts(ints);
}

uint16_t storage_read(int index, uint8_t *out, uint16_t out_cap) {
    int seen = 0;
    for (int i = 0; i < VAULT_SECTORS; i++) {
        if (!sector_used(i)) continue;
        if (seen == index) {
            const uint8_t *p = sector_ptr(i);
            uint16_t len = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
            if (len > out_cap) len = out_cap;
            memcpy(out, p + HDR_LEN, len);
            return len;
        }
        seen++;
    }
    return 0;
}
