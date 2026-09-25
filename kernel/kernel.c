/* =============================================================================
 * Nyota OS — Kernel Main & Core Subsystems
 * Target: x86_64 Long Mode
 * =========================================================================== */

#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "cpu.h"
#include "gdt.h"
#include "memory.h"

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

    /* 4. Log Phase 1 Initialization Steps */
    kinfo("Bootloader initialized");
    kinfo("CPU: x86_64");

    /* 5. Initialize Kernel GDT */
    gdt_init();
    kinfo("GDT initialized");

    kinfo("Paging enabled");
    kinfo("Kernel loaded");

    /* 6. Initialize CPUID & interrogate CPU hardware */
    cpu_init();
    kinfo("Kernel initialization complete");
    vga_println("");

    /* 7. Display CPU information */
    cpu_print_info();
    vga_println("");

    /* 8. Display Status Checklist & Completion Banner */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Bootloader       : OK");
    vga_println("CPU              : x86_64");
    vga_println("Long Mode        : OK");
    vga_println("Paging           : OK");
    vga_println("GDT              : OK");
    vga_println("Kernel           : OK");
    vga_println("");
    vga_println("----------------------------------------");
    vga_println("");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("Nyota Kernel v0.1");
    vga_println("System initialized successfully.");
    vga_println("");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_println("nyota kernel is running...");

    /* 9. Safe idle loop */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
