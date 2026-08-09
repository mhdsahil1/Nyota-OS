/* =============================================================================
 * Nyota OS — Serial Console Driver (COM1 / 0x3F8)
 * Minimal 8250 UART driver for serial output to host via QEMU
 * =========================================================================== */

#include "serial.h"

#define SERIAL_PORT 0x3F8

/* 8250 UART I/O ports (relative to base port) */
#define UART_DATA       0  /* Data register (THR/RBR) */
#define UART_IER        1  /* Interrupt Enable Register */
#define UART_IIR        2  /* Interrupt Identification Register */
#define UART_LCR        3  /* Line Control Register */
#define UART_MCR        4  /* Modem Control Register */
#define UART_LSR        5  /* Line Status Register */
#define UART_MSR        6  /* Modem Status Register */
#define UART_SR         7  /* Scratch Register */

/* Line Status Register bits */
#define LSR_THRE   0x20  /* Transmitter Holding Register Empty */

/* Port I/O helpers */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Wait for the transmitter to be ready */
static void serial_wait_tx(void) {
    while (!(inb(SERIAL_PORT + UART_LSR) & LSR_THRE))
        ;
}

/* ── Public: initialise serial port ──────────────────────────────────────── */
void serial_init(void) {
    /* Disable interrupts */
    outb(SERIAL_PORT + UART_IER, 0x00);
    
    /* Set DLAB (Divisor Latch Access Bit) to configure baud rate */
    outb(SERIAL_PORT + UART_LCR, 0x80);
    
    /* Set baud rate to 115200
     * Divisor = 1 (115200 baud)
     * DLL = 0x01, DLH = 0x00 */
    outb(SERIAL_PORT + UART_DATA, 0x01);  /* DLL */
    outb(SERIAL_PORT + UART_IER,  0x00);  /* DLH */
    
    /* 8 data bits, 1 stop bit, no parity; clear DLAB */
    outb(SERIAL_PORT + UART_LCR, 0x03);
    
    /* Enable FIFO, clear buffers */
    outb(SERIAL_PORT + UART_IIR, 0xC7);
    
    /* Set RTS and DTR to signal we're ready */
    outb(SERIAL_PORT + UART_MCR, 0x0B);
}

/* ── Public: send one character ──────────────────────────────────────────── */
void serial_putchar(char c) {
    serial_wait_tx();
    outb(SERIAL_PORT + UART_DATA, (uint8_t)c);
}

/* ── Public: send null-terminated string ────────────────────────────────── */
void serial_write(const char *str) {
    while (*str) {
        serial_putchar(*str);
        str++;
    }
}
