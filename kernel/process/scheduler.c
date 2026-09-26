/* =============================================================================
 * Nyota OS — Preemptive Round-Robin Scheduler
 * Time-slice preemption, ready/sleep queue management, TSS RSP0, and CR3 switching.
 * =========================================================================== */

#include "scheduler.h"
#include "process.h"
#include "timer.h"
#include "tss.h"
#include "paging.h"
#include "heap.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"
#include "gdt.h"

/* Ready Queue (Doubly linked list) */
static process_t *ready_head = NULL;
static process_t *ready_tail = NULL;

/* Sleep Queue (Singly linked list) */
static process_t *sleep_queue = NULL;

/* Current running process and idle process */
static process_t *current_proc = NULL;
static process_t *idle_proc = NULL;

/* Scheduler state flags & metrics */
static bool scheduler_active = false;
static bool reschedule_requested = false;
static uint32_t current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
static uint64_t total_context_switches = 0;

/* ── Kernel Idle Task ─────────────────────────────────────────────────────── */

static void idle_task(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

/* ── Ready Queue Operations ───────────────────────────────────────────────── */

void scheduler_add(process_t *proc) {
    if (!proc || proc == idle_proc) return;

    /* Ensure process is not already in the ready queue */
    process_t *chk = ready_head;
    while (chk) {
        if (chk == proc) return;
        chk = chk->next;
    }

    proc->state = PROCESS_READY;
    proc->next = NULL;
    proc->prev = ready_tail;

    if (ready_tail) {
        ready_tail->next = proc;
    } else {
        ready_head = proc;
    }
    ready_tail = proc;
}

void scheduler_remove(process_t *proc) {
    if (!proc) return;

    if (proc->prev) {
        proc->prev->next = proc->next;
    } else if (ready_head == proc) {
        ready_head = proc->next;
    }

    if (proc->next) {
        proc->next->prev = proc->prev;
    } else if (ready_tail == proc) {
        ready_tail = proc->prev;
    }

    proc->next = NULL;
    proc->prev = NULL;
}

process_t *scheduler_next(void) {
    if (!ready_head) {
        return idle_proc;
    }

    process_t *next = ready_head;
    ready_head = next->next;

    if (ready_head) {
        ready_head->prev = NULL;
    } else {
        ready_tail = NULL;
    }

    next->next = NULL;
    next->prev = NULL;
    return next;
}

/* ── Lifecycle & Initialization ───────────────────────────────────────────── */

void scheduler_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    sleep_queue = NULL;
    scheduler_active = false;
    reschedule_requested = false;
    current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
    total_context_switches = 0;

    /* Create dedicated Kernel Idle Process (PID 0) */
    idle_proc = (process_t *)kmalloc(sizeof(process_t));
    if (!idle_proc) {
        kernel_panic("scheduler_init: failed to allocate idle PCB");
    }
    memset(idle_proc, 0, sizeof(process_t));

    idle_proc->pid = 0;
    memcpy(idle_proc->name, "idle", 5);
    idle_proc->state = PROCESS_IDLE;
    idle_proc->cr3 = (uint64_t)paging_get_kernel_pml4();

    /* Allocate dedicated 4 KiB kernel stack for idle task */
    void *idle_kstack = kmalloc(4096);
    if (!idle_kstack) {
        kernel_panic("scheduler_init: failed to allocate idle stack");
    }
    idle_proc->kernel_stack = (uint64_t)idle_kstack;
    idle_proc->kernel_stack_top = (uint64_t)idle_kstack + 4096;

    /* Fabricate initial interrupt_frame_t for idle_task (executes in Ring 0) */
    interrupt_frame_t *frame = (interrupt_frame_t *)(idle_proc->kernel_stack_top - sizeof(interrupt_frame_t));
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = (uint64_t)idle_task;
    frame->cs = GDT_KERNEL_CODE_SEG;       /* Ring 0 Kernel Code */
    frame->rflags = 0x202;                 /* IF = 1 */
    frame->rsp = idle_proc->kernel_stack_top - 64;
    frame->ss = GDT_KERNEL_DATA_SEG;       /* Ring 0 Kernel Data */
    frame->vector = 0x20;

    idle_proc->saved_rsp = (uint64_t)frame;
    current_proc = idle_proc;
}

