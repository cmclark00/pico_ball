#pragma once

#include "protocol.h"

#include <stdint.h>

struct PokemonDetails {
    int generation;
    int species;
    int national;
    int level;
    uint16_t hp;
    uint16_t max_hp;
    uint16_t stats[6];              // HP, Atk, Def, Spd, SpA/Spc, SpD
    uint8_t stat_count;
    uint16_t moves[4];
    uint8_t move_pp[4];
    uint8_t move_count;
    uint16_t held_item;
    uint16_t ot_id;
    uint16_t sid;
    uint32_t exp;
    uint32_t pid;
    uint8_t happiness;
    uint8_t nature;
    uint8_t ability_slot;
    uint8_t ivs[6];                // HP, Atk, Def, Spd, SpA/Spc, SpD
    uint8_t evs[6];
    bool shiny;
    bool checksum_valid;
    char gender;                   // 'M', 'F', or '-'
    char nickname[16];
    char ot_name[16];
};

bool decode_pokemon(const VaultEntry &entry, PokemonDetails *out);
