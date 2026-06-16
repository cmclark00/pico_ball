// On-device Gen 3 trade-partner capture — C port of webui/gen3_trade.js (the
// buffered 896-byte section exchange from rse_sp_trading.py read_section). Runs
// over the Gen 3 SIO32 link after the GBA has been multibooted into Gen3-to-GenX
// and is sitting on its Gen 3 trade screen.
#ifndef GEN3_TRADE_H
#define GEN3_TRADE_H
#include <stdint.h>

#define GEN3_PK3_LEN 0x64   // 100-byte party struct == a .pk3 record

// Present the baked partner party, read the cartridge's party, and write up to
// `max_records` captured 100-byte .pk3 records into `records`. `pacing_us` is the
// delay between SIO32 words. Returns the number of Pokémon captured, or -1 on
// failure. Assumes the link is already in Gen 3 mode.
int gen3_capture_party(uint32_t pacing_us, uint8_t records[][GEN3_PK3_LEN],
                       int max_records);

// Extract the Gen 3 internal species id from a 100-byte .pk3 record (the dex key
// for storage). Decrypts only the one word it needs.
uint16_t gen3_species(const uint8_t *rec);

// Inject a stored Pokémon (its 100-byte .pk3 and species) back into a cartridge:
// present it as a one-mon party and run the trade-commit handshake. The mon the
// cartridge trades back is written to `received` (+ *recv_species) so the caller
// can vault it. Returns 1 = committed, 0 = the GBA side cancelled/declined,
// -1 = failed/timed out. Assumes the link is in Gen 3 mode and the GBA is on the
// Gen3-to-GenX trade screen.
int gen3_inject_mon(uint32_t pacing_us, const uint8_t *pk3, uint16_t our_species,
                    uint8_t received[GEN3_PK3_LEN], uint16_t *recv_species);

#endif // GEN3_TRADE_H
