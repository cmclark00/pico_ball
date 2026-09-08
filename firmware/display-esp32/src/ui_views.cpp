#include "ui_views.h"

#include "species_names.h"
#include "sprite_renderer.h"

#include <TFT_eSPI.h>
#include <stdio.h>

namespace {
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t PANEL = 0x18E3;
constexpr uint16_t ACCENT = 0xFBE0;
constexpr uint16_t GO = 0x0600;
constexpr uint16_t TEXT = TFT_WHITE;
constexpr uint16_t MUTED = 0xBDF7;
constexpr int ROWS_PER_PAGE = 6;
constexpr int LIST_TOP = 35;
constexpr int ROW_HEIGHT = 27;
constexpr int BUTTON_TOP = 205;

const char *const NATURES[] = {
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky",
};

void button(TFT_eSPI &tft, int x, int width, const char *label, uint16_t color) {
    tft.fillRoundRect(x + 2, BUTTON_TOP, width - 4, 33, 5, color);
    tft.setTextColor(TEXT, color);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, x + width / 2, BUTTON_TOP + 16, 2);
}

void text_line(TFT_eSPI &tft, int x, int y, const char *label,
               uint16_t color = TEXT, int font = 2) {
    tft.setTextColor(color, BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(label, x, y, font);
}
}  // namespace

