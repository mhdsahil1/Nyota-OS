/* =============================================================================
 * Nyota OS — Symmetric Multiprocessing (SMP) Implementation (Phase 11)
 * CPU topology, AP startup sequence, per-CPU state, IPIs, and TLB shootdown.
 * =========================================================================== */

#include "smp.h"
#include "acpi.h"
#include "lapic.h"
#include "pmm.h"
#include "paging.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "timer.h"
#include "interrupts.h"
#include "idt.h"
#include "memory.h"
#include "scheduler.h"

/* Linker symbols for embedded AP trampoline binary */
extern uint8_t trampoline_blob_start[];
extern uint8_t trampoline_blob_end[];

static cpu_t cpu_table[MAX_CPUS];
static uint32_t cpu_detected_count = 1;
static uint32_t cpu_online_total = 1;
static int apic_to_cpu_id[256];
static bool smp_initialized = false;
static volatile bool smp_all_ready = false;

/* TLB Shootdown Synchronization */
static spinlock_t tlb_shootdown_lock = SPINLOCK_INIT;
static volatile uint64_t tlb_shootdown_addr = 0;
static volatile uint32_t tlb_shootdown_ack = 0;

/* ── CPU Lookup APIs ──────────────────────────────────────────────────────── */

uint32_t cpu_count(void) {
    return cpu_detected_count;
}

uint32_t cpu_online_count(void) {
    return cpu_online_total;
}

cpu_t *cpu_get(uint32_t id) {
    if (id >= cpu_detected_count) return NULL;
    return &cpu_table[id];
}

bool cpu_is_online(uint32_t id) {
    if (id >= cpu_detected_count) return false;
    return cpu_table[id].online;
}

cpu_t *cpu_current(void) {
    if (!smp_initialized) {
        return &cpu_table[0];
    }
    uint32_t apic_id = lapic_get_id();
    if (apic_id < 256) {
        int id = apic_to_cpu_id[apic_id];
        if (id >= 0 && (uint32_t)id < cpu_detected_count) {
            return &cpu_table[id];
        }
    }
    return &cpu_table[0];
}

uint32_t cpu_get_current_id(void) {
    cpu_t *c = cpu_current();
    return c ? c->id : 0;
}

/* ── Hardware Feature Enable (SSE / CR0 / CR4) ────────────────────────────── */

static void cpu_enable_features(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); /* Clear EM (Emulation) */
    cr0 &= ~(1ULL << 3); /* Clear TS (Task Switched) */
    cr0 |= (1ULL << 1);  /* Set MP (Monitor Coprocessor) */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile ("clts");

    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  /* OSFXSR: FXSAVE/FXRSTOR and SSE instructions */
    cr4 |= (1ULL << 10); /* OSXMMEXCPT: unmasked SSE FP exceptions */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
}

/* ── IPI & LAPIC Interrupt Handlers ───────────────────────────────────────── */

static void lapic_timer_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    cpu_t *c = cpu_current();
    if (!c) return;

    c->ticks++;
    if (scheduler_is_active()) {
        scheduler_on_timer_tick();
    }
}

static void ipi_reschedule_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    cpu_t *c = cpu_current();
    if (c) {
        c->ipi_count++;
        c->reschedule_requested = true;
    }
}

static inline void local_invlpg(uint64_t addr) {
    if (addr == 0) {
        paging_load_cr3(paging_read_cr3());
    } else {
        __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
    }
}

static void ipi_tlb_shootdown_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    cpu_t *c = cpu_current();
    if (c) c->ipi_count++;

    local_invlpg(tlb_shootdown_addr);
    atomic_inc(&tlb_shootdown_ack);
}

static void ipi_stop_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* ── IPI Dispatch APIs ────────────────────────────────────────────────────── */

void smp_send_ipi(uint32_t cpu_id, uint8_t vector) {
    if (cpu_id >= cpu_detected_count || !cpu_table[cpu_id].online) return;
    lapic_send_ipi(cpu_table[cpu_id].apic_id, vector);
}

void smp_broadcast_ipi(uint8_t vector) {
    lapic_broadcast_ipi(vector);
}

void smp_tlb_shootdown(uint64_t virt) {
    if (!smp_initialized || !smp_all_ready || cpu_online_total <= 1) {
        local_invlpg(virt);
        return;
    }

    uint64_t rflags = spinlock_acquire_irqsave(&tlb_shootdown_lock);

    tlb_shootdown_addr = virt;
    tlb_shootdown_ack = 0;
    uint32_t targets = cpu_online_total - 1;

    /* Flush local core TLB */
    local_invlpg(virt);

    /* Broadcast shootdown IPI to all remote cores */
    lapic_broadcast_ipi(IPI_VECTOR_TLB_SHOOTDOWN);

    /* Wait for remote core acknowledgments */
    uint32_t spins = 0;
    while (atomic_load(&tlb_shootdown_ack) < targets && spins++ < 50000) {
        __asm__ volatile ("pause");
    }

    spinlock_release_irqrestore(&tlb_shootdown_lock, rflags);
}

