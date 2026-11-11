#ifndef CROSS_GEN_H
#define CROSS_GEN_H
#include <stdint.h>
#include <stdbool.h>

// Convert a Gen 1 mon record (66 bytes: 44-byte struct + 11 OT + 11 nick)
// into a Gen 2 record (70 bytes: 48-byte struct + 11 OT + 11 nick).
// Sp. Atk / Sp. Def are recomputed from Gen 2's base stats.
// Returns false if the species byte isn't a real Gen 1 Pokémon.
bool conv_g1_to_g2_rec(const uint8_t *g1, uint8_t *g2);

// Convert a Gen 2 mon record (70 bytes; any trailing mail is dropped) into a
// Gen 1 record (66 bytes). Gen 2-only moves (id > 165) are removed and the
// remaining moves packed; Special is recomputed from Gen 1's base stat.
// Returns false if the species is Gen 2-exclusive (Pokédex > 151) or the mon
// knows no move that exists in Gen 1.
bool conv_g2_to_g1_rec(const uint8_t *g2, uint8_t *g1);

#endif // CROSS_GEN_H
