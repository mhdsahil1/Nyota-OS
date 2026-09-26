/* =============================================================================
 * Nyota OS — 4-Level x86_64 Paging Architecture (VMM)
 * Page table structures, translation, mapping, CR3, and TLB management.
 * =========================================================================== */

#ifndef NYOTA_PAGING_H
#define NYOTA_PAGING_H

#include "types.h"
#include "pmm.h"

/* x86_64 Page Table Entry Flags */
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITE_THROUGH  (1ULL << 3)
#define PAGE_CACHE_DISABLE  (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NX             (1ULL << 63)

/* Mask to extract 4 KiB physical frame base address */
#define PAGE_ENTRY_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* Virtual address breakdown macros (9-9-9-9-12 scheme) */
#define PML4_INDEX(v)   (((uint64_t)(v) >> 39) & 0x1FF)
#define PDPT_INDEX(v)   (((uint64_t)(v) >> 30) & 0x1FF)
#define PD_INDEX(v)     (((uint64_t)(v) >> 21) & 0x1FF)
#define PT_INDEX(v)     (((uint64_t)(v) >> 12) & 0x1FF)
#define PAGE_OFFSET(v)  ((uint64_t)(v) & 0xFFF)

/* Page Table Entry type */
typedef uint64_t pt_entry_t;

/* 512-entry page table structure (4 KiB aligned) */
typedef struct {
    pt_entry_t entries[512];
} __attribute__((aligned(4096))) page_table_t;

/* Paging Management APIs */
void paging_init(void);

uint64_t paging_read_cr3(void);
void paging_load_cr3(uint64_t pml4_phys);
void paging_invlpg(uint64_t virt);

bool paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
bool paging_map_page_in(page_table_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
bool paging_unmap_page(uint64_t virt);
uint64_t paging_get_physical(uint64_t virt);
uint64_t paging_get_physical_in(page_table_t *pml4, uint64_t virt);

page_table_t *paging_create_address_space(void);
page_table_t *paging_get_kernel_pml4(void);

#endif /* NYOTA_PAGING_H */
