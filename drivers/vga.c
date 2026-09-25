/* =============================================================================
 * Nyota OS — VGA Text Mode Driver
 * Framebuffer driver for standard 80x25 text mode at 0xB8000.
 * =========================================================================== */

#include "vga.h"
#include "serial.h"
#include "io.h"

static volatile uint16_t * const vga_buffer = (volatile uint16_t *)VGA_MEMORY;

static int vga_col = 0;
static int vga_row = 0;
static uint8_t vga_color_attr = 0x07; /* Light grey on black */

static inline uint8_t vga_make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(fg | (bg << 4));
}

static inline uint16_t vga_make_cell(char c, uint8_t color) {
    return (uint16_t)((uint8_t)c | ((uint16_t)color << 8));
}

static void vga_update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    /* Shift each row up by one */
    for (int r = 0; r < VGA_HEIGHT - 1; r++) {
        for (int c = 0; c < VGA_WIDTH; c++) {
            vga_buffer[r * VGA_WIDTH + c] = vga_buffer[(r + 1) * VGA_WIDTH + c];
        }
    }
    /* Clear bottom row */
    uint16_t blank = vga_make_cell(' ', vga_color_attr);
    for (int c = 0; c < VGA_WIDTH; c++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = blank;
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = vga_make_cell(' ', vga_color_attr);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    vga_col = 0;
    vga_row = 0;
    vga_update_hw_cursor();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    vga_color_attr = vga_make_color(fg, bg);
}

void vga_get_color(vga_color_t *fg, vga_color_t *bg) {
    if (fg) *fg = (vga_color_t)(vga_color_attr & 0x0F);
    if (bg) *bg = (vga_color_t)((vga_color_attr >> 4) & 0x0F);
}

void vga_set_cursor(int col, int row) {
    if (col >= 0 && col < VGA_WIDTH)  vga_col = col;
    if (row >= 0 && row < VGA_HEIGHT) vga_row = row;
    vga_update_hw_cursor();
}

void vga_putchar(char c) {
    /* Mirror output to serial console */
    if (c == '\n') {
        serial_putchar('\r');
    }
    serial_putchar(c);

    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
        vga_update_hw_cursor();
        return;
    }

    if (c == '\r') {
        vga_col = 0;
        vga_update_hw_cursor();
        return;
    }

    if (c == '\t') {
        vga_col = (vga_col + 4) & ~3;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_HEIGHT) vga_scroll();
        }
        vga_update_hw_cursor();
        return;
    }

    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_make_cell(' ', vga_color_attr);
        }
        vga_update_hw_cursor();
        return;
    }

    vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_make_cell(c, vga_color_attr);
    vga_col++;

    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
    vga_update_hw_cursor();
}

void vga_print(const char *str) {
    if (!str) return;
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_println(const char *str) {
    if (str) {
        vga_print(str);
    }
    vga_putchar('\n');
}

void vga_print_hex(uint64_t val) {
    vga_print("0x");
    char buf[17];
    int idx = 0;
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (uint8_t)((val >> i) & 0x0F);
        if (nibble != 0 || idx > 0) {
            buf[idx++] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
    }
    buf[idx] = '\0';
    vga_print(buf);
}

void vga_print_dec(uint64_t val) {
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    char buf[21];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        vga_putchar(buf[i]);
    }
}
