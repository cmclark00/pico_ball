// On-device GBA multiboot of the baked Gen3-to-GenX image — the standalone
// equivalent of webui/multiboot.js / host gen3_boot.py, but the board itself is
// the uploader (no PC). Uses the Gen 3 SIO32 link (gb_link_set_gen3 + swap32).
#ifndef GEN3_MULTIBOOT_H
#define GEN3_MULTIBOOT_H
#include <stdbool.h>
#include <stdint.h>

// Multiboot any GBA multiboot image into a GBA sitting at its BIOS/boot screen.
// Assumes the link is already in Gen 3 mode. `pacing_us` is the delay between
// SIO32 words (the host's reconfigurable firmware uses 36; on-device a tight loop
// may need more so the GBA doesn't drop words). Returns true on a successful
// handshake + CRC confirmation (the image then runs on the GBA).
bool gba_multiboot(const uint8_t *image, uint32_t fsize, uint32_t pacing_us);

// Multiboot the baked Gen3-to-GenX image (for Gen 3 capture/inject).
bool gen3_multiboot(uint32_t pacing_us);

// Multiboot the baked Poke Transporter GB image (for the Gen 1/2 -> Gen 3
// transfer onto a real Gen 3 cartridge swapped into the GBA after it loads).
bool ptgb_multiboot(uint32_t pacing_us);

#endif // GEN3_MULTIBOOT_H
