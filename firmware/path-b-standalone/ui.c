#include "ui.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "ws2812.pio.h"

#define WS2812_PIN 16   // RP2040-Zero on-board RGB LED
static PIO ws_pio = pio1;
static uint ws_sm = 0;

void ui_init(void) {
    uint offset = pio_add_program(ws_pio, &ws2812_program);
    ws_sm = pio_claim_unused_sm(ws_pio, true);
    ws2812_program_init(ws_pio, ws_sm, offset, WS2812_PIN, 800000, false);
    ui_color(0, 0, 0);
}

void ui_color(uint8_t r, uint8_t g, uint8_t b) {
    // WS2812 wants GRB, MSB-first, in the top 24 bits of the 32-bit word.
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    pio_sm_put_blocking(ws_pio, ws_sm, grb << 8u);
}

static int tick_phase = 0;

void ui_idle(void)  { ui_color(6, 6, 6); }
void ui_armed(void) { tick_phase = 0; ui_color(0, 0, 40); }
void ui_ok(void)    { ui_color(0, 40, 0); }
void ui_full(void)  { ui_color(40, 24, 0); }
void ui_error(void) { ui_color(40, 0, 0); }

void ui_tick(void) {
    tick_phase ^= 1;
    // Alternate between a bright and dim blue so the pulse is clearly visible.
    ui_color(0, tick_phase ? 10 : 0, tick_phase ? 55 : 20);
}

// Read the BOOTSEL button at runtime by briefly driving the QSPI CS pin as an
// input. Must run from RAM (it disables XIP). Canonical RP2040 technique.
bool __no_inline_not_in_flash_func(ui_button_down)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i)
        ;
    bool down = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return down;
}
