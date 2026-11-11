#include "mon_identity.h"
#include <string.h>

typedef struct { uint8_t ot_id, dvs, min_len; } id_layout_t;

// Offsets within the Gen 1 / Gen 2 party struct (the start of every record).
static const id_layout_t G1 = {0x0C, 0x1B, 0x1D};
static const id_layout_t G2 = {0x06, 0x15, 0x17};
#define G3_ID_LEN 8   // PID (4) + OT ID (4)

bool mon_same_individual(int gen, const uint8_t *a, uint16_t alen,
                         const uint8_t *b, uint16_t blen) {
    if (gen == 3) {
        if (alen >= G3_ID_LEN && blen >= G3_ID_LEN) return memcmp(a, b, G3_ID_LEN) == 0;
    } else {
        const id_layout_t *l = (gen == 2) ? &G2 : &G1;
        if (alen >= l->min_len && blen >= l->min_len)
            return memcmp(a + l->ot_id, b + l->ot_id, 2) == 0 &&
                   memcmp(a + l->dvs, b + l->dvs, 2) == 0;
    }
    return alen == blen && memcmp(a, b, alen) == 0;
}
