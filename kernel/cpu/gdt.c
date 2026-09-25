/* =============================================================================
 * Nyota OS — Global Descriptor Table (64-bit Long Mode)
 * =========================================================================== */

#include "gdt.h"
#include "kernel.h"

/* GDT entry structure */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

/* GDT descriptor pointer */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/* 3 descriptors: 0=Null, 1=Kernel Code 64-bit, 2=Kernel Data 64-bit */
static gdt_entry_t gdt_entries[3];
static gdt_ptr_t   gdt_ptr;

extern void gdt_flush(gdt_ptr_t *ptr);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_entries[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt_entries[num].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt_entries[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_entries[num].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt_entries[num].granularity |= (uint8_t)(gran & 0xF0);
    gdt_entries[num].access      = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (uint16_t)(sizeof(gdt_entries) - 1);
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    /* 0x00: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (64-bit Long Mode: L=1, D=0)
     * Access: Present(1), Ring 0(00), S=1, Executable(1), Direction(0), Readable(1), Accessed(0) -> 0x9A
     * Granularity/Flags: Long mode(0x20) */
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);

    /* 0x10: Kernel Data Segment (64-bit Long Mode)
     * Access: Present(1), Ring 0(00), S=1, Executable(0), Direction(0), Writable(1), Accessed(0) -> 0x92
     * Granularity/Flags: 0x00 */
    gdt_set_gate(2, 0, 0, 0x92, 0x00);

    /* Flush and reload */
    gdt_flush(&gdt_ptr);
}
