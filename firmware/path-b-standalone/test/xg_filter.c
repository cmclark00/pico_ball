// Host-side filter around cross_gen.c, used by tools/test_firmware_host.py to
// check the firmware's Gen 1 <-> Gen 2 conversion against the trade engine.
//
// stdin:  one request per line, "12 <hex Gen 1 record>" or "21 <hex Gen 2 record>"
// stdout: one line per request, the converted record as hex, or "FAIL".
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cross_gen.h"

#define G1_REC_LEN 66
#define G2_REC_LEN 70
#define REC_MAX 128

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(void) {
    char line[4 + 2 * REC_MAX + 8];
    while (fgets(line, sizeof(line), stdin)) {
        uint8_t in[REC_MAX] = {0}, out[REC_MAX] = {0};
        const char *h = line + 3;
        int n = 0;
        while (n < REC_MAX && hexval(h[0]) >= 0 && hexval(h[1]) >= 0) {
            in[n++] = (uint8_t)((hexval(h[0]) << 4) | hexval(h[1]));
            h += 2;
        }
        bool to_g2 = strncmp(line, "12 ", 3) == 0;
        bool to_g1 = strncmp(line, "21 ", 3) == 0;
        bool ok = false;
        int out_len = 0;
        if (to_g2 && n >= G1_REC_LEN) { ok = conv_g1_to_g2_rec(in, out); out_len = G2_REC_LEN; }
        if (to_g1 && n >= G2_REC_LEN) { ok = conv_g2_to_g1_rec(in, out); out_len = G1_REC_LEN; }
        if (!ok) { puts("FAIL"); continue; }
        for (int i = 0; i < out_len; i++) printf("%02X", out[i]);
        putchar('\n');
    }
    return 0;
}
