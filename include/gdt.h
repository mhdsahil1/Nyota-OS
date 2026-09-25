#ifndef NYOTA_GDT_H
#define NYOTA_GDT_H

#include "types.h"

#define GDT_KERNEL_CODE_SEG  0x08
#define GDT_KERNEL_DATA_SEG  0x10

void gdt_init(void);

#endif /* NYOTA_GDT_H */
