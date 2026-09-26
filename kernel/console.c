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
#include "pmm.h"
#include "heap.h"
#include "memtest.h"

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

    if (strcmp(line, "mem") == 0) {
        pmm_print_stats();
        vga_println("");
        heap_print_stats();
        return;
    }

    if (strcmp(line, "mmap") == 0) {
        pmm_print_mmap();
        return;
    }

    if (strcmp(line, "memtest") == 0) {
        memtest_run_all();
        return;
    }

    if (strcmp(line, "crashpf") == 0) {
        memtest_trigger_page_fault();
        return;
    }

    if (strcmp(line, "help") == 0) {
        vga_println("Nyota OS v0.3.0 Console");
        vga_println("Built-in commands:");
        vga_println("  uptime   - System uptime");
        vga_println("  cpu      - CPU hardware info");
        vga_println("  mem      - Memory usage & statistics");
        vga_println("  mmap     - BIOS E820 physical memory map");
        vga_println("  memtest  - Run Phase 3 memory validation suite");
        vga_println("  crashpf  - Trigger controlled page fault test");
        vga_println("  clear    - Clear display terminal");
        vga_println("  help     - Show available commands");
        return;
    }

    /* Standard behavior: echo the input line */
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
