/* =============================================================================
 * Nyota OS — Global Descriptor Table (64-bit Long Mode)
 * Configures Ring 0 (Kernel) & Ring 3 (User) segments and 64-bit TSS descriptor.
 * =========================================================================== */

#include "gdt.h"
#include "kernel.h"
#include "memory.h"

/* Standard 8-byte GDT entry structure */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

/* 16-byte 64-bit TSS descriptor structure */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper32;
    uint32_t reserved;
} gdt_tss_entry_t;

/* GDT descriptor pointer for LGDT */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

/* 7 descriptors:
 * 0: Null Descriptor            (0x00)
 * 1: Kernel Code 64-bit (DPL 0) (0x08)
 * 2: Kernel Data 64-bit (DPL 0) (0x10)
 * 3: User Data 64-bit   (DPL 3) (0x18 | 3 = 0x1B)
 * 4: User Code 64-bit   (DPL 3) (0x20 | 3 = 0x23)
 * 5..6: 64-bit TSS (16 bytes)   (0x28)
 */
static gdt_entry_t gdt_entries[7];
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

void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit) {
    gdt_tss_entry_t *tss_entry = (gdt_tss_entry_t *)&gdt_entries[5];

    tss_entry->limit_low    = (uint16_t)(tss_limit & 0xFFFF);
    tss_entry->base_low     = (uint16_t)(tss_base & 0xFFFF);
    tss_entry->base_middle  = (uint8_t)((tss_base >> 16) & 0xFF);
    /* Access: Present(1), DPL 0(00), System(0), Type 0x9 (64-bit Available TSS) -> 0x89 */
    tss_entry->access       = 0x89;
    tss_entry->granularity  = (uint8_t)((tss_limit >> 16) & 0x0F);
    tss_entry->base_high    = (uint8_t)((tss_base >> 24) & 0xFF);
    tss_entry->base_upper32 = (uint32_t)((tss_base >> 32) & 0xFFFFFFFF);
    tss_entry->reserved     = 0;
}

void gdt_init(void) {
    memset(gdt_entries, 0, sizeof(gdt_entries));

    gdt_ptr.limit = (uint16_t)(sizeof(gdt_entries) - 1);
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    /* 0x00: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (64-bit Long Mode: L=1, D=0, DPL 0) -> 0x9A, 0x20 */
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);

    /* 0x10: Kernel Data Segment (64-bit Long Mode, DPL 0) -> 0x92, 0x00 */
    gdt_set_gate(2, 0, 0, 0x92, 0x00);

    /* 0x18: User Data Segment (64-bit Long Mode, DPL 3) -> 0xF2, 0x00 */
    gdt_set_gate(3, 0, 0, 0xF2, 0x00);

    /* 0x20: User Code Segment (64-bit Long Mode: L=1, D=0, DPL 3) -> 0xFA, 0x20 */
    gdt_set_gate(4, 0, 0, 0xFA, 0x20);

    /* Flush and reload GDTR and segment registers */
    gdt_flush(&gdt_ptr);
}
