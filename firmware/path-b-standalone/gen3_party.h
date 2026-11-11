// Gen 3 box record -> party record.
//
// A Pokémon in a Gen 3 PC box is 80 bytes; in the party (and so in a link trade)
// it is 100: the same 80 plus status, level, current HP and the six calculated
// stats. The Gen 1/2 -> Gen 3 conversion produces the 80-byte form, so before it
// can be traded in we have to work those extra fields out, the way the game does
// when it withdraws a Pokémon from a box. Pure logic (no SDK), host-tested.
#ifndef GEN3_PARTY_H
#define GEN3_PARTY_H
#include <stdint.h>
#include <stdbool.h>

#define GEN3_BOX_LEN   80
#define GEN3_PARTY_LEN 100

// Build the 100-byte party record for an 80-byte (encrypted) box record: fully
// healed, no status, no mail, level from its experience, stats from its species,
// IVs, EVs, level and nature. Returns false — leaving `party` untouched — if the
// record's checksum is wrong, it is an Egg, or its species is outside 1..251
// (the only species a converted Gen 1/2 Pokémon can be).
bool gen3_party_from_box(const uint8_t box[GEN3_BOX_LEN], uint8_t party[GEN3_PARTY_LEN]);

#endif // GEN3_PARTY_H
