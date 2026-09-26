/* =============================================================================
 * Nyota OS — Global Descriptor Table (GDT) & Segment Selectors
 * Supports Ring 0 (Kernel) and Ring 3 (User) 64-bit execution & TSS.
 * =========================================================================== */

#ifndef NYOTA_GDT_H
#define NYOTA_GDT_H

#include "types.h"

/* GDT Segment Selectors */
#define GDT_KERNEL_CODE_SEG  0x08  /* Ring 0 Code (Selector: 0x08) */
#define GDT_KERNEL_DATA_SEG  0x10  /* Ring 0 Data (Selector: 0x10) */
#define GDT_USER_DATA_SEG    0x18  /* Ring 3 Data Base */
#define GDT_USER_CODE_SEG    0x20  /* Ring 3 Code Base */
#define GDT_TSS_SEG          0x28  /* 64-bit TSS Selector */

/* Selectors with Requested Privilege Level (RPL = 3) */
#define GDT_USER_DATA_SEL    (GDT_USER_DATA_SEG | 0x03)  /* 0x1B */
#define GDT_USER_CODE_SEL    (GDT_USER_CODE_SEG | 0x03)  /* 0x23 */

void gdt_init(void);
void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit);

#endif /* NYOTA_GDT_H */
