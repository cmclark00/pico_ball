#include "display_console.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdio/driver.h"

#define DISPLAY_UART uart1
#define DISPLAY_UART_BAUD 115200
#define DISPLAY_UART_TX_PIN 4
#define DISPLAY_UART_RX_PIN 5

void display_console_write(const char *buf, size_t length) {
    uart_write_blocking(DISPLAY_UART, (const uint8_t *)buf, length);
}

static int display_in_chars(char *buf, int length) {
    int count = 0;
    while (count < length && uart_is_readable(DISPLAY_UART))
        buf[count++] = (char)uart_getc(DISPLAY_UART);
    return count > 0 ? count : PICO_ERROR_NO_DATA;
}

static stdio_driver_t display_stdio = {
    .in_chars = display_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

void display_console_init(void) {
    uart_init(DISPLAY_UART, DISPLAY_UART_BAUD);
    gpio_set_function(DISPLAY_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DISPLAY_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(DISPLAY_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(DISPLAY_UART, true);
    stdio_set_driver_enabled(&display_stdio, true);
}
