// Host-side test of the standalone Gen 1/2 trade FSM (capture.c + inject.c).
//
// The real firmware modules are compiled unmodified and linked against a
// simulated cartridge: gb_link_swap() is answered by a small model of the game's
// side of the link (handshake, section stream, trade menu), so capture_run() and
// inject_run() run end to end without hardware. Run via tools/test_firmware_host.py.
//
// What the cartridge model does NOT prove: link timing and the exact handshake
// bytes of a real game — those come from the upstream engine and hardware runs.
// It does pin down everything on our side of the wire: section framing, the
// 0xFE patch lists in both directions, party splitting, mail, the commit
// handshake (incl. Gen 2's Mew/Celebi success byte), Gen 2 Eggs, vaulting the
// mon we get, and the rule that decides when two records are the same Pokémon.
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture.h"
#include "inject.h"
#include "gen_profile.h"
#include "mon_identity.h"
#include "gb_link.h"
#include "storage.h"
#include "ui.h"

#define NO_INPUT 0xFE
#define NO_DATA  0x00
#define PATCH_BASE  0x13   // first patched byte within the party section
#define PATCH_START 7      // first patch-list entry within the patch section
#define PATCH_PART  0xFC   // bytes covered by each of the two patch-list parts

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// --- storage stub ------------------------------------------------------------
static struct {
    int calls, gen, species;
    uint8_t flags;
    uint8_t data[DEX_MON_MAX];
    uint16_t len;
    bool accept;
} store;

bool storage_put_mon(int gen, int species, uint8_t flags,
                     const uint8_t *data, uint16_t len) {
    store.calls++;
    store.gen = gen;
    store.species = species;
    store.flags = flags;
    store.len = len;
    memcpy(store.data, data, len);
    return store.accept;
}

void ui_tick(void) {}

// --- simulated cartridge -----------------------------------------------------
#define SCRIPT_MAX 4096
static struct {
    uint8_t script[SCRIPT_MAX];      // phase 1: what the game sends, swap by swap
    uint8_t master[SCRIPT_MAX];      // ...and what we sent it during each swap
    int len, pos;
    int data_at[4];                  // script index of each section's first data byte

    // phase 2: the trade menu, which reacts to what we send
    int stage, hold;
    uint8_t choice, accept, success;         // what the game reports at each step
    uint8_t got_choice, got_accept, got_success;  // what we answered
    long swaps;
} cart;

static void script_put(uint8_t b) {
    if (cart.len >= SCRIPT_MAX) { printf("script overflow\n"); exit(2); }
    cart.script[cart.len++] = b;
}

// Each menu step: the game repeats its value until we answer with anything but
// NO_INPUT, then acknowledges with one NO_DATA and a few NO_INPUTs.
static uint8_t menu_swap(uint8_t out) {
    uint8_t reply;
    switch (cart.stage) {
        case 0: case 2: case 4: {
            reply = cart.stage == 0 ? cart.choice : cart.stage == 2 ? cart.accept : cart.success;
            if (out != NO_INPUT) {
                if (cart.stage == 0) cart.got_choice = out;
                else if (cart.stage == 2) cart.got_accept = out;
                else cart.got_success = out;
                cart.stage++;
                cart.hold = 0;
            }
            return reply;
        }
        case 1: case 3:
            reply = cart.hold == 0 ? NO_DATA : NO_INPUT;
            if (++cart.hold > 4) cart.stage++;
            return reply;
        default:
            reply = cart.hold == 0 ? NO_DATA : NO_INPUT;
            cart.hold++;
            return reply;
    }
}

uint8_t gb_link_swap(uint8_t out) {
    if (++cart.swaps > 200000) { printf("runaway link exchange\n"); exit(2); }
    if (cart.pos < cart.len) {
        cart.master[cart.pos] = out;
        return cart.script[cart.pos++];
    }
    return menu_swap(out);
}

// --- building the cartridge's side ------------------------------------------
typedef struct {
    uint8_t sec1[GB_SEC1_MAX];      // party section as the game holds it (real 0xFE bytes)
    uint8_t wire1[GB_SEC1_MAX];     // ...as sent (0xFE patched out)
    uint8_t sec2[GB_SEC2_LEN];      // patch list
    uint8_t sec3[GB_SEC3_MAX];      // mail (Gen 2)
    int count;
} cart_party_t;

static uint8_t *cp_mon(const gen_profile_t *p, cart_party_t *cp, int i) {
    return cp->sec1 + p->mon_pos + i * p->mon_len;
}

