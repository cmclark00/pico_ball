// Linked instead of pccs_bridge.cpp when PCCS isn't available at build time
// (tools/gen_pccs_firmware.py found no tables): the firmware still builds, and
// reports the Gen 1/2 -> Gen 3 conversion as unavailable.
#include "pccs_bridge.h"

bool pccs_available(void) { return false; }

bool pccs_convert_record(int gen, const uint8_t *rec, uint16_t rec_len,
                         uint8_t box[PCCS_BOX_LEN]) {
    (void)gen; (void)rec; (void)rec_len; (void)box;
    return false;
}
