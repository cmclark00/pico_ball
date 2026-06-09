// pico_ball -- standalone Pokémon vault firmware (Path B).
//
// No PC needed: press the button, sit at the Cable Club Trade Center table, and
// the board copies your party into its own flash. The LED reports state.
//
// USB serial (CDC) is available for debug, retrieval, and control:
//   'c'      -> print stored count
//   'd'      -> dump all stored records as hex (import_standalone.py / WebUI read)
//   'a'      -> arm/run a capture now (same as pressing the button)
//   '1'/'2'  -> select generation (1 = R/B/Y, 2 = G/S/C) for the next capture
//   'r' <n>  -> delete record n (digits terminated by newline)
//   'w'      -> wipe the whole vault
#include <stdio.h>
#include "pico/stdlib.h"
#include "gb_link.h"
#include "capture.h"
#include "gen_profile.h"
#include "storage.h"
#include "ui.h"

static uint8_t record[GB_CAPTURE_MAX];
static uint8_t readbuf[GB_CAPTURE_MAX + 16];
static int current_gen = 1;

static void dump_all(void) {
    int n = storage_count();
    printf("DUMP %d\n", n);
    for (int i = 0; i < n; i++) {
        uint16_t len = storage_read(i, readbuf, sizeof(readbuf));
        printf("REC %d %u ", i, len);
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

// Hold this long to switch generations (vs a short tap to capture).
#define HOLD_US 700000  // 0.7 s

// Called just after a button press (button down). Returns true if the button is
// held past the hold threshold (a "switch generation" gesture), false if it was
// a short tap (a "capture" gesture). On a hold it returns as soon as the
// threshold is crossed — the button may still be down.
static bool read_hold(void) {
    sleep_ms(30);  // debounce press
    absolute_time_t start = get_absolute_time();
    while (ui_button_down()) {
        if (absolute_time_diff_us(start, get_absolute_time()) >= HOLD_US) return true;
        sleep_ms(10);
    }
    return false;  // released before the threshold
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
    if (storage_put(current_gen, record, p->record_len)) {
        printf("CAPTURE_RESULT ok %d/%d\n", storage_count(), storage_capacity());
        ui_ok();
    } else {
        printf("CAPTURE_RESULT err\n");
        ui_error();
    }
    sleep_ms(2500);
}

static void handle_serial(void) {
    int ch = getchar_timeout_us(0);
    switch (ch) {
        case 'd': dump_all(); break;
        case 'c': printf("COUNT %d/%d\n", storage_count(), storage_capacity()); break;
        case 'a': do_capture(); show_idle(); break;
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
           "Tap = capture (Gen %d). Hold ~1s = switch Gen 1 <-> Gen 2.\n",
           storage_count(), storage_capacity(), current_gen);
    show_idle();

    bool prev = false;
    while (true) {
        handle_serial();

        bool now = ui_button_down();
        if (now && !prev) {
            if (read_hold()) {                // hold: toggle generation
                current_gen = (current_gen == 1) ? 2 : 1;
                printf("GEN %d (hold)\n", current_gen);
                blink_gen();                  // confirm; user can release now
                while (ui_button_down()) sleep_ms(10);  // wait for release
            } else {                          // tap: capture
                do_capture();
            }
            show_idle();
            prev = false;  // button already released
            continue;
        }
        prev = now;
        sleep_ms(30);  // debounce + poll cadence
    }
}