// A party of `count` mons with recognisable, distinct contents.
static void make_party(const gen_profile_t *p, cart_party_t *cp, int count,
                       const uint8_t *species) {
    memset(cp, 0, sizeof(*cp));
    cp->count = count;
    for (int j = 0; j < p->name_len; j++) cp->sec1[j] = (uint8_t)(0x80 + j);  // trainer name
    cp->sec1[p->count_pos] = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        cp->sec1[p->count_pos + 1 + i] = species[i];
        uint8_t *st = cp_mon(p, cp, i);
        for (int j = 0; j < p->mon_len; j++) st[j] = (uint8_t)(0x10 + i * 37 + j * 3);
        st[0] = species[i];
        if (p->gen == 2) st[1] = 0x03;   // held item: not Mail
        for (int j = 0; j < p->name_len; j++) {
            cp->sec1[p->ot_pos + i * p->name_len + j] = (uint8_t)(0xA0 + i);
            cp->sec1[p->nick_pos + i * p->name_len + j] = (uint8_t)(0xB0 + i + j);
        }
    }
    cp->sec1[p->count_pos + 1 + count] = 0xFF;
    cp->sec3[0] = 0x4E;                  // mail section never starts with its 0x20 preamble
}

// The game's own patching: every 0xFE in the party-mon area is replaced by 0xFF
// and its 1-based offset recorded, in two parts of 0xFC bytes each.
static void game_patch(const gen_profile_t *p, cart_party_t *cp) {
    int area_len = (p->mon_pos - PATCH_BASE) + 6 * p->mon_len;
    int w = PATCH_START, off = 0;
    memcpy(cp->wire1, cp->sec1, p->lens[1]);
    memset(cp->sec2, 0, sizeof(cp->sec2));
    for (int part = 0; part < 2; part++) {
        for (int c = 1; c <= PATCH_PART && off < area_len; c++, off++) {
            if (cp->wire1[PATCH_BASE + off] == 0xFE) {
                cp->wire1[PATCH_BASE + off] = 0xFF;
                cp->sec2[w++] = (uint8_t)c;
            }
        }
        cp->sec2[w++] = 0xFF;
    }
}

// The game applying a patch list we sent it.
static void game_unpatch(uint8_t *sec1, const uint8_t *sec2, int sec2_len) {
    int part = 0;
    for (int i = PATCH_START; i < sec2_len && part < 2; i++) {
        if (sec2[i] == 0xFF) { part++; continue; }
        if (sec2[i] == 0) continue;
        sec1[PATCH_BASE + part * PATCH_PART + sec2[i] - 1] = 0xFE;
    }
}

static void script_section(int index, uint8_t starter, int preamble,
                           const uint8_t *data, int len) {
    for (int i = 0; i < preamble; i++) script_put(starter);
    cart.data_at[index] = cart.len;
    for (int i = 0; i < len; i++) script_put(data[i]);
    script_put(0x00);   // exchanged while we send our last byte; not data
}

static void start_cart(const gen_profile_t *p, cart_party_t *cp) {
    memset(&cart, 0, sizeof(cart));
    game_patch(p, cp);

    // Cable Club handshake, with a few idle bytes first so the retry loop runs.
    script_put(0x00); script_put(0x00);
    if (p->gen == 2) {
        static const uint8_t hs[] = {0x61, 0xD1, 0x00, 0xFE, 0xFE, 0x75, 0x00, 0xFD};
        for (size_t i = 0; i < sizeof(hs); i++) script_put(hs[i]);
    } else {
        static const uint8_t hs[] = {0x60, 0xD0, 0xD0, 0x60, 0x60, 0xFD};
        for (size_t i = 0; i < sizeof(hs); i++) script_put(hs[i]);
    }

    static const uint8_t rnd[10] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A};
    script_section(0, 0xFD, 3, rnd, p->lens[0]);
    script_section(1, 0xFD, 4, cp->wire1, p->lens[1]);
    script_section(2, 0xFD, 3, cp->sec2, p->lens[2]);
    if (p->n_sections > 3) script_section(3, 0x20, 5, cp->sec3, p->lens[3]);
}

// What we sent the game for section `index` (see trade_capture_section: our
// byte i rides the swap that returns the game's byte i+1).
static const uint8_t *sent_section(int index) {
    return cart.master + cart.data_at[index] + 1;
}

// Expected dex record for the game's party slot i: struct + OT + nickname.
static int expect_record(const gen_profile_t *p, cart_party_t *cp, int i, uint8_t *out) {
    memcpy(out, cp_mon(p, cp, i), p->mon_len);
    memcpy(out + p->mon_len, cp->sec1 + p->ot_pos + i * p->name_len, p->name_len);
    memcpy(out + p->mon_len + p->name_len,
           cp->sec1 + p->nick_pos + i * p->name_len, p->name_len);
    return p->mon_len + 2 * p->name_len;
}

