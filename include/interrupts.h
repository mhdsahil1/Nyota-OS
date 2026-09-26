#ifndef NYOTA_INTERRUPTS_H
#define NYOTA_INTERRUPTS_H

#include "types.h"

/* Total supported interrupt vectors */
#define INTERRUPT_VECTOR_COUNT 256

/* IRQ Base Vector (remapped by 8259 PIC) */
#define IRQ_BASE_VECTOR         32
#define IRQ_COUNT               16

/* Standard IRQ definitions */
#define IRQ_TIMER               0   /* Vector 32 */
#define IRQ_KEYBOARD            1   /* Vector 33 */
#define IRQ_CASCADE             2   /* Vector 34 */
#define IRQ_COM2                3   /* Vector 35 */
#define IRQ_COM1                4   /* Vector 36 */
#define IRQ_LPT2                5   /* Vector 37 */
#define IRQ_FLOPPY              6   /* Vector 38 */
#define IRQ_SPURIOUS_MASTER     7   /* Vector 39 */
#define IRQ_RTC                 8   /* Vector 40 */
#define IRQ_ACPI                9   /* Vector 41 */
#define IRQ_MOUSE               12  /* Vector 44 */
#define IRQ_FPU                 13  /* Vector 45 */
#define IRQ_PRIMARY_ATA         14  /* Vector 46 */
#define IRQ_SECONDARY_ATA       15  /* Vector 47 */

/*
 * x86_64 Interrupt Frame structure
 * Must precisely match the stack frame pushed by CPU and ASM stubs.
 */
typedef struct __attribute__((packed)) {
    /* Pushed by ASM common stub (in reverse push order) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Pushed by specific ISR stub */
    uint64_t vector;
    uint64_t error_code;

    /* Pushed automatically by CPU on interrupt entry */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/* Function pointer type for C interrupt handlers */
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);

/* Core Dispatcher & Registration APIs */
void interrupts_init(void);
void interrupt_register_handler(uint8_t vector, interrupt_handler_t handler);
void interrupt_unregister_handler(uint8_t vector);
void interrupt_dispatch(interrupt_frame_t *frame);

/* Enable / Disable interrupts */
static inline void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

static inline void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

static inline bool interrupts_are_enabled(void) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
    return (rflags & (1 << 9)) != 0;
}

#endif /* NYOTA_INTERRUPTS_H */
