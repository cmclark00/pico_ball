// UART console used by the optional Hosyond E32R32P touch display.
#ifndef DISPLAY_CONSOLE_H
#define DISPLAY_CONSOLE_H
#include <stddef.h>

// Adds a 115200-baud stdio transport on UART1: GPIO4 TX, GPIO5 RX. The existing
// command protocol and printf output are then available over USB and UART.
void display_console_init(void);

// Mirror console output to the display before any potentially blocking USB
// transport. This keeps the touchscreen responsive after a WebUSB client exits.
void display_console_write(const char *buf, size_t length);

#endif // DISPLAY_CONSOLE_H
