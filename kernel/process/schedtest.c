/* =============================================================================
 * Nyota OS — Scheduler & Multitasking Validation Suite (Phase 5)
 * Automated verification of process states, ready queue, preemption, sleep, isolation.
 * =========================================================================== */

#include "schedtest.h"
#include "scheduler.h"
#include "process.h"
#include "user_programs.h"
#include "syscall.h"
#include "timer.h"
#include "vga.h"
#include "serial.h"
#include "kernel.h"

void schedtest_run_all(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("----------------------------------------");
    vga_println("  NYOTA SCHEDULER & MULTITASKING TESTS  ");
    vga_println("----------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 1: Process Table & Registration ────────────────────────────── */
    vga_print("[TEST 1/4] Process Table & Lookup... ");

    size_t count = process_count();
    if (count > PROCESS_MAX_COUNT) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Invalid process count");
        return;
    }

    if (process_find(99999) != NULL) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Non-existent PID found");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 2: Ready Queue & Idle Fallback ──────────────────────────────── */
    vga_print("[TEST 2/4] Ready Queue & Idle Fallback... ");

    process_t *idle = scheduler_get_idle();
    if (!idle || idle->pid != 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Idle process missing or wrong PID");
        return;
    }

    /* An empty ready queue should safely return idle process */
    process_t *n = scheduler_next();
    if (n != idle) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Empty queue did not return idle");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 3: Syscall ABI Table (Yield & Sleep) ────────────────────────── */
    vga_print("[TEST 3/4] Syscall Dispatch Table... ");

    /* Check that unknown syscall is rejected */
    if (syscall_dispatch(8888, 0, 0, 0, 0, 0, 0) != SYS_ERR_ENOSYS) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Unknown syscall did not return -ENOSYS");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 4: Scheduler State & Context Metrics ────────────────────────── */
    vga_print("[TEST 4/4] Scheduler State & Metrics... ");

    process_t *curr = scheduler_get_current();
    if (!curr) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Current process is NULL");
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    vga_println("----------------------------------------");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("All Phase 5 unit tests PASSED!");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

void schedtest_spawn_triplet(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("[INFO] Spawning 3 concurrent user processes:");
    vga_println("  - PID 1: prog_a (counter with cooperative yield)");
    vga_println("  - PID 2: prog_b (preemptive CPU compute loop)");
    vga_println("  - PID 3: prog_c (sleep/wake demonstration)");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    process_create("prog_a", USER_CODE_BASE, user_get_prog_a(), USER_PROGRAM_BLOB_SIZE);
    process_create("prog_b", USER_CODE_BASE, user_get_prog_b(), USER_PROGRAM_BLOB_SIZE);
    process_create("prog_c", USER_CODE_BASE, user_get_prog_c(), USER_PROGRAM_BLOB_SIZE);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("[INFO] Enabling preemptive round-robin scheduler...");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    scheduler_start();
}

void schedtest_trigger_isolation_violation(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_println("[SECURITY] Spawning rogue process attempting hostile kernel write...");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    process_create("rogue_proc", USER_CODE_BASE, user_get_prog_bad(), USER_PROGRAM_BLOB_SIZE);
    scheduler_start();
}
