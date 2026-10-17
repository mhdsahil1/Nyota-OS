/* =============================================================================
 * Nyota OS — Extended Page Tables (EPT) & Nested Page Tables (NPT) Manager
 * Implements guest physical memory mapping and isolation from the host kernel.
 * =========================================================================== */

#include "vmm/ept.h"
#include "vmm/vmm.h"
#include "pmm.h"
#include "memory.h"
#include "kernel.h"

/* Mask to extract physical address from 64-bit page table entry */
#define EPT_PHYS_ADDR_MASK      0x000FFFFFFFFFF000ULL

int ept_build_guest_mapping(vmm_vm_t *vm) {
    if (!vm || vm->guest_mem_size == 0 || vm->guest_mem_phys == 0) {
        return -1;
    }

    /* 1. Allocate Level-4 Page Table (PML4) */
    nested_page_table_t *pml4 = (nested_page_table_t *)pmm_alloc_page();
    if (!pml4) {
        return -2;
    }
    memset(pml4, 0, sizeof(nested_page_table_t));

    /* 2. Allocate Level-3 Page Table (PDPT) */
    nested_page_table_t *pdpt = (nested_page_table_t *)pmm_alloc_page();
    if (!pdpt) {
        pmm_free_page(pml4);
        return -3;
    }
    memset(pdpt, 0, sizeof(nested_page_table_t));

    /* Link PML4[0] -> PDPT */
    uint64_t pml4_flags;
    if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
        pml4_flags = EPT_FLAG_READ | EPT_FLAG_WRITE | EPT_FLAG_EXECUTE;
    } else {
        pml4_flags = NPT_FLAG_PRESENT | NPT_FLAG_WRITABLE | NPT_FLAG_USER;
    }
    pml4->entries[0] = ((uint64_t)pdpt & EPT_PHYS_ADDR_MASK) | pml4_flags;

    /* 3. Allocate Level-2 Page Table (PD) */
    nested_page_table_t *pd = (nested_page_table_t *)pmm_alloc_page();
    if (!pd) {
        pmm_free_page(pdpt);
        pmm_free_page(pml4);
        return -4;
    }
    memset(pd, 0, sizeof(nested_page_table_t));

    /* Link PDPT[0] -> PD */
    pdpt->entries[0] = ((uint64_t)pd & EPT_PHYS_ADDR_MASK) | pml4_flags;

    /* 4. Map Guest Physical Address (GPA) to Host Physical Address (HPA) */
    /* Map 2MB pages or 4KB pages covering guest_mem_size */
    size_t mapped = 0;
    uint64_t gpa = 0;
    uint64_t hpa = vm->guest_mem_phys;

    /* Use 2MB large pages if guest memory is >= 2MB and aligned */
    while (mapped + (2 * 1024 * 1024) <= vm->guest_mem_size && (hpa & 0x1FFFFF) == 0) {
        uint32_t pd_idx = (uint32_t)((gpa >> 21) & 0x1FF);
        uint64_t leaf_flags;

        if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
            leaf_flags = EPT_FLAG_READ | EPT_FLAG_WRITE | EPT_FLAG_EXECUTE |
                         EPT_FLAG_MEM_TYPE_WB | EPT_FLAG_LARGE_PAGE;
        } else {
            leaf_flags = NPT_FLAG_PRESENT | NPT_FLAG_WRITABLE | NPT_FLAG_USER |
                         NPT_FLAG_LARGE_PAGE;
        }

        pd->entries[pd_idx] = (hpa & 0x000FFFFFE00000ULL) | leaf_flags;

        mapped += 2 * 1024 * 1024;
        gpa    += 2 * 1024 * 1024;
        hpa    += 2 * 1024 * 1024;
    }

    /* Map remaining with 4KB page tables if needed */
    if (mapped < vm->guest_mem_size) {
        uint32_t pd_idx = (uint32_t)((gpa >> 21) & 0x1FF);
        nested_page_table_t *pt = (nested_page_table_t *)pmm_alloc_page();
        if (pt) {
            memset(pt, 0, sizeof(nested_page_table_t));
            pd->entries[pd_idx] = ((uint64_t)pt & EPT_PHYS_ADDR_MASK) | pml4_flags;

            while (mapped < vm->guest_mem_size && (gpa >> 21) == pd_idx) {
                uint32_t pt_idx = (uint32_t)((gpa >> 12) & 0x1FF);
                uint64_t leaf_flags;

                if (vm->vendor == VMM_VENDOR_INTEL_VMX) {
                    leaf_flags = EPT_FLAG_READ | EPT_FLAG_WRITE | EPT_FLAG_EXECUTE |
                                 EPT_FLAG_MEM_TYPE_WB;
                } else {
                    leaf_flags = NPT_FLAG_PRESENT | NPT_FLAG_WRITABLE | NPT_FLAG_USER;
                }

                pt->entries[pt_idx] = (hpa & EPT_PHYS_ADDR_MASK) | leaf_flags;

                mapped += 4096;
                gpa    += 4096;
                hpa    += 4096;
            }
        }
    }

    /* Store root of nested paging */
    vm->nested_cr3_virt = pml4;
    vm->nested_cr3_phys = (uint64_t)pml4;

    return 0;
}

uint64_t ept_create_eptp(uint64_t pml4_phys) {
    /* EPTP format:
     * Bits 2:0   = Memory type (6 = Write-back)
     * Bits 5:3   = Page-walk length minus 1 (3 for 4-level walk)
     * Bit 6      = Access/dirty flags enabled (0)
     * Bits 11:7  = Reserved (0)
     * Bits 63:12 = Physical address of EPT PML4
     */
    return (pml4_phys & EPT_PHYS_ADDR_MASK) | (3ULL << 3) | 6ULL;
}

bool ept_validate_gpa(vmm_vm_t *vm, uint64_t gpa, size_t len) {
    if (!vm) return false;
    if (gpa >= vm->guest_mem_size) return false;
    if (gpa + len > vm->guest_mem_size) return false;
    return true;
}

void ept_free_guest_mapping(vmm_vm_t *vm) {
    if (!vm || !vm->nested_cr3_virt) return;

    nested_page_table_t *pml4 = (nested_page_table_t *)vm->nested_cr3_virt;
    if (pml4->entries[0]) {
        nested_page_table_t *pdpt = (nested_page_table_t *)(pml4->entries[0] & EPT_PHYS_ADDR_MASK);
        if (pdpt && pdpt->entries[0]) {
            nested_page_table_t *pd = (nested_page_table_t *)(pdpt->entries[0] & EPT_PHYS_ADDR_MASK);
            if (pd) {
                for (int i = 0; i < 512; i++) {
                    if (pd->entries[i] && !(pd->entries[i] & EPT_FLAG_LARGE_PAGE)) {
                        nested_page_table_t *pt = (nested_page_table_t *)(pd->entries[i] & EPT_PHYS_ADDR_MASK);
                        if (pt) pmm_free_page(pt);
                    }
                }
                pmm_free_page(pd);
            }
            pmm_free_page(pdpt);
        }
    }
    pmm_free_page(pml4);
    vm->nested_cr3_virt = NULL;
    vm->nested_cr3_phys = 0;
}
