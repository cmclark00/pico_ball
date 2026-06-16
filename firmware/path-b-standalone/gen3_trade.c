// On-device Gen 3 trade-partner capture — C port of webui/gen3_trade.js, which
// itself ports rse_sp_trading.py read_section (the buffered 896-byte section
// exchange). The board is the trade peer: it presents a baked throwaway party and
// reads back the cartridge's party, then slices it into 100-byte .pk3 records.
#include "gen3_trade.h"
#include "gb_link.h"
#include "baked_party_gen3.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define SECTION_LEN 0x380
#define SECTION_HALF (SECTION_LEN >> 1)        // 448 words
#define ASKING_DATA_NYBBLE 0xC
#define DONE_FLAG 0x20
#define NOT_DONE_FLAG 0x40
#define SENDING_DATA_FLAG 0x10
#define IN_PARTY_FLAG 0x80
#define SINCE_LAST_USEFUL_LIMIT 10
#define BASE_SEND_DATA_START 1
#define BASE_DATA_CHUNK_SIZE 0xFE
#define TRADE_CANCEL 0x8F
#define STOP_TRADE ((uint32_t)TRADE_CANCEL << 16)
#define OPTION_CONFIRM_THRESHOLD 10

#define TRADING_MAIL_POS 8
#define TRADING_MAIL_LENGTH 0x24
#define TRADING_PARTY_MAX 6
#define TRADING_PARTY_INFO_POS 0xE4
#define TRADING_POKEMON_POS 0xE8

static uint32_t g_pace = 1000;

static inline uint32_t u32le(const uint8_t *b, uint32_t p) {
    return (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) |
           ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
}

static inline uint32_t swap_word(uint32_t out) {
    uint32_t r = gb_link_swap32(out);
    busy_wait_us(g_pace);
    return r;
}

static uint32_t get_bytes_from_pos(uint32_t index) {
    uint32_t base_pos = index & 0xFFF;
    uint32_t byte_base = BASE_SEND_DATA_START;
    while (base_pos >= BASE_DATA_CHUNK_SIZE) { base_pos -= BASE_DATA_CHUNK_SIZE; byte_base++; }
    return (byte_base << 8) | base_pos;
}

static uint32_t get_pos_from_bytes(uint32_t value) {
    uint32_t final_pos = value & 0xFF;
    if (final_pos >= BASE_DATA_CHUNK_SIZE) final_pos = 0;
    return final_pos + BASE_DATA_CHUNK_SIZE * (((value >> 8) & 0xF) - BASE_SEND_DATA_START);
}

typedef struct {
    uint32_t next, position, other_pos, other_end;
    bool is_valid, is_asking, is_complete, is_done;
} setup_t;

static setup_t interpret_setup(uint32_t data) {
    setup_t r = {0};
    r.next = data & 0xFFFF;
    r.position = (data >> 16) & 0xFF;
    uint32_t control = (data >> 24) & 0xFF;
    r.other_pos = data & 0xFFF;
    r.other_end = (data >> 12) & 0xFFF;
    if ((control & 0xF) >= ASKING_DATA_NYBBLE) {
        control &= ~SENDING_DATA_FLAG;
        if (control & NOT_DONE_FLAG) {
            r.is_asking = true;
            if (r.other_end > SECTION_HALF) r.other_end = SECTION_HALF;
            if (r.other_pos >= r.other_end) r.other_pos = r.other_end;
        } else if (control & DONE_FLAG) {
            r.other_pos = r.other_end;
        }
    }
    if (control & SENDING_DATA_FLAG) {
        uint32_t recv_pos = get_pos_from_bytes(data >> 16);
        r.position = recv_pos;
        r.is_valid = true;
        if (recv_pos >= SECTION_HALF) { control &= ~SENDING_DATA_FLAG; r.is_valid = false; }
    }
    if (control & DONE_FLAG) {
        r.is_done = true;
        if (control & IN_PARTY_FLAG) r.is_complete = true;
    }
    return r;
}

static setup_t swap_trade_setup_data(uint32_t next, uint32_t index, bool is_complete) {
    uint32_t data = next;
    data |= (uint32_t)(is_complete ? DONE_FLAG : NOT_DONE_FLAG) << 24;
    data |= (uint32_t)SENDING_DATA_FLAG << 24;
    data |= get_bytes_from_pos(index) << 16;
    data |= next & 0xFFFF;
    return interpret_setup(swap_word(data));
}

static setup_t ask_trade_setup_data(uint32_t start, uint32_t end) {
    uint32_t data = (uint32_t)(NOT_DONE_FLAG | ASKING_DATA_NYBBLE) << 24;
    data |= start & 0xFFF;
    data |= (end & 0xFFF) << 12;
    return interpret_setup(swap_word(data));
}

static void find_uncompleted_range(const bool *completed, int n, int *out_s, int *out_e) {
    int i = 0, max_size = 0, max_start = 0, max_end = 0;
    while (i < n) {
        int k = i;
        for (int l = i; l < n; l++) { if (completed[l]) break; k++; }
        if (k - i > max_size) { max_size = k - i; max_start = i; max_end = k; }
        if (k != i) i = k; else i++;
    }
    *out_s = max_start; *out_e = max_end;
}