// --- tests -------------------------------------------------------------------
static void test_capture(int gen) {
    printf("capture, Gen %d\n", gen);
    const gen_profile_t *p = gen_profile(gen);
    static cart_party_t cp;
    const uint8_t species[6] = {0x99, 0x54, 0x15, 0xB4, 0x24, 0x85};
    make_party(p, &cp, 6, species);
    // 0xFE bytes the link can't carry: one in each patch-list part, plus the
    // last byte of the last mon.
    cp_mon(p, &cp, 0)[0x10] = 0xFE;
    cp_mon(p, &cp, 0)[0x11] = 0xFE;
    cp_mon(p, &cp, 5)[0x05] = 0xFE;
    cp_mon(p, &cp, 5)[p->mon_len - 1] = 0xFE;
    if (gen == 2) {   // slot 3 is an Egg, slot 1 holds Mail
        cp.sec1[p->count_pos + 1 + 3] = GP_G2_EGG;
        cp_mon(p, &cp, 1)[1] = 0x9E;
        for (int j = 0; j < p->mail_len; j++) cp.sec3[1 * p->mail_len + j] = (uint8_t)(0x81 + j);
        for (int j = 0; j < p->mail_sender_len; j++)
            cp.sec3[p->mail_sender_pos + 1 * p->mail_sender_len + j] = (uint8_t)(0xC1 + j);
    }
    start_cart(p, &cp);

    static uint8_t raw[GB_CAPTURE_MAX];
    gb_capture_result_t r = capture_run(p, raw);
    CHECK(r == GB_CAP_OK, "capture_run returned %d", (int)r);
    CHECK(cart.pos == cart.len, "consumed %d of %d scripted bytes", cart.pos, cart.len);

    // We must present the baked partner party, byte for byte.
    for (int s = 0; s < p->n_sections; s++)
        CHECK(memcmp(sent_section(s), p->baked[s], p->lens[s]) == 0,
              "section %d: sent bytes differ from the baked partner", s);

    uint8_t sp[6]; static uint8_t data[6][DEX_MON_MAX]; uint16_t lens[6]; uint8_t fl[6];
    int n = extract_mons(p, raw, sp, data, lens, fl);
    CHECK(n == 6, "extract_mons returned %d", n);
    for (int i = 0; i < n && i < 6; i++) {
        uint8_t want[DEX_MON_MAX];
        int wlen = expect_record(p, &cp, i, want);
        if (gen == 2 && i == 1) {
            memcpy(want + wlen, cp.sec3 + i * p->mail_len, p->mail_len);
            memcpy(want + wlen + p->mail_len,
                   cp.sec3 + p->mail_sender_pos + i * p->mail_sender_len, p->mail_sender_len);
            wlen += p->mail_len + p->mail_sender_len;
        }
        CHECK(sp[i] == species[i], "mon %d species 0x%02X", i, sp[i]);
        CHECK(fl[i] == ((gen == 2 && i == 3) ? MON_FLAG_EGG : 0), "mon %d flags 0x%02X", i, fl[i]);
        CHECK(lens[i] == wlen, "mon %d length %d (want %d)", i, lens[i], wlen);
        CHECK(memcmp(data[i], want, wlen) == 0, "mon %d bytes differ", i);
    }
}