void scheduler_start(void) {
    scheduler_active = true;
    reschedule_requested = true;
}

bool scheduler_is_active(void) {
    return scheduler_active;
}

process_t *scheduler_get_current(void) {
    return current_proc;
}

process_t *scheduler_get_idle(void) {
    return idle_proc;
}

void scheduler_request_reschedule(void) {
    reschedule_requested = true;
}

/* ── Cooperative Scheduling APIs ──────────────────────────────────────────── */

void scheduler_yield(void) {
    reschedule_requested = true;
}

void scheduler_sleep(uint64_t ms) {
    if (!current_proc) return;

    uint64_t ticks = (ms * PIT_DEFAULT_HZ) / 1000;
    if (ticks == 0) ticks = 1;

    current_proc->wakeup_tick = timer_ticks() + ticks;
    current_proc->state = PROCESS_SLEEPING;

    /* Insert into sleep queue */
    current_proc->next = sleep_queue;
    sleep_queue = current_proc;

    reschedule_requested = true;
}

/* ── Timer Tick Hook ──────────────────────────────────────────────────────── */

void scheduler_on_timer_tick(void) {
    uint64_t now = timer_ticks();

    /* 1. Track runtime ticks of current process */
    if (current_proc) {
        current_proc->runtime_ticks++;
    }

    /* 2. Check sleep queue for processes ready to wake up */
    process_t **curr = &sleep_queue;
    while (*curr) {
        process_t *p = *curr;
        if (now >= p->wakeup_tick) {
            /* Unlink from sleep queue */
            *curr = p->next;
            p->next = NULL;
            p->prev = NULL;

            /* Transition to READY and add to scheduler ready queue */
            p->state = PROCESS_READY;
            scheduler_add(p);
        } else {
            curr = &(p->next);
        }
    }

    /* 3. Decrement time slice for preemption */
    if (current_time_slice > 0) {
        current_time_slice--;
    }
    if (current_time_slice == 0) {
        reschedule_requested = true;
    }
}

/* ── Context Switch Engine ────────────────────────────────────────────────── */

interrupt_frame_t *scheduler_schedule(interrupt_frame_t *frame) {
    if (!scheduler_active || !current_proc) {
        return frame;
    }

    /* If time slice hasn't expired and no voluntary yield requested, continue current process */
    if (!reschedule_requested && current_time_slice > 0 && current_proc->state == PROCESS_RUNNING) {
        return frame;
    }

    reschedule_requested = false;
    current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;

    process_t *prev = current_proc;

    /* Save current process's kernel stack pointer */
    prev->saved_rsp = (uint64_t)frame;

    /* If prev is still RUNNING, move to READY and requeue */
    if (prev->state == PROCESS_RUNNING) {
        prev->state = PROCESS_READY;
        if (prev != idle_proc) {
            scheduler_add(prev);
        }
    }

    /* Select next process from ready queue */
    process_t *next = scheduler_next();
    if (!next) {
        next = idle_proc;
    }

    if (next == idle_proc) {
        next->state = PROCESS_IDLE;
    } else {
        next->state = PROCESS_RUNNING;
    }

    current_proc = next;
    total_context_switches++;
    next->context_switches++;

    /* Update TSS.RSP0 to target process's kernel stack top */
    tss_set_rsp0(next->kernel_stack_top);

    /* Switch CR3 address space if different */
    if (next->cr3 != prev->cr3) {
        paging_load_cr3(next->cr3);
    }

    return (interrupt_frame_t *)next->saved_rsp;
}

/* ── Diagnostic Statistics ────────────────────────────────────────────────── */

void scheduler_print_stats(void) {
    vga_print("Total Context Switches: ");
    vga_print_dec(total_context_switches);
    vga_println("");

    vga_print("Active Process: PID ");
    if (current_proc) {
        vga_print_dec(current_proc->pid);
        vga_print(" (");
        vga_print(current_proc->name);
        vga_println(")");
    } else {
        vga_println("None");
    }
}
