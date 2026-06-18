// pccs_convert -- thin CLI around the Pokemon Community Conversion Standard
// (third_party/PCCS, MIT, GearsProgress). Reads one Gen 1/2 mon on stdin and
// writes the converted Gen 3 .pk3 box (80 bytes, encrypted) to stdout as
// space-separated hex.
//
// stdin layout (raw bytes), matching host/picovault/savedata.py's records but
// reordered to PCCS's loadData(lang, data, nickname, OT, idx):
//   gen 1: box[33] + nickname[11] + OT[11]
//   gen 2: box[32] + nickname[11] + OT[11]
// The species index PCCS needs is box[0] (Gen 1 internal index / Gen 2 dex no.),
// exactly as PCCS's own test harness passes it.
//
// Usage:  pccs_convert <1|2> [--no-sanitize-mythicals]
// Exit:   0 ok, 1 conversion rejected the mon, 2 bad args/short input.
#include <iostream>
#include <iterator>
#include <vector>
#include <cstring>
#include <cstdlib>
#include "PokeBox.h"

int main(int argc, char **argv) {
    if (argc < 2) { std::cerr << "usage: pccs_convert <1|2> [--no-sanitize-mythicals]\n"; return 2; }
    int gen = std::atoi(argv[1]);
    if (gen != 1 && gen != 2) { std::cerr << "gen must be 1 or 2\n"; return 2; }
    bool sanitize = true;
    for (int i = 2; i < argc; i++)
        if (!std::strcmp(argv[i], "--no-sanitize-mythicals")) sanitize = false;

    std::vector<unsigned char> in((std::istreambuf_iterator<char>(std::cin)),
                                   std::istreambuf_iterator<char>());
    int boxLen = (gen == 1) ? 33 : 32;
    int need = boxLen + 11 + 11;
    if ((int)in.size() < need) {
        std::cerr << "short input: got " << in.size() << " need " << need << "\n";
        return 2;
    }

    byte data[33] = {0}, nick[11] = {0}, ot[11] = {0};
    for (int i = 0; i < boxLen; i++) data[i] = in[i];
    for (int i = 0; i < 11; i++) nick[i] = in[boxLen + i];
    for (int i = 0; i < 11; i++) ot[i]   = in[boxLen + 11 + i];
    byte species = data[0];

    PokemonTables table;
    Gen3Pokemon g3(&table);
    bool ok = false;
    if (gen == 1) {
        Gen1Pokemon p(&table);
        p.loadData(ENGLISH, data, nick, ot, species);
        ok = p.convertToGen3(&g3, sanitize);
    } else {
        Gen2Pokemon p(&table);
        p.loadData(ENGLISH, data, nick, ot, species);
        ok = p.convertToGen3(&g3, sanitize);
    }
    if (!ok) { std::cerr << "conversion failed (invalid/unsupported mon)\n"; return 1; }
    std::cout << g3.printDataArray(true) << "\n";
    return 0;
}
