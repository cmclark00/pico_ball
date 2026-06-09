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

void gb_link_init(void) {
    uint offs = pio_add_program(spi.pio, &spi_cpha1_program);
    pio_spi_init(spi.pio, spi.sm, offs, 8, 4058.838f, 1, 1, PIN_SCK, PIN_SOUT,
                 PIN_SIN);
}

uint8_t gb_link_swap(uint8_t out) {
    sleep_us(GB_LINK_PACING_US);
    uint8_t in = 0;
    pio_spi_write8_read8_blocking(&spi, &out, &in, 1);
    return in;
}