// Run one inject and check the whole exchange. `ours`/`theirs` are species ids.
static void run_inject(const char *name, int gen, uint8_t ours, uint8_t theirs,
                       int give_idx, uint8_t expect_success, bool vault_ok, bool with_mail,
                       bool our_egg, bool their_egg) {
    printf("inject, Gen %d: %s\n", gen, name);
    const gen_profile_t *p = gen_profile(gen);
    static cart_party_t cp;
    uint8_t species[6] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
    species[give_idx] = theirs;
    make_party(p, &cp, 4, species);
    cp_mon(p, &cp, give_idx)[0x12] = 0xFE;
    if (their_egg) cp.sec1[p->count_pos + 1 + give_idx] = GP_G2_EGG;
    start_cart(p, &cp);
    uint8_t base = gen == 2 ? 0x70 : 0x60;
    cart.choice = (uint8_t)(base + give_idx);
    cart.accept = (uint8_t)(base + 2);
    cart.success = expect_success;

    // The vaulted record we trade in, with 0xFE bytes that must be patched out.
    uint8_t rec[DEX_MON_MAX];
    uint16_t base_len = (uint16_t)(p->mon_len + 2 * p->name_len);
    uint16_t rec_len = base_len;
    for (int j = 0; j < DEX_MON_MAX; j++) rec[j] = (uint8_t)(0x41 + j);
    rec[0] = ours;
    rec[0x09] = 0xFE;
    rec[p->mon_len - 1] = 0xFE;
    if (gen == 2) rec[1] = with_mail ? 0xB5 : 0x03;
    if (with_mail) rec_len = (uint16_t)(base_len + p->mail_len + p->mail_sender_len);

    memset(&store, 0, sizeof(store));
    store.accept = vault_ok;
    gb_inject_received_t got = {0xEE, true};
    gb_inject_result_t r = inject_run(p, rec, rec_len, our_egg ? MON_FLAG_EGG : 0, &got);
    CHECK(r == GB_INJ_OK, "inject_run returned %d", (int)r);

    // Commit handshake: we offer slot 0, accept, and answer the success sync.
    CHECK(cart.got_choice == base, "offered 0x%02X", cart.got_choice);
    CHECK(cart.got_accept == base + 2, "accept byte 0x%02X", cart.got_accept);
    CHECK(cart.got_success == expect_success, "success byte 0x%02X (want 0x%02X)",
          cart.got_success, expect_success);

    // The party the game received: one mon, ours, with its real bytes restored.
    static uint8_t s1[GB_SEC1_MAX];
    memcpy(s1, sent_section(1), p->lens[1]);
    for (int j = 0; j < p->lens[1]; j++)
        if (s1[j] == NO_INPUT) { CHECK(0, "raw 0xFE sent at party offset 0x%X", j); break; }
    game_unpatch(s1, sent_section(2), p->lens[2]);
    CHECK(s1[p->count_pos] == 1, "party count %d", s1[p->count_pos]);
    CHECK(s1[p->count_pos + 1] == (our_egg ? GP_G2_EGG : ours), "species list 0x%02X",
          s1[p->count_pos + 1]);
    CHECK(memcmp(s1 + p->mon_pos, rec, p->mon_len) == 0, "mon struct differs");
    CHECK(memcmp(s1 + p->ot_pos, rec + p->mon_len, p->name_len) == 0, "OT name differs");
    CHECK(memcmp(s1 + p->nick_pos, rec + p->mon_len + p->name_len, p->name_len) == 0,
          "nickname differs");
    if (p->n_sections > 3) {
        const uint8_t *s3 = sent_section(3);
        const uint8_t *tmpl = p->baked[3];
        if (with_mail) {
            CHECK(memcmp(s3 + p->mail_pos, rec + base_len, p->mail_len) == 0, "mail differs");
            CHECK(memcmp(s3 + p->mail_sender_pos, rec + base_len + p->mail_len,
                         p->mail_sender_len) == 0, "mail sender differs");
        } else {
            CHECK(memcmp(s3, tmpl, p->lens[3]) == 0, "mail section should be the empty template");
        }
    }

    // The mon the game gave away must be vaulted, and the outcome reported.
    uint8_t want[DEX_MON_MAX];
    int wlen = expect_record(p, &cp, give_idx, want);
    CHECK(store.calls == 1, "storage_put_mon called %d times", store.calls);
    CHECK(store.gen == gen && store.species == theirs, "stored gen %d species 0x%02X",
          store.gen, store.species);
    CHECK(store.flags == (their_egg ? MON_FLAG_EGG : 0), "stored flags 0x%02X", store.flags);
    CHECK(store.len == wlen && memcmp(store.data, want, wlen) == 0, "stored record differs");
    CHECK(got.species == theirs, "reported species 0x%02X", got.species);
    CHECK(got.stored == vault_ok, "reported stored=%d", got.stored);
}

static void test_inject_refusals(int gen) {
    printf("inject, Gen %d: cancel / decline / short record\n", gen);
    const gen_profile_t *p = gen_profile(gen);
    static cart_party_t cp;
    const uint8_t species[6] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
    uint8_t base = gen == 2 ? 0x70 : 0x60;
    uint8_t rec[DEX_MON_MAX];
    memset(rec, 0x42, sizeof(rec));
    uint16_t rec_len = (uint16_t)(p->mon_len + 2 * p->name_len);

    make_party(p, &cp, 2, species);
    start_cart(p, &cp);
    cart.choice = (uint8_t)(base + 0x0F);          // stop_trade
    memset(&store, 0, sizeof(store));
    CHECK(inject_run(p, rec, rec_len, 0, NULL) == GB_INJ_CANCELLED, "cancel not reported");
    CHECK(store.calls == 0, "stored a mon on cancel");

    make_party(p, &cp, 2, species);
    start_cart(p, &cp);
    cart.choice = base;
    cart.accept = (uint8_t)(base + 1);             // decline
    memset(&store, 0, sizeof(store));
    CHECK(inject_run(p, rec, rec_len, 0, NULL) == GB_INJ_DECLINED, "decline not reported");
    CHECK(store.calls == 0, "stored a mon on decline");

    cart.swaps = 0;
    CHECK(inject_run(p, rec, (uint16_t)(rec_len - 1), 0, NULL) == GB_INJ_BAD_RECORD,
          "short record accepted");
    CHECK(cart.swaps == 0, "talked to the cartridge with a bad record");
}

