#include "inject.h"
#include "capture.h"
#include "gb_link.h"
#include "storage.h"
#include "ui.h"
#include <string.h>

// -- Gen-specific selection/commit control bytes ---------------------------

#define NO_INPUT  0xFE
#define NO_DATA   0x00

// Per-generation trade-phase control bytes (Gen 1 uses 0x6x, Gen 2 uses 0x7x).
typedef struct {
    uint8_t choice_min;  // first_trade_index
    uint8_t choice_max;  // stop_trade
    uint8_t our_choice;  // first_trade_index (party slot 0)
    uint8_t accept;
    uint8_t decline;
    uint8_t stop;
} trade_ctrl_t;

static const trade_ctrl_t CTRL_G1 = {0x60, 0x6F, 0x60, 0x62, 0x61, 0x6F};
static const trade_ctrl_t CTRL_G2 = {0x70, 0x7F, 0x70, 0x72, 0x71, 0x7F};

static const trade_ctrl_t *ctrl_for(int gen) {
    return gen == 2 ? &CTRL_G2 : &CTRL_G1;
}

// -- Selection-phase helpers -----------------------------------------------

#define OPTION_CONFIRM  10    // consecutive identical bytes to confirm a choice
#define RESENDS_LIMIT   20
#define MAX_WAIT_SWAPS  20000

// Keep sending NO_INPUT until we see the same byte (in [lo, hi]) 10 times in a
// row. Returns that byte, or -1 on timeout.
// Pulses the LED every ~50 swaps (~1 s) so the user can see we're waiting.
static int wait_for_range(uint8_t lo, uint8_t hi) {
    int consec = 0;
    uint8_t last = 0;
    for (int i = 0; i < MAX_WAIT_SWAPS; i++) {
        uint8_t r = gb_link_swap(NO_INPUT);
        if (r >= lo && r <= hi) {
            if (r == last) {
                if (++consec >= OPTION_CONFIRM) return r;
            } else {
                consec = 1;
                last = r;
            }
        } else {
            consec = 0;
        }
        if ((i & 0x1F) == 0) ui_tick();  // pulse every ~32 swaps (~0.7 s)
    }
    return -1;
}

// Keep sending NO_INPUT until accept or decline appears 10 times in a row.
static int wait_for_accept_decline(const trade_ctrl_t *c) {
    int consec = 0;
    uint8_t last = 0;
    for (int i = 0; i < MAX_WAIT_SWAPS; i++) {
        uint8_t r = gb_link_swap(NO_INPUT);
        if (r == c->accept || r == c->decline) {
            if (r == last) {
                if (++consec >= OPTION_CONFIRM) return r;
            } else {
                consec = 1;
                last = r;
            }
        } else {
            consec = 0;
        }
        if ((i & 0x1F) == 0) ui_tick();
    }
    return -1;
}

// Send `byte` once, then keep resending until we get NO_DATA (0x00) back or
// hit the resend limit. Returns the last received byte.
static uint8_t wait_for_no_data(uint8_t byte) {
    uint8_t nxt = gb_link_swap(byte);
    for (int i = 0; i < RESENDS_LIMIT && nxt != NO_DATA; i++)
        nxt = gb_link_swap(byte);
    return nxt;
}

// If the last received byte was NO_DATA (0x00), drain NO_INPUT (0xFE) bytes.
static void drain_no_input_if(uint8_t nxt) {
    if (nxt != NO_DATA) return;
    for (int i = 0; i < 200 && nxt != NO_INPUT; i++)
        nxt = gb_link_swap(NO_INPUT);
}

// -- Party-section builders ------------------------------------------------

// Largest party section we ever build (Gen 2 = 444 bytes).
#define INJECT_SEC1_MAX GB_SEC1_MAX

// Splice the stored mon record into a copy of the baked section-1 template.
// This gives a valid 1-mon trade block with the actual Pokémon data.
static void build_sec1(const gen_profile_t *p, const uint8_t *mon_rec,
                       uint8_t *sec1) {
    memcpy(sec1, p->baked[1], p->lens[1]);
    // Species list: byte immediately after the count (count_pos+1).
    sec1[p->count_pos + 1] = mon_rec[0];
    // Mon struct, OT name, nickname in their canonical positions.
    memcpy(sec1 + p->mon_pos,  mon_rec,                       p->mon_len);
    memcpy(sec1 + p->ot_pos,   mon_rec + p->mon_len,          p->name_len);
    memcpy(sec1 + p->nick_pos, mon_rec + p->mon_len + p->name_len, p->name_len);
}

// Build the mail section (Gen 2 section 3) from the baked empty template, then
// splice in our mon's mail when it holds Mail. mon_rec carrying mail is laid out
// [struct][OT][nick][mail(mail_len)][sender(mail_sender_len)].
static void build_sec3(const gen_profile_t *p, const uint8_t *mon_rec,
                       uint16_t rec_len, uint8_t *sec3) {
    int idx = p->n_sections - 1;
    memcpy(sec3, p->baked[idx], p->lens[idx]);
    uint16_t base_len = p->mon_len + 2 * p->name_len;
    uint16_t with_mail = base_len + p->mail_len + p->mail_sender_len;
    if (p->mail_len && rec_len >= with_mail && gp_is_mail_item(mon_rec[1])) {
        // Our injected Pokémon is party slot 0, so it owns mail entry 0.
        memcpy(sec3 + p->mail_pos, mon_rec + base_len, p->mail_len);
        memcpy(sec3 + p->mail_sender_pos,
               mon_rec + base_len + p->mail_len, p->mail_sender_len);
    }
}

