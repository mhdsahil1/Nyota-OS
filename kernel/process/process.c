/* =============================================================================
 * Nyota OS — Process Subsystem & Process Control Block (PCB) Management
 * Process table, PID allocation, memory isolation, and process termination.
 * =========================================================================== */

#include "process.h"
#include "scheduler.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "tss.h"
#include "console.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"
#include "gdt.h"
#include "user_programs.h"

static process_t *process_table[PROCESS_MAX_COUNT] = {0};
static uint32_t next_pid = 1;

/* ── Process Management ───────────────────────────────────────────────────── */

void process_system_init(void) {
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_table[i] = NULL;
    }
    next_pid = 1;
}

process_t *process_get_current(void) {
    return scheduler_get_current();
}

void process_set_current(process_t *proc) {
    (void)proc;
}

process_t *process_find(uint32_t pid) {
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            return process_table[i];
        }
    }
    return NULL;
}

size_t process_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i]) {
            count++;
        }
    }
    return count;
}

process_t *process_create(const char *name, uint64_t entry_point, const void *code_blob, size_t code_size) {
    /* Find free slot in process table */
    int slot = -1;
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (!process_table[i]) {
            slot = (int)i;
            break;
        }
    }

    if (slot == -1) {
        kwarn("process_create: process table full");
        return NULL;
    }

    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        kwarn("process_create: failed to allocate PCB");
        return NULL;
    }
    memset(proc, 0, sizeof(process_t));

    proc->pid = next_pid++;
    proc->state = PROCESS_NEW;
    proc->exit_status = 0;
    proc->runtime_ticks = 0;
    proc->context_switches = 0;

    if (name) {
        size_t nlen = strlen(name);
        if (nlen >= PROCESS_NAME_MAX) nlen = PROCESS_NAME_MAX - 1;
        memcpy(proc->name, name, nlen);
        proc->name[nlen] = '\0';
    } else {
        memcpy(proc->name, "user_proc", 10);
    }

    /* 1. Allocate dedicated 16 KiB kernel stack */
    void *kstack = kmalloc(16384);
    if (!kstack) {
        kwarn("process_create: failed to allocate kernel stack");
        kfree(proc);
        return NULL;
    }
    proc->kernel_stack = (uint64_t)kstack;
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
    proc->user_stack_top = USER_STACK_TOP - 16; /* 16-byte alignment */

    /* 5. Fabricate initial interrupt_frame_t on kernel stack */
    interrupt_frame_t *frame = (interrupt_frame_t *)(proc->kernel_stack_top - sizeof(interrupt_frame_t));
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = proc->entry_point;
    frame->cs = GDT_USER_CODE_SEG | 3;       /* Ring 3 User Code (0x23) */
    frame->rflags = 0x202;                   /* IF = 1, reserved bit 1 = 1 */
    frame->rsp = proc->user_stack_top;
    frame->ss = GDT_USER_DATA_SEG | 3;       /* Ring 3 User Data (0x1B) */
    frame->vector = 0x20;

    proc->saved_rsp = (uint64_t)frame;

    /* 6. Register in process table and ready queue */
    process_table[slot] = proc;
    proc->state = PROCESS_READY;
    scheduler_add(proc);

    return proc;
}

void process_exit(int status) {
    process_t *curr = process_get_current();
    if (curr) {
        curr->state = PROCESS_TERMINATED;
        curr->exit_status = status;

        /* Print exit information */
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO] Process ");
        vga_print_dec(curr->pid);
        vga_print(" (");
        vga_print(curr->name);
        vga_print(") exited with status ");
        vga_print_dec((uint64_t)status);
        vga_println("");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        /* Remove from scheduler ready queue and trigger context switch */
        scheduler_remove(curr);
        scheduler_request_reschedule();
    }

    /* Wait for next interrupt / context switch */
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

/* ── Process Table Listing (ps command) ───────────────────────────────────── */

static const char *state_to_string(process_state_t st) {
    switch (st) {
        case PROCESS_NEW:        return "NEW";
        case PROCESS_READY:      return "READY";
        case PROCESS_RUNNING:    return "RUNNING";
        case PROCESS_SLEEPING:   return "SLEEPING";
        case PROCESS_TERMINATED: return "TERMINATED";
        case PROCESS_IDLE:       return "IDLE";
        default:                 return "UNKNOWN";
    }
}

void process_list(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("PID   STATE       RUNTIME      SWITCHES  NAME");
    vga_println("--------------------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* Print idle task first */
    process_t *idle = scheduler_get_idle();
    if (idle) {
        vga_print_dec(idle->pid);
        vga_print("     ");
        vga_print(state_to_string(idle->state));
        vga_print("        ");
        vga_print_dec(idle->runtime_ticks);
        vga_print("        ");
        vga_print_dec(idle->context_switches);
        vga_print("         ");
        vga_println(idle->name);
    }

    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = process_table[i];
        if (!p) continue;

        vga_print_dec(p->pid);
        if (p->pid < 10) vga_print("     ");
        else vga_print("    ");

        const char *st = state_to_string(p->state);
        vga_print(st);
        size_t slen = strlen(st);
        for (size_t s = slen; s < 12; s++) vga_print(" ");

        vga_print_dec(p->runtime_ticks);
        if (p->runtime_ticks < 10) vga_print("          ");
        else if (p->runtime_ticks < 100) vga_print("         ");
        else vga_print("        ");

        vga_print_dec(p->context_switches);
        if (p->context_switches < 10) vga_print("         ");
        else if (p->context_switches < 100) vga_print("        ");
        else vga_print("       ");

        vga_println(p->name);
    }
}

process_t *process_spawn_init(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO] Creating init process PID ");
    vga_print_dec(next_pid);
    vga_println("");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    const void *blob = user_get_prog_a();
    process_t *proc = process_create("init_proc", USER_CODE_BASE, blob, USER_PROGRAM_BLOB_SIZE);
    if (!proc) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FATAL: Failed to create init process");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return NULL;
    }
    return proc;
}

void process_run(process_t *proc) {
    if (!proc) return;
    if (!scheduler_is_active()) {
        scheduler_start();
    }
    scheduler_yield();
}
