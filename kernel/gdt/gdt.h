#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* ── GDT entry (segment descriptor) — 8 bytes ────────────────────────────── */
struct gdt_entry {
    uint16_t limit_low;     /* Limit  [15:0]  */
    uint16_t base_low;      /* Base   [15:0]  */
    uint8_t  base_middle;   /* Base   [23:16] */
    uint8_t  access;        /* Access byte    */
    uint8_t  granularity;   /* Flags + Limit  [19:16] */
    uint8_t  base_high;     /* Base   [31:24] */
} __attribute__((packed));

/* ── GDTR value passed to lgdt ────────────────────────────────────────────── */
struct gdt_ptr {
    uint16_t limit;         /* GDT size - 1       */
    uint32_t base;          /* Physical GDT address */
} __attribute__((packed));

void gdt_init(void);

#endif /* GDT_H */
