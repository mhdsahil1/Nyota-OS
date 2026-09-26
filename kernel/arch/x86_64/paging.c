/* =============================================================================
 * Nyota OS — 4-Level x86_64 Paging Architecture (VMM)
 * Page table management, virtual address translation, CR3 and TLB management.
 * =========================================================================== */

#include "paging.h"
#include "kernel.h"
#include "memory.h"
#include "vga.h"

static page_table_t *kernel_pml4 = NULL;

/* ── CR3 & TLB Operations ─────────────────────────────────────────────────── */

uint64_t paging_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void paging_load_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void paging_invlpg(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

page_table_t *paging_get_kernel_pml4(void) {
    return kernel_pml4;
}

/* ── Initialization ───────────────────────────────────────────────────────── */

void paging_init(void) {
    uint64_t cr3 = paging_read_cr3() & PAGE_ENTRY_ADDR_MASK;
    kernel_pml4 = (page_table_t *)cr3;

    if (!kernel_pml4) {
        kernel_panic("paging_init: PML4 base pointer is NULL");
    }
}

/* ── Virtual Page Mapping ─────────────────────────────────────────────────── */

bool paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!kernel_pml4) return false;

    virt = PAGE_ALIGN_DOWN(virt);
    phys = PAGE_ALIGN_DOWN(phys);

    uint64_t pml4_i = PML4_INDEX(virt);
    uint64_t pdpt_i = PDPT_INDEX(virt);
    uint64_t pd_i   = PD_INDEX(virt);
    uint64_t pt_i   = PT_INDEX(virt);

    /* 1. Walk or create PDPT */
    if (!(kernel_pml4->entries[pml4_i] & PAGE_PRESENT)) {
        void *new_pdpt = pmm_alloc_page();
        if (!new_pdpt) return false;
        kernel_pml4->entries[pml4_i] = ((uint64_t)new_pdpt) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    page_table_t *pdpt = (page_table_t *)(kernel_pml4->entries[pml4_i] & PAGE_ENTRY_ADDR_MASK);

    /* 2. Walk or create PD */
    if (!(pdpt->entries[pdpt_i] & PAGE_PRESENT)) {
        void *new_pd = pmm_alloc_page();
        if (!new_pd) return false;
        pdpt->entries[pdpt_i] = ((uint64_t)new_pd) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    page_table_t *pd = (page_table_t *)(pdpt->entries[pdpt_i] & PAGE_ENTRY_ADDR_MASK);

    /* 3. Walk or create PT */
    if (!(pd->entries[pd_i] & PAGE_PRESENT)) {
        void *new_pt = pmm_alloc_page();
        if (!new_pt) return false;
        pd->entries[pd_i] = ((uint64_t)new_pt) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else if (pd->entries[pd_i] & PAGE_HUGE) {
        /* Already mapped as 2MB huge page */
        return false;
    }
    page_table_t *pt = (page_table_t *)(pd->entries[pd_i] & PAGE_ENTRY_ADDR_MASK);

    /* 4. Set PT entry and invalidate TLB */
    pt->entries[pt_i] = phys | (flags & ~PAGE_ENTRY_ADDR_MASK) | PAGE_PRESENT;
    paging_invlpg(virt);

    return true;
}

bool paging_unmap_page(uint64_t virt) {
    if (!kernel_pml4) return false;

    virt = PAGE_ALIGN_DOWN(virt);

    uint64_t pml4_i = PML4_INDEX(virt);
    uint64_t pdpt_i = PDPT_INDEX(virt);
    uint64_t pd_i   = PD_INDEX(virt);
    uint64_t pt_i   = PT_INDEX(virt);

    if (!(kernel_pml4->entries[pml4_i] & PAGE_PRESENT)) return false;
    page_table_t *pdpt = (page_table_t *)(kernel_pml4->entries[pml4_i] & PAGE_ENTRY_ADDR_MASK);

    if (!(pdpt->entries[pdpt_i] & PAGE_PRESENT)) return false;
    page_table_t *pd = (page_table_t *)(pdpt->entries[pdpt_i] & PAGE_ENTRY_ADDR_MASK);

    if (!(pd->entries[pd_i] & PAGE_PRESENT)) return false;
    if (pd->entries[pd_i] & PAGE_HUGE) return false;

    page_table_t *pt = (page_table_t *)(pd->entries[pd_i] & PAGE_ENTRY_ADDR_MASK);

    if (!(pt->entries[pt_i] & PAGE_PRESENT)) return false;

    pt->entries[pt_i] = 0;
    paging_invlpg(virt);

    return true;
}

uint64_t paging_get_physical(uint64_t virt) {
    if (!kernel_pml4) return 0;

    uint64_t pml4_i = PML4_INDEX(virt);
    uint64_t pdpt_i = PDPT_INDEX(virt);
    uint64_t pd_i   = PD_INDEX(virt);
    uint64_t pt_i   = PT_INDEX(virt);

    if (!(kernel_pml4->entries[pml4_i] & PAGE_PRESENT)) return 0;
    page_table_t *pdpt = (page_table_t *)(kernel_pml4->entries[pml4_i] & PAGE_ENTRY_ADDR_MASK);

    if (!(pdpt->entries[pdpt_i] & PAGE_PRESENT)) return 0;
    page_table_t *pd = (page_table_t *)(pdpt->entries[pdpt_i] & PAGE_ENTRY_ADDR_MASK);

    if (!(pd->entries[pd_i] & PAGE_PRESENT)) return 0;

    /* Handle 2MB huge page */
    if (pd->entries[pd_i] & PAGE_HUGE) {
        uint64_t base = pd->entries[pd_i] & 0x000FFFFFFFE00000ULL;
        return base + (virt & 0x1FFFFFULL);
    }

    page_table_t *pt = (page_table_t *)(pd->entries[pd_i] & PAGE_ENTRY_ADDR_MASK);
    if (!(pt->entries[pt_i] & PAGE_PRESENT)) return 0;

    uint64_t base = pt->entries[pt_i] & PAGE_ENTRY_ADDR_MASK;
    return base + PAGE_OFFSET(virt);
}
