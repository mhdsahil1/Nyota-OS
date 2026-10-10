/* =============================================================================
 * Nyota OS — Symmetric Multiprocessing Preemptive Scheduler (Phase 11)
 * Per-CPU run queues, load-balancing distribution, work-stealing, and affinity.
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
#include "smp.h"
#include "lapic.h"
#include "spinlock.h"

/* Global Sleep Queue (Shared, protected by sleep_lock) */
static process_t *sleep_queue = NULL;
static spinlock_t sleep_lock = SPINLOCK_INIT;

/* Scheduler state flags & metrics */
static bool scheduler_active = false;
static uint64_t total_context_switches = 0;

/* ── Kernel Idle Task ─────────────────────────────────────────────────────── */

static void idle_task(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

/* ── Multicore Process Safety Queries ─────────────────────────────────────── */

bool process_is_running_anywhere(process_t *proc) {
    if (!proc) return false;
    uint32_t total_cpus = cpu_count();
    for (uint32_t i = 0; i < total_cpus; i++) {
        if (!cpu_is_online(i)) continue;
        cpu_t *c = cpu_get(i);
        if (c && c->current_proc == proc) {
            return true;
        }
    }
    return false;
}

bool process_is_running_on_other_cpu(process_t *proc, uint32_t my_cpu_id) {
    if (!proc) return false;
    uint32_t total_cpus = cpu_count();
    for (uint32_t i = 0; i < total_cpus; i++) {
        if (i == my_cpu_id || !cpu_is_online(i)) continue;
        cpu_t *c = cpu_get(i);
        if (c && c->current_proc == proc) {
            return true;
        }
    }
    return false;
}

bool scheduler_is_enqueued(process_t *proc) {
    if (!proc) return false;
    uint32_t total_cpus = cpu_count();
    for (uint32_t i = 0; i < total_cpus; i++) {
        cpu_t *c = cpu_get(i);
        if (!c) continue;
        uint64_t rflags = spinlock_acquire_irqsave(&c->lock);
        for (process_t *chk = c->ready_head; chk != NULL; chk = chk->next) {
            if (chk == proc) {
                spinlock_release_irqrestore(&c->lock, rflags);
                return true;
            }
        }
        spinlock_release_irqrestore(&c->lock, rflags);
    }
    return false;
}

/* ── Ready Queue Operations (Per-CPU) ─────────────────────────────────────── */

void scheduler_add(process_t *proc) {
    if (!proc) return;

    /* Do not enqueue idle tasks */
    cpu_t *curr_cpu = cpu_current();
    if (curr_cpu && proc == &curr_cpu->idle_proc) return;

    /* If process is currently executing on any core, do not enqueue it */
    if (process_is_running_anywhere(proc)) return;

    /* Verify not already present in any core's ready queue */
    if (scheduler_is_enqueued(proc)) return;

    /* Default affinity to all CPUs if not configured */
    if (proc->cpu_affinity == 0) {
        proc->cpu_affinity = 0xFFFFFFFFFFFFFFFFULL;
    }

    /* Select the optimal online CPU matching affinity with the lowest load */
    uint32_t target_cpu_id = 0;
    uint32_t min_load = 0xFFFFFFFF;
    uint32_t total_cpus = cpu_count();

    for (uint32_t i = 0; i < total_cpus; i++) {
        if (!cpu_is_online(i)) continue;
        if (!(proc->cpu_affinity & (1ULL << i))) continue;

        cpu_t *c = cpu_get(i);
        if (!c) continue;

        uint32_t load = 0;
        uint64_t rflags = spinlock_acquire_irqsave(&c->lock);
        if (c->current_proc && c->current_proc != &c->idle_proc) {
            load++;
        }
        for (process_t *p = c->ready_head; p != NULL; p = p->next) {
            load++;
        }
        spinlock_release_irqrestore(&c->lock, rflags);

        if (load < min_load) {
            min_load = load;
            target_cpu_id = i;
        }
    }

    cpu_t *target_cpu = cpu_get(target_cpu_id);
    if (!target_cpu) target_cpu = curr_cpu ? curr_cpu : cpu_get(0);

    uint64_t rflags = spinlock_acquire_irqsave(&target_cpu->lock);

    /* Verify not already present in this CPU's ready queue */
    for (process_t *chk = target_cpu->ready_head; chk != NULL; chk = chk->next) {
        if (chk == proc) {
            spinlock_release_irqrestore(&target_cpu->lock, rflags);
            return;
        }
    }

    proc->state = PROCESS_READY;
    proc->current_cpu = target_cpu->id;
    proc->next = NULL;
    proc->prev = target_cpu->ready_tail;

    if (target_cpu->ready_tail) {
        target_cpu->ready_tail->next = proc;
    } else {
        target_cpu->ready_head = proc;
    }
    target_cpu->ready_tail = proc;

    spinlock_release_irqrestore(&target_cpu->lock, rflags);

    /* If assigned to an idle remote CPU, send reschedule IPI to wake it;
     * If assigned to current CPU and it is idle, request local reschedule */
    if (curr_cpu) {
        if (target_cpu->id != curr_cpu->id) {
            if (target_cpu->current_proc == &target_cpu->idle_proc) {
                smp_send_ipi(target_cpu->id, IPI_VECTOR_RESCHEDULE);
            }
        } else {
            if (target_cpu->current_proc == &target_cpu->idle_proc) {
                curr_cpu->reschedule_requested = true;
            }
        }
    }
}

void scheduler_remove(process_t *proc) {
    if (!proc) return;

    /* 1. Unlink from sleep queue if present */
    uint64_t s_rflags = spinlock_acquire_irqsave(&sleep_lock);
    process_t **s_curr = &sleep_queue;
    while (*s_curr) {
        if (*s_curr == proc) {
            *s_curr = proc->next;
            proc->next = NULL;
            proc->prev = NULL;
            spinlock_release_irqrestore(&sleep_lock, s_rflags);
            return;
        }
        s_curr = &((*s_curr)->next);
    }
    spinlock_release_irqrestore(&sleep_lock, s_rflags);

    /* 2. Unlink from per-CPU ready queue if present */
    uint32_t total_cpus = cpu_count();
    for (uint32_t i = 0; i < total_cpus; i++) {
        cpu_t *c = cpu_get(i);
        if (!c) continue;

        uint64_t rflags = spinlock_acquire_irqsave(&c->lock);
        bool found = false;
        for (process_t *curr = c->ready_head; curr != NULL; curr = curr->next) {
            if (curr == proc) {
                found = true;
                break;
            }
        }

        if (found) {
            if (proc->prev) {
                proc->prev->next = proc->next;
            } else {
                c->ready_head = proc->next;
            }

            if (proc->next) {
                proc->next->prev = proc->prev;
            } else {
                c->ready_tail = proc->prev;
            }

            proc->next = NULL;
            proc->prev = NULL;
            spinlock_release_irqrestore(&c->lock, rflags);
            return;
        }

        spinlock_release_irqrestore(&c->lock, rflags);
    }

    proc->next = NULL;
    proc->prev = NULL;
}

process_t *scheduler_next(void) {
    cpu_t *cpu = cpu_current();
    if (!cpu) return NULL;

    uint64_t rflags = spinlock_acquire_irqsave(&cpu->lock);

    for (process_t *curr = cpu->ready_head; curr != NULL; curr = curr->next) {
        if (curr->state == PROCESS_READY && !process_is_running_on_other_cpu(curr, cpu->id)) {
            if (curr->prev) {
                curr->prev->next = curr->next;
            } else {
                cpu->ready_head = curr->next;
            }
            if (curr->next) {
                curr->next->prev = curr->prev;
            } else {
                cpu->ready_tail = curr->prev;
            }
            curr->next = NULL;
            curr->prev = NULL;
            spinlock_release_irqrestore(&cpu->lock, rflags);
            return curr;
        }
    }

    /* Release local run-queue lock before attempting to steal from remote cores.
     * This avoids deadlocks where multiple idle cores concurrently hold their local
     * lock and attempt to acquire each other's lock. */
    spinlock_release_irqrestore(&cpu->lock, rflags);

    /* Work Stealing: If local run queue is empty, attempt to steal from other cores */
    uint32_t ncpus = cpu_count();
    for (uint32_t i = 0; i < ncpus; i++) {
        if (i == cpu->id || !cpu_is_online(i)) continue;

        cpu_t *victim = cpu_get(i);
        if (!victim) continue;

        uint64_t v_rflags = spinlock_acquire_irqsave(&victim->lock);
        /* Only steal if victim has at least 2 ready processes */
        if (victim->ready_head && victim->ready_head->next) {
            process_t *stolen = victim->ready_tail;
            if (stolen && stolen != victim->current_proc && stolen->state == PROCESS_READY &&
                !process_is_running_on_other_cpu(stolen, cpu->id) && (stolen->cpu_affinity & (1ULL << cpu->id))) {
                /* Unlink from victim */
                if (stolen->prev) {
                    stolen->prev->next = NULL;
                    victim->ready_tail = stolen->prev;
                } else {
                    victim->ready_head = NULL;
                    victim->ready_tail = NULL;
                }
                stolen->next = NULL;
                stolen->prev = NULL;
                stolen->current_cpu = cpu->id;

                spinlock_release_irqrestore(&victim->lock, v_rflags);
                return stolen;
            }
        }
        spinlock_release_irqrestore(&victim->lock, v_rflags);
    }

    return &cpu->idle_proc;
}

/* ── Lifecycle & Initialization ───────────────────────────────────────────── */

void scheduler_init(void) {
    sleep_queue = NULL;
    scheduler_active = false;
    total_context_switches = 0;

    /* Initialize BSP idle task */
    cpu_t *bsp = cpu_get(0);
    if (bsp) {
        memset(&bsp->idle_proc, 0, sizeof(process_t));
        bsp->idle_proc.pid = 0;
        memcpy(bsp->idle_proc.name, "idle", 5);
        bsp->idle_proc.state = PROCESS_IDLE;
        bsp->idle_proc.cr3 = (uint64_t)paging_get_kernel_pml4();
        bsp->idle_proc.kernel_stack = (uint64_t)bsp->idle_stack;
        bsp->idle_proc.kernel_stack_top = (uint64_t)bsp->idle_stack + sizeof(bsp->idle_stack);

        interrupt_frame_t *frame = (interrupt_frame_t *)(bsp->idle_proc.kernel_stack_top - sizeof(interrupt_frame_t));
        memset(frame, 0, sizeof(interrupt_frame_t));

        frame->rip = (uint64_t)idle_task;
        frame->cs = GDT_KERNEL_CODE_SEG;
        frame->rflags = 0x202; /* IF = 1 */
        frame->rsp = bsp->idle_proc.kernel_stack_top - 64;
        frame->ss = GDT_KERNEL_DATA_SEG;
        frame->vector = 0x20;

        bsp->idle_proc.saved_rsp = (uint64_t)frame;
        bsp->current_proc = &bsp->idle_proc;
        bsp->ready_head = NULL;
        bsp->ready_tail = NULL;
        bsp->current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
    }
}

void scheduler_start(void) {
    scheduler_active = true;
    smp_set_all_ready();
    cpu_t *cpu = cpu_current();
    if (cpu) {
        cpu->reschedule_requested = true;
    }
}

bool scheduler_is_active(void) {
    return scheduler_active;
}

process_t *scheduler_get_current(void) {
    cpu_t *cpu = cpu_current();
    return cpu ? cpu->current_proc : NULL;
}

process_t *scheduler_get_idle(void) {
    cpu_t *cpu = cpu_current();
    return cpu ? &cpu->idle_proc : NULL;
}

void scheduler_request_reschedule(void) {
    cpu_t *cpu = cpu_current();
    if (cpu) {
        cpu->reschedule_requested = true;
    }
}

/* ── Cooperative Scheduling APIs ──────────────────────────────────────────── */

void scheduler_yield(void) {
    cpu_t *cpu = cpu_current();
    if (cpu) {
        cpu->reschedule_requested = true;
    }
}

void scheduler_sleep(uint64_t ms) {
    cpu_t *cpu = cpu_current();
    if (!cpu || !cpu->current_proc) return;

    process_t *curr = cpu->current_proc;
    uint64_t ticks = (ms * PIT_DEFAULT_HZ) / 1000;
    if (ticks == 0) ticks = 1;

    curr->wakeup_tick = timer_ticks() + ticks;
    curr->state = PROCESS_SLEEPING;

    /* Insert into global sleep queue under spinlock */
    uint64_t rflags = spinlock_acquire_irqsave(&sleep_lock);
    curr->next = sleep_queue;
    sleep_queue = curr;
    spinlock_release_irqrestore(&sleep_lock, rflags);

    cpu->reschedule_requested = true;
}

void scheduler_wake(process_t *proc) {
    if (!proc) return;

    /* 1. Unlink from sleep queue if present */
    uint64_t rflags = spinlock_acquire_irqsave(&sleep_lock);
    process_t **curr = &sleep_queue;
    while (*curr) {
        if (*curr == proc) {
            *curr = proc->next;
            proc->next = NULL;
            proc->prev = NULL;
            break;
        }
        curr = &((*curr)->next);
    }
    spinlock_release_irqrestore(&sleep_lock, rflags);

    /* 2. Transition from SLEEPING to READY and enroll into run queue */
    if (proc->state == PROCESS_SLEEPING) {
        proc->state = PROCESS_READY;
        scheduler_add(proc);
    }
}

/* ── Timer Tick Hook ──────────────────────────────────────────────────────── */

void scheduler_on_timer_tick(void) {
    uint64_t now = timer_ticks();
    cpu_t *cpu = cpu_current();

    if (cpu && cpu->current_proc && cpu->current_proc->state == PROCESS_RUNNING) {
        cpu->current_proc->runtime_ticks++;
    }

    /* Check global sleep queue */
    uint64_t rflags = spinlock_acquire_irqsave(&sleep_lock);
    process_t **curr = &sleep_queue;
    while (*curr) {
        process_t *p = *curr;
        if (now >= p->wakeup_tick) {
            *curr = p->next;
            p->next = NULL;
            p->prev = NULL;
            p->state = PROCESS_READY;
            scheduler_add(p);
        } else {
            curr = &(p->next);
        }
    }
    spinlock_release_irqrestore(&sleep_lock, rflags);

    /* Decrement time slice on calling core */
    if (cpu) {
        if (cpu->current_time_slice > 0) {
            cpu->current_time_slice--;
        }
        if (cpu->current_time_slice == 0) {
            cpu->reschedule_requested = true;
        }
    }
}

/* ── Context Switch Engine (Per-CPU) ──────────────────────────────────────── */

interrupt_frame_t *scheduler_schedule(interrupt_frame_t *frame) {
    if (!scheduler_active) {
        return frame;
    }

    cpu_t *cpu = cpu_current();
    if (!cpu) {
        return frame;
    }
    if (!cpu->current_proc) {
        cpu->current_proc = &cpu->idle_proc;
    }

    /* Continue current task if time slice hasn't expired and no voluntary yield */
    if (!cpu->reschedule_requested && cpu->current_time_slice > 0 && cpu->current_proc->state == PROCESS_RUNNING) {
        return frame;
    }

    cpu->reschedule_requested = false;
    cpu->current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;

    process_t *prev = cpu->current_proc;
    prev->saved_rsp = (uint64_t)frame;

    /* Dequeue next process for this core */
    process_t *next = scheduler_next();
    if (!next || next == &cpu->idle_proc) {
        if (prev->state == PROCESS_RUNNING && prev != &cpu->idle_proc) {
            next = prev;
        } else {
            next = &cpu->idle_proc;
        }
    } else {
        /* If prev was RUNNING, re-enqueue locally at tail of this core's run queue */
        if (prev->state == PROCESS_RUNNING && prev != &cpu->idle_proc) {
            prev->state = PROCESS_READY;
            uint64_t q_rflags = spinlock_acquire_irqsave(&cpu->lock);
            prev->next = NULL;
            prev->prev = cpu->ready_tail;
            if (cpu->ready_tail) {
                cpu->ready_tail->next = prev;
            } else {
                cpu->ready_head = prev;
            }
            cpu->ready_tail = prev;
            spinlock_release_irqrestore(&cpu->lock, q_rflags);
        }
    }

    if (next == &cpu->idle_proc) {
        next->state = PROCESS_IDLE;
    } else {
        next->state = PROCESS_RUNNING;
        next->current_cpu = cpu->id;
    }

    cpu->current_proc = next;
    cpu->context_switches++;
    next->context_switches++;
    total_context_switches++;

    /* Update TSS.RSP0 for this CPU core */
    cpu->tss.rsp0 = next->kernel_stack_top;
    tss_set_rsp0(next->kernel_stack_top);

    /* Switch CR3 address space if necessary */
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

    uint32_t ncpus = cpu_count();
    for (uint32_t i = 0; i < ncpus; i++) {
        cpu_t *c = cpu_get(i);
        if (!c || !c->online) continue;

        vga_print("  CPU ");
        vga_print_dec(c->id);
        vga_print(" (APIC ");
        vga_print_dec(c->apic_id);
        vga_print("): switches=");
        vga_print_dec(c->context_switches);
        vga_print(" ticks=");
        vga_print_dec(c->ticks);
        vga_print(" proc=");
        if (c->current_proc) {
            vga_print_dec(c->current_proc->pid);
            vga_print(" (");
            vga_print(c->current_proc->name);
            vga_print(")");
        } else {
            vga_print("none");
        }
        vga_println("");
    }
}
