// pico_ball -- standalone Pokémon vault firmware (Path B).
//
// No PC needed: press the button, sit at the Cable Club Trade Center table, and
// the board copies your party into its own flash. The LED reports state.
//
// USB serial (CDC) is available for debug, retrieval, and control:
//   'c'      -> print stored count
//   'd'      -> dump all stored records as hex (import_standalone.py / WebUI read)
//   'a'      -> arm/run a capture now (same as pressing the button)
//   'i' <n>  -> inject slot n (omit n to inject most recently captured) back into a cartridge
//   '1'/'2'  -> select generation (1 = R/B/Y, 2 = G/S/C) for the next operation
//   'r' <n>  -> delete record n (digits terminated by newline)
//   'w!'     -> wipe the whole vault ('w' arms, '!' confirms)
//
// Button gestures:
//   Tap        -> capture (current gen)
//   Hold ~0.7s -> switch gen (Gen 1 <-> Gen 2)
//   Hold ~2s   -> inject most recently captured mon (cross-gen auto-converted if needed)
#include <stdio.h>
#include "pico/stdlib.h"
#include "gb_link.h"
#include "capture.h"
#include "inject.h"
#include "cross_gen.h"
#include "gen_profile.h"
#include "storage.h"
#include "ui.h"
#include "gen3_multiboot.h"
#include "gen3_trade.h"
#include "usb_console.h"
#include "pico/bootrom.h"

static uint8_t record[GB_CAPTURE_MAX];
static uint8_t readbuf[GB_CAPTURE_MAX + 16];
static int current_gen = 1;
static int inject_slot = 0; // which dex slot to inject (default: slot 0; USB 'i <n>' overrides, -1 = most recent)
static bool wipe_armed = false; // 'w' arms; '!' confirms (guards the flash erase)

static void dump_all(void) {
    int n = storage_count();
    printf("DUMP %d\n", n);
    for (int i = 0; i < n; i++) {
        int gen = 0, species = 0;
        uint16_t len = storage_read_slot(i, &gen, &species, readbuf, sizeof(readbuf));
        printf("MON %d %d %u ", gen, species, len);
        for (uint16_t j = 0; j < len; j++) printf("%02X", readbuf[j]);
        printf("\n");
    }
    printf("END\n");
}

// Read a non-negative integer typed after a command (digits, ended by newline
// or a short idle). Returns -1 if none.
static int read_uint(void) {
    int val = -1;
    absolute_time_t end = make_timeout_time_ms(800);
    while (!time_reached(end)) {
        int ch = getchar_timeout_us(2000);
        if (ch >= '0' && ch <= '9') {
            if (val < 0) val = 0;
            val = val * 10 + (ch - '0');
        } else if (val >= 0) {
            break;  // digits done (newline or other)
        }
    }
    return val;
}

// Idle LED shows the selected generation: dim white = Gen 1, dim purple = Gen 2,
// dim cyan = Gen 3.
static void show_idle(void) {
    if (current_gen == 2) ui_color(12, 0, 18);
    else if (current_gen == 3) ui_color(0, 12, 12);
    else ui_color(6, 6, 6);
}

// Confirm a generation change by pulsing `current_gen` times:
// 1x green = Gen 1 (R/B/Y), 2x blue = Gen 2 (G/S/C), 3x cyan = Gen 3 (R/S/E/FR/LG).
static void blink_gen(void) {
    for (int i = 0; i < current_gen; i++) {
        if (current_gen == 2) ui_color(0, 0, 60);
        else if (current_gen == 3) ui_color(0, 50, 50);
        else ui_color(0, 60, 0);
        sleep_ms(180);
        ui_color(0, 0, 0);
        sleep_ms(160);
    }
}

// Hold thresholds.
#define HOLD_GEN_US  700000   // 0.7 s: switch generation (release before inject threshold)
#define HOLD_INJ_US 2000000   // 2.0 s: inject (LED turns amber; return while still held)

typedef enum { GESTURE_TAP, GESTURE_SWITCH_GEN, GESTURE_INJECT } gesture_t;

