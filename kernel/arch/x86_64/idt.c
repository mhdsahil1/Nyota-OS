/* =============================================================================
 * Nyota OS — Interrupt Descriptor Table (64-bit Long Mode)
 * =========================================================================== */

#include "idt.h"
#include "kernel.h"
#include "memory.h"

/* 256-entry Interrupt Descriptor Table */
static idt_entry_t idt_entries[IDT_ENTRIES] __attribute__((aligned(16)));
static idt_ptr_t   idt_pointer;

/* Defined in kernel/arch/x86_64/interrupts.asm */
extern uint64_t isr_stub_table[IDT_ENTRIES];

void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt_entries[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt_entries[vector].selector    = selector;
    idt_entries[vector].ist         = 0;  /* 0 = legacy stack (no IST) */
    idt_entries[vector].type_attr   = flags;
    idt_entries[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt_entries[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt_entries[vector].zero        = 0;
}

void idt_load(void) {
    idt_pointer.limit = (uint16_t)(sizeof(idt_entries) - 1);
    idt_pointer.base  = (uint64_t)&idt_entries[0];

    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
}

void idt_init(void) {
    /* Clear table */
    memset(idt_entries, 0, sizeof(idt_entries));

    /* Populate all 256 interrupt gates with corresponding assembly stubs */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        /* Vector 3 (Breakpoint) can be triggered from user mode (Ring 3) if needed */
        uint8_t flags = (i == 3) ? IDT_GATE_USER_TRAP : IDT_GATE_INTERRUPT;
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x08, flags);
    }

    /* Load IDTR register */
    idt_load();
}
