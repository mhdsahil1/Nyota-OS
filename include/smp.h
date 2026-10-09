/* =============================================================================
 * Nyota OS — Symmetric Multiprocessing (SMP) Architecture Header (Phase 11)
 * Per-CPU state, multi-core boot sequence, IPIs, and TLB shootdown.
 * =========================================================================== */

#ifndef NYOTA_SMP_H
#define NYOTA_SMP_H

#include "types.h"
#include "gdt.h"
#include "tss.h"
#include "process.h"
#include "spinlock.h"

#define MAX_CPUS 32

/* Trampoline Mailbox Addresses in Low Memory (Identity Mapped) */
#define SMP_TRAMPOLINE_ADDR         0x8000ULL
#define SMP_TRAMPOLINE_STACK        0x8F00ULL   /* uint64_t: target RSP */
#define SMP_TRAMPOLINE_ENTRY        0x8F08ULL   /* uint64_t: target RIP */
#define SMP_TRAMPOLINE_CPU_ID       0x8F10ULL   /* uint32_t: target logical CPU ID */
#define SMP_TRAMPOLINE_STATUS       0x8F14ULL   /* uint32_t: status (1 = booted) */

/* Per-CPU Control Block */
typedef struct cpu {
    uint32_t id;                        /* Logical CPU index (0..MAX_CPUS-1) */
    uint32_t apic_id;                   /* Hardware Local APIC ID */
    volatile bool online;               /* True if core has booted and is active */
    bool is_bsp;                        /* True if Bootstrap Processor */

    /* CPU-local GDT and TSS */
    gdt_entry_t gdt[7];
    gdt_ptr_t gdt_ptr;
    tss_t tss;

    /* Per-CPU Kernel Stack (Dedicated 16 KiB) */
    uint64_t kernel_stack;              /* Base of allocated kernel stack */
    uint64_t kernel_stack_top;          /* Top of kernel stack (TSS.rsp0) */

    /* Scheduler & Task State */
    process_t *current_proc;            /* Currently executing process */
    process_t idle_proc;                /* Dedicated idle task for this CPU */
    uint8_t idle_stack[4096] __attribute__((aligned(16)));

    /* Per-CPU Metrics */
    volatile uint64_t ticks;            /* Local APIC timer ticks */
    volatile uint64_t syscall_count;    /* Total syscalls serviced on this core */
    volatile uint64_t context_switches; /* Total context switches */
    volatile uint64_t ipi_count;        /* Total IPIs received */

    /* Per-CPU Run Queue */
    process_t *ready_head;
    process_t *ready_tail;
    uint32_t current_time_slice;
    volatile bool reschedule_requested;
    spinlock_t lock;                    /* Protects this CPU's run queue */
} cpu_t;

/* Public SMP APIs */
void smp_init(void);
uint32_t cpu_count(void);
uint32_t cpu_online_count(void);
cpu_t *cpu_get(uint32_t id);
cpu_t *cpu_current(void);
bool cpu_is_online(uint32_t id);
uint32_t cpu_get_current_id(void);

void smp_send_ipi(uint32_t cpu_id, uint8_t vector);
void smp_broadcast_ipi(uint8_t vector);
void smp_tlb_shootdown(uint64_t virt);
void smp_set_all_ready(void);

/* Called by AP after trampoline transition to 64-bit mode */
void ap_kernel_entry(uint32_t cpu_id);

#endif /* NYOTA_SMP_H */