// Called just after a button press (button down). Returns which gesture was
// performed. GESTURE_INJECT fires as soon as the hold crosses 2 s (button may
// still be held); the others return after the button is released.
static gesture_t read_gesture(void) {
    sleep_ms(30);  // debounce press
    absolute_time_t start = get_absolute_time();
    while (ui_button_down()) {
        uint64_t us = absolute_time_diff_us(start, get_absolute_time());
        if (us >= HOLD_INJ_US) {
            ui_color(40, 24, 0);  // amber: inject threshold crossed
            return GESTURE_INJECT;
        }
        sleep_ms(10);
    }
    return (absolute_time_diff_us(start, get_absolute_time()) >= HOLD_GEN_US)
           ? GESTURE_SWITCH_GEN : GESTURE_TAP;
}

static void do_capture(void) {
    const gen_profile_t *p = gen_profile(current_gen);
    printf("Armed (Gen %d). Sit at the Trade Center table and select a Pokémon...\n",
           current_gen);
    ui_armed();
    gb_capture_result_t r = capture_run(p, record);
    if (r != GB_CAP_OK) {
        printf("CAPTURE_RESULT fail %d\n", (int)r);
        ui_error();
        sleep_ms(2000);
        return;
    }

    // Split the captured party and upsert each Pokémon into the dex by species.
    static uint8_t mondata[6][DEX_MON_MAX];
    uint8_t species[6];
    uint16_t lens[6];
    int n = extract_mons(p, record, species, mondata, lens);
    int stored = 0;
    for (int i = 0; i < n; i++)
        if (storage_put_mon(current_gen, species[i], mondata[i], lens[i])) stored++;

    if (n > 0) {
        printf("CAPTURE_RESULT ok stored %d dex %d/%d\n",
               stored, storage_count(), storage_capacity());
        ui_ok();
    } else {
        printf("CAPTURE_RESULT badparse\n");
        ui_error();
    }
    sleep_ms(2500);
}

// --- Gen 3 standalone capture (two phases: multiboot, then trade) ------------
// Gen 3 can't use the Cable Club FSM: the board multiboots Gen3-to-GenX into the
// GBA, the user navigates to its trade screen, then the board runs the trade.
// `gen3_booted` tracks which phase a tap (or serial m/t) should do next.
static bool gen3_booted = false;

static void run_gen3_multiboot(uint32_t pace) {
    printf("MB: multibooting (GBA must be at the BIOS/boot screen), pacing=%luus...\n",
           (unsigned long)pace);
    ui_armed();
    gb_link_set_gen3(true);
    bool ok = gen3_multiboot(pace);
    gb_link_set_gen3(false);
    if (ok) {
        gen3_booted = true;
        ui_color(40, 24, 0);  // amber: navigate the GBA to its trade screen, then capture
        printf("MB_RESULT ok — on the GBA pick the Gen 3 trade option, then capture again\n");
    } else {
        gen3_booted = false;
        ui_error();
        printf("MB_RESULT fail\n");
        sleep_ms(2000);
    }
}

static void run_gen3_trade(uint32_t pace) {
    printf("G3T: capturing (GBA on the Gen3-to-GenX trade screen), pacing=%luus...\n",
           (unsigned long)pace);
    ui_armed();
    gb_link_set_gen3(true);
    static uint8_t recs[6][GEN3_PK3_LEN];
    int n = gen3_capture_party(pace, recs, 6);
    gb_link_set_gen3(false);
    gen3_booted = false;
    if (n < 0) {
        printf("G3T_RESULT fail\n");
        ui_error();
        sleep_ms(2000);
        return;
    }
    int stored = 0;
    for (int i = 0; i < n; i++) {
        uint16_t sp = gen3_species(recs[i]);
        if (storage_put_mon(3, sp, recs[i], GEN3_PK3_LEN)) stored++;
        printf("MON3 %d %u ", i, sp);
        for (int b = 0; b < GEN3_PK3_LEN; b++) printf("%02x", recs[i][b]);
        printf("\n");
    }
    printf("G3T_RESULT ok %d stored %d dex %d/%d\n",
           n, stored, storage_count(), storage_capacity());
    ui_ok();
    sleep_ms(2500);
}

