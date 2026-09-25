#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* ── IDT entry (gate descriptor) — 8 bytes ───────────────────────────────── */
struct idt_entry {
    uint16_t base_lo;   /* Handler address [15:0]  */
    uint16_t sel;       /* Code segment selector    */
    uint8_t  always0;   /* Reserved, must be 0      */
    uint8_t  flags;     /* Gate type + DPL + present */
    uint16_t base_hi;   /* Handler address [31:16] */
} __attribute__((packed));

/* ── IDTR value passed to lidt ────────────────────────────────────────────── */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* ── CPU register state pushed by ISR/IRQ stubs ────────────────────────────
 * Layout matches what idt_asm.asm pushes onto the stack (bottom = lower addr):
 *   ds | edi,esi,ebp,esp,ebx,edx,ecx,eax (pusha) | int_no,err_code | eip,cs,eflags
 * ─────────────────────────────────────────────────────────────────────────── */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha order */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;                         /* pushed by CPU */
};

/* ── Interrupt handler function pointer ────────────────────────────────────── */
typedef void (*isr_t)(struct registers *);

/* ── Public API ────────────────────────────────────────────────────────────── */
void idt_init(void);
void idt_set_handler(uint8_t n, isr_t handler);

#endif /* IDT_H */
