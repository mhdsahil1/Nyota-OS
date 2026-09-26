/* =============================================================================
 * Nyota OS — User Space & Syscall Security Validation Suite
 * Tests pointer validation, privilege levels, syscall bounds, and protections.
 * =========================================================================== */

#include "usertest.h"
#include "syscall.h"
#include "process.h"
#include "tss.h"
#include "gdt.h"
#include "vga.h"
#include "serial.h"
#include "kernel.h"

void usertest_run_all(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("----------------------------------------");
    vga_println("    NYOTA USER & SECURITY TEST SUITE    ");
    vga_println("----------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 1: GDT & TSS Verification ──────────────────────────────────── */
    vga_print("[TEST 1/4] TSS & Segment Selectors... ");

    uint16_t tr_val = 0;
    __asm__ volatile ("str %0" : "=r"(tr_val));

    if (tr_val != GDT_TSS_SEG) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: TR register mismatch");
        return;
    }

    if (tss_get_rsp0() == 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: TSS RSP0 is NULL");
        return;
    }

    uint16_t cs_val = 0;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs_val));
    if ((cs_val & 3) != 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Kernel not running in Ring 0");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 2: Unknown Syscall Dispatcher Handling ─────────────────────── */
    vga_print("[TEST 2/4] Invalid Syscall Number Handling... ");

    int64_t ret_invalid = syscall_dispatch(9999, 0, 0, 0, 0, 0);
    if (ret_invalid != SYS_ERR_ENOSYS) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Did not return -ENOSYS");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 3: Hostile Pointer Rejection in Syscalls ───────────────────── */
    vga_print("[TEST 3/4] Hostile Pointer Validation... ");

    /* Null pointer */
    if (syscall_dispatch(SYS_WRITE, 0, 10, 0, 0, 0) != SYS_ERR_EFAULT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Null pointer not rejected");
        return;
    }

    /* Hostile kernel code pointer (0x100000) */
    if (syscall_dispatch(SYS_WRITE, 0x100000, 10, 0, 0, 0) != SYS_ERR_EFAULT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Kernel code pointer not rejected");
        return;
    }

    /* Hostile kernel heap pointer (0xFFFFFFFF90000000) */
    if (syscall_dispatch(SYS_WRITE, 0xFFFFFFFF90000000ULL, 10, 0, 0, 0) != SYS_ERR_EFAULT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Kernel heap pointer not rejected");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 4: copy_from_user Security Bounding ────────────────────────── */
    vga_print("[TEST 4/4] copy_from_user bounds checking... ");

    char kbuf[32];
    if (copy_from_user(kbuf, (const void *)0x100000, sizeof(kbuf)) != SYS_ERR_EFAULT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: copy_from_user allowed kernel memory read");
        return;
    }

    if (copy_from_user(kbuf, NULL, sizeof(kbuf)) != SYS_ERR_EFAULT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: copy_from_user allowed NULL pointer");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    vga_println("----------------------------------------");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("All Phase 4 security tests PASSED!");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* Malicious user blob that attempts to write to kernel memory at 0x100000 */
__attribute__((naked)) static void malicious_user_blob(void) {
    __asm__ volatile (
        "mov $0x100000, %%rax\n"
        "movl $0xDEADBEEF, (%%rax)\n"  /* Attempt write to kernel memory from Ring 3 */
        "1: jmp 1b\n"
        ::: "rax", "memory"
    );
}

void usertest_trigger_kernel_write(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_println("Creating user process attempting hostile write to 0x100000...");
    serial_write("Creating user process attempting hostile write to 0x100000...\n");

    process_t *bad_proc = process_create(USER_CODE_BASE, (const void *)malicious_user_blob, 64);
    if (!bad_proc) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("Failed to create malicious process");
        return;
    }

    vga_println("Entering Ring 3 to execute write to kernel memory (expecting #PF)...");
    process_run(bad_proc);
}
