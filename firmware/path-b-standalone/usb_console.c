// USB console for the standalone vault: drives a composite CDC + WebUSB-vendor
// device so the same text command protocol ('d','a','c',... + responses) is
// available over BOTH the CDC interface (desktop Web Serial + Python host tools)
// and a WebUSB vendor interface (mobile Chrome, which won't expose CDC to
// WebUSB). printf/getchar route through a custom stdio driver that writes to both
// and reads from either. The tud_task() background-servicing (low-priority IRQ +
// periodic timer) is adapted from the pico-sdk's pico_stdio_usb so long on-device
// operations (multiboot, trades) don't stall USB.
#include "tusb.h"
#include "usb_descriptors.h"
#include "display_console.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/mutex.h"
#include "pico/time.h"
#include "hardware/irq.h"
#include <string.h>

#ifndef VAULT_USB_TASK_INTERVAL_US
#define VAULT_USB_TASK_INTERVAL_US 1000
#endif
#define STDOUT_TIMEOUT_US 500000

static mutex_t usb_mutex;
static uint8_t low_priority_irq_num;
static volatile bool web_connected = false;   // a WebUSB client has attached

// --- background tud_task ----------------------------------------------------
static void low_priority_worker_irq(void) {
    if (mutex_try_enter(&usb_mutex, NULL)) {
        tud_task();
        mutex_exit(&usb_mutex);
    }
}

static bool timer_task(repeating_timer_t *rt) {
    (void)rt;
    if (irq_is_enabled(low_priority_irq_num)) irq_set_pending(low_priority_irq_num);
    return true;
}

// --- stdio driver: write to vendor (+ CDC if built), read from either -------
static bool vendor_ready(void) { return tud_mounted() && web_connected; }

// Write `buf` to the vendor IN endpoint, pacing on its TX FIFO.
static void write_vendor(const char *buf, int len) {
    uint64_t last_avail = time_us_64();
    for (int i = 0; i < len;) {
        int n = len - i;
        uint32_t avail = tud_vendor_write_available();
        if ((uint32_t)n > avail) n = (int)avail;
        if (n > 0) {
            int wrote = (int)tud_vendor_write(buf + i, (uint32_t)n);
            tud_vendor_flush();
            i += wrote;
            last_avail = time_us_64();
        } else {
            tud_task();
            tud_vendor_flush();
            if (!vendor_ready() || time_us_64() > last_avail + STDOUT_TIMEOUT_US) break;
        }
    }
}

#if CFG_TUD_CDC
static bool cdc_ready(void) { return tud_cdc_connected(); }
static void write_cdc(const char *buf, int len) {
    uint64_t last_avail = time_us_64();
    for (int i = 0; i < len;) {
        int n = len - i;
        uint32_t avail = tud_cdc_write_available();
        if ((uint32_t)n > avail) n = (int)avail;
        if (n > 0) {
            int wrote = (int)tud_cdc_write(buf + i, (uint32_t)n);
            tud_cdc_write_flush();
            i += wrote;
            last_avail = time_us_64();
        } else {
            tud_task();
            tud_cdc_write_flush();
            if (!cdc_ready() || time_us_64() > last_avail + STDOUT_TIMEOUT_US) break;
        }
    }
}
#endif

static void vault_out_chars(const char *buf, int length) {
    // UART first: an abandoned WebUSB client can fill its endpoint and time out,
    // but must never delay or suppress the standalone touchscreen UI.
    display_console_write(buf, (size_t)length);
    if (!mutex_try_enter_block_until(&usb_mutex, make_timeout_time_ms(100))) return;
#if CFG_TUD_CDC
    if (cdc_ready()) write_cdc(buf, length);
#endif
    if (vendor_ready()) write_vendor(buf, length);
    mutex_exit(&usb_mutex);
}

static void vault_out_flush(void) {
    if (!mutex_try_enter_block_until(&usb_mutex, make_timeout_time_ms(100))) return;
#if CFG_TUD_CDC
    tud_cdc_write_flush();
#endif
    tud_vendor_flush();
    mutex_exit(&usb_mutex);
}

static int vault_in_chars(char *buf, int length) {
    int rc = PICO_ERROR_NO_DATA;
    if (!mutex_try_enter_block_until(&usb_mutex, make_timeout_time_ms(100))) return rc;
#if CFG_TUD_CDC
    if (tud_cdc_connected() && tud_cdc_available()) {
        int n = (int)tud_cdc_read(buf, (uint32_t)length);
        if (n) { mutex_exit(&usb_mutex); return n; }
    }
#endif
    if (tud_mounted() && tud_vendor_available()) {
        int n = (int)tud_vendor_read(buf, (uint32_t)length);
        if (n) { web_connected = true; rc = n; }   // first vendor byte = a client is talking
    }
    mutex_exit(&usb_mutex);
    return rc;
}

static stdio_driver_t vault_stdio = {
    .out_chars = vault_out_chars,
    .out_flush = vault_out_flush,
    .in_chars  = vault_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

void usb_console_init(void) {
    tusb_init();
    mutex_init(&usb_mutex);
    low_priority_irq_num = (uint8_t)user_irq_claim_unused(true);
    irq_set_exclusive_handler(low_priority_irq_num, low_priority_worker_irq);
    irq_set_enabled(low_priority_irq_num, true);
    static repeating_timer_t timer;
    add_repeating_timer_us(-VAULT_USB_TASK_INTERVAL_US, timer_task, NULL, &timer);
    stdio_set_driver_enabled(&vault_stdio, true);
}

// --- WebUSB / vendor control requests ---------------------------------------
#define URL "cmclark00.github.io/pico_ball"
static const tusb_desc_webusb_url_t desc_url = {
    .bLength = 3 + sizeof(URL) - 1,
    .bDescriptorType = 3,   // WEBUSB URL
    .bScheme = 1,           // https
    .url = URL,
};

// Handles vendor + WebUSB control transfers, and our 0x22 "connect" (a CLASS
// request the WebUI sends so we know a WebUSB client is attached).
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest) {
                case VENDOR_REQUEST_WEBUSB:
                    return tud_control_xfer(rhport, request, (void *)(uintptr_t)&desc_url, desc_url.bLength);
                case VENDOR_REQUEST_MICROSOFT:
                    if (request->wIndex == 7) {
                        uint16_t total = tu_le16toh(*(uint16_t *)(desc_ms_os_20 + 8));
                        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total);
                    }
                    return false;
                default: return false;
            }
        case TUSB_REQ_TYPE_CLASS:
            if (request->bRequest == 0x22) {     // WebUI "connect"
                web_connected = (request->wValue != 0);
                return tud_control_status(rhport, request);
            }
            return false;
        default: return false;
    }
}

void tud_mount_cb(void)   { web_connected = false; }
void tud_umount_cb(void)  { web_connected = false; }
