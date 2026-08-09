/* =============================================================================
 * Nyota OS — VGA Text Mode Driver
 * Writes directly to the VGA framebuffer at physical address 0xB8000.
 * Supports colour output, scrolling, and hardware cursor positioning.
 * =========================================================================== */

#include "vga.h"
#include "../serial/serial.h"

/* ── Module state ─────────────────────────────────────────────────────────── */
int vga_row = 0;
int vga_col = 0;
static uint8_t   vga_color  = 0;
static uint16_t *vga_buffer = (uint16_t *)VGA_MEMORY;

/* ── Port I/O helpers ─────────────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ── Internal helpers ─────────────────────────────────────────────────────── */
static inline uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((uint8_t)fg | ((uint8_t)bg << 4));
}

static inline uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    int row, col;
    /* Shift every row up by one */
    for (row = 0; row < VGA_HEIGHT - 1; row++) {
        for (col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[row * VGA_WIDTH + col] =
                vga_buffer[(row + 1) * VGA_WIDTH + col];
        }
    }
    /* Blank the last row */
    for (col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            make_entry(' ', vga_color);
    }
    vga_row = VGA_HEIGHT - 1;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void vga_init(void) {
    vga_color = make_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_row   = 0;
    vga_col   = 0;
    vga_clear();
}

void vga_clear(void) {
    int row, col;
    for (row = 0; row < VGA_HEIGHT; row++)
        for (col = 0; col < VGA_WIDTH; col++)
            vga_buffer[row * VGA_WIDTH + col] = make_entry(' ', vga_color);
    vga_row = 0;
    vga_col = 0;
    update_hw_cursor();
}

void vga_setcolor(vga_color_t fg, vga_color_t bg) {
    vga_color = make_color(fg, bg);
}

void vga_set_cursor(int row, int col) {
    vga_row = row;
    vga_col = col;
    update_hw_cursor();
}

void vga_putchar(char c) {
    /* Also send to serial console */
    serial_putchar(c);
    
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) scroll();
        update_hw_cursor();
        return;
    }
    if (c == '\r') {
        vga_col = 0;
        update_hw_cursor();
        return;
    }
    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buffer[vga_row * VGA_WIDTH + vga_col] = make_entry(' ', vga_color);
            update_hw_cursor();
        }
        return;
    }
    if (c == '\t') {
        int spaces = 4 - (vga_col % 4);
        while (spaces-- > 0) vga_putchar(' ');
        return;
    }

    vga_buffer[vga_row * VGA_WIDTH + vga_col] = make_entry(c, vga_color);

    if (++vga_col >= VGA_WIDTH) {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) scroll();
    }
    update_hw_cursor();
}

void vga_print(const char *str) {
    while (*str) vga_putchar(*str++);
}

void vga_println(const char *str) {
    vga_print(str);
    vga_putchar('\n');
}

void vga_print_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    int i;
    vga_print("0x");
    for (i = 28; i >= 0; i -= 4)
        vga_putchar(hex[(val >> i) & 0xF]);
}

void vga_print_dec(uint32_t val) {
    if (val == 0) { vga_putchar('0'); return; }
    char buf[12];
    int  idx = 0;
    while (val > 0) { buf[idx++] = (char)('0' + val % 10); val /= 10; }
    while (--idx >= 0) vga_putchar(buf[idx]);
}
