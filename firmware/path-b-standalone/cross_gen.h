#ifndef CROSS_GEN_H
#define CROSS_GEN_H
#include <stdint.h>
#include <stdbool.h>

// Convert a Gen 1 mon record (66 bytes: 44-byte struct + 11 OT + 11 nick)
// into a Gen 2 record (70 bytes: 48-byte struct + 11 OT + 11 nick).
// Returns false if the species has no Gen 2 equivalent.
bool conv_g1_to_g2_rec(const uint8_t *g1, uint8_t *g2);

// Convert a Gen 2 mon record (70 bytes) into a Gen 1 record (66 bytes).
// Returns false if the species is Gen 2-exclusive (Pokédex > 151 or no mapping).
// Gen 2-exclusive moves (index > 165) are replaced with no-move (0).
bool conv_g2_to_g1_rec(const uint8_t *g2, uint8_t *g1);

#endif // CROSS_GEN_H
