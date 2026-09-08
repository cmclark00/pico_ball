#include "../src/protocol.h"
#include "../src/decoder.h"
#include "../src/species_names.h"
#include "../src/interaction.h"

#include <assert.h>
#include <cstring>
#include <stdio.h>
#include <string>

static std::string record(int gen, int species, int length, int level_offset, int level) {
    std::string hex((size_t)length * 2, '0');
    static const char digits[] = "0123456789ABCDEF";
    hex[(size_t)level_offset * 2] = digits[level >> 4];
    hex[(size_t)level_offset * 2 + 1] = digits[level & 15];
    return "MON " + std::to_string(gen) + " " + std::to_string(species) + " " +
           std::to_string(length) + " " + hex;
}

static void put16(uint8_t *data, size_t offset, uint16_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *data, size_t offset, uint32_t value) {
    put16(data, offset, (uint16_t)value);
    put16(data, offset + 2, (uint16_t)(value >> 16));
}

int main() {
    assert(trade_action_allowed(TradeState::Idle));
    assert(trade_action_allowed(TradeState::WaitForGba));
    assert(!trade_action_allowed(TradeState::Multibooting));
    assert(!trade_action_allowed(TradeState::Injecting));
    assert(!interaction_locked(TradeState::Idle));
    assert(!interaction_locked(TradeState::WaitForGba));
    assert(interaction_locked(TradeState::Multibooting));
    assert(interaction_locked(TradeState::Injecting));
    assert(parse_trade_result("PTGB_MB_RESULT ok") == TradeResult::None);
    assert(parse_trade_result("MB_RESULT ok - ready") == TradeResult::MultibootOk);
    assert(parse_trade_result("INJECT_RESULT fail") == TradeResult::InjectFail);
    assert(state_after_result(TradeState::Multibooting, TradeResult::MultibootOk) ==
           TradeState::WaitForGba);
    assert(state_after_result(TradeState::Idle, TradeResult::MultibootOk) == TradeState::Idle);
    assert(state_after_result(TradeState::Injecting, TradeResult::MultibootOk) ==
           TradeState::Injecting);
    assert(state_after_result(TradeState::Injecting, TradeResult::InjectOk) == TradeState::Idle);
    assert(state_after_result(TradeState::Multibooting, TradeResult::InjectOk) ==
           TradeState::Multibooting);

    VaultProtocol parser;
    assert(parser.consume("DUMP 3"));
    std::string g1 = record(1, 84, 66, 33, 42);
    std::string g2 = record(2, 155, 70, 31, 17);
    std::string g3 = record(3, 25, 100, 84, 51);
    assert(parser.consume(g1.c_str()));
    assert(parser.consume(g2.c_str()));
    assert(parser.consume(g3.c_str()));
    assert(parser.consume("END"));
    const VaultSnapshot &s = parser.snapshot();
    assert(s.complete && s.count == 3);
    assert(s.entries[0]->slot == 0 && s.entries[0]->level == 42);
    assert(s.entries[0]->raw_length == 66);
    assert(s.entries[0]->raw[33] == 42);
    assert(s.entries[1]->generation == 2 && s.entries[1]->level == 17);
    assert(s.entries[1]->raw_length == 70 && s.entries[1]->raw[31] == 17);
    assert(s.entries[2]->species == 25 && s.entries[2]->level == 51);
    assert(s.entries[2]->raw_length == 100 && s.entries[2]->raw[84] == 51);

    parser.reset();
    assert(parser.consume("DUMP 1"));
    assert(!parser.consume("MON 3 25 100 BAD"));
    assert(parser.consume("END"));
    assert(!parser.snapshot().complete);

    VaultEntry entry{};
    entry.generation = 1;
    entry.species = 0x54;  // Pikachu in the Gen 1 internal index
    entry.raw_length = 66;
    entry.raw[0] = 0x54;
    entry.raw[1] = 0x00; entry.raw[2] = 35;       // current HP
    entry.raw[8] = 33; entry.raw[9] = 45;        // Tackle, Growl
    entry.raw[12] = 0x12; entry.raw[13] = 0x34;  // OT ID
    entry.raw[14] = 0x01; entry.raw[15] = 0x02; entry.raw[16] = 0x03;
    entry.raw[27] = 0xA9; entry.raw[28] = 0x87;  // Atk/Def/Spd/Spc DVs
    entry.raw[29] = 30; entry.raw[30] = 40;
    entry.raw[33] = 42;
    entry.raw[34] = 0; entry.raw[35] = 70;       // max HP
    entry.raw[36] = 0; entry.raw[37] = 55;
    entry.raw[38] = 0; entry.raw[39] = 44;
    entry.raw[40] = 0; entry.raw[41] = 66;
    entry.raw[42] = 0; entry.raw[43] = 50;
    const uint8_t ot[] = {0x82, 0x8E, 0x91, 0x84, 0x98, 0x50}; // COREY
    const uint8_t nick[] = {0x8F, 0x88, 0x8A, 0x80, 0x50};     // PIKA
    memcpy(entry.raw + 44, ot, sizeof(ot));
    memcpy(entry.raw + 55, nick, sizeof(nick));

    PokemonDetails details{};
    assert(decode_pokemon(entry, &details));
    assert(strcmp(details.nickname, "PIKA") == 0);
    assert(strcmp(details.ot_name, "COREY") == 0);
    assert(details.level == 42 && details.hp == 35 && details.max_hp == 70);
    assert(details.stats[1] == 55 && details.stats[4] == 50);
    assert(details.ot_id == 0x1234 && details.exp == 0x010203);
    assert(details.moves[0] == 33 && details.move_pp[0] == 30);
    assert(details.ivs[0] == 5 && details.ivs[1] == 10 && details.ivs[2] == 9);

    VaultEntry g3entry{};
    g3entry.generation = 3;
    g3entry.raw_length = 100;
    const uint32_t pid = 0;
    const uint32_t otid = 0x00020001;
    put32(g3entry.raw, 0, pid);
    put32(g3entry.raw, 4, otid);
    const uint8_t g3nick[] = {0xCA, 0xC3, 0xC5, 0xBB, 0xFF};
    const uint8_t g3ot[] = {0xBD, 0xC9, 0xCC, 0xBF, 0xD3, 0xFF};
    memcpy(g3entry.raw + 8, g3nick, sizeof(g3nick));
    memcpy(g3entry.raw + 20, g3ot, sizeof(g3ot));
    uint8_t decrypted[48]{}; // PID 0 order: growth, attacks, EVs, misc
    put16(decrypted, 0, 25);
    put16(decrypted, 2, 13);
    put32(decrypted, 4, 123456);
    decrypted[9] = 200;
    put16(decrypted, 12, 85); put16(decrypted, 14, 98);
    decrypted[20] = 15; decrypted[21] = 20;
    decrypted[24] = 1; decrypted[25] = 2; decrypted[26] = 3;
    decrypted[27] = 4; decrypted[28] = 5; decrypted[29] = 6;
    uint32_t iv_word = 31u | (30u << 5) | (29u << 10) | (28u << 15) |
                       (27u << 20) | (26u << 25) | (1u << 31);
    put32(decrypted, 40, iv_word);
    uint16_t checksum = 0;
    for (size_t i = 0; i < sizeof(decrypted); i += 2)
        checksum = (uint16_t)(checksum + decrypted[i] + ((uint16_t)decrypted[i + 1] << 8));
    put16(g3entry.raw, 28, checksum);
    const uint32_t key = pid ^ otid;
    for (size_t i = 0; i < sizeof(decrypted); i += 4) {
        uint32_t word = (uint32_t)decrypted[i] | ((uint32_t)decrypted[i + 1] << 8) |
                        ((uint32_t)decrypted[i + 2] << 16) | ((uint32_t)decrypted[i + 3] << 24);
        put32(g3entry.raw, 32 + i, word ^ key);
    }
    g3entry.raw[84] = 50;
    put16(g3entry.raw, 86, 88); put16(g3entry.raw, 88, 100);
    put16(g3entry.raw, 90, 77); put16(g3entry.raw, 92, 66);
    put16(g3entry.raw, 94, 99); put16(g3entry.raw, 96, 81);
    put16(g3entry.raw, 98, 70);

    assert(decode_pokemon(g3entry, &details));
    assert(details.species == 25 && details.national == 25 && details.level == 50);
    assert(details.gender == 'F');
    assert(strcmp(details.nickname, "PIKA") == 0 && strcmp(details.ot_name, "COREY") == 0);
    assert(details.held_item == 13 && details.exp == 123456 && details.happiness == 200);
    assert(details.hp == 88 && details.max_hp == 100 && details.stats[5] == 70);
    assert(details.moves[0] == 85 && details.move_pp[1] == 20);
    assert(details.ivs[0] == 31 && details.ivs[5] == 26 && details.ability_slot == 1);
    assert(details.evs[0] == 1 && details.evs[5] == 6 && details.checksum_valid);
    assert(strcmp(species_name(3, 25), "Pikachu") == 0);
    assert(strcmp(move_name(3, 85), "Thunderbolt") == 0);
    assert(strcmp(item_name(3, 13), "Potion") == 0);
    assert(strcmp(type_name(3, 25), "Electric") == 0);
    assert(strcmp(type_name(3, 277), "Grass") == 0);
    assert(strcmp(ability_name(25, 0), "Static") == 0);
    assert(national_species(3, 277) == 252);
    puts("display protocol tests passed");
}
