/* =============================================================================
 * Nyota OS — Serial Driver (COM1 / 0x3F8)
 * 8250 / 16550 UART driver for host-accessible debugging via QEMU
 * =========================================================================== */

#include "serial.h"
#include "io.h"

#define COM1_PORT       0x3F8

#define UART_DATA       0  /* Data register */
#define UART_IER        1  /* Interrupt Enable */
#define UART_IIR        2  /* Interrupt Ident / FIFO Control */
#define UART_LCR        3  /* Line Control */
#define UART_MCR        4  /* Modem Control */
#define UART_LSR        5  /* Line Status */
#define UART_MSR        6  /* Modem Status */
#define UART_SCRATCH    7  /* Scratch */

#define LSR_THRE        0x20  /* Transmitter Holding Register Empty */

static bool serial_initialized = false;

static void serial_wait_ready(void) {
    while ((inb(COM1_PORT + UART_LSR) & LSR_THRE) == 0) {
        /* Busy wait */
    }
}

void serial_init(void) {
    /* Disable all serial interrupts */
    outb(COM1_PORT + UART_IER, 0x00);

    /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + UART_LCR, 0x80);

    /* Set divisor to 1 (115200 baud) */
    outb(COM1_PORT + UART_DATA, 0x01);  /* DLL: low byte */
    outb(COM1_PORT + UART_IER, 0x00);   /* DLH: high byte */

    /* 8 bits, no parity, one stop bit (8N1) */
    outb(COM1_PORT + UART_LCR, 0x03);

    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1_PORT + UART_IIR, 0xC7);

    /* IRQs enabled, RTS/DSR set */
    outb(COM1_PORT + UART_MCR, 0x0B);

    serial_initialized = true;
}

void serial_putchar(char c) {
    if (!serial_initialized) {
        serial_init();
    }
    serial_wait_ready();
    outb(COM1_PORT + UART_DATA, (uint8_t)c);
}

void serial_write(const char *str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str);
        str++;
    }
}

bool serial_has_data(void) {
    if (!serial_initialized) {
        serial_init();
    }
    return (inb(COM1_PORT + UART_LSR) & 0x01) != 0;
}

char serial_getchar(void) {
    while (!serial_has_data()) {
        __asm__ volatile ("hlt");
    }
    return (char)inb(COM1_PORT + UART_DATA);
}
