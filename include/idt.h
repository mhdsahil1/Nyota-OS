#ifndef NYOTA_IDT_H
#define NYOTA_IDT_H

#include "types.h"

#define IDT_ENTRIES 256

/* Gate Types and Attribute Flags (64-bit Long Mode) */
#define IDT_GATE_INTERRUPT  0x8E  /* Present(1), Ring 0(00), 64-bit Interrupt Gate (clears IF) */
#define IDT_GATE_TRAP       0x8F  /* Present(1), Ring 0(00), 64-bit Trap Gate */
#define IDT_GATE_USER_TRAP  0xEE  /* Present(1), Ring 3(11), 64-bit Trap Gate (e.g. INT3) */

/* 16-byte x86_64 IDT Gate Descriptor */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;    /* Handler address bits 0..15 */
    uint16_t selector;      /* Kernel Code Segment Selector (0x08) */
    uint8_t  ist;           /* IST index (bits 0..2), bits 3..7 reserved 0 */
    uint8_t  type_attr;     /* Type and attributes (P, DPL, Gate Type) */
    uint16_t offset_mid;    /* Handler address bits 16..31 */
    uint32_t offset_high;   /* Handler address bits 32..63 */
    uint32_t zero;          /* Reserved (must be 0) */
} idt_entry_t;

/* IDTR pointer structure loaded by LIDT */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

void idt_init(void);
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t flags);
void idt_load(void);

#endif /* NYOTA_IDT_H */
