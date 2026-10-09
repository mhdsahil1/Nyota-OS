/* =============================================================================
 * Nyota OS — Local APIC (LAPIC) Implementation (Phase 11 SMP)
 * Manages LAPIC MMIO, per-core timer calibration, EOI, and inter-core IPIs.
 * =========================================================================== */

#include "lapic.h"
#include "acpi.h"
#include "paging.h"
#include "kernel.h"
#include "interrupts.h"
#include "timer.h"
#include "serial.h"

#define IA32_APIC_BASE_MSR      0x1B
#define IA32_APIC_BASE_ENABLE   (1ULL << 11)

static volatile uint8_t *lapic_base = NULL;
static bool lapic_initialized = false;
static uint32_t lapic_calibrated_ticks = 0;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return *(volatile uint32_t *)(lapic_base + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) return;
    *(volatile uint32_t *)(lapic_base + reg) = val;
}

void lapic_send_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

uint32_t lapic_get_id(void) {
    if (!lapic_base) return 0;
    return (lapic_read(LAPIC_REG_ID) >> 24) & 0xFF;
}

bool lapic_is_enabled(void) {
    return lapic_initialized;
}

/* ── Inter-Processor Interrupts (IPIs) ────────────────────────────────────── */

void lapic_send_ipi(uint32_t dest_apic_id, uint8_t vector) {
    if (!lapic_base) return;

    lapic_write(LAPIC_REG_ICR_HIGH, (dest_apic_id & 0xFF) << 24);
    lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_FIXED | ICR_LEVEL_ASSERT | ICR_TRIGGER_EDGE | vector);

    while (lapic_read(LAPIC_REG_ICR_LOW) & ICR_STATUS_PENDING) {
        __asm__ volatile ("pause");
    }
}

void lapic_broadcast_ipi(uint8_t vector) {
    if (!lapic_base) return;

    lapic_write(LAPIC_REG_ICR_HIGH, 0);
    lapic_write(LAPIC_REG_ICR_LOW, ICR_SHORTHAND_ALL_EXC_SELF | ICR_DELIVERY_FIXED | ICR_LEVEL_ASSERT | ICR_TRIGGER_EDGE | vector);

    while (lapic_read(LAPIC_REG_ICR_LOW) & ICR_STATUS_PENDING) {
        __asm__ volatile ("pause");
    }
}

void lapic_send_init(uint32_t dest_apic_id) {
    if (!lapic_base) return;

    lapic_write(LAPIC_REG_ICR_HIGH, (dest_apic_id & 0xFF) << 24);
    lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_INIT | ICR_LEVEL_ASSERT | ICR_TRIGGER_EDGE);

    while (lapic_read(LAPIC_REG_ICR_LOW) & ICR_STATUS_PENDING) {
        __asm__ volatile ("pause");
    }
}

void lapic_send_startup(uint32_t dest_apic_id, uint8_t vector) {
    if (!lapic_base) return;

    lapic_write(LAPIC_REG_ICR_HIGH, (dest_apic_id & 0xFF) << 24);
    lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_STARTUP | ICR_LEVEL_ASSERT | ICR_TRIGGER_EDGE | vector);

    while (lapic_read(LAPIC_REG_ICR_LOW) & ICR_STATUS_PENDING) {
        __asm__ volatile ("pause");
    }
}

/* ── LAPIC Initialization ─────────────────────────────────────────────────── */

static void lapic_spurious_handler(interrupt_frame_t *frame) {
    (void)frame;
    /* Spurious interrupt does not require EOI per Intel spec */
}