// A Gen 3 tap does multiboot first, then (after the user reaches the trade
// screen) the trade capture.
static void do_capture_gen3(void) {
    if (!gen3_booted) run_gen3_multiboot(100);
    else run_gen3_trade(1000);
}

// Inject a stored Gen 3 mon back into a cartridge. The GBA must already be on the
// Gen3-to-GenX trade screen (multiboot first). Select your give-away on the GBA;
// the mon it gives back is vaulted so nothing is lost.
static void run_gen3_inject(int slot, uint32_t pace) {
    if (slot < 0) slot = storage_last_written_index();
    int mon_gen = 0, species = 0;
    uint16_t len = (slot < 0) ? 0
        : storage_read_slot(slot, &mon_gen, &species, record, sizeof(record));
    if (len == 0 || mon_gen != 3) {
        printf("INJECT_RESULT fail: slot %d is not a Gen 3 record\n", slot);
        ui_error(); sleep_ms(2000); return;
    }
    printf("G3I: injecting slot %d (Gen 3 species %d) — pick your give-away on the GBA, "
           "pacing=%luus...\n", slot, species, (unsigned long)pace);
    ui_armed();
    gb_link_set_gen3(true);
    static uint8_t recv[GEN3_PK3_LEN];
    uint16_t recv_sp = 0;
    int r = gen3_inject_mon(pace, record, (uint16_t)species, recv, &recv_sp);
    gb_link_set_gen3(false);
    gen3_booted = false;
    if (r == 1) {
        bool stored = storage_put_mon(3, recv_sp, recv, GEN3_PK3_LEN);
        printf("INJECT_RESULT ok received_species %d stored %d dex %d/%d\n",
               recv_sp, stored ? 1 : 0, storage_count(), storage_capacity());
        ui_ok();
    } else {
        printf("INJECT_RESULT %s\n", r == 0 ? "cancelled" : "fail");
        ui_error();
    }
    sleep_ms(2500);
}

static void do_inject(int slot) {
    if (current_gen == 3) { run_gen3_inject(slot, 1000); return; }
    // -1 means "most recently captured"
    if (slot < 0) {
        slot = storage_last_written_index();
        if (slot < 0) {
            printf("INJECT_RESULT fail: vault empty\n");
            ui_error(); sleep_ms(2000); return;
        }
    }

    int mon_gen = 0, species = 0;
    uint16_t len = storage_read_slot(slot, &mon_gen, &species, record, sizeof(record));

    if (len == 0) {
        printf("INJECT_RESULT fail: slot %d empty\n", slot);
        ui_error(); sleep_ms(2000); return;
    }

    const gen_profile_t *p = gen_profile(current_gen);
    const uint8_t *inject_data = record;
    uint16_t inject_len = len;
    static uint8_t conv_buf[DEX_MON_MAX];

    if (mon_gen != current_gen) {
        bool ok = (mon_gen == 1)
            ? conv_g1_to_g2_rec(record, conv_buf)
            : conv_g2_to_g1_rec(record, conv_buf);
        if (!ok) {
            printf("INJECT_RESULT fail: Gen %d species 0x%02X has no Gen %d equivalent\n",
                   mon_gen, species, current_gen);
            ui_error(); sleep_ms(2000); return;
        }
        inject_data = conv_buf;
        inject_len  = (uint16_t)(p->mon_len + 2 * p->name_len);
        printf("Cross-gen: converting Gen %d -> Gen %d (species 0x%02X)\n",
               mon_gen, current_gen, species);
    }

    printf("Inject slot %d (Gen %d species 0x%02X -> Gen %d). "
           "Cable Club -> Trade Center -> sit at table, pick the Pokémon to give away.\n",
           slot, mon_gen, species, current_gen);
    ui_armed();

    gb_inject_result_t r = inject_run(p, inject_data, inject_len);
    switch (r) {
        case GB_INJ_OK:       printf("INJECT_RESULT ok\n");         ui_ok();    break;
        case GB_INJ_CANCELLED:printf("INJECT_RESULT cancelled\n");  ui_error(); break;
        case GB_INJ_DECLINED: printf("INJECT_RESULT declined\n");   ui_error(); break;
        default:              printf("INJECT_RESULT fail %d\n", (int)r); ui_error(); break;
    }
    sleep_ms(2500);
}

