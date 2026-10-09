/* =============================================================================
 * Nyota OS — Local Advanced Programmable Interrupt Controller (LAPIC) Header
 * Memory-mapped I/O registers, timer calibration, EOI, and IPI generation.
 * =========================================================================== */

#ifndef NYOTA_LAPIC_H
#define NYOTA_LAPIC_H

#include "types.h"

/* Standard Local APIC MMIO Base Physical Address */
#define LAPIC_DEFAULT_PHYS_BASE     0xFEE00000ULL

/* LAPIC Register Offsets */
#define LAPIC_REG_ID                0x0020
#define LAPIC_REG_VERSION           0x0030
#define LAPIC_REG_TPR               0x0080
#define LAPIC_REG_APR               0x0090
#define LAPIC_REG_PPR               0x00A0
#define LAPIC_REG_EOI               0x00B0
#define LAPIC_REG_RRD               0x00C0
#define LAPIC_REG_LDR               0x00D0
#define LAPIC_REG_DFR               0x00E0
#define LAPIC_REG_SVR               0x00F0
#define LAPIC_REG_ISR_BASE          0x0100
#define LAPIC_REG_TMR_BASE          0x0180
#define LAPIC_REG_IRR_BASE          0x0200
#define LAPIC_REG_ESR               0x0280
#define LAPIC_REG_ICR_LOW           0x0300
#define LAPIC_REG_ICR_HIGH          0x0310
#define LAPIC_REG_LVT_TIMER         0x0320
#define LAPIC_REG_LVT_THERMAL       0x0330
#define LAPIC_REG_LVT_PERF          0x0340
#define LAPIC_REG_LVT_LINT0         0x0350
#define LAPIC_REG_LVT_LINT1         0x0360
#define LAPIC_REG_LVT_ERROR         0x0370
#define LAPIC_REG_TIMER_INIT        0x0380
#define LAPIC_REG_TIMER_CURR        0x0390
#define LAPIC_REG_TIMER_DIV         0x03E0

/* Interrupt Vectors for LAPIC and SMP */
#define LAPIC_TIMER_VECTOR          0x40    /* Vector 64: Per-CPU Scheduler Tick */
#define IPI_VECTOR_RESCHEDULE       0x50    /* Vector 80: Request Scheduler Preemption */
#define IPI_VECTOR_TLB_SHOOTDOWN    0x51    /* Vector 81: Flush TLB on Page Mutation */
#define IPI_VECTOR_STOP             0x52    /* Vector 82: Halt Core (Panic/Shutdown) */
#define LAPIC_SPURIOUS_VECTOR       0x00FF  /* Vector 255: Spurious Interrupt */

/* ICR Low Register Command Flags */
#define ICR_DELIVERY_FIXED          0x00000000
#define ICR_DELIVERY_LOWEST         0x00000100
#define ICR_DELIVERY_SMI            0x00000200
#define ICR_DELIVERY_NMI            0x00000400
#define ICR_DELIVERY_INIT           0x00000500
#define ICR_DELIVERY_STARTUP        0x00000600

#define ICR_DEST_PHYSICAL           0x00000000
#define ICR_DEST_LOGICAL            0x00000800

#define ICR_STATUS_IDLE             0x00000000
#define ICR_STATUS_PENDING          0x00001000

#define ICR_LEVEL_DEASSERT          0x00000000
#define ICR_LEVEL_ASSERT            0x00004000

#define ICR_TRIGGER_EDGE            0x00000000
#define ICR_TRIGGER_LEVEL           0x00008000

#define ICR_SHORTHAND_NONE          0x00000000
#define ICR_SHORTHAND_SELF          0x00040000
#define ICR_SHORTHAND_ALL_INC_SELF  0x00080000
#define ICR_SHORTHAND_ALL_EXC_SELF  0x000C0000

/* LVT Timer Modes */
#define LVT_TIMER_ONESHOT           0x00000000
#define LVT_TIMER_PERIODIC          0x00020000
#define LVT_TIMER_MASKED            0x00010000

/* Timer Divider Configuration Values */
#define LAPIC_TIMER_DIV_1           0x0B
#define LAPIC_TIMER_DIV_2           0x00
#define LAPIC_TIMER_DIV_4           0x01
#define LAPIC_TIMER_DIV_8           0x02
#define LAPIC_TIMER_DIV_16          0x03
#define LAPIC_TIMER_DIV_32          0x08
#define LAPIC_TIMER_DIV_64          0x09
#define LAPIC_TIMER_DIV_128         0x0A

/* Public LAPIC APIs */
void lapic_init(void);
bool lapic_is_enabled(void);
uint32_t lapic_get_id(void);
void lapic_send_eoi(void);

void lapic_send_ipi(uint32_t dest_apic_id, uint8_t vector);
void lapic_broadcast_ipi(uint8_t vector);
void lapic_send_init(uint32_t dest_apic_id);
void lapic_send_startup(uint32_t dest_apic_id, uint8_t vector);

void lapic_timer_init(uint32_t frequency_hz);
void lapic_timer_stop(void);

#endif /* NYOTA_LAPIC_H */
