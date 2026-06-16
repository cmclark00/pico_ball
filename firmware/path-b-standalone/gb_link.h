// Game Boy link layer: RP2040 as SPI master (drives the clock), exactly like
// the proven Path A firmware. One call exchanges one byte with the cartridge.
#ifndef GB_LINK_H
#define GB_LINK_H
#include <stdint.h>
#include <stdbool.h>

// Microseconds to wait before each byte exchange, giving the cartridge time to
// load its next serial byte. ~20 ms matches the proven Path A host pacing.
#ifndef GB_LINK_PACING_US
#define GB_LINK_PACING_US 20000
#endif

void gb_link_init(void);

// Send `out` to the cartridge and return the byte it sent back. Blocks for the
// pacing delay plus the ~1 ms transfer. (Gen 1/2: ~8 kHz, 1 byte.)
uint8_t gb_link_swap(uint8_t out);

// Gen 3 (GBA "SIO32") uses the *same* PIO program at a faster clock, clocking
// 4 bytes back-to-back with caller-controlled pacing between groups — exactly
// like Lorenzooone's reconfigurable firmware. Switch the link clock with this
// before/after the Gen 3 multiboot + trade, then restore Gen 1/2 mode.
void gb_link_set_gen3(bool on);

// Exchange one 32-bit word (sent MSB-first / big-endian, like host send_byte).
// No internal pacing — the caller does busy_wait_us(us_between) between words.
uint32_t gb_link_swap32(uint32_t out);

#endif // GB_LINK_H
