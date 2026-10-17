/* =============================================================================
 * Nyota OS — Extended Page Tables (EPT) & Nested Page Tables (NPT) Manager
 * Provides guest physical memory mapping and memory isolation from host kernel.
 * =========================================================================== */

#ifndef NYOTA_VMM_EPT_H
#define NYOTA_VMM_EPT_H

#include "types.h"
#include "vmm/vmm.h"

/* ── Intel EPT Entry Flags ────────────────────────────────────────────────── */
#define EPT_FLAG_READ               (1ULL << 0)
#define EPT_FLAG_WRITE              (1ULL << 1)
#define EPT_FLAG_EXECUTE            (1ULL << 2)
#define EPT_FLAG_MEM_TYPE_WB        (6ULL << 3)     /* Write-Back Memory */
#define EPT_FLAG_IGNORE_PAT         (1ULL << 6)
#define EPT_FLAG_LARGE_PAGE         (1ULL << 7)
#define EPT_FLAG_ACCESSED           (1ULL << 8)
#define EPT_FLAG_DIRTY              (1ULL << 9)

#define EPT_RWX                     (EPT_FLAG_READ | EPT_FLAG_WRITE | EPT_FLAG_EXECUTE)

/* ── AMD NPT Entry Flags ──────────────────────────────────────────────────── */
#define NPT_FLAG_PRESENT            (1ULL << 0)
#define NPT_FLAG_WRITABLE           (1ULL << 1)
#define NPT_FLAG_USER               (1ULL << 2)
#define NPT_FLAG_PWT                (1ULL << 3)
#define NPT_FLAG_PCD                (1ULL << 4)
#define NPT_FLAG_ACCESSED           (1ULL << 5)
#define NPT_FLAG_DIRTY              (1ULL << 6)
#define NPT_FLAG_LARGE_PAGE         (1ULL << 7)

#define NPT_RWX                     (NPT_FLAG_PRESENT | NPT_FLAG_WRITABLE | NPT_FLAG_USER)

/* EPT/NPT Page Table Structure (512 64-bit entries, 4096-byte aligned) */
typedef struct __attribute__((aligned(4096))) {
    uint64_t entries[512];
} nested_page_table_t;

/* Function Declarations */
int      ept_build_guest_mapping(vmm_vm_t *vm);
void     ept_free_guest_mapping(vmm_vm_t *vm);
uint64_t ept_create_eptp(uint64_t pml4_phys);
bool     ept_validate_gpa(vmm_vm_t *vm, uint64_t gpa, size_t len);

#endif /* NYOTA_VMM_EPT_H */
