// Host-side filter around the on-device Gen 1/2 -> Gen 3 path, used by
// tools/test_firmware_host.py: pccs_bridge.cpp (PCCS) + gen3_party.c.
//
// stdin:  one request per line, "<gen> <hex vault record>"
// stdout: one line per request, the 100-byte Gen 3 party record as hex, or "FAIL".
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pccs_bridge.h"
extern "C" {
#include "gen3_party.h"
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main() {
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        uint8_t rec[256] = {0}, box[PCCS_BOX_LEN], party[GEN3_PARTY_LEN];
        int gen = line[0] - '0', n = 0;
        const char *h = line + 2;
        while (n < 256 && hexval(h[0]) >= 0 && hexval(h[1]) >= 0) {
            rec[n++] = (uint8_t)((hexval(h[0]) << 4) | hexval(h[1]));
            h += 2;
        }
        if (!pccs_convert_record(gen, rec, (uint16_t)n, box) ||
            !gen3_party_from_box(box, party)) {
            puts("FAIL");
            continue;
        }
        for (int i = 0; i < GEN3_PARTY_LEN; i++) printf("%02X", party[i]);
        putchar('\n');
    }
    return 0;
}
