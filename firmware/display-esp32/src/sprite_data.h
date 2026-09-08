#pragma once

#include <stdint.h>

struct SpriteAsset {
    const uint8_t *data;
    uint32_t length;
    uint8_t width;
    uint8_t height;
};

// Look up a generation-specific front sprite by National Pokédex number.
bool sprite_asset(int generation, int national, SpriteAsset *out);
