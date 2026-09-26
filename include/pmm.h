/* =============================================================================
 * Nyota OS — Physical Memory Manager (PMM)
 * Page frame allocation based on BIOS E820 memory map and bitmap allocator.
 * =========================================================================== */

#ifndef NYOTA_PMM_H
#define NYOTA_PMM_H

#include "types.h"

#define PAGE_SIZE           4096ULL
#define PAGE_MASK           (~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(a)  ((uint64_t)(a) & PAGE_MASK)
#define PAGE_ALIGN_UP(a)    (((uint64_t)(a) + PAGE_SIZE - 1) & PAGE_MASK)

/* BIOS E820 Memory Types */
typedef enum {
    MEMORY_USABLE           = 1,
    MEMORY_RESERVED         = 2,
    MEMORY_ACPI             = 3,
    MEMORY_ACPI_RECLAIMABLE = 4,
    MEMORY_BAD              = 5
} memory_region_type_t;

/* Memory region descriptor */
typedef struct {
    uint64_t base;
    uint64_t length;
    memory_region_type_t type;
} memory_region_t;

/* BIOS E820 Memory Map entry (24-byte layout from INT 15h, AX=E820h) */
typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t extended_attributes;
} e820_entry_t;

/* Bootloader E820 addresses */
#define E820_MAP_COUNT_ADDR  0x5000
#define E820_MAP_BUF_ADDR    0x5008

/* Physical Memory Manager APIs */
void pmm_init(void);

void *pmm_alloc_page(void);
void *pmm_alloc_pages(size_t count);
void pmm_free_page(void *address);
void pmm_free_pages(void *address, size_t count);

uint64_t pmm_total_memory(void);
uint64_t pmm_usable_memory(void);
uint64_t pmm_used_memory(void);
uint64_t pmm_free_memory(void);

uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages_count(void);

void pmm_print_mmap(void);
void pmm_print_stats(void);

#endif /* NYOTA_PMM_H */
