// Decides whether two stored records are the same individual Pokémon, so the
// vault can update a recaptured mon in place without ever overwriting a
// different one of the same species. Pure logic (no flash / SDK), host-tested.
#ifndef MON_IDENTITY_H
#define MON_IDENTITY_H
#include <stdint.h>
#include <stdbool.h>

// Slot flags stored alongside a record.
#define MON_FLAG_EGG 0x80   // Gen 2: the party listed this mon as an Egg

// True if record `a` and record `b` (both of generation `gen`, same species for
// Gen 1/2) are the same individual:
//   Gen 1/2: same original-trainer ID and same DVs (neither ever changes).
//   Gen 3:   same personality value and trainer ID (the first 8 bytes).
// Records too short to carry those fields only match when byte-identical.
bool mon_same_individual(int gen, const uint8_t *a, uint16_t alen,
                         const uint8_t *b, uint16_t blen);

// Gen 1/2 records are matched within a species (an evolved recapture is kept as
// a new entry); Gen 3's personality value is unique enough to follow a mon
// through evolution, so species is not part of its key.
static inline bool mon_identity_needs_species(int gen) { return gen != 3; }

#endif // MON_IDENTITY_H
