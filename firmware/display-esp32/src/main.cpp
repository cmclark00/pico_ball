#include <Arduino.h>
#include <TFT_eSPI.h>

#include "protocol.h"
#include "species_names.h"
#include "decoder.h"
#include "ui_views.h"
#include "interaction.h"

namespace {
constexpr int VAULT_RX_PIN = 32;  // E32R32P I2C connector SDA pin
constexpr int VAULT_TX_PIN = 25;  // E32R32P I2C connector SCL pin
constexpr int ROWS_PER_PAGE = 6;
constexpr int LIST_TOP = 35;
constexpr int ROW_HEIGHT = 27;
constexpr int BUTTON_TOP = 205;

TFT_eSPI tft;
HardwareSerial vault_uart(2);
VaultProtocol protocol;
char line_buffer[320];
size_t line_length = 0;
int page = 0;
int selected_slot = -1;
uint32_t last_refresh = 0;
DisplayScreen current_screen = DisplayScreen::Browser;
PokemonDetails selected_details{};
int detail_tab = 0;

TradeState trade_state = TradeState::Idle;
char status_text[64] = "Starting...";

void set_status(const char *text) {
    snprintf(status_text, sizeof(status_text), "%s", text ? text : "");
}

void draw_ui() {
    const VaultSnapshot &vault = protocol.snapshot();
    int max_page = vault.count == 0 ? 0 : (int)(vault.count - 1) / ROWS_PER_PAGE;
    if (page > max_page) page = max_page;
    if (current_screen == DisplayScreen::Detail && selected_slot >= 0 &&
        selected_slot < (int)vault.count &&
        decode_pokemon(*vault.entries[selected_slot], &selected_details)) {
        draw_pokemon_detail(tft, *vault.entries[selected_slot], selected_details,
                            detail_tab, status_text,
                            trade_state == TradeState::WaitForGba,
                            !trade_action_allowed(trade_state));
    } else {
        current_screen = DisplayScreen::Browser;
        draw_vault_browser(tft, vault, page, status_text);
    }
}

void request_refresh() {
    set_status("Reading vault...");
    last_refresh = millis();
    // Finish the relatively slow LCD/sprite redraw before the Pico starts its
    // continuous dump. Drawing after sending used to overflow the UART RX FIFO.
    draw_ui();
    vault_uart.print("d\n");
}

void start_trade() {
    if (!trade_action_allowed(trade_state)) {
        set_status("Operation in progress...");
        draw_ui();
        return;
    }
    const VaultSnapshot &vault = protocol.snapshot();
    if (trade_state == TradeState::WaitForGba) {
        if (selected_slot < 0 || selected_slot >= (int)vault.count) return;
        char command[20];
        snprintf(command, sizeof(command), "i %d\n", selected_slot);
        vault_uart.print(command);
        trade_state = TradeState::Injecting;
        set_status("Trading - choose give-away on GBA");
        draw_ui();
        return;
    }
    if (selected_slot < 0 || selected_slot >= (int)vault.count) {
        set_status("Tap a Pokemon first");
        draw_ui();
        return;
    }
    if (vault.entries[selected_slot]->generation != 3) {
        set_status("Gen 3 records only for this trade");
        draw_ui();
        return;
    }
    // Select Gen 3, then multiboot Gen3-to-GenX. The Pico ignores line endings
    // between commands and defaults to the known-good pacing when no number follows m.
    vault_uart.print("3\nm\n");
    trade_state = TradeState::Multibooting;
    set_status("Multibooting GBA at BIOS screen...");
    draw_ui();
}

void handle_protocol_line(const char *line) {
    bool protocol_line = protocol.consume(line);
    if (protocol_line) last_refresh = millis();
    if (protocol_line && protocol.snapshot().complete) {
        if (selected_slot >= (int)protocol.snapshot().count) {
            selected_slot = -1;
            current_screen = DisplayScreen::Browser;
        }
        set_status(protocol.snapshot().count ? "Tap a Pokemon for details" : "Vault is empty");
        draw_ui();
        return;
    }
    if (protocol_line && !protocol.snapshot().receiving && !protocol.snapshot().complete &&
        protocol.snapshot().declared_count >= 0) {
        char incomplete[64];
        snprintf(incomplete, sizeof(incomplete), "Incomplete %u/%d - tap Refresh",
                 (unsigned)protocol.snapshot().count,
                 protocol.snapshot().declared_count);
        set_status(incomplete);
        draw_ui();
        return;
    }
    TradeState result_source = trade_state;
    TradeResult result = parse_trade_result(line);
    trade_state = state_after_result(trade_state, result);
    if (result == TradeResult::MultibootOk && result_source == TradeState::Multibooting) {
        set_status("On GBA: open Gen 3 trade, then tap Trade now");
        draw_ui();
    } else if (result == TradeResult::MultibootFail &&
               result_source == TradeState::Multibooting) {
        set_status("Multiboot failed - check cable / BIOS screen");
        draw_ui();
    } else if (result == TradeResult::InjectOk && result_source == TradeState::Injecting) {
        set_status("Trade complete; refreshing vault...");
        draw_ui();
        delay(150);
        request_refresh();
    } else if (result == TradeResult::InjectFail && result_source == TradeState::Injecting) {
        set_status("Trade failed or was cancelled");
        draw_ui();
    }
}

void poll_vault_uart() {
    while (vault_uart.available()) {
        char c = (char)vault_uart.read();
        if (c == '\n') {
            line_buffer[line_length] = '\0';
            if (line_length) handle_protocol_line(line_buffer);
            line_length = 0;
        } else if (c != '\r') {
            if (line_length + 1 < sizeof(line_buffer)) line_buffer[line_length++] = c;
            else line_length = 0;
        }
    }
}

void wait_for_touch_release() {
    uint16_t x, y;
    uint32_t deadline = millis() + 1000;
    while (millis() < deadline && tft.getTouch(&x, &y, 600)) delay(10);
}

void handle_touch() {
    uint16_t x = 0, y = 0;
    if (!tft.getTouch(&x, &y, 600)) return;
    wait_for_touch_release();
    if (interaction_locked(trade_state)) {
        set_status("Operation in progress...");
        draw_ui();
        return;
    }
    const VaultSnapshot &vault = protocol.snapshot();
    if (current_screen == DisplayScreen::Detail) {
        if ((y < 32 && x < 80) || (y >= BUTTON_TOP && x < 70)) {
            current_screen = DisplayScreen::Browser;
            trade_state = TradeState::Idle;
            set_status("Tap a Pokemon for details");
            draw_ui();
        } else if (y >= BUTTON_TOP && x < 160) {
            detail_tab ^= 1;
            draw_ui();
        } else if (y >= BUTTON_TOP) {
            start_trade();
        }
        return;
    }
    if (y >= LIST_TOP && y < LIST_TOP + ROWS_PER_PAGE * ROW_HEIGHT) {
        int index = page * ROWS_PER_PAGE + (y - LIST_TOP) / ROW_HEIGHT;
        if (index < (int)vault.count) {
            selected_slot = index;
            current_screen = DisplayScreen::Detail;
            detail_tab = 0;
            trade_state = TradeState::Idle;
            set_status("");
            draw_ui();
        }
    } else if (y >= BUTTON_TOP) {
        if (x < 70) {
            if (page > 0) page--;
            draw_ui();
        } else if (x < 140) {
            int max_page = vault.count == 0 ? 0 : (int)(vault.count - 1) / ROWS_PER_PAGE;
            if (page < max_page) page++;
            draw_ui();
        } else {
            request_refresh();
        }
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    vault_uart.setRxBufferSize(8192);
    vault_uart.begin(115200, SERIAL_8N1, VAULT_RX_PIN, VAULT_TX_PIN);
    tft.init();
    tft.setRotation(1);
    // Vendor's rotation-1 baseline. Run the vendor Touch_calibrate example and
    // replace this array if a particular panel needs tighter calibration.
    uint16_t calibration[5] = {366, 3573, 257, 3590, 3};
    tft.setTouch(calibration);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    draw_ui();
    delay(350);
    request_refresh();
}

void loop() {
    poll_vault_uart();
    handle_touch();
    if (protocol.snapshot().receiving && millis() - last_refresh > 15000) {
        protocol.reset();
        set_status("No Pico response - check TX/RX/GND");
        draw_ui();
    }
    delay(8);
}
