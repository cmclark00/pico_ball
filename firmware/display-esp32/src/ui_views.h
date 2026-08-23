#pragma once

#include "decoder.h"
#include "protocol.h"

class TFT_eSPI;

enum class DisplayScreen { Browser, Detail };

void draw_vault_browser(TFT_eSPI &tft, const VaultSnapshot &vault, int page,
                        const char *status);
void draw_pokemon_detail(TFT_eSPI &tft, const VaultEntry &entry,
                         const PokemonDetails &details, int tab,
                         const char *status, bool ready_to_trade, bool busy);
