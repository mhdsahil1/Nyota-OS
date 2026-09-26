/* =============================================================================
 * Nyota OS — Process Subsystem & User Mode Task Management
 * Process Control Block (PCB), PID allocation, memory isolation, and execution.
 * =========================================================================== */

#include "process.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "tss.h"
#include "console.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"
#include "usertest.h"

static process_t *current_process = NULL;
static uint32_t next_pid = 1;

/* ── Minimal Position-Independent User Program ────────────────────────────── */

__attribute__((naked)) static void user_program_blob(void) {
    __asm__ volatile (
        /* 1. Write "Hello from user space!\n" */
        "lea msg_hello(%%rip), %%rdi\n"
        "mov $24, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* 2. Get PID */
        "mov $2, %%rax\n"              /* SYS_GETPID */
        "int $0x80\n"
        "mov %%rax, %%rbx\n"          /* Save PID */

        /* 3. Write "PID: " */
        "lea msg_pid(%%rip), %%rdi\n"
        "mov $5, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* 4. Format and write single-digit PID */
        "add $48, %%rbx\n"             /* Convert to ASCII */
        "push %%rbx\n"
        "mov %%rsp, %%rdi\n"
        "mov $1, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"
        "pop %%rbx\n"

        /* 5. Write newline */
        "lea msg_nl(%%rip), %%rdi\n"
        "mov $2, %%rsi\n"
        "mov $0, %%rax\n"              /* SYS_WRITE */
        "int $0x80\n"

        /* 6. Exit with status 0 */
        "xor %%rdi, %%rdi\n"          /* status = 0 */
        "mov $1, %%rax\n"              /* SYS_EXIT */
        "int $0x80\n"

        /* Hang if exit does not return */
        "1: jmp 1b\n"

        "msg_hello: .ascii \"\\nHello from user space!\\n\"\n"
        "msg_pid:   .ascii \"PID: \"\n"
        "msg_nl:    .ascii \"\\n\\n\"\n"
        ::: "rax", "rdi", "rsi", "rbx", "memory"
    );
}

static const size_t USER_PROGRAM_SIZE = 256;

/* ── Process Management ───────────────────────────────────────────────────── */

void process_system_init(void) {
    current_process = NULL;
    next_pid = 1;
}

process_t *process_get_current(void) {
    return current_process;
}

process_t *process_create(uint64_t entry_point, const void *code_blob, size_t code_size) {
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        kwarn("process_create: failed to allocate PCB");
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->exit_status = 0;
    proc->next = NULL;

    /* 1. Allocate dedicated 16 KiB kernel stack for this process */
    void *kstack = kmalloc(16384);
    if (!kstack) {
        kwarn("process_create: failed to allocate kernel stack");
        kfree(proc);
        return NULL;
    }
    proc->kernel_stack_top = (uint64_t)kstack + 16384;

    /* 2. Create isolated process page table (cloned from kernel) */
    page_table_t *pml4 = paging_create_address_space();
    if (!pml4) {
        kwarn("process_create: failed to allocate process PML4");
        kfree(kstack);
        kfree(proc);
        return NULL;
    }
    proc->cr3 = (uint64_t)pml4;

    /* 3. Map user code page at USER_CODE_BASE */
    void *code_phys = pmm_alloc_page();
    if (!code_phys) {
        kwarn("process_create: out of memory for user code frame");
        kfree(kstack);
        kfree(proc);
        return NULL;
    }
    memset(code_phys, 0, PAGE_SIZE);

    if (code_blob && code_size > 0) {
        size_t copy_sz = (code_size > PAGE_SIZE) ? PAGE_SIZE : code_size;
        memcpy(code_phys, code_blob, copy_sz);
    }

    if (!paging_map_page_in(pml4, USER_CODE_BASE, (uint64_t)code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) {
        kwarn("process_create: failed to map user code page");
        pmm_free_page(code_phys);
        kfree(kstack);
        kfree(proc);
        return NULL;
    }

    /* 4. Map user stack pages (16 KiB = 4 pages) */
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint64_t sp = stack_base; sp < USER_STACK_TOP; sp += PAGE_SIZE) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) {
            kwarn("process_create: out of memory for user stack frame");
            kfree(kstack);
            kfree(proc);
            return NULL;
        }
        memset(stack_phys, 0, PAGE_SIZE);

        if (!paging_map_page_in(pml4, sp, (uint64_t)stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) {
            kwarn("process_create: failed to map user stack page");
            pmm_free_page(stack_phys);
            kfree(kstack);
            kfree(proc);
            return NULL;
        }
    }

    proc->entry_point = entry_point ? entry_point : USER_CODE_BASE;
    proc->user_stack_top = USER_STACK_TOP - 16; /* 16-byte aligned */

    return proc;
}

void process_run(process_t *proc) {
    if (!proc) {
        kernel_panic("process_run: NULL process pointer");
    }

    current_process = proc;
    proc->state = PROCESS_RUNNING;

    /* Configure TSS RSP0 to process kernel stack top */
    tss_set_rsp0(proc->kernel_stack_top);

    /* Switch address space to process page table */
    paging_load_cr3(proc->cr3);

    /* Enter Ring 3 */
    user_enter_ring3(proc->entry_point, proc->user_stack_top);

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void process_exit(int status) {
    if (current_process) {
        current_process->state = PROCESS_TERMINATED;
        current_process->exit_status = status;

        /* Print exit information */
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO] Process ");
        vga_print_dec(current_process->pid);
        vga_println(" exited");

        vga_print("[INFO] Status: ");
        vga_print_dec((uint64_t)status);
        vga_println("");
        vga_println("");

        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_println("Kernel idle.");
        vga_println("");
        vga_set_color(VGA_WHITE, VGA_BLACK);
    }

    /* Restore kernel CR3 */
    paging_load_cr3((uint64_t)paging_get_kernel_pml4());

    /* Run Phase 4 User & Security Test Suite */
    usertest_run_all();

    /* Return to interactive kernel console */
    console_run();
}

/* ── Convenience Initial Process Spawner ──────────────────────────────────── */

process_t *process_spawn_init(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO] Creating process PID ");
    vga_print_dec(next_pid);
    vga_println("");

    process_t *proc = process_create(USER_CODE_BASE, (const void *)user_program_blob, USER_PROGRAM_SIZE);
    if (!proc) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FATAL: Failed to create init process");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return NULL;
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("[INFO] User memory initialized");
    vga_println("[INFO] User stack initialized");
    vga_println("[INFO] Entering Ring 3");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    return proc;
}