// Build the patch list (section 2) for the given section-1 data.
// Any 0xFE byte in sec1[0x13..] is replaced with 0xFF (no_input_alternative)
// and its 1-based position within its 0xFC-byte pass is recorded in sec2.
// sec2 must be pre-zeroed with length sec2_len.
static void build_sec2(uint8_t *sec1, int sec1_len,
                       uint8_t *sec2, int sec2_len) {
    int base  = 0x13;  // patch_set_base_pos (same for Gen 1 and Gen 2)
    int start = 7;     // patch_set_start_info_pos
    int wi    = 0;     // write index relative to start in sec2
    int j     = 0;     // position within current 0xFC-byte pass
    int passes = 2;

    while (passes > 0 && (start + wi) < sec2_len && (base + j) < sec1_len) {
        if (sec1[base + j] == NO_INPUT) {
            sec1[base + j] = 0xFF;
            sec2[start + wi++] = (uint8_t)(j + 1);
        }
        if (++j == 0xFC) {
            base += 0xFC;
            j = 0;
            if ((start + wi) < sec2_len)
                sec2[start + wi++] = 0xFF;  // pass sentinel
            passes--;
        }
    }
    if ((start + wi) < sec2_len)
        sec2[start + wi] = 0xFF;  // final sentinel
}

// -- Main injection entry point --------------------------------------------

gb_inject_result_t inject_run(const gen_profile_t *p,
                               const uint8_t *mon_rec, uint16_t rec_len) {
    uint16_t min_len = (uint16_t)(p->mon_len + 2 * p->name_len);
    if (rec_len < min_len) return GB_INJ_BAD_RECORD;

    // Build the outgoing party sections (sections 1 and 2 derived from mon_rec;
    // sections 0 and 3 reuse the baked templates unchanged).
    static uint8_t inject_sec1[INJECT_SEC1_MAX];
    static uint8_t inject_sec2[GB_SEC2_LEN];   // same size for Gen 1 and Gen 2
    static uint8_t inject_sec3[GB_SEC3_MAX];   // Gen 2 mail section

    build_sec1(p, mon_rec, inject_sec1);
    memset(inject_sec2, 0, p->lens[2]);
    build_sec2(inject_sec1, (int)p->lens[1], inject_sec2, (int)p->lens[2]);
    if (p->n_sections > 3) build_sec3(p, mon_rec, rec_len, inject_sec3);

    const uint8_t *inject_secs[4] = {
        p->baked[0],   // section 0: baked random seed
        inject_sec1,   // section 1: our inject party
        inject_sec2,   // section 2: patch list for our party
        (p->n_sections > 3) ? inject_sec3 : NULL,  // section 3: mail (Gen 2)
    };

    // Run the section exchange, recording the cartridge's party bytes.
    static uint8_t cart_rec[GB_CAPTURE_MAX];

    if (!trade_enter_room(p->gen))   return GB_INJ_NO_GAME;
    if (!trade_sit_to_table(p->gen)) return GB_INJ_NO_GAME;

    int off = 0;
    for (int s = 0; s < p->n_sections; s++) {
        int len = p->lens[s];
        if (!trade_capture_section(s, p->starters[s], p->syncs[s],
                                   p->preamble[s], inject_secs[s],
                                   len, cart_rec + off))
            return GB_INJ_SECTION;
        off += len;
    }

    // Selection/commit phase.
    const trade_ctrl_t *c = ctrl_for(p->gen);

    // 1. Wait for the cartridge's Pokémon choice (0x6x or 0x7x depending on gen).
    int choice = wait_for_range(c->choice_min, c->choice_max);
    if (choice < 0)        return GB_INJ_TIMEOUT;
    if (choice == c->stop) return GB_INJ_CANCELLED;
    int given_idx = choice - c->choice_min;  // their party slot index

    // 2. Send our choice: party slot 0 (the injected mon).
    uint8_t nxt = wait_for_no_data(c->our_choice);
    drain_no_input_if(nxt);

    // 3. Wait for the cartridge to accept or decline.
    int accepted = wait_for_accept_decline(c);
    if (accepted < 0)           return GB_INJ_TIMEOUT;
    if (accepted == c->decline) return GB_INJ_DECLINED;

    // 4. Send our acceptance.
    nxt = wait_for_no_data(c->accept);
    drain_no_input_if(nxt);

    // 5. Success handshake: wait for the game's success byte, then send the
    // first success value (choice_min) back, matching the engine's behavior.
    // The success range equals the choice range for both Gen 1 and Gen 2.
    int success = wait_for_range(c->choice_min, c->choice_max);
    if (success < 0) return GB_INJ_TIMEOUT;
    nxt = wait_for_no_data(c->choice_min);  // send first_trade_index as success byte
    drain_no_input_if(nxt);

    // Trade committed. Extract and store the Pokémon the cartridge gave away.
    if (given_idx >= 0) {
        uint8_t species[6];
        static uint8_t mondata[6][DEX_MON_MAX];
        uint16_t lens[6];
        int n = extract_mons(p, cart_rec, species, mondata, lens);
        if (given_idx < n)
            storage_put_mon(p->gen, species[given_idx],
                            mondata[given_idx], lens[given_idx]);
    }

    return GB_INJ_OK;
}
