#pragma once

#include <stdint.h>

class TFT_eSPI;

bool draw_pokemon_sprite(TFT_eSPI &tft, int generation, int national,
                         int x, int y, int size, uint16_t background);
