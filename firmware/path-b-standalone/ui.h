// LED + button UI for the screenless standalone vault. Uses the RP2040-Zero's
// on-board WS2812 RGB LED (GPIO16) and the BOOTSEL button as the single input.
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include <stdbool.h>

void ui_init(void);

void ui_color(uint8_t r, uint8_t g, uint8_t b);
void ui_idle(void);     // dim white: ready
void ui_armed(void);    // blue: waiting for / talking to a cartridge
void ui_ok(void);       // green: captured & stored
void ui_full(void);     // amber: captured but vault full
void ui_error(void);    // red: failed / no game

// True while the BOOTSEL button is held down (raw, undebounced).
bool ui_button_down(void);

#endif // UI_H
