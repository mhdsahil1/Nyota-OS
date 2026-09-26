#ifndef NYOTA_PIC_H
#define NYOTA_PIC_H

#include "types.h"

/* 8259 PIC Ports */
#define PIC1_COMMAND    0x20    /* Master PIC Command */
#define PIC1_DATA       0x21    /* Master PIC Data / IMR */
#define PIC2_COMMAND    0xA0    /* Slave PIC Command */
#define PIC2_DATA       0xA1    /* Slave PIC Data / IMR */

/* PIC Commands */
#define PIC_EOI         0x20    /* End of Interrupt */

/* Vector Offsets (remapped to avoid CPU exceptions 0..31) */
#define PIC1_OFFSET     0x20    /* Master IRQ 0..7 -> Vectors 32..39 */
#define PIC2_OFFSET     0x28    /* Slave IRQ 8..15 -> Vectors 40..47 */

/* Public API */
void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_disable(void);

#endif /* NYOTA_PIC_H */
