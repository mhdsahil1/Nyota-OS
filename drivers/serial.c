/* =============================================================================
 * Nyota OS — Serial Driver (COM1 / 0x3F8)
 * 8250 / 16550 UART driver for host-accessible debugging via QEMU.
 * Features IRQ 4-driven RX ring buffer for zero-loss serial communication.
 * =========================================================================== */

#include "serial.h"
#include "io.h"
#include "interrupts.h"
#include "pic.h"

#define COM1_PORT       0x3F8

#define UART_DATA       0  /* Data register */
#define UART_IER        1  /* Interrupt Enable */
#define UART_IIR        2  /* Interrupt Ident / FIFO Control */
#define UART_LCR        3  /* Line Control */
#define UART_MCR        4  /* Modem Control */
#define UART_LSR        5  /* Line Status */
#define UART_MSR        6  /* Modem Status */
#define UART_SCRATCH    7  /* Scratch */

#define LSR_DATA_READY  0x01  /* Data Ready */
#define LSR_THRE        0x20  /* Transmitter Holding Register Empty */

#define SERIAL_RX_BUF_SIZE 512
static volatile char serial_rx_buf[SERIAL_RX_BUF_SIZE];
static volatile uint32_t serial_rx_head = 0;
static volatile uint32_t serial_rx_tail = 0;

static bool serial_initialized = false;

static void serial_wait_ready(void) {
    while ((inb(COM1_PORT + UART_LSR) & LSR_THRE) == 0) {
        /* Busy wait */
    }
}

static void serial_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    while ((inb(COM1_PORT + UART_LSR) & LSR_DATA_READY) != 0) {
        char c = (char)inb(COM1_PORT + UART_DATA);
        uint32_t next = (serial_rx_head + 1) % SERIAL_RX_BUF_SIZE;
        if (next != serial_rx_tail) {
            serial_rx_buf[serial_rx_head] = c;
            serial_rx_head = next;
        }
    }
}

void serial_init(void) {
    /* Disable all serial interrupts during config */
    outb(COM1_PORT + UART_IER, 0x00);

    /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + UART_LCR, 0x80);

    /* Set divisor to 1 (115200 baud) */
    outb(COM1_PORT + UART_DATA, 0x01);  /* DLL: low byte */
    outb(COM1_PORT + UART_IER, 0x00);   /* DLH: high byte */

    /* 8 bits, no parity, one stop bit (8N1) */
    outb(COM1_PORT + UART_LCR, 0x03);

    /* Enable FIFO, clear them, with 1-byte threshold (0x07) */
    outb(COM1_PORT + UART_IIR, 0x07);

    /* IRQs enabled (OUT2 = 1), RTS/DSR set */
    outb(COM1_PORT + UART_MCR, 0x0B);

    /* Enable Received Data Available interrupt (IER bit 0) */
    outb(COM1_PORT + UART_IER, 0x01);

    /* Register IRQ 4 (vector 32 + 4 = 36) */
    interrupt_register_handler(IRQ_BASE_VECTOR + 4, serial_irq_handler);
    pic_unmask_irq(4);

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

void serial_write_hex(uint64_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putchar(hex_chars[(val >> i) & 0xF]);
    }
}

void serial_write_dec(uint64_t val) {
    if (val == 0) {
        serial_putchar('0');
        return;
    }
    char buf[21];
    int i = 0;
    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        serial_putchar(buf[j]);
    }
}

bool serial_has_data(void) {
    if (!serial_initialized) {
        serial_init();
    }
    if (serial_rx_head != serial_rx_tail) {
        return true;
    }
    return (inb(COM1_PORT + UART_LSR) & LSR_DATA_READY) != 0;
}

char serial_getchar(void) {
    while (!serial_has_data()) {
        __asm__ volatile ("sti; hlt");
    }
    if (serial_rx_head != serial_rx_tail) {
        char c = serial_rx_buf[serial_rx_tail];
        serial_rx_tail = (serial_rx_tail + 1) % SERIAL_RX_BUF_SIZE;
        return c;
    }
    return (char)inb(COM1_PORT + UART_DATA);
}
