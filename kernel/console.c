/* =============================================================================
 * Nyota OS — Interactive Kernel Console
 * Provides interactive keyboard line input, echo, backspace, and basic commands.
 * =========================================================================== */

#include "console.h"
#include "keyboard.h"
#include "vga.h"
#include "timer.h"
#include "memory.h"
#include "kernel.h"

static char line_buf[CONSOLE_LINE_MAX];
static size_t line_len = 0;

void console_prompt(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("nyota> ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

void console_init(void) {
    line_len = 0;
    line_buf[0] = '\0';
    console_prompt();
}

#include "cpu.h"

static void console_handle_line(const char *line) {
    if (!line || line[0] == '\0') {
        return;
    }

    if (strcmp(line, "clear") == 0) {
        vga_clear();
        return;
    }

    if (strcmp(line, "uptime") == 0) {
        char uptime_str[16];
        timer_format_uptime(uptime_str, sizeof(uptime_str));
        vga_print("Uptime: ");
        vga_println(uptime_str);
        return;
    }

    if (strcmp(line, "cpu") == 0) {
        cpu_print_info();
        return;
    }

    if (strcmp(line, "help") == 0) {
        vga_println("Nyota OS v0.2.0 Console");
        vga_println("Type any text to echo it back.");
        vga_println("Built-in commands: uptime, cpu, clear, help");
        return;
    }

    /* Standard Phase 2 behavior: echo the input line */
    vga_println(line);
}

void console_run(void) {
    console_init();

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            line_buf[line_len] = '\0';
            console_handle_line(line_buf);
            line_len = 0;
            line_buf[0] = '\0';
            console_prompt();
        } else if (c == '\b') {
            if (line_len > 0) {
                line_len--;
                line_buf[line_len] = '\0';
                vga_putchar('\b');
            }
        } else if (c >= 32 && c <= 126) {
            if (line_len < CONSOLE_LINE_MAX - 1) {
                line_buf[line_len++] = c;
                line_buf[line_len] = '\0';
                vga_putchar(c);
            }
        }
    }
}
