/* =============================================================================
 * Nyota OS — Interrupt Descriptor Table
 * Sets up all 256 IDT gates, remaps the 8259 PIC so hardware IRQs land at
 * vectors 32–47 (clear of CPU exception vectors 0–31), then calls idt_load().
 * =========================================================================== */

#include "idt.h"
#include "../vga/vga.h"

/* ── Internal state ──────────────────────────────────────────────────────── */
static struct idt_entry idt[256];
static struct idt_ptr   idt_p;
static isr_t            handlers[256];

/* ── ASM helpers ─────────────────────────────────────────────────────────── */
extern void idt_load(uint32_t idt_ptr_addr);

/* ── ISR stubs (CPU exceptions 0-31) ─────────────────────────────────────── */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* ── IRQ stubs (hardware IRQs 0-15 → vectors 32-47) ─────────────────────── */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

/* ── 8259 PIC port definitions ───────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ── PIC remapping ───────────────────────────────────────────────────────── */
/* Default BIOS maps IRQ 0-7 → INT 8-15, which conflicts with CPU exceptions.
 * We remap: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47. */
static void pic_remap(void) {
    /* ICW1: begin initialisation, ICW4 required */
    outb(PIC1_CMD,  0x11);
    outb(PIC2_CMD,  0x11);
    /* ICW2: vector offsets */
    outb(PIC1_DATA, 0x20); /* master: IRQ 0-7  → INT 32-39 */
    outb(PIC2_DATA, 0x28); /* slave:  IRQ 8-15 → INT 40-47 */
    /* ICW3: cascade wiring */
    outb(PIC1_DATA, 0x04); /* master: slave on IRQ2 (bit mask) */
    outb(PIC2_DATA, 0x02); /* slave:  cascade identity = 2     */
    /* ICW4: 8086/88 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    /* Unmask all IRQs */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

/* ── Gate installer ──────────────────────────────────────────────────────── */
static void idt_set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = (uint16_t)(base & 0xFFFF);
    idt[n].base_hi = (uint16_t)((base >> 16) & 0xFFFF);
    idt[n].sel     = sel;
    idt[n].always0 = 0;
    idt[n].flags   = flags; /* 0x8E = present, ring0, 32-bit interrupt gate */
}

/* ── Exception messages ──────────────────────────────────────────────────── */
static const char *exc_msgs[32] = {
    "Division By Zero",          "Debug",
    "Non-Maskable Interrupt",    "Breakpoint",
    "Overflow",                  "BOUND Range Exceeded",
    "Invalid Opcode",            "Device Not Available",
    "Double Fault",              "Coprocessor Segment Overrun",
    "Invalid TSS",               "Segment Not Present",
    "Stack-Segment Fault",       "General Protection Fault",
    "Page Fault",                "Reserved",
    "x87 FPU Error",             "Alignment Check",
    "Machine Check",             "SIMD Floating-Point",
    "Virtualisation",            "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security Exception",        "Reserved"
};

/* ── C-level exception handler (called by isr_common_stub) ─────────────── */
void isr_handler(struct registers *r) {
    if (r->int_no < 32) {
        uint8_t saved_color;
        /* Save current VGA color and paint a red panic banner */
        (void)saved_color;
        vga_setcolor(VGA_WHITE, VGA_RED);
        vga_println("");
        vga_print("  *** KERNEL EXCEPTION #");
        vga_print_dec(r->int_no);
        vga_print(": ");
        vga_println(exc_msgs[r->int_no]);
        vga_print("  EIP="); vga_print_hex(r->eip);
        vga_print("  ERR="); vga_print_hex(r->err_code);
        vga_println("");
        vga_println("  System halted.");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if (handlers[r->int_no])
        handlers[r->int_no](r);
}

/* ── C-level IRQ handler (called by irq_common_stub) ───────────────────── */
void irq_handler(struct registers *r) {
    /* Send End-of-Interrupt to PICs */
    if (r->int_no >= 40)
        outb(PIC2_CMD, PIC_EOI); /* Slave PIC */
    outb(PIC1_CMD, PIC_EOI);     /* Master PIC */

    if (handlers[r->int_no])
        handlers[r->int_no](r);
}

/* ── Public: register a handler for interrupt vector n ──────────────────── */
void idt_set_handler(uint8_t n, isr_t handler) {
    handlers[n] = handler;
}

/* ── Public: initialise the IDT ─────────────────────────────────────────── */
void idt_init(void) {
    int i;
    idt_p.limit = (uint16_t)((sizeof(struct idt_entry) * 256) - 1);
    idt_p.base  = (uint32_t)&idt;

    /* Clear everything */
    for (i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
        handlers[i] = 0;
    }

    /* Install CPU exception handlers (#0-#31) — flags 0x8E = present, ring0, 32-bit gate */
    idt_set_gate( 0, (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate( 1, (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate( 2, (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate( 3, (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate( 4, (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate( 5, (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate( 6, (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate( 7, (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate( 8, (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate( 9, (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Install hardware IRQ handlers (vectors 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    pic_remap();
    idt_load((uint32_t)&idt_p);
}
