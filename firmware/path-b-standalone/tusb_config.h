// TinyUSB config for the standalone vault: as built, a single WebUSB vendor
// (class 0xFF) interface, which desktop and mobile Chrome and the Python host
// tools all use. A composite CDC + vendor build is still supported by the code
// (CFG_TUD_CDC below) but is off by default. See usb_console.c.
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED         1
#define CFG_TUD_ENDPOINT0_SIZE  64

// Vendor-only (no CDC): Android won't expose a device to WebUSB if it has a CDC
// interface (the kernel claims it), so the vault presents a single WebUSB vendor
// interface. Desktop + host tools talk to it over WebUSB too. (Set CFG_TUD_CDC 1
// to get the composite CDC+vendor build, but that hides the device on Android.)
#define CFG_TUD_CDC             0
#define CFG_TUD_VENDOR          1
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0

#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256
#define CFG_TUD_CDC_EP_BUFSIZE  64

#define CFG_TUD_VENDOR_RX_BUFSIZE 256
#define CFG_TUD_VENDOR_TX_BUFSIZE 256
#define CFG_TUD_VENDOR_EPSIZE     64

#endif
