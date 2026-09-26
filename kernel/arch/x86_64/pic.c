/* =============================================================================
 * Nyota OS — 8259 Programmable Interrupt Controller (PIC) Driver
 * Remaps IRQ 0..15 to IDT vectors 32..47 and manages masks & EOI.
 * =========================================================================== */

#include "pic.h"
#include "io.h"

#define ICW1_INIT       0x10    /* Initialization control word 1 flag */
#define ICW1_ICW4       0x01    /* ICW4 needed flag */
#define ICW4_8086       0x01    /* 8086/88 (MCS-80/85) mode */

void pic_init(void) {
    /* 1. ICW1: Start initialization sequence in cascade mode */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* 2. ICW2: Remap IRQ vector offsets (Master -> 32, Slave -> 40) */
    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    /* 3. ICW3: Tell Master PIC that Slave is at IRQ2 (0000 0100b = 4) */
    outb(PIC1_DATA, 0x04);
    io_wait();
    /* Tell Slave PIC its cascade identity (0000 0010b = 2) */
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* 4. ICW4: Set 8086/88 operational mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* 5. Mask all interrupts initially (0xFF) */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    /* If the IRQ came from the Slave PIC, send EOI to Slave */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to Master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = (uint8_t)(inb(port) | (1 << irq));
    outb(port, value);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = (uint8_t)(inb(port) & ~(1 << irq));
    outb(port, value);
}

void pic_disable(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
