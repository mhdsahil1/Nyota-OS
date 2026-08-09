/* =============================================================================
 * Nyota OS — Global Descriptor Table
 * Three segments: null, kernel code (ring 0), kernel data (ring 0).
 * After loading the new GDT, gdt_flush() reloads all segment registers.
 * =========================================================================== */

#include "gdt.h"

static struct gdt_entry gdt[3];
static struct gdt_ptr   gdt_p;

/* Defined in gdt_flush.asm */
extern void gdt_flush(uint32_t gdt_ptr_addr);

static void gdt_set_gate(int idx, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = (uint16_t)(base  & 0xFFFF);
    gdt[idx].base_middle = (uint8_t )((base  >> 16) & 0xFF);
    gdt[idx].base_high   = (uint8_t )((base  >> 24) & 0xFF);
    gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[idx].granularity = (uint8_t )(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[idx].access      = access;
}

void gdt_init(void) {
    gdt_p.limit = (uint16_t)((sizeof(struct gdt_entry) * 3) - 1);
    gdt_p.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0,          0x00, 0x00); /* Null descriptor      */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel code (ring 0) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel data (ring 0) */

    gdt_flush((uint32_t)&gdt_p);
}