// The vault's "is this the same Pokémon?" rule (storage.c uses it to decide
// between updating a slot and taking a new one).
static void test_identity(void) {
    printf("vault identity rule\n");
    uint8_t a[DEX_MON_MAX], b[DEX_MON_MAX];
    for (int gen = 1; gen <= 2; gen++) {
        const gen_profile_t *p = gen_profile(gen);
        uint16_t len = (uint16_t)(p->mon_len + 2 * p->name_len);
        int ot = gen == 2 ? 0x06 : 0x0C, dv = gen == 2 ? 0x15 : 0x1B;
        for (int j = 0; j < DEX_MON_MAX; j++) a[j] = (uint8_t)(j * 7 + 1);
        memcpy(b, a, sizeof(a));
        CHECK(mon_same_individual(gen, a, len, b, len), "Gen %d: identical records differ", gen);
        // Levelled up, renamed, new moves, now holding mail: still the same mon.
        for (int j = 0; j < DEX_MON_MAX; j++)
            if (j != ot && j != ot + 1 && j != dv && j != dv + 1) b[j] ^= 0x5A;
        CHECK(mon_same_individual(gen, a, len, b, (uint16_t)(len + 47)),
              "Gen %d: a recapture should match", gen);
        memcpy(b, a, sizeof(a)); b[dv + 1] ^= 1;
        CHECK(!mon_same_individual(gen, a, len, b, len), "Gen %d: different DVs matched", gen);
        memcpy(b, a, sizeof(a)); b[ot] ^= 1;
        CHECK(!mon_same_individual(gen, a, len, b, len), "Gen %d: different OT ID matched", gen);
        CHECK(mon_identity_needs_species(gen), "Gen %d must key on species", gen);
    }
    for (int j = 0; j < 100; j++) a[j] = (uint8_t)(j + 3);
    memcpy(b, a, 100);
    for (int j = 8; j < 100; j++) b[j] ^= 0xA5;       // everything but PID + OT ID
    CHECK(mon_same_individual(3, a, 100, b, 100), "Gen 3: same PID/OTID should match");
    b[3] ^= 1;
    CHECK(!mon_same_individual(3, a, 100, b, 100), "Gen 3: different PID matched");
    CHECK(!mon_identity_needs_species(3), "Gen 3 must follow a mon through evolution");
    // Records too short to carry the key only match when identical.
    CHECK(mon_same_individual(1, a, 4, a, 4), "short identical records differ");
    CHECK(!mon_same_individual(1, a, 4, a, 5), "short records of different length matched");
}

int main(void) {
    test_identity();
    test_capture(1);
    test_capture(2);

    run_inject("plain trade", 1, 0x54, 0x99, 2, 0x60, true, false, false, false);
    run_inject("vault full", 1, 0x54, 0x99, 0, 0x60, false, false, false, false);
    run_inject("plain trade", 2, 25, 1, 3, 0x70, true, false, false, false);
    run_inject("we trade in Mew", 2, 151, 1, 1, 0x71, true, false, false, false);
    run_inject("cartridge gives Mew", 2, 25, 151, 1, 0x71, true, false, false, false);
    run_inject("we trade in Celebi", 2, 251, 1, 0, 0x72, true, false, false, false);
    run_inject("cartridge gives Celebi", 2, 25, 251, 2, 0x72, true, false, false, false);
    run_inject("our mon holds Mail", 2, 25, 1, 0, 0x70, true, true, false, false);
    run_inject("vault full", 2, 25, 1, 0, 0x70, false, false, false, false);
    run_inject("we trade in an Egg", 2, 25, 1, 1, 0x70, true, false, true, false);
    run_inject("cartridge gives an Egg", 2, 25, 1, 2, 0x70, true, false, false, true);
    // Gen 1 has no such check: Mew (internal 0x15) trades with the plain byte.
    run_inject("Mew, no special byte", 1, 0x15, 0x99, 1, 0x60, true, false, false, false);

    test_inject_refusals(1);
    test_inject_refusals(2);

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nall trade FSM checks passed\n");
    return 0;
}
