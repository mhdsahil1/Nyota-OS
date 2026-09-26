/* =============================================================================
 * Nyota OS — User Programs Suite (Phase 5)
 * Independent position-independent user-space programs for multitasking validation.
 * =========================================================================== */

#include "user_programs.h"

#define PROGRAM_BLOB_SIZE 256

/* ── Program A: Cooperative Yield & Counter ───────────────────────────────── */

__attribute__((naked)) static void user_prog_a_code(void) {
    __asm__ volatile (
        /* Write start message */
        "lea msg_a_start(%%rip), %%rdi\n"
        "mov $28, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Loop 3 times with voluntary yield */
        "mov $3, %%r12\n"
    "1:\n"
        "lea msg_a_tick(%%rip), %%rdi\n"
        "mov $24, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        "mov $3, %%rax\n"              /* SYS_YIELD */
        "int $0x80\n"

        "dec %%r12\n"
        "jnz 1b\n"

        /* Write finish message */
        "lea msg_a_done(%%rip), %%rdi\n"
        "mov $25, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Exit cleanly with status 0 */
        "xor %%rdi, %%rdi\n"
        "mov $1, %%rax\n"              /* SYS_EXIT */
        "int $0x80\n"

        "2: jmp 2b\n"

        "msg_a_start: .ascii \"[PID 1] Program A running...\\n\"\n"
        "msg_a_tick:  .ascii \"[PID 1] A: yield to CPU\\n\"\n"
        "msg_a_done:  .ascii \"[PID 1] Program A complete!\\n\"\n"
        ::: "rax", "rdi", "rsi", "r12", "memory"
    );
}

/* ── Program B: Pure Preemptive Counter (Never Calls Yield) ───────────────── */

__attribute__((naked)) static void user_prog_b_code(void) {
    __asm__ volatile (
        /* Write start message */
        "lea msg_b_start(%%rip), %%rdi\n"
        "mov $35, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Loop doing computation without yielding (timer will preempt!) */
        "mov $3, %%r12\n"
    "3:\n"
        "lea msg_b_tick(%%rip), %%rdi\n"
        "mov $24, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Burn CPU cycles in loop so timer preemption occurs */
        "mov $5000000, %%rcx\n"
    "4:\n"
        "dec %%rcx\n"
        "jnz 4b\n"

        "dec %%r12\n"
        "jnz 3b\n"

        /* Write finish message */
        "lea msg_b_done(%%rip), %%rdi\n"
        "mov $25, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Exit cleanly with status 0 */
        "xor %%rdi, %%rdi\n"
        "mov $1, %%rax\n"              /* SYS_EXIT */
        "int $0x80\n"

        "5: jmp 5b\n"

        "msg_b_start: .ascii \"[PID 2] Program B (preemption)...\\n\"\n"
        "msg_b_tick:  .ascii \"[PID 2] B: busy compute\\n\"\n"
        "msg_b_done:  .ascii \"[PID 2] Program B complete!\\n\"\n"
        ::: "rax", "rdi", "rsi", "rcx", "r12", "memory"
    );
}

/* ── Program C: Sleep & Wakeup Demonstration ──────────────────────────────── */

__attribute__((naked)) static void user_prog_c_code(void) {
    __asm__ volatile (
        /* Write start message */
        "lea msg_c_start(%%rip), %%rdi\n"
        "mov $28, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Write sleep notification */
        "lea msg_c_sleep(%%rip), %%rdi\n"
        "mov $26, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Sleep 100 ms (10 timer ticks) */
        "mov $100, %%rdi\n"
        "mov $4, %%rax\n"              /* SYS_SLEEP */
        "int $0x80\n"

        /* Write wakeup message */
        "lea msg_c_wake(%%rip), %%rdi\n"
        "mov $23, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Exit cleanly with status 0 */
        "xor %%rdi, %%rdi\n"
        "mov $1, %%rax\n"              /* SYS_EXIT */
        "int $0x80\n"

        "6: jmp 6b\n"

        "msg_c_start: .ascii \"[PID 3] Program C started.\\n\"\n"
        "msg_c_sleep: .ascii \"[PID 3] C: sleeping 100ms\\n\"\n"
        "msg_c_wake:  .ascii \"[PID 3] C: woke up! Exit\\n\"\n"
        ::: "rax", "rdi", "rsi", "memory"
    );
}

/* ── Program Bad: Isolation Violation Attempt (Hostile Write) ─────────────── */

__attribute__((naked)) static void user_prog_bad_code(void) {
    __asm__ volatile (
        /* Write notice */
        "lea msg_bad(%%rip), %%rdi\n"
        "mov $38, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* Attempt to write to kernel memory at 0x100000 */
        "mov $0x100000, %%rax\n"
        "movl $0xDEADBEEF, (%%rax)\n"

        "7: jmp 7b\n"

        "msg_bad: .ascii \"[ROGUE] Attempting write to 0x100000!\\n\"\n"
        ::: "rax", "rdi", "rsi", "memory"
    );
}

/* ── Exported Program Blobs ───────────────────────────────────────────────── */

const uint8_t user_prog_a_blob[] = {0};
const size_t user_prog_a_size = PROGRAM_BLOB_SIZE;

const uint8_t user_prog_b_blob[] = {0};
const size_t user_prog_b_size = PROGRAM_BLOB_SIZE;

const uint8_t user_prog_c_blob[] = {0};
const size_t user_prog_c_size = PROGRAM_BLOB_SIZE;

const uint8_t user_prog_bad_blob[] = {0};
const size_t user_prog_bad_size = PROGRAM_BLOB_SIZE;

/* Pointer accessors */
const void *user_get_prog_a(void) { return (const void *)user_prog_a_code; }
const void *user_get_prog_b(void) { return (const void *)user_prog_b_code; }
const void *user_get_prog_c(void) { return (const void *)user_prog_c_code; }
const void *user_get_prog_bad(void) { return (const void *)user_prog_bad_code; }
