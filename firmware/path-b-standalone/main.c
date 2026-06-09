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
    if (storage_append(record, p->record_len)) {
        printf("CAPTURE_RESULT ok %d/%d\n", storage_count(), storage_capacity());
        ui_ok();
    } else {
        printf("CAPTURE_RESULT full %d\n", storage_capacity());
        ui_full();
    }
    sleep_ms(2500);
}

static void handle_serial(void) {
    int ch = getchar_timeout_us(0);
    switch (ch) {
        case 'd': dump_all(); break;
        case 'c': printf("COUNT %d/%d\n", storage_count(), storage_capacity()); break;
        case 'a': do_capture(); ui_idle(); break;
        case '1': current_gen = 1; printf("GEN 1\n"); break;
        case '2': current_gen = 2; printf("GEN 2\n"); break;
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
    printf("\npico_ball standalone vault. Stored %d/%d. Hold the button to capture.\n",
           storage_count(), storage_capacity());
    ui_idle();

    bool prev = false;
    while (true) {
        handle_serial();

        bool now = ui_button_down();
        if (now && !prev) {
            do_capture();
            ui_idle();
        }
        prev = now;
        sleep_ms(30);  // debounce + poll cadence
    }
}