// --- Gen 1/2 -> Gen 3 transfer via Poke Transporter GB ----------------------
// The board multiboots Poke Transporter GB (PTGB) into the GBA, then offers a
// stored Gen 1/2 mon to it over the normal Game Boy link trade. PTGB converts it
// (Pal-Park style) and writes it to the real Gen 3 cartridge the user swaps into
// the GBA after PTGB loads. No flashcart needed — the board is the loader.
static bool ptgb_booted = false;

static void run_ptgb_multiboot(uint32_t pace) {
    printf("PTGB: multibooting Poke Transporter GB (GBA at its BIOS screen), "
           "pacing=%luus...\n", (unsigned long)pace);
    ui_armed();
    gb_link_set_gen3(true);
    bool ok = ptgb_multiboot(pace);
    gb_link_set_gen3(false);
    if (ok) {
        ptgb_booted = true;
        ui_color(40, 24, 0);  // amber: set up PTGB on the GBA, then offer the mon
        printf("PTGB_MB_RESULT ok — on the GBA: insert your Gen 3 cartridge, choose\n"
               "the Gen 1 or Gen 2 source and start receiving, then send 'o [slot]'.\n");
    } else {
        ptgb_booted = false;
        ui_error();
        printf("PTGB_MB_RESULT fail\n");
        sleep_ms(2000);
    }
}

// Offer a stored Gen 1/2 mon to PTGB (running on the GBA) as a link trade. Reuses
// the proven Gen 1/2 inject FSM; PTGB is just the trade partner. Offers the mon
// in its native generation (PTGB must be set to the matching Gen source).
static void run_ptgb_offer(int slot, uint32_t pace) {
    (void)pace;
    if (slot < 0) slot = storage_last_written_index();
    if (slot < 0) {
        printf("PTGB_RESULT fail: vault empty\n");
        ui_error(); sleep_ms(2000); return;
    }
    int mon_gen = 0, species = 0;
    uint16_t len = storage_read_slot(slot, &mon_gen, &species, record, sizeof(record));
    if (len == 0) {
        printf("PTGB_RESULT fail: slot %d empty\n", slot);
        ui_error(); sleep_ms(2000); return;
    }
    if (mon_gen != 1 && mon_gen != 2) {
        printf("PTGB_RESULT fail: slot %d is Gen %d (PTGB takes Gen 1/2 only)\n",
               slot, mon_gen);
        ui_error(); sleep_ms(2000); return;
    }
    const gen_profile_t *p = gen_profile(mon_gen);
    printf("PTGB: offering slot %d (Gen %d species 0x%02X) to Poke Transporter GB. "
           "On the GBA pick Gen %d as the source and start the trade.\n",
           slot, mon_gen, species, mon_gen);
    ui_armed();
    gb_inject_result_t r = inject_run(p, record, len);
    switch (r) {
        case GB_INJ_OK:        printf("PTGB_RESULT ok — PTGB has the mon; let it write "
                                      "to the Gen 3 cart.\n"); ui_ok(); break;
        case GB_INJ_CANCELLED: printf("PTGB_RESULT cancelled\n"); ui_error(); break;
        case GB_INJ_DECLINED:  printf("PTGB_RESULT declined\n");  ui_error(); break;
        default:               printf("PTGB_RESULT fail %d\n", (int)r); ui_error(); break;
    }
    sleep_ms(2500);
}

