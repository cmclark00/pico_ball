// pico_ball -- standalone Pokémon vault firmware (Path B).
//
// No PC needed: press the button, sit at the Cable Club Trade Center table, and
// the board copies your party into its own flash. The LED reports state.
//
// USB serial (CDC) is available for debug, retrieval, and control:
//   'c'      -> print stored count
//   'd'      -> dump all stored records as hex (import_standalone.py / WebUI read)
//   'a'      -> arm/run a capture now (same as pressing the button)
//   'i' <n>  -> inject slot n (default 0) back into a cartridge
//   '1'/'2'  -> select generation (1 = R/B/Y, 2 = G/S/C) for the next operation
//   'r' <n>  -> delete record n (digits terminated by newline)
//   'w'      -> wipe the whole vault
//
// Button gestures:
//   Tap        -> capture (current gen)
//   Hold ~0.7s -> switch gen (Gen 1 <-> Gen 2)
//   Hold ~2s   -> inject slot 0 of current gen (LED turns amber briefly to confirm)
#include <stdio.h>
#include "pico/stdlib.h"
#include "gb_link.h"
#include "capture.h"
#include "inject.h"
#include "gen_profile.h"
#include "storage.h"
#include "ui.h"

static uint8_t record[GB_CAPTURE_MAX];
static uint8_t readbuf[GB_CAPTURE_MAX + 16];
static int current_gen = 1;
static int inject_slot = 0;  // which dex slot to inject (by index into stored list)

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

// Idle LED shows the selected generation: dim white = Gen 1, dim purple = Gen 2.
static void show_idle(void) {
    if (current_gen == 2) ui_color(12, 0, 18);
    else ui_color(6, 6, 6);
}

// Confirm a generation change by pulsing `current_gen` times:
// 1x green = Gen 1 (R/B/Y), 2x blue = Gen 2 (G/S/C).
static void blink_gen(void) {
    for (int i = 0; i < current_gen; i++) {
        if (current_gen == 2) ui_color(0, 0, 60);
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

static void do_inject(int slot) {
    const gen_profile_t *p = gen_profile(current_gen);

    int mon_gen = 0, species = 0;
    uint16_t len = storage_read_slot(slot, &mon_gen, &species, record, sizeof(record));

    if (len == 0) {
        printf("INJECT slot %d: empty\n", slot);
        ui_error();
        sleep_ms(2000);
        return;
    }
    if (mon_gen != current_gen) {
        printf("INJECT slot %d: Gen %d but current gen is %d\n",
               slot, mon_gen, current_gen);
        ui_error();
        sleep_ms(2000);
        return;
    }

    printf("Inject slot %d (Gen %d species 0x%02X). "
           "Cable Club -> Trade Center -> sit at table, pick the Pokémon to give away.\n",
           slot, mon_gen, species);
    ui_armed();

    gb_inject_result_t r = inject_run(p, record, len);
    switch (r) {
        case GB_INJ_OK:
            printf("INJECT_RESULT ok\n");
            ui_ok();
            break;
        case GB_INJ_CANCELLED:
            printf("INJECT_RESULT cancelled\n");
            ui_error();
            break;
        case GB_INJ_DECLINED:
            printf("INJECT_RESULT declined\n");
            ui_error();
            break;
        default:
            printf("INJECT_RESULT fail %d\n", (int)r);
            ui_error();
            break;
    }
    sleep_ms(2500);
}

static void handle_serial(void) {
    int ch = getchar_timeout_us(0);
    switch (ch) {
        case 'd': dump_all(); break;
        case 'c': printf("COUNT %d/%d\n", storage_count(), storage_capacity()); break;
        case 'a': do_capture(); show_idle(); break;
        case 'i': {
            int n = read_uint();
            if (n < 0) n = inject_slot;
            else inject_slot = n;
            do_inject(n);
            show_idle();
            break;
        }
        case '1': current_gen = 1; printf("GEN 1\n"); show_idle(); break;
        case '2': current_gen = 2; printf("GEN 2\n"); show_idle(); break;
        case 'w': storage_wipe(); printf("WIPED %d\n", storage_count()); break;
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
    stdio_init_all();   // USB CDC for debug/retrieval
    gb_link_init();
    ui_init();

    sleep_ms(300);
    printf("\npico_ball standalone vault. Stored %d/%d.\n"
           "Tap = capture | Hold ~0.7s = switch gen | Hold ~2s = inject slot 0\n"
           "Serial: 'a'=capture 'i [n]'=inject 'c'=count 'd'=dump '1'/'2'=gen 'r n'=delete 'w'=wipe\n"
           "Current: Gen %d. Inject slot: %d.\n",
           storage_count(), storage_capacity(), current_gen, inject_slot);
    show_idle();

    bool prev = false;
    while (true) {
        handle_serial();

        bool now = ui_button_down();
        if (now && !prev) {
            gesture_t g = read_gesture();
            if (g == GESTURE_INJECT) {
                while (ui_button_down()) sleep_ms(10);  // wait for release
                do_inject(inject_slot);
            } else if (g == GESTURE_SWITCH_GEN) {
                current_gen = (current_gen == 1) ? 2 : 1;
                printf("GEN %d (hold)\n", current_gen);
                blink_gen();
                while (ui_button_down()) sleep_ms(10);  // wait for release
            } else {                          // GESTURE_TAP: capture
                do_capture();
            }
            show_idle();
            prev = false;
            continue;
        }
        prev = now;
        sleep_ms(30);  // debounce + poll cadence
    }
}
