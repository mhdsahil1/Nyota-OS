/* =============================================================================
 * Nyota OS — Kernel Main & Core Subsystems (Phase 2)
 * Target: x86_64 Long Mode
 * =========================================================================== */

#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "cpu.h"
#include "gdt.h"
#include "memory.h"
#include "interrupts.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "console.h"
#include "exceptions.h"

/* ── Kernel Logging System ─────────────────────────────────────────────────── */

void kprint(const char *str) {
    vga_print(str);
}

void kprintln(const char *str) {
    vga_println(str);
}

void klog(const char *str) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("[LOG]   ");
    vga_println(str);
}

void kinfo(const char *str) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println(str);
}

void kwarn(const char *str) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("[WARN]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println(str);
}

void kerror(const char *str) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_print("[ERROR] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println(str);
}

/* ── Kernel Panic System ───────────────────────────────────────────────────── */

void kernel_panic(const char *reason) {
    __asm__ volatile ("cli");

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_println("");
    vga_println("========================================");
    vga_println("          NYOTA KERNEL PANIC            ");
    vga_println("========================================");
    vga_println("");
    vga_println("Reason:");
    vga_println(reason ? reason : "Unspecified kernel panic condition.");
    vga_println("");
    vga_println("System halted.");
    vga_println("========================================");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* ── Kernel Entry ──────────────────────────────────────────────────────────── */

void kernel_main(void) {
    /* 1. Initialize serial debug console (COM1) */
    serial_init();

    /* 2. Initialize VGA 80x25 text terminal */
    vga_init();

    /* 3. Display OS Boot Banner */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("========================================");
    vga_println("              NYOTA OS                  ");
    vga_println("========================================");
    vga_println("");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("Kernel       : v");
    vga_println(NYOTA_OS_VERSION);
    vga_print("Architecture : ");
    vga_println(NYOTA_ARCH);
    vga_println("");

    /* 4. Initialize Global Descriptor Table */
    gdt_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("GDT");

    /* 5. Initialize CPU Hardware Interrogation */
    cpu_init();

    /* 6. Initialize Interrupt Infrastructure (PIC, IDT, Exceptions) */
    interrupts_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("IDT");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Exceptions");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("PIC");

    /* 7. Initialize Programmable Interval Timer (PIT at 100 Hz) */
    timer_init(100);
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Timer");

    /* 8. Initialize PS/2 Keyboard Driver */
    keyboard_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Keyboard");

    /* 9. Enable CPU Interrupts */
    interrupts_enable();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Interrupts");
    vga_println("");

    /* 10. Display Initial Uptime */
    char uptime_str[16];
    timer_format_uptime(uptime_str, sizeof(uptime_str));
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("Uptime: ");
    vga_println(uptime_str);
    vga_println("");

    /* 11. Launch Interactive Kernel Console */
    console_run();
}