void smp_set_all_ready(void) {
    smp_all_ready = true;
}

/* ── Application Processor (AP) Entry Point ───────────────────────────────── */

static void ap_idle_task(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

void ap_kernel_entry(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) {
        while (1) { __asm__ volatile ("cli; hlt"); }
    }

    cpu_t *cpu = &cpu_table[cpu_id];

    /* 1. Load IDT */
    idt_load();

    /* 2. Initialize and load per-CPU GDT & TSS */
    gdt_init_cpu(cpu);

    /* 3. Enable SSE and CPU hardware extensions */
    cpu_enable_features();

    /* 4. Initialize Local APIC on this core */
    lapic_init();

    /* 5. Initialize Local APIC periodic timer */
    lapic_timer_init(100);

    /* 6. Setup dedicated Idle Task for this core */
    memset(&cpu->idle_proc, 0, sizeof(process_t));
    cpu->idle_proc.pid = 0;
    memcpy(cpu->idle_proc.name, "idle", 5);
    cpu->idle_proc.state = PROCESS_IDLE;
    cpu->idle_proc.cr3 = (uint64_t)paging_get_kernel_pml4();
    cpu->idle_proc.kernel_stack = (uint64_t)cpu->idle_stack;
    cpu->idle_proc.kernel_stack_top = (uint64_t)cpu->idle_stack + sizeof(cpu->idle_stack);

    interrupt_frame_t *frame = (interrupt_frame_t *)(cpu->idle_proc.kernel_stack_top - sizeof(interrupt_frame_t));
    memset(frame, 0, sizeof(interrupt_frame_t));
    frame->rip = (uint64_t)ap_idle_task;
    frame->cs = GDT_KERNEL_CODE_SEG;
    frame->rflags = 0x202; /* IF = 1 */
    frame->rsp = cpu->idle_proc.kernel_stack_top - 64;
    frame->ss = GDT_KERNEL_DATA_SEG;
    frame->vector = 0x20;
    cpu->idle_proc.saved_rsp = (uint64_t)frame;

    cpu->current_proc = &cpu->idle_proc;
    cpu->current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
    cpu->ready_head = NULL;
    cpu->ready_tail = NULL;
    spinlock_init(&cpu->lock);

    /* 7. Signal online to BSP and mailbox */
    cpu->online = true;
    *(volatile uint32_t *)SMP_TRAMPOLINE_STATUS = 1;

    /* 8. Wait for BSP to signal multitasking start */
    while (!smp_all_ready) {
        __asm__ volatile ("pause");
    }

    /* 9. Enable interrupts and enter core idle loop */
    __asm__ volatile ("sti");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void smp_delay_ms(uint32_t ms) {
    for (uint32_t m = 0; m < ms; m++) {
        for (volatile uint32_t i = 0; i < 50000; i++) {
            __asm__ volatile ("pause");
        }
    }
}

/* ── SMP Bootstrap Initialization (Executed by BSP) ───────────────────────── */

void smp_init(void) {
    for (int i = 0; i < 256; i++) {
        apic_to_cpu_id[i] = -1;
    }
    memset(cpu_table, 0, sizeof(cpu_table));

    /* 1. Discover CPU topology via ACPI MADT */
    acpi_init();
    cpu_detected_count = acpi_get_cpu_count();
    if (cpu_detected_count == 0) cpu_detected_count = 1;
    if (cpu_detected_count > MAX_CPUS) cpu_detected_count = MAX_CPUS;

    uint32_t bsp_apic = acpi_get_bsp_apic_id();

    vga_print("[SMP] detected ");
    vga_print_dec(cpu_detected_count);
    vga_println(" CPUs");

    vga_print("[SMP] BSP APIC ID: ");
    vga_print_dec(bsp_apic);
    vga_println("");

    /* 2. Configure Bootstrap Processor (CPU 0) */
    cpu_t *bsp = &cpu_table[0];
    bsp->id = 0;
    bsp->apic_id = bsp_apic;
    bsp->online = true;
    bsp->is_bsp = true;
    bsp->kernel_stack = 0x80000;
    bsp->kernel_stack_top = 0x90000;
    bsp->current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
    spinlock_init(&bsp->lock);

    memset(&bsp->idle_proc, 0, sizeof(process_t));
    bsp->idle_proc.pid = 0;
    memcpy(bsp->idle_proc.name, "idle", 5);
    bsp->idle_proc.state = PROCESS_IDLE;
    bsp->idle_proc.cr3 = (uint64_t)paging_get_kernel_pml4();
    bsp->idle_proc.kernel_stack = (uint64_t)bsp->idle_stack;
    bsp->idle_proc.kernel_stack_top = (uint64_t)bsp->idle_stack + sizeof(bsp->idle_stack);

    interrupt_frame_t *bsp_frame = (interrupt_frame_t *)(bsp->idle_proc.kernel_stack_top - sizeof(interrupt_frame_t));
    memset(bsp_frame, 0, sizeof(interrupt_frame_t));
    bsp_frame->rip = (uint64_t)ap_idle_task;
    bsp_frame->cs = GDT_KERNEL_CODE_SEG;
    bsp_frame->rflags = 0x202; /* IF = 1 */
    bsp_frame->rsp = bsp->idle_proc.kernel_stack_top - 64;
    bsp_frame->ss = GDT_KERNEL_DATA_SEG;
    bsp_frame->vector = 0x20;
    bsp->idle_proc.saved_rsp = (uint64_t)bsp_frame;

    bsp->current_proc = &bsp->idle_proc;

    apic_to_cpu_id[bsp->apic_id & 0xFF] = 0;
    cpu_online_total = 1;

    /* Initialize BSP's per-CPU GDT and TSS */
    gdt_init_cpu(bsp);

    /* 3. Initialize BSP Local APIC */
    lapic_init();

    /* Register LAPIC and IPI vector handlers */
    interrupt_register_handler(LAPIC_TIMER_VECTOR, lapic_timer_irq_handler);
    interrupt_register_handler(IPI_VECTOR_RESCHEDULE, ipi_reschedule_irq_handler);
    interrupt_register_handler(IPI_VECTOR_TLB_SHOOTDOWN, ipi_tlb_shootdown_irq_handler);
    interrupt_register_handler(IPI_VECTOR_STOP, ipi_stop_irq_handler);

    /* Start BSP Local APIC periodic timer (100 Hz) */
    lapic_timer_init(100);

    smp_initialized = true;

    /* 4. Boot Application Processors if multiple cores detected */
    if (cpu_detected_count > 1) {
        /* Copy AP trampoline machine code to physical address 0x8000 */
        size_t tramp_len = (size_t)(trampoline_blob_end - trampoline_blob_start);
        memcpy((void *)SMP_TRAMPOLINE_ADDR, trampoline_blob_start, tramp_len);

        for (uint32_t i = 1; i < cpu_detected_count; i++) {
            const acpi_cpu_t *acpi_c = acpi_get_cpu(i);
            if (!acpi_c) continue;

            cpu_t *ap = &cpu_table[i];
            ap->id = i;
            ap->apic_id = acpi_c->apic_id;
            ap->online = false;
            ap->is_bsp = false;
            ap->current_time_slice = SCHEDULER_DEFAULT_TIME_SLICE;
            spinlock_init(&ap->lock);
            apic_to_cpu_id[ap->apic_id & 0xFF] = (int)i;

            /* Allocate 16 KiB dedicated kernel stack */
            void *kstack = pmm_alloc_pages(4);
            if (!kstack) {
                kwarn("[SMP] failed to allocate kernel stack for AP");
                continue;
            }
            ap->kernel_stack = (uint64_t)kstack;
            ap->kernel_stack_top = ap->kernel_stack + (4 * PAGE_SIZE);

            vga_print("[SMP] starting APIC ID: ");
            vga_print_dec(ap->apic_id);
            vga_println("");

            /* Set up trampoline communication mailbox */
            *(volatile uint64_t *)SMP_TRAMPOLINE_STACK = ap->kernel_stack_top;
            *(volatile uint64_t *)SMP_TRAMPOLINE_ENTRY = (uint64_t)ap_kernel_entry;
            *(volatile uint32_t *)SMP_TRAMPOLINE_CPU_ID = i;
            *(volatile uint32_t *)SMP_TRAMPOLINE_STATUS = 0;

            /* INIT-SIPI-SIPI Startup Sequence */
            lapic_send_init(ap->apic_id);
            smp_delay_ms(10);

            /* SIPI vector 0x08 targets physical address 0x8000 */
            lapic_send_startup(ap->apic_id, 0x08);
            smp_delay_ms(2);

            if (!ap->online) {
                lapic_send_startup(ap->apic_id, 0x08);
                smp_delay_ms(2);
            }

            /* Wait up to 50 ms for AP to signal online */
            uint32_t spins = 0;
            while (!ap->online && spins++ < 500000) {
                __asm__ volatile ("pause");
            }

            if (ap->online) {
                cpu_online_total++;
                vga_print("[SMP] CPU ");
                vga_print_dec(i);
                vga_println(" online");
            } else {
                vga_print("[SMP] CPU ");
                vga_print_dec(i);
                vga_println(" startup failed");
            }
        }

        if (cpu_online_total == cpu_detected_count) {
            vga_println("[SMP] all CPUs online");
        } else {
            vga_print("[SMP] continuing with ");
            vga_print_dec(cpu_online_total);
            vga_println(" online CPUs");
        }
    }
}