static void handle_serial(void) {
    int ch = getchar_timeout_us(0);
    // Any command other than the wipe handshake disarms a pending wipe.
    if (ch != PICO_ERROR_TIMEOUT && ch != 'w' && ch != '!') wipe_armed = false;
    switch (ch) {
        case 'd': dump_all(); break;
        case 'c': printf("COUNT %d/%d\n", storage_count(), storage_capacity()); break;
        case 'a': do_capture(); show_idle(); break;
        case 'i': {
            int n = read_uint();
            if (n >= 0) inject_slot = n;   // explicit index overrides auto
            do_inject(inject_slot);
            show_idle();
            break;
        }
        case '1': current_gen = 1; gen3_booted = false; printf("GEN 1\n"); show_idle(); break;
        case '2': current_gen = 2; gen3_booted = false; printf("GEN 2\n"); show_idle(); break;
        case '3': current_gen = 3; gen3_booted = false; printf("GEN 3\n"); show_idle(); break;
        case 'm': {  // Gen 3: multiboot Gen3-to-GenX into the GBA (optional 'm 100' pacing)
            int pace = read_uint();
            run_gen3_multiboot(pace < 0 ? 100 : (uint32_t)pace);
            break;
        }
        case 't': {  // Gen 3: trade capture (GBA on the trade screen; optional 't 1000' pacing)
            int pace = read_uint();
            run_gen3_trade(pace < 0 ? 1000 : (uint32_t)pace);
            show_idle();
            break;
        }
        case 'P': {  // Gen 1/2 -> Gen 3: multiboot Poke Transporter GB (optional 'P 100' pacing)
            int pace = read_uint();
            run_ptgb_multiboot(pace < 0 ? 100 : (uint32_t)pace);
            break;
        }
        case 'o': {  // offer a stored Gen 1/2 mon to PTGB (optional 'o <slot>'; default last)
            int n = read_uint();
            run_ptgb_offer(n, 1000);
            show_idle();
            break;
        }
        case 'B': printf("BOOTSEL\n"); sleep_ms(50); reset_usb_boot(0, 0); break;
        case 'w':
            wipe_armed = true;
            printf("WIPE? send '!' to confirm (any other key cancels)\n");
            break;
        case '!':
            if (wipe_armed) { storage_wipe(); printf("WIPED %d\n", storage_count()); }
            wipe_armed = false;
            break;
        case 'r': {
            int n = read_uint();
            bool ok = (n >= 0) && storage_delete(n);
            printf("DELETED %d %d\n", n, ok ? 1 : 0);
            break;
        }
        default: break;
    }
}

int main(void) {
    usb_console_init();  // composite CDC + WebUSB-vendor console (mobile-capable)
    gb_link_init();
    ui_init();

    sleep_ms(300);
    printf("\npico_ball standalone vault. Stored %d/%d.\n"
           "Tap = capture | Hold ~0.7s = switch gen (1>2>3) | Hold ~2s = inject slot 0 (USB 'i n' picks)\n"
           "Gen 3: tap multiboots the GBA, then (on its trade screen) tap again to capture.\n"
           "Serial: 'a'=capture 'i [n]'=inject 'c'=count 'd'=dump '1'/'2'/'3'=gen\n"
           "        'm'=gen3 multiboot 't'=gen3 trade 'r n'=delete 'w!'=wipe\n"
           "        'P'=multiboot Poke Transporter GB  'o [n]'=send Gen 1/2 mon to it (-> Gen 3 cart)\n"
           "Current: Gen %d. Cross-gen injection supported (Gen 1<->2).\n",
           storage_count(), storage_capacity(), current_gen);
    show_idle();

    bool prev = false;
    while (true) {
        handle_serial();

        bool now = ui_button_down();
        if (now && !prev) {
            gesture_t g = read_gesture();
            if (g == GESTURE_INJECT) {
                while (ui_button_down()) sleep_ms(10);  // wait for release
                if (current_gen == 3 && !gen3_booted)
                    run_gen3_multiboot(100);   // boot first; hold again to inject
                else
                    do_inject(inject_slot);
            } else if (g == GESTURE_SWITCH_GEN) {
                current_gen = (current_gen % 3) + 1;   // cycle 1 -> 2 -> 3 -> 1
                gen3_booted = false;
                printf("GEN %d (hold)\n", current_gen);
                blink_gen();
                while (ui_button_down()) sleep_ms(10);  // wait for release
            } else {                          // GESTURE_TAP: capture
                if (current_gen == 3) do_capture_gen3();
                else do_capture();
            }
            show_idle();
            prev = false;
            continue;
        }
        prev = now;
        sleep_ms(30);  // debounce + poll cadence
    }
}
