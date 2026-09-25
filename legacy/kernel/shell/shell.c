/* =============================================================================
 * Nyota OS — Command Shell (Phase 3)
 * Displays the OS banner, reads lines from the keyboard, and dispatches
 * built-in commands.  No dynamic memory is used — everything is static.
 * =========================================================================== */

#include "shell.h"
#include "../vga/vga.h"
#include "../keyboard/keyboard.h"

/* ── Tiny string utilities (no stdlib) ────────────────────────────────────── */
static int kstrcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int kstrncmp(const char *a, const char *b, size_t n) {
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── Boot banner ──────────────────────────────────────────────────────────── */
static void print_banner(void) {
    vga_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println(" ==========================================================");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("  _   _             _            ___  ____");
    vga_println(" | \\ | |_   _  ___ | |_ __ _   / _ \\/ ___|");
    vga_println(" |  \\| | | | |/ _ \\| __/ _` | | | | \\___ \\");
    vga_println(" | |\\  | |_| | (_) | || (_| | | |_| |___) |");
    vga_println(" |_| \\_|\\__, |\\___/ \\__\\__,_|  \\___/|____/");
    vga_println("          |___/");
    vga_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println(" ==========================================================");
    vga_println("  Version 0.1  |  x86-32 Protected Mode  |  Phase 3: Shell");
    vga_println(" ==========================================================");
    vga_println("");
    vga_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("  Kernel loaded successfully. Type 'help' for commands.");
    vga_println("");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
}

/* ── Built-in command implementations ────────────────────────────────────── */
static void cmd_help(void) {
    vga_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("  Available commands:");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    vga_println("  ------------------------------------------------");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("   help      Show this help message");
    vga_println("   about     About Nyota OS");
    vga_println("   clear     Clear the screen");
    vga_println("   version   Show OS version and build info");
    vga_println("   reboot    Reboot the system");
    vga_println("   shutdown  Shut down the system");
    vga_println("   echo      Print text to the terminal");
    vga_println("   time      Show system time and uptime");
    vga_println("   debug     Show keyboard debug info");
    vga_println("   panic     Trigger a manual kernel panic");
    vga_println("   meminfo   Display physical memory information");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    vga_println("  ------------------------------------------------");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
}

static void cmd_about(void) {
    vga_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("  Nyota OS");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("  A hobby operating system built from scratch in C and x86 Assembly.");
    vga_println("");
    vga_println("  Purpose  : Learn OS internals — kernel, memory, drivers, scheduling.");
    vga_println("  Author   : Nyota OS Project");
    vga_println("  License  : Open Source / Educational");
    vga_println("");
    vga_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("  Roadmap phases:");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("   [x] Phase 1 — Bootloader");
    vga_println("   [x] Phase 2 — Kernel Foundation (VGA, GDT, IDT, Keyboard)");
    vga_println("   [x] Phase 3 — Command Shell  <- You are here");
    vga_println("   [ ] Phase 4 — Memory Management");
    vga_println("   [ ] Phase 5 — Process Management");
    vga_println("   [ ] Phase 6 — File System");
    vga_println("   [ ] Phase 7 — Hardware Drivers");
    vga_println("   [ ] Phase 8 — Graphical Environment");
}

static void cmd_version(void) {
    vga_setcolor(VGA_YELLOW, VGA_BLACK);
    vga_println("  Nyota OS v0.1.0");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("  Architecture : x86, 32-bit Protected Mode");
    vga_println("  Kernel type  : Monolithic (freestanding C)");
    vga_println("  Build phase  : 3 - Command Shell");
    vga_println("  Toolchain    : NASM + GCC + GNU ld");
    vga_println("  Emulator     : QEMU qemu-system-i386");
}

static void cmd_clear(void) {
    vga_clear();
}

static void cmd_reboot(void) {
    vga_setcolor(VGA_YELLOW, VGA_BLACK);
    vga_println("  Rebooting...");
    /* Triple-fault reboot: load a zero-length IDT, trigger an exception */
    volatile struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt0 = {0, 0};
    __asm__ volatile (
        "cli\n"
        "lidt (%0)\n"
        "int $3\n"
        : : "r"(&idt0)
    );
    for (;;); /* Should never reach here */
}

static void cmd_shutdown(void) {
    vga_setcolor(VGA_YELLOW, VGA_BLACK);
    vga_println("  Shutting down...");
    /* QEMU ACPI power-off via PM1a control register at port 0x604 */
    __asm__ volatile (
        "mov $0x2000, %%ax\n"
        "mov $0x604,  %%dx\n"
        "out %%ax, %%dx\n"
        : : : "eax", "edx"
    );
    /* Bochs / older QEMU fallback */
    __asm__ volatile (
        "mov $0xB004, %%dx\n"
        "mov $0x2000, %%ax\n"
        "out %%ax, %%dx\n"
        : : : "eax", "edx"
    );
    /* If nothing worked, just halt */
    vga_println("  Power-off failed. System halted.");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_echo(const char *arg) {
    vga_println(arg);
}

static void cmd_time(void) {
    vga_setcolor(VGA_YELLOW, VGA_BLACK);
    vga_println("  System Time:");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("    Uptime: Unknown (PIT timer not initialised)");
    vga_println("    RTC   : Unknown");
}

static void cmd_panic(void) {
    vga_setcolor(VGA_WHITE, VGA_RED);
    vga_println("");
    vga_println("  *** KERNEL PANIC: Manual panic triggered from shell ***");
    vga_println("  System halted.");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_meminfo(void) {
    vga_println("Physical Memory:");
    vga_println("Unknown");
}

static void cmd_debug(void) {
    vga_setcolor(VGA_YELLOW, VGA_BLACK);
    vga_print("  Keyboard handler called: ");
    vga_print_dec(keyboard_get_debug_count());
    vga_println(" times");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
}

static void cmd_unknown(const char *cmd) {
    vga_setcolor(VGA_LIGHT_RED, VGA_BLACK);
    vga_print("  Unknown command: '");
    vga_print(cmd);
    vga_println("'");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_println("  Type 'help' to list available commands.");
}

/* ── Line reader ──────────────────────────────────────────────────────────── */
#define INPUT_MAX 255
static char input_buf[INPUT_MAX + 1];
static int  input_len;

static void read_line(void) {
    char c;
    input_len = 0;
    vga_setcolor(VGA_WHITE, VGA_BLACK);

    while (1) {
        c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            break;
        }
        if (c == '\b') {
            if (input_len > 0) {
                input_len--;
                vga_putchar('\b');
            }
            continue;
        }
        if (input_len < INPUT_MAX) {
            input_buf[input_len++] = c;
            vga_putchar(c);
        }
    }
    input_buf[input_len] = '\0';
}

/* ── Prompt ───────────────────────────────────────────────────────────────── */
static void print_prompt(void) {
    vga_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("nyota");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("@os");
    vga_setcolor(VGA_WHITE, VGA_BLACK);
    vga_print("> ");
}

/* ── Public: one-time banner ─────────────────────────────────────────────── */
void shell_init(void) {
    print_banner();
}

/* ── Public: main shell loop ─────────────────────────────────────────────── */
void shell_run(void) {
    for (;;) {
        print_prompt();
        read_line();

        if (input_len == 0) continue;

        if      (kstrcmp(input_buf, "help")     == 0) cmd_help();
        else if (kstrcmp(input_buf, "about")    == 0) cmd_about();
        else if (kstrcmp(input_buf, "clear")    == 0) cmd_clear();
        else if (kstrcmp(input_buf, "version")  == 0) cmd_version();
        else if (kstrcmp(input_buf, "reboot")   == 0) cmd_reboot();
        else if (kstrcmp(input_buf, "shutdown") == 0) cmd_shutdown();
        else if (kstrcmp(input_buf, "echo")     == 0) cmd_echo("");
        else if (kstrncmp(input_buf, "echo ", 5) == 0) cmd_echo(input_buf + 5);
        else if (kstrcmp(input_buf, "time")     == 0) cmd_time();
        else if (kstrcmp(input_buf, "meminfo")  == 0) cmd_meminfo();
        else if (kstrcmp(input_buf, "debug")    == 0) cmd_debug();
        else if (kstrcmp(input_buf, "panic")    == 0) cmd_panic();
        else                                           cmd_unknown(input_buf);

        vga_println("");
    }
}
