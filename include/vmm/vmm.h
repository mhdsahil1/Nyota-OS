/* =============================================================================
 * Nyota OS — Hardware-Assisted Virtualization & Hypervisor (VMM)
 * Architecture-aware hypervisor interfaces for Intel VT-x (VMX) and AMD-V (SVM).
 * =========================================================================== */

#ifndef NYOTA_VMM_H
#define NYOTA_VMM_H

#include "types.h"

/* Hypervisor Hardware Vendors */
typedef enum {
    VMM_VENDOR_NONE = 0,
    VMM_VENDOR_INTEL_VMX = 1,
    VMM_VENDOR_AMD_SVM   = 2
} vmm_vendor_t;

/* Hypervisor Operational Status */
typedef enum {
    VMM_STATUS_UNINITIALIZED = 0,
    VMM_STATUS_AVAILABLE     = 1,
    VMM_STATUS_ACTIVE        = 2,
    VMM_STATUS_UNSUPPORTED   = 3,
    VMM_STATUS_DISABLED      = 4,
    VMM_STATUS_ERROR         = 5
} vmm_status_t;

/* Standard VM-Exit Reasons (Unified abstraction) */
typedef enum {
    VMM_EXIT_UNKNOWN         = 0,
    VMM_EXIT_CPUID           = 1,
    VMM_EXIT_HLT             = 2,
    VMM_EXIT_HYPERCALL       = 3,   /* VMCALL (Intel) / VMMCALL (AMD) */
    VMM_EXIT_CR_ACCESS       = 4,
    VMM_EXIT_DR_ACCESS       = 5,
    VMM_EXIT_IO              = 6,
    VMM_EXIT_EPT_VIOLATION   = 7,   /* Extended / Nested Page Fault */
    VMM_EXIT_SHUTDOWN        = 8,   /* Triple fault / fatal condition */
    VMM_EXIT_INTERRUPT       = 9
} vmm_exit_reason_t;

/* Guest General Purpose Registers */
typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t efer;
} vmm_guest_regs_t;

/* Hypervisor Hardware Capabilities */
typedef struct {
    vmm_vendor_t vendor;
    vmm_status_t status;
    char         vendor_string[16];
    bool         has_hardware_virt;
    bool         has_nested_paging; /* EPT on Intel, NPT on AMD */
    bool         has_unrestricted_guest;
    uint32_t     revision_id;       /* VMCS revision or SVM revision */
    uint32_t     asid_count;
    char         status_message[64];
} vmm_capabilities_t;

/* Hypervisor Performance & Exit Statistics */
typedef struct {
    uint64_t vm_entries_total;
    uint64_t vm_exits_total;
    uint64_t exits_cpuid;
    uint64_t exits_hlt;
    uint64_t exits_hypercall;
    uint64_t exits_cr_access;
    uint64_t exits_page_fault;
    uint64_t exits_io;
    uint64_t exits_other;
    uint32_t active_vms;
} vmm_stats_t;

/* Forward declarations */
struct vmm_vm;

/* Virtual Machine Instance Descriptor */
typedef struct vmm_vm {
    uint32_t           vm_id;
    vmm_vendor_t       vendor;
    bool               active;
    bool               halted;

    /* Guest Physical Memory (GPA) */
    void              *guest_mem_virt;
    uint64_t           guest_mem_phys;
    size_t             guest_mem_size;

    /* Nested Paging Tables (EPT / NPT) */
    void              *nested_cr3_virt;
    uint64_t           nested_cr3_phys;

    /* Architecture-specific Control Block (VMCS or VMCB) */
    void              *arch_cb_virt;
    uint64_t           arch_cb_phys;

    /* Virtual CPU Register State */
    vmm_guest_regs_t   regs;

    /* Last Exit Info */
    vmm_exit_reason_t  last_exit_reason;
    uint64_t           last_exit_qual;
    uint64_t           last_exit_rip;

    /* VM-specific exit counters */
    uint64_t           exit_count;
    uint64_t           entry_count;
} vmm_vm_t;

/* User / Syscall Info Structure */
typedef struct {
    uint32_t vendor;               /* 1 = Intel VMX, 2 = AMD SVM, 0 = None */
    uint32_t status;               /* vmm_status_t */
    uint32_t has_nested_paging;    /* 1 if EPT/NPT supported */
    uint32_t revision_id;
    uint64_t total_entries;
    uint64_t total_exits;
    uint64_t exits_cpuid;
    uint64_t exits_hlt;
    uint64_t exits_hypercall;
    uint64_t exits_ept_fault;
    char     vendor_name[16];
    char     status_msg[64];
} vmm_info_t;

/* Syscall Sub-Operations for SYS_VMM_OP (87) */
#define VMM_OP_GET_INFO     1
#define VMM_OP_RUN_GUEST    2
#define VMM_OP_GET_STATS    3
#define VMM_OP_RESET_STATS  4

/* Hypervisor Public APIs */
void                      vmm_init(void);
const vmm_capabilities_t *vmm_get_capabilities(void);
const vmm_stats_t        *vmm_get_stats(void);

int  vmm_create_guest(vmm_vm_t **out_vm, size_t mem_size);
int  vmm_run_guest(vmm_vm_t *vm, uint32_t max_cycles);
int  vmm_destroy_guest(vmm_vm_t *vm);
int  vmm_run_diagnostics(char *out_buf, size_t buf_len);

#endif /* NYOTA_VMM_H */
