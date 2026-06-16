#include "gb_link.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "spi.pio.h"
#include "pio_spi.h"

// Same pin mapping and SPI config as the proven Path A firmware:
//   SCK  = GPIO0, our serial-out (MOSI) = GPIO2, cartridge serial-out -> our
//   serial-in (MISO) = GPIO1. CPHA=1, CPOL=1, clkdiv tuned for ~8 kHz.
#define PIN_SCK 0
#define PIN_SIN 1   // cartridge SO -> RP2040 MISO
#define PIN_SOUT 2  // RP2040 MOSI -> cartridge SI

static pio_spi_inst_t spi = {.pio = pio0, .sm = 0};
static uint spi_prog_offs;

// Gen 1/2 cartridge link: ~8 kHz. Gen 3 GBA link: the *same* program at
// 1/128th the divisor (~1 MHz) — the exact clkdiv Lorenzooone's reconfigurable
// firmware uses for the GBA's 4-byte SIO32 transfers.
#define GB_LINK_CLKDIV_GEN12 4058.838f
#define GB_LINK_CLKDIV_GEN3  (4058.838f / 128.0f)

void gb_link_init(void) {
    spi_prog_offs = pio_add_program(spi.pio, &spi_cpha1_program);
    pio_spi_init(spi.pio, spi.sm, spi_prog_offs, 8, GB_LINK_CLKDIV_GEN12, 1, 1,
                 PIN_SCK, PIN_SOUT, PIN_SIN);
}

void gb_link_set_gen3(bool on) {
    // Re-init the state machine at the right clock; the PIO program is shared.
    pio_sm_set_enabled(spi.pio, spi.sm, false);
    pio_spi_init(spi.pio, spi.sm, spi_prog_offs, 8,
                 on ? GB_LINK_CLKDIV_GEN3 : GB_LINK_CLKDIV_GEN12, 1, 1,
                 PIN_SCK, PIN_SOUT, PIN_SIN);
}

uint8_t gb_link_swap(uint8_t out) {
    sleep_us(GB_LINK_PACING_US);
    uint8_t in = 0;
    pio_spi_write8_read8_blocking(&spi, &out, &in, 1);
    return in;
}

uint32_t gb_link_swap32(uint32_t out) {
    uint8_t tx[4] = { (uint8_t)(out >> 24), (uint8_t)(out >> 16),
                      (uint8_t)(out >> 8), (uint8_t)out };
    uint8_t rx[4] = { 0, 0, 0, 0 };
    pio_spi_write8_read8_blocking(&spi, tx, rx, 4);
    return ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) |
           ((uint32_t)rx[2] << 8) | rx[3];
}
