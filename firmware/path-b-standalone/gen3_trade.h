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

#endif // GEN3_TRADE_H
