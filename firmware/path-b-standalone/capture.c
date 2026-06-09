#include "capture.h"
#include "gb_link.h"
#include <string.h>

#define NO_INPUT 0xFE

#define MAX_HANDSHAKE_SWAPS 4000   // ~84 s at ~21 ms/byte
#define MAX_SYNC_SWAPS 600

static bool in_set(uint8_t v, const uint8_t *set, int n) {
    for (int i = 0; i < n; i++)
        if (v == set[i]) return true;
    return false;
}

// Walk a fixed list of bytes to send, advancing when the cartridge replies with
// an accepted value. Returns false on timeout instead of looping forever.
static bool send_predefined(const uint8_t *send, const uint8_t *const *sets,
                            const int *set_lens, int steps, int max_swaps) {
    int idx = 0, swaps = 0;
    while (idx < steps) {
        uint8_t recv = gb_link_swap(send[idx]);
        if (in_set(recv, sets[idx], set_lens[idx])) idx++;
        if (++swaps >= max_swaps) return false;
    }
    return true;
}

static bool enter_room(int gen) {
    if (gen == 2) {  // GSC enter_room_states
        static const uint8_t send[5] = {0x01, 0x61, 0xD1, 0x00, 0xFE};
        static const uint8_t a[] = {0x61}, b[] = {0xD1}, c[] = {0x00}, d[] = {0xFE};
        const uint8_t *sets[5] = {a, b, c, d, d};
        const int lens[5] = {1, 1, 1, 1, 1};
        return send_predefined(send, sets, lens, 5, MAX_HANDSHAKE_SWAPS);
    }
    static const uint8_t send[4] = {0x01, 0x60, 0xD0, 0xD4};  // RBY
    static const uint8_t idx[] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x6F};
    static const uint8_t room[] = {0xD0, 0xD1, 0xD2, 0xD3, 0xD4};
    const uint8_t *sets[4] = {idx, room, room, idx};
    const int lens[4] = {7, 5, 5, 7};
    return send_predefined(send, sets, lens, 4, MAX_HANDSHAKE_SWAPS);
}

static bool sit_to_table(int gen) {
    if (gen == 2) {  // GSC start_trading_states
        static const uint8_t send[3] = {0x75, 0x75, 0x76};
        static const uint8_t a[] = {0x75}, b[] = {0x00}, c[] = {0xFD};
        const uint8_t *sets[3] = {a, b, c};
        const int lens[3] = {1, 1, 1};
        return send_predefined(send, sets, lens, 3, MAX_HANDSHAKE_SWAPS);
    }
    static const uint8_t send[2] = {0x60, 0x60};  // RBY
    static const uint8_t a[] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x6F}, b[] = {0xFD};
    const uint8_t *sets[2] = {a, b};
    const int lens[2] = {7, 1};
    return send_predefined(send, sets, lens, 2, MAX_HANDSHAKE_SWAPS);
}

// Buffered exchange of one section: sync onto its preamble byte, then stream our
// baked bytes while recording the cartridge's `len` bytes into out[0..len-1].
static bool capture_section(int index, uint8_t starter, bool sync, uint8_t preamble,
                            const uint8_t *baked, int len, uint8_t *out) {
    uint8_t next = sync ? NO_INPUT : starter;
    uint8_t recv = next;
    int swaps = 0;

    while (recv != starter) {  // sync onto the preamble byte
        recv = gb_link_swap(next);
        if (++swaps >= MAX_SYNC_SWAPS) return false;
    }
    next = starter;

    if (index == 0) {  // random section: bounded preamble consume
        for (int i = 0; i < preamble + 1; i++) {
            next = gb_link_swap(next);
            if (next != starter) break;
        }
    } else {
        swaps = 0;
        while (next == starter) {
            next = gb_link_swap(next);
            if (++swaps >= MAX_SYNC_SWAPS) return false;
        }
    }

    out[0] = next;
    for (int i = 0; i < len - 1; i++) out[i + 1] = gb_link_swap(baked[i]);
    gb_link_swap(baked[len - 1]);  // last byte; its reply isn't part of the data
    return true;
}

gb_capture_result_t capture_run(const gen_profile_t *p, uint8_t *out) {
    if (!enter_room(p->gen)) return GB_CAP_NO_GAME;
    if (!sit_to_table(p->gen)) return GB_CAP_NO_GAME;

    int off = 0;
    for (int s = 0; s < p->n_sections; s++) {
        int len = p->lens[s];
        if (!capture_section(s, p->starters[s], p->syncs[s], p->preamble[s],
                             p->baked[s], len, out + off))
            return GB_CAP_SECTION;
        off += len;
    }
    return GB_CAP_OK;
}

// Restore the real 0xFE bytes the trade protocol patched out of the party data.
// Same patch scheme for RBY and GSC party sections (base 0x13, start 7, 2 sets).
static void apply_patches(uint8_t *data, int data_len, const uint8_t *patch, int patch_len) {
    int num = 2, base = 0x13, start = 7, i = 0;
    while (num > 0 && start + i < patch_len) {
        uint8_t read_pos = patch[start + i];
        i++;
        if (read_pos == 0xFF) { num--; base += 0xFC; }
        else if (read_pos > 0 && read_pos + base < data_len) data[read_pos + base - 1] = 0xFE;
    }
}

int extract_mons(const gen_profile_t *p, uint8_t *raw,
                 uint8_t species[6], uint8_t data[6][DEX_MON_MAX], uint16_t lens[6]) {
    uint8_t *sec1 = raw + p->lens[0];               // party section
    uint8_t *sec2 = sec1 + p->lens[1];              // patch list
    apply_patches(sec1, p->lens[1], sec2, p->lens[2]);

    int count = sec1[p->count_pos];
    if (count < 1 || count > 6) return 0;
    uint16_t rec_len = p->mon_len + 2 * p->name_len;
    for (int i = 0; i < count; i++) {
        uint8_t *st = sec1 + p->mon_pos + i * p->mon_len;
        uint8_t *ot = sec1 + p->ot_pos + i * p->name_len;
        uint8_t *nk = sec1 + p->nick_pos + i * p->name_len;
        species[i] = st[0];
        memcpy(data[i], st, p->mon_len);
        memcpy(data[i] + p->mon_len, ot, p->name_len);
        memcpy(data[i] + p->mon_len + p->name_len, nk, p->name_len);
        lens[i] = rec_len;
    }
    return count;
}