// Section checksum (RSESPTradingData.are_checksum_valid). uint32_t adds wrap.
static bool checksums_valid(const uint8_t *buf) {
    uint32_t c = 0;
    for (int i = 0; i < TRADING_PARTY_MAX; i++)
        for (int j = 0; j < TRADING_MAIL_LENGTH / 4; j++)
            c += u32le(buf, i * TRADING_MAIL_LENGTH + j * 4 + TRADING_MAIL_POS);
    if (u32le(buf, TRADING_MAIL_POS + TRADING_PARTY_MAX * TRADING_MAIL_LENGTH) != c) return false;

    c = u32le(buf, TRADING_PARTY_INFO_POS);
    for (int i = 0; i < TRADING_PARTY_MAX; i++)
        for (int j = 0; j < GEN3_PK3_LEN / 4; j++)
            c += u32le(buf, i * GEN3_PK3_LEN + j * 4 + TRADING_POKEMON_POS);
    if (u32le(buf, TRADING_POKEMON_POS + TRADING_PARTY_MAX * GEN3_PK3_LEN) != c) return false;

    c = 0;
    for (int i = 0; i < (SECTION_LEN - 4) / 4; i++) c += u32le(buf, i * 4);
    if (u32le(buf, SECTION_LEN - 4) != c) return false;
    return true;
}

static bool read_section(const uint8_t *send_data, uint8_t *buf) {
    static bool completed[SECTION_HALF];
    memset(completed, 0, sizeof(completed));
    memset(buf, 0, SECTION_LEN);
    int num_uncompleted = SECTION_HALF;
    uint32_t other_pos = 0, other_end = 0, next = 0;
    int since_last_useful = SINCE_LAST_USEFUL_LIMIT;
    bool transfer_successful = false, has_all_data = false;
    uint32_t guard = 0;
    const uint32_t GUARD_MAX = 3000000u;

    while (!transfer_successful) {
        if (++guard > GUARD_MAX) { printf("G3T: trade timed out (GBA on trade screen?)\n"); return false; }
        setup_t r;
        if (since_last_useful >= SINCE_LAST_USEFUL_LIMIT && !has_all_data) {
            int s, e; find_uncompleted_range(completed, SECTION_HALF, &s, &e);
            r = ask_trade_setup_data((uint32_t)s, (uint32_t)e);
            since_last_useful = 0;
        } else {
            if (send_data) {
                next = (other_pos < other_end)
                    ? (send_data[other_pos * 2] | (send_data[other_pos * 2 + 1] << 8)) : 0;
            } else { next = 0; other_pos = 0; }
            r = swap_trade_setup_data(next, other_pos, has_all_data);
            if (other_pos < other_end) other_pos++;
            if (other_pos >= SECTION_HALF) other_pos = 0;
        }
        since_last_useful++;

        if (r.is_asking) {
            other_pos = r.other_pos; other_end = r.other_end;
        } else if (!(r.is_done && r.is_complete)) {
            if (!has_all_data && r.is_valid) {
                buf[r.position * 2] = r.next & 0xFF;
                buf[r.position * 2 + 1] = (r.next >> 8) & 0xFF;
                if (!completed[r.position]) {
                    since_last_useful = 0;
                    completed[r.position] = true;
                    if (--num_uncompleted == 0) {
                        if (checksums_valid(buf)) {
                            has_all_data = true;
                            if (!send_data) transfer_successful = true;
                        } else {
                            memset(completed, 0, sizeof(completed));
                            since_last_useful = SINCE_LAST_USEFUL_LIMIT;
                            num_uncompleted = SECTION_HALF;
                        }
                    }
                }
            }
        } else if (has_all_data) {
            transfer_successful = true;
        }
    }
    return true;
}

// Best-effort: back out of the trade menu (port of RSESPTrading.end_trade).
static void end_trade(void) {
    uint32_t next = 0; bool valid = false; int tries = 0;
    do {
        uint32_t data = ((uint32_t)(DONE_FLAG | IN_PARTY_FLAG) << 24) | ((uint32_t)TRADE_CANCEL << 16);
        uint32_t recv = swap_word(data);
        valid = (((recv >> 24) & 0xFF) == (IN_PARTY_FLAG | DONE_FLAG));
        next = valid ? (recv & 0xFFFFFF) : 0;
    } while ((!valid || (next & 0xFF0000) != STOP_TRADE) && ++tries < 2000);
    for (int i = 0; i <= OPTION_CONFIRM_THRESHOLD; i++)
        swap_word(((uint32_t)(DONE_FLAG | IN_PARTY_FLAG) << 24) | STOP_TRADE);
}

// Gen 3 species (the dex key) lives in the encrypted growth substructure: XOR the
// block with pid^otid, then read the first u16 of the growth slot (its physical
// position is set by pid%24). growth_slot[] = (enc_positions[i]>>0)&3, matching
// the engine's init_enc_positions. Only the one word we need is decrypted.
uint16_t gen3_species(const uint8_t *rec) {
    static const uint8_t growth_slot[24] =
        {0,0,0,0,0,0,1,1,2,3,2,3,1,1,2,3,2,3,1,1,2,3,2,3};
    uint32_t pid = u32le(rec, 0), otid = u32le(rec, 4);
    uint32_t key = pid ^ otid;
    uint32_t off = 32 + 12 * growth_slot[pid % 24];   // enc_data_pos=32, 12-byte slots
    return (uint16_t)((u32le(rec, off) ^ key) & 0xFFFF);
}

int gen3_capture_party(uint32_t pacing_us, uint8_t records[][GEN3_PK3_LEN], int max_records) {
    g_pace = pacing_us;
    static uint8_t buf[SECTION_LEN];
    if (!read_section(baked_party_gen3, buf)) return -1;
    uint32_t count = u32le(buf, TRADING_PARTY_INFO_POS);
    if (count > TRADING_PARTY_MAX) count = TRADING_PARTY_MAX;
    if (count > (uint32_t)max_records) count = (uint32_t)max_records;
    for (uint32_t i = 0; i < count; i++)
        memcpy(records[i], buf + TRADING_POKEMON_POS + i * GEN3_PK3_LEN, GEN3_PK3_LEN);
    end_trade();
    return (int)count;
}
