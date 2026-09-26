/* =============================================================================
 * Nyota OS — x86_64 Task State Segment (TSS)
 * Manages Ring 3 -> Ring 0 kernel stack switching (RSP0).
 * =========================================================================== */

#ifndef NYOTA_TSS_H
#define NYOTA_TSS_H

#include "types.h"

/* x86_64 Task State Segment structure (104 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for Ring 0 transitions */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        /* Interrupt Stack Table 1..7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;    /* I/O Map Base Address */
} tss_t;

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);
uint64_t tss_get_rsp0(void);
void tss_load(uint16_t selector);

#endif /* NYOTA_TSS_H */