void lapic_init(void) {
    /* 1. Retrieve LAPIC physical address from ACPI MADT */
    uint64_t phys_addr = acpi_get_lapic_address();
    if (phys_addr == 0) {
        phys_addr = LAPIC_DEFAULT_PHYS_BASE;
    }

    /* 2. Map LAPIC physical address in page tables if not already mapped */
    if (!lapic_base) {
        paging_map_page(phys_addr, phys_addr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
        lapic_base = (volatile uint8_t *)phys_addr;
    }

    /* 3. Ensure hardware Local APIC is globally enabled in MSR */
    uint64_t msr = rdmsr(IA32_APIC_BASE_MSR);
    if (!(msr & IA32_APIC_BASE_ENABLE)) {
        wrmsr(IA32_APIC_BASE_MSR, msr | IA32_APIC_BASE_ENABLE);
    }

    /* 4. Enable Local APIC and set spurious interrupt vector */
    interrupt_register_handler(LAPIC_SPURIOUS_VECTOR, lapic_spurious_handler);
    lapic_write(LAPIC_REG_SVR, (1 << 8) | LAPIC_SPURIOUS_VECTOR);

    /* 5. Set Task Priority to 0 (accept all interrupt priorities) */
    lapic_write(LAPIC_REG_TPR, 0);

    /* 6. Clear error status */
    lapic_write(LAPIC_REG_ESR, 0);
    lapic_write(LAPIC_REG_ESR, 0);

    /* 7. Route 8259 PIC interrupts via Virtual Wire Mode:
     * Only BSP routes LINT0 (ExtINT) and LINT1 (NMI).
     * APs must have LINT0 and LINT1 masked to avoid bus arbitration conflicts. */
    if (lapic_get_id() == acpi_get_bsp_apic_id()) {
        lapic_write(LAPIC_REG_LVT_LINT0, 0x00000700);
        lapic_write(LAPIC_REG_LVT_LINT1, 0x00000400);
    } else {
        lapic_write(LAPIC_REG_LVT_LINT0, 0x00010000);
        lapic_write(LAPIC_REG_LVT_LINT1, 0x00010000);
    }
    lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_SPURIOUS_VECTOR);

    /* 8. Send End-of-Interrupt to clear any stale state */
    lapic_send_eoi();

    lapic_initialized = true;
}

/* ── LAPIC Timer Calibration & Initialization ─────────────────────────────── */

void lapic_timer_init(uint32_t frequency_hz) {
    if (!lapic_base) return;

    if (lapic_calibrated_ticks == 0) {
        if (frequency_hz == 0) frequency_hz = 100;

        /* 1. Set divisor 16 and mask timer for measurement */
        lapic_write(LAPIC_REG_TIMER_DIV, LAPIC_TIMER_DIV_16);
        lapic_write(LAPIC_REG_LVT_TIMER, LVT_TIMER_MASKED);
        lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFF);

        /* 2. Synchronize with next PIT tick boundary */
        uint64_t start_tick = timer_ticks();
        while (timer_ticks() == start_tick) {
            __asm__ volatile ("pause");
        }
        uint32_t count_start = lapic_read(LAPIC_REG_TIMER_CURR);

        /* 3. Measure elapsed LAPIC counts over 2 PIT ticks (20 ms at 100 Hz) */
        uint64_t target_tick = timer_ticks() + 2;
        while (timer_ticks() < target_tick) {
            __asm__ volatile ("pause");
        }
        uint32_t count_end = lapic_read(LAPIC_REG_TIMER_CURR);

        uint32_t elapsed = (count_start > count_end) ? (count_start - count_end) : 2000000;
        /* elapsed is ticks in 20 ms. For 100 Hz (10 ms), count is elapsed / 2 */
        lapic_calibrated_ticks = (elapsed / 2) * (100 / frequency_hz);
        if (lapic_calibrated_ticks < 1000) {
            lapic_calibrated_ticks = 1000000;
        }
    }

    /* Configure periodic timer on LAPIC_TIMER_VECTOR */
    lapic_write(LAPIC_REG_TIMER_DIV, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_TIMER_PERIODIC | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_REG_TIMER_INIT, lapic_calibrated_ticks);
}

void lapic_timer_stop(void) {
    if (!lapic_base) return;
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_TIMER_MASKED);
    lapic_write(LAPIC_REG_TIMER_INIT, 0);
}
