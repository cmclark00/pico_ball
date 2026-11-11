// See pccs_bridge.h. Mirrors tools/pccs_convert.cpp + host/picovault/pccs.py.
#include "pccs_bridge.h"

#include <new>
#include <string>
#include "PokeBox.h"

namespace {

// Vault record geometry: [party struct][OT name][nickname]. PCCS reads only the
// leading box part of the party struct.
const int NAME_LEN = 11;
const int G1_BOX_LEN = 33, G1_STRUCT_LEN = 0x2C;
const int G2_BOX_LEN = 32, G2_STRUCT_LEN = 0x30;

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// PCCS's tables are a few KB — far too big for the RP2040's default 2 KB stack —
// so the working objects live in static storage. Conversions are never
// concurrent (single core, called from the main loop only).
PokemonTables g_tables;

} // namespace

extern "C" bool pccs_available(void) { return true; }

extern "C" bool pccs_convert_record(int gen, const uint8_t *rec, uint16_t rec_len,
                                    uint8_t box[PCCS_BOX_LEN]) {
    if (gen != 1 && gen != 2) return false;
    const int box_len = (gen == 1) ? G1_BOX_LEN : G2_BOX_LEN;
    const int struct_len = (gen == 1) ? G1_STRUCT_LEN : G2_STRUCT_LEN;
    if (rec_len < struct_len + 2 * NAME_LEN) return false;

    static byte data[G1_BOX_LEN], nick[NAME_LEN], ot[NAME_LEN];
    for (int i = 0; i < G1_BOX_LEN; i++) data[i] = (i < box_len) ? rec[i] : 0;
    for (int i = 0; i < NAME_LEN; i++) {
        ot[i] = rec[struct_len + i];
        nick[i] = rec[struct_len + NAME_LEN + i];
    }
    const byte species = data[0];   // Gen 1 internal index / Gen 2 Pokédex number

    // Build the working objects in static storage with placement new. They must
    // not be copy-assigned: each one keeps pointers into its own arrays.
    alignas(Gen3Pokemon) static unsigned char g3_buf[sizeof(Gen3Pokemon)];
    alignas(Gen1Pokemon) static unsigned char g1_buf[sizeof(Gen1Pokemon)];
    alignas(Gen2Pokemon) static unsigned char g2_buf[sizeof(Gen2Pokemon)];
    Gen3Pokemon *g3p = new (g3_buf) Gen3Pokemon(&g_tables);
    Gen3Pokemon &g3 = *g3p;
    bool ok;
    if (gen == 1) {
        Gen1Pokemon *p = new (g1_buf) Gen1Pokemon(&g_tables);
        p->loadData(ENGLISH, data, nick, ot, species);
        ok = p->convertToGen3(&g3, true);
        p->~Gen1Pokemon();
    } else {
        Gen2Pokemon *p = new (g2_buf) Gen2Pokemon(&g_tables);
        p->loadData(ENGLISH, data, nick, ot, species);
        ok = p->convertToGen3(&g3, true);
        p->~Gen2Pokemon();
    }
    if (!ok) { g3.~Gen3Pokemon(); return false; }

    // printDataArray(true) finalises the record (substructure order, checksum,
    // encryption) and is the only public way to read it out — the same call the
    // host converter uses. It yields 80 space-separated hex bytes.
    const std::string hex = g3.printDataArray(true);
    int n = 0;
    for (size_t i = 0; i + 1 < hex.size() && n < PCCS_BOX_LEN; i++) {
        int hi = hex_nibble(hex[i]), lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) continue;
        box[n++] = (uint8_t)((hi << 4) | lo);
        i++;
    }
    g3.~Gen3Pokemon();
    return n == PCCS_BOX_LEN;
}