void draw_vault_browser(TFT_eSPI &tft, const VaultSnapshot &vault, int page,
                        const char *status) {
    tft.fillScreen(BG);
    tft.fillRect(0, 0, 320, 33, ACCENT);
    tft.setTextColor(TFT_BLACK, ACCENT);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("pico_ball vault", 7, 11, 2);
    char count[32];
    if (vault.declared_count >= 0 && !vault.complete)
        snprintf(count, sizeof(count), "%u/%d Pokemon", (unsigned)vault.count, vault.declared_count);
    else
        snprintf(count, sizeof(count), "%u Pokemon", (unsigned)vault.count);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(count, 313, 11, 2);

    int first = page * ROWS_PER_PAGE;
    for (int row = 0; row < ROWS_PER_PAGE; ++row) {
        int index = first + row;
        int y = LIST_TOP + row * ROW_HEIGHT;
        uint16_t row_bg = row & 1 ? BG : PANEL;
        tft.fillRect(0, y, 320, ROW_HEIGHT - 1, row_bg);
        if (index >= (int)vault.count) continue;
        const VaultEntry &entry = *vault.entries[index];
        int national = national_species(entry.generation, entry.species);
        draw_pokemon_sprite(tft, entry.generation, national, 2, y + 1, 24, row_bg);
        tft.setTextColor(TEXT, row_bg);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(species_name(entry.generation, entry.species), 31, y + 12, 2);
        char details[32];
        snprintf(details, sizeof(details), "G%d  Lv%d  #%03d",
                 entry.generation, entry.level, national);
        tft.setTextColor(MUTED, row_bg);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(details, 313, y + 12, 2);
    }

    button(tft, 0, 70, "< Prev", PANEL);
    button(tft, 70, 70, "Next >", PANEL);
    button(tft, 140, 180, "Refresh whole vault", 0x0190);
    tft.fillRect(0, 197, 320, 8, BG);
    tft.setTextColor(MUTED, BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(status ? status : "", 4, 196, 1);
}

void draw_pokemon_detail(TFT_eSPI &tft, const VaultEntry &entry,
                         const PokemonDetails &d, int tab,
                         const char *status, bool ready_to_trade, bool busy) {
    tft.fillScreen(BG);
    tft.fillRect(0, 0, 320, 31, ACCENT);
    tft.setTextColor(TFT_BLACK, ACCENT);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("< Vault", 6, 10, 2);
    tft.setTextDatum(MR_DATUM);
    char heading[40];
    snprintf(heading, sizeof(heading), "Gen %d  slot %d", entry.generation, entry.slot);
    tft.drawString(heading, 314, 10, 2);

    draw_pokemon_sprite(tft, entry.generation, d.national, 4, 34, 90, PANEL);
    const char *name = species_name(entry.generation, entry.species);
    text_line(tft, 101, 34, name, TEXT, 4);
    char line[80];
    snprintf(line, sizeof(line), "\"%s\"  Lv%d  %c%s", d.nickname[0] ? d.nickname : name,
             d.level, d.gender, d.shiny ? "  SHINY" : "");
    text_line(tft, 101, 62, line, d.shiny ? ACCENT : MUTED, 2);
    snprintf(line, sizeof(line), "%s  HP %u/%u", type_name(entry.generation, entry.species),
             d.hp, d.max_hp);
    text_line(tft, 101, 80, line, TEXT, 2);
    snprintf(line, sizeof(line), "Held: %s", item_name(entry.generation, d.held_item));
    text_line(tft, 101, 98, line, MUTED, 2);
    if (entry.generation == 3) {
        snprintf(line, sizeof(line), "%s  %s", NATURES[d.nature],
                 ability_name(d.species, d.ability_slot));
        text_line(tft, 101, 116, line, MUTED, 2);
    } else {
        snprintf(line, sizeof(line), "OT %s  ID %u", d.ot_name, d.ot_id);
        text_line(tft, 101, 116, line, MUTED, 2);
    }

    if (tab == 0) {
        static const char *const labels[] = {"HP", "ATK", "DEF", "SPD", "SpA", "SpD"};
        for (int i = 0; i < d.stat_count; ++i) {
            int x = i * (320 / d.stat_count);
            int width = 320 / d.stat_count;
            tft.fillRoundRect(x + 2, 136, width - 4, 29, 4, PANEL);
            tft.setTextColor(MUTED, PANEL); tft.setTextDatum(TC_DATUM);
            tft.drawString(labels[i], x + width / 2, 138, 1);
            tft.setTextColor(TEXT, PANEL);
            char value[12]; snprintf(value, sizeof(value), "%u", d.stats[i]);
            tft.drawString(value, x + width / 2, 148, 2);
        }
        snprintf(line, sizeof(line), "%s HP%u A%u D%u S%u A%u D%u",
                 entry.generation == 3 ? "IV" : "DV", d.ivs[0], d.ivs[1], d.ivs[2],
                 d.ivs[3], d.ivs[4], d.ivs[5]);
        text_line(tft, 5, 170, line, MUTED, 2);
        snprintf(line, sizeof(line), "OT %s  ID %u%s", d.ot_name, d.ot_id,
                 entry.generation == 3 ? " / SID available" : "");
        text_line(tft, 5, 188, line, MUTED, 2);
    } else {
        int y = 136;
        for (int i = 0; i < 4; ++i) {
            if (!d.moves[i]) continue;
            snprintf(line, sizeof(line), "%s", move_name(entry.generation, d.moves[i]));
            text_line(tft, 7, y, line, TEXT, 2);
            snprintf(line, sizeof(line), "%u PP", d.move_pp[i]);
            tft.setTextColor(MUTED, BG); tft.setTextDatum(TR_DATUM);
            tft.drawString(line, 313, y, 2);
            y += 18;
        }
        if (entry.generation == 3) {
            snprintf(line, sizeof(line), "EV %u/%u/%u/%u/%u/%u  EXP %lu",
                     d.evs[0], d.evs[1], d.evs[2], d.evs[3], d.evs[4], d.evs[5],
                     (unsigned long)d.exp);
        } else {
            snprintf(line, sizeof(line), "EXP %lu%s", (unsigned long)d.exp,
                     entry.generation == 2 ? "  Happiness stored" : "");
        }
        text_line(tft, 5, 191, line, MUTED, 1);
    }

    button(tft, 0, 70, "Back", PANEL);
    button(tft, 70, 90, tab == 0 ? "Moves" : "Stats", 0x0190);
    const char *action = busy ? "Working..." :
                         (ready_to_trade ? "Trade now" : "Send to GBA");
    button(tft, 160, 160, action, busy ? PANEL : GO);
    if (status && *status) {
        tft.fillRect(0, 201, 320, 4, BG);
        tft.setTextColor(MUTED, BG); tft.setTextDatum(MC_DATUM);
        tft.drawString(status, 160, 200, 1);
    }
}
