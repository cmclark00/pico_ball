#include "decoder.h"
#include "species_names.h"

#include <string.h>

static uint16_t u16be(const uint8_t *data, size_t offset) {
    return (uint16_t)((uint16_t)data[offset] << 8) | data[offset + 1];
}

static uint32_t u24be(const uint8_t *data, size_t offset) {
    return ((uint32_t)data[offset] << 16) |
           ((uint32_t)data[offset + 1] << 8) | data[offset + 2];
}

static uint16_t u16le(const uint8_t *data, size_t offset) {
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t u32le(const uint8_t *data, size_t offset) {
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
}

static void decode_gb_text(const uint8_t *data, size_t length, char *out, size_t out_size) {
    size_t used = 0;
    if (!out_size) return;
    for (size_t i = 0; i < length && used + 1 < out_size; ++i) {
        uint8_t value = data[i];
        char decoded = '?';
        if (value == 0x50 || value == 0x00) break;
        if (value == 0x7F) decoded = ' ';
        else if (value >= 0x80 && value <= 0x99) decoded = (char)('A' + value - 0x80);
        else if (value >= 0xA0 && value <= 0xB9) decoded = (char)('a' + value - 0xA0);
        else if (value >= 0xF6) decoded = (char)('0' + value - 0xF6);
        out[used++] = decoded;
    }
    out[used] = '\0';
}

static void decode_g3_text(const uint8_t *data, size_t length, char *out, size_t out_size) {
    size_t used = 0;
    if (!out_size) return;
    for (size_t i = 0; i < length && used + 1 < out_size; ++i) {
        uint8_t value = data[i];
        char decoded = '?';
        if (value == 0xFF) break;
        if (value == 0x00) decoded = ' ';
        else if (value >= 0xA1 && value <= 0xAA) decoded = (char)('0' + value - 0xA1);
        else if (value >= 0xBB && value <= 0xD4) decoded = (char)('A' + value - 0xBB);
        else if (value >= 0xD5 && value <= 0xEE) decoded = (char)('a' + value - 0xD5);
        else if (value == 0xAB) decoded = '!';
        else if (value == 0xAC) decoded = '?';
        else if (value == 0xAD) decoded = '.';
        else if (value == 0xAE) decoded = '-';
        else if (value == 0xB4) decoded = '\'';
        out[used++] = decoded;
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
}

static bool decode_gen12(const VaultEntry &entry, PokemonDetails *out) {
    const bool gen2 = entry.generation == 2;
    const size_t mon_length = gen2 ? 48 : 44;
    const size_t base_length = mon_length + 22;
    if (entry.raw_length < base_length) return false;
    const uint8_t *m = entry.raw;

    out->generation = entry.generation;
    out->species = m[0];
    out->national = national_species(entry.generation, m[0]);
    out->level = m[gen2 ? 0x1F : 0x21];
    out->held_item = gen2 ? m[1] : 0;
    out->hp = u16be(m, gen2 ? 0x22 : 1);
    out->max_hp = u16be(m, gen2 ? 0x24 : 0x22);
    out->ot_id = u16be(m, gen2 ? 6 : 0x0C);
    out->exp = u24be(m, gen2 ? 0x08 : 0x0E);
    out->happiness = gen2 ? m[0x1B] : 0;
    out->gender = '-';
    out->checksum_valid = true;

    const size_t moves_offset = gen2 ? 2 : 8;
    const size_t pp_offset = gen2 ? 0x17 : 0x1D;
    for (size_t i = 0; i < 4; ++i) {
        out->moves[i] = m[moves_offset + i];
        out->move_pp[i] = m[pp_offset + i] & 0x3F;
        if (out->moves[i]) out->move_count++;
    }

    const uint8_t d1 = m[gen2 ? 0x15 : 0x1B];
    const uint8_t d2 = m[(gen2 ? 0x15 : 0x1B) + 1];
    out->ivs[1] = d1 >> 4;
    out->ivs[2] = d1 & 0x0F;
    out->ivs[3] = d2 >> 4;
    out->ivs[4] = d2 & 0x0F;
    out->ivs[5] = out->ivs[4];
    out->ivs[0] = (uint8_t)(((out->ivs[1] & 1) << 3) |
                            ((out->ivs[2] & 1) << 2) |
                            ((out->ivs[3] & 1) << 1) |
                            (out->ivs[4] & 1));
    out->shiny = gen2 && out->ivs[2] == 10 && out->ivs[3] == 10 &&
                  out->ivs[4] == 10 && (out->ivs[1] & 2) != 0;
    if (gen2) {
        int rate = gender_rate(2, out->national);
        if (rate >= 0) out->gender = out->ivs[1] < 2 * rate ? 'F' : 'M';
    }

    if (gen2) {
        const size_t offsets[] = {0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E};
        out->stat_count = 6;
        for (size_t i = 0; i < 6; ++i) out->stats[i] = u16be(m, offsets[i]);
    } else {
        const size_t offsets[] = {0x22, 0x24, 0x26, 0x28, 0x2A};
        out->stat_count = 5;
        for (size_t i = 0; i < 5; ++i) out->stats[i] = u16be(m, offsets[i]);
    }

    decode_gb_text(entry.raw + mon_length, 11, out->ot_name, sizeof(out->ot_name));
    decode_gb_text(entry.raw + mon_length + 11, 11, out->nickname, sizeof(out->nickname));
    return true;
}

static uint8_t gen3_positions(uint32_t permutation) {
    uint32_t current = 0;
    for (uint8_t i = 0; i < 4; ++i)
        for (uint8_t j = 0; j < 4; ++j) {
            if (j == i) continue;
            for (uint8_t k = 0; k < 4; ++k) {
                if (k == i || k == j) continue;
                for (uint8_t l = 0; l < 4; ++l) {
                    if (l == i || l == j || l == k) continue;
                    uint8_t packed = (uint8_t)((1u << (j * 2)) |
                                               (2u << (k * 2)) | (3u << (l * 2)));
                    if (current++ == permutation) return packed;
                }
            }
        }
    return 0;
}

static bool decode_gen3(const VaultEntry &entry, PokemonDetails *out) {
    if (entry.raw_length < 100) return false;
    const uint8_t *record = entry.raw;
    out->generation = 3;
    out->pid = u32le(record, 0);
    uint32_t otid = u32le(record, 4);
    uint32_t key = out->pid ^ otid;
    out->ot_id = (uint16_t)otid;
    out->sid = (uint16_t)(otid >> 16);
    out->level = record[84];
    out->hp = u16le(record, 86);
    out->max_hp = u16le(record, 88);
    out->stat_count = 6;
    const size_t stat_offsets[] = {88, 90, 92, 94, 96, 98};
    for (size_t i = 0; i < 6; ++i) out->stats[i] = u16le(record, stat_offsets[i]);
    decode_g3_text(record + 8, 10, out->nickname, sizeof(out->nickname));
    decode_g3_text(record + 20, 7, out->ot_name, sizeof(out->ot_name));

    uint8_t decrypted[48];
    uint16_t checksum = 0;
    for (size_t i = 0; i < sizeof(decrypted); i += 4) {
        uint32_t word = u32le(record, 32 + i) ^ key;
        decrypted[i] = (uint8_t)word;
        decrypted[i + 1] = (uint8_t)(word >> 8);
        decrypted[i + 2] = (uint8_t)(word >> 16);
        decrypted[i + 3] = (uint8_t)(word >> 24);
        checksum = (uint16_t)(checksum + (uint16_t)word + (uint16_t)(word >> 16));
    }
    out->checksum_valid = checksum == u16le(record, 28);

    uint8_t positions = gen3_positions(out->pid % 24);
    const uint8_t *sub[4];
    for (uint8_t kind = 0; kind < 4; ++kind) {
        uint8_t quarter = (positions >> (kind * 2)) & 3;
        sub[kind] = decrypted + quarter * 12;
    }
    const uint8_t *growth = sub[0];
    const uint8_t *attacks = sub[1];
    const uint8_t *evs = sub[2];
    const uint8_t *misc = sub[3];
    out->species = u16le(growth, 0);
    out->national = national_species(3, out->species);
    out->held_item = u16le(growth, 2);
    out->exp = u32le(growth, 4);
    out->happiness = growth[9];
    out->nature = (uint8_t)(out->pid % 25);
    for (size_t i = 0; i < 4; ++i) {
        out->moves[i] = u16le(attacks, i * 2);
        out->move_pp[i] = attacks[8 + i];
        if (out->moves[i]) out->move_count++;
    }
    memcpy(out->evs, evs, 6);
    uint32_t iv_word = u32le(misc, 4);
    out->ivs[0] = iv_word & 31;
    out->ivs[1] = (iv_word >> 5) & 31;
    out->ivs[2] = (iv_word >> 10) & 31;
    out->ivs[3] = (iv_word >> 15) & 31;
    out->ivs[4] = (iv_word >> 20) & 31;
    out->ivs[5] = (iv_word >> 25) & 31;
    out->ability_slot = (iv_word >> 31) & 1;
    out->shiny = (uint16_t)(out->ot_id ^ out->sid ^
                            (uint16_t)(out->pid >> 16) ^ (uint16_t)out->pid) < 8;
    out->gender = '-';
    int rate = gender_rate(3, out->national);
    if (rate >= 0) {
        if (rate == 0) out->gender = 'M';
        else if (rate == 8) out->gender = 'F';
        else out->gender = (out->pid & 0xFF) < (uint32_t)(rate * 32 - 1) ? 'F' : 'M';
    }
    return true;
}

bool decode_pokemon(const VaultEntry &entry, PokemonDetails *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (entry.generation == 1 || entry.generation == 2) return decode_gen12(entry, out);
    if (entry.generation == 3) return decode_gen3(entry, out);
    return false;
}
