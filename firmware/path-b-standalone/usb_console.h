#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H

// Bring up the composite CDC + WebUSB-vendor console and route stdio (printf /
// getchar) over both. Call once at startup instead of stdio_init_all().
void usb_console_init(void);

#endif // USB_CONSOLE_H
