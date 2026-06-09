// Game Boy link layer: RP2040 as SPI master (drives the clock), exactly like
// the proven Path A firmware. One call exchanges one byte with the cartridge.
#ifndef GB_LINK_H
#define GB_LINK_H
#include <stdint.h>

// Microseconds to wait before each byte exchange, giving the cartridge time to
// load its next serial byte. ~20 ms matches the proven Path A host pacing.
#ifndef GB_LINK_PACING_US
#define GB_LINK_PACING_US 20000
#endif

void gb_link_init(void);

// Send `out` to the cartridge and return the byte it sent back. Blocks for the
// pacing delay plus the ~1 ms transfer.
uint8_t gb_link_swap(uint8_t out);

#endif // GB_LINK_H
