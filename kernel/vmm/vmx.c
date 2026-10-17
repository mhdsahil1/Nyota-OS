/* =============================================================================
 * Nyota OS — Intel VT-x (VMX) Hardware Hypervisor Driver
 * Implements hardware detection, VMXON operation, VMCS configuration, EPT, and VM exits.
 * =========================================================================== */

#include "vmm/vmx.h"
#include "vmm/ept.h"
#include "pmm.h"
#include "memory.h"
#include "kernel.h"

static void *g_vmxon_region = NULL;
static uint32_t g_vmcs_revision = 0;

/* Inline MSR helpers */
static inline uint64_t vmx_rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void vmx_wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline void vmx_cpuid(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

bool vmx_detect_hardware(vmm_capabilities_t *caps) {
    if (!caps) return false;

    uint32_t eax, ebx, ecx, edx;

    /* 1. Check CPUID.1:ECX bit 5 (VMX) */
    vmx_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (!(ecx & (1 << 5))) {
        return false;
    }

    /* 2. Check IA32_FEATURE_CONTROL MSR (0x3A) */
    uint64_t feat_ctrl = vmx_rdmsr(IA32_FEATURE_CONTROL_MSR);
    if (!(feat_ctrl & IA32_FEATURE_CONTROL_LOCK)) {
        /* Enable VMX outside SMX and lock the MSR */
        feat_ctrl |= IA32_FEATURE_CONTROL_LOCK | IA32_FEATURE_CONTROL_VMXON_OUTSIDE_SMX;
        vmx_wrmsr(IA32_FEATURE_CONTROL_MSR, feat_ctrl);
        feat_ctrl = vmx_rdmsr(IA32_FEATURE_CONTROL_MSR);
    }

    if (!(feat_ctrl & IA32_FEATURE_CONTROL_VMXON_OUTSIDE_SMX)) {
        caps->vendor = VMM_VENDOR_INTEL_VMX;
        caps->status = VMM_STATUS_DISABLED;
        strncpy(caps->status_message, "Intel VT-x disabled by BIOS/firmware", sizeof(caps->status_message));
        return false;
    }

    /* 3. Read VMX Basic MSR (0x480) */
    uint64_t vmx_basic = vmx_rdmsr(IA32_VMX_BASIC_MSR);
    g_vmcs_revision = (uint32_t)(vmx_basic & 0x7FFFFFFFULL);

    /* 4. Check EPT Support (IA32_VMX_EPT_VPID_CAP_MSR 0x48C) */
    uint64_t ept_cap = 0;
    /* Secondary proc controls support bit 63 of IA32_VMX_PROCBASED_CTLS */
    uint64_t proc_ctls = vmx_rdmsr(IA32_VMX_PROCBASED_CTLS_MSR);
    bool has_secondary = (proc_ctls & (1ULL << 63)) != 0;

    if (has_secondary) {
        ept_cap = vmx_rdmsr(IA32_VMX_EPT_VPID_CAP_MSR);
    }

    caps->vendor                 = VMM_VENDOR_INTEL_VMX;
    caps->status                 = VMM_STATUS_AVAILABLE;
    caps->has_hardware_virt      = true;
    caps->has_nested_paging      = (ept_cap & (1ULL << 0)) != 0; /* Page-walk length 4 */
    caps->has_unrestricted_guest = (ept_cap & (1ULL << 7)) != 0;
    caps->revision_id            = g_vmcs_revision;
    strncpy(caps->vendor_string, "Intel VT-x", sizeof(caps->vendor_string));
    strncpy(caps->status_message, "Intel VT-x (VMX) Operation Ready", sizeof(caps->status_message));

    return true;
}

int vmx_init_host(void) {
    if (g_vmxon_region) return 0; /* Already initialized */

    /* 1. Adjust CR0 and CR4 according to fixed MSRs */
    uint64_t cr0_fixed0 = vmx_rdmsr(IA32_VMX_CR0_FIXED0_MSR);
    uint64_t cr0_fixed1 = vmx_rdmsr(IA32_VMX_CR0_FIXED1_MSR);
    uint64_t cr4_fixed0 = vmx_rdmsr(IA32_VMX_CR4_FIXED0_MSR);
    uint64_t cr4_fixed1 = vmx_rdmsr(IA32_VMX_CR4_FIXED1_MSR);

    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    /* Enable CR4.VMXE (bit 13) */
    cr4 |= (1ULL << 13);
    cr4 |= cr4_fixed0;
    cr4 &= cr4_fixed1;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");

    cr0 |= cr0_fixed0;
    cr0 &= cr0_fixed1;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    /* 2. Allocate 4 KiB VMXON Region (Page Aligned) */
    g_vmxon_region = pmm_alloc_page();
    if (!g_vmxon_region) {
        return -1;
    }
    memset(g_vmxon_region, 0, 4096);

    /* Write 31-bit VMCS Revision Identifier */
    *(uint32_t *)g_vmxon_region = g_vmcs_revision;

    /* 3. Execute VMXON */
    uint64_t vmxon_pa = (uint64_t)g_vmxon_region;
    uint8_t err = vmx_vmxon(vmxon_pa);
    if (err) {
        pmm_free_page(g_vmxon_region);
        g_vmxon_region = NULL;
        return -2;
    }

    klog_write(KLOG_LEVEL_INFO, "Intel VT-x (VMX) host operation initialized successfully");
    return 0;
}

static uint32_t adjust_control_field(uint32_t requested, uint32_t msr) {
    uint64_t val = vmx_rdmsr(msr);
    uint32_t allowed0 = (uint32_t)val;
    uint32_t allowed1 = (uint32_t)(val >> 32);

    requested |= allowed0;  /* Must-be-1 bits */
    requested &= allowed1;  /* Must-be-0 bits */
    return requested;
}

int vmx_create_vm(vmm_vm_t *vm) {
    if (!vm) return -1;

    /* Allocate 4 KiB VMCS Region */
    vm->arch_cb_virt = pmm_alloc_page();
    if (!vm->arch_cb_virt) {
        return -1;
    }
    vm->arch_cb_phys = (uint64_t)vm->arch_cb_virt;
    memset(vm->arch_cb_virt, 0, 4096);

    /* Set VMCS Revision Identifier */
    *(uint32_t *)vm->arch_cb_virt = g_vmcs_revision;

    /* Clear and load VMCS pointer */
    if (vmx_vmclear(vm->arch_cb_phys) != 0) {
        pmm_free_page(vm->arch_cb_virt);
        return -2;
    }

    if (vmx_vmptrld(vm->arch_cb_phys) != 0) {
        pmm_free_page(vm->arch_cb_virt);
        return -3;
    }

    /* ── Configure VMCS Control Fields ────────────────────────────────────── */
    uint32_t pin_ctls = adjust_control_field(0, IA32_VMX_PINBASED_CTLS_MSR);
    vmx_vmwrite(VMCS_PIN_BASED_VM_EXEC_CONTROL, pin_ctls);

    /* Primary Processor Controls: intercept HLT (bit 7) and CPUID */
    uint32_t proc_ctls = (1U << 7); /* HLT exit */
    proc_ctls = adjust_control_field(proc_ctls, IA32_VMX_PROCBASED_CTLS_MSR);
    vmx_vmwrite(VMCS_PROC_BASED_VM_EXEC_CONTROL, proc_ctls);

    /* VM-Exit Controls: 64-bit host address space size (bit 9) */
    uint32_t exit_ctls = (1U << 9);
    exit_ctls = adjust_control_field(exit_ctls, IA32_VMX_EXIT_CTLS_MSR);
    vmx_vmwrite(VMCS_VM_EXIT_CONTROLS, exit_ctls);

    /* VM-Entry Controls: 64-bit IA-32e guest mode (bit 9) */
    uint32_t entry_ctls = (1U << 9);
    entry_ctls = adjust_control_field(entry_ctls, IA32_VMX_ENTRY_CTLS_MSR);
    vmx_vmwrite(VMCS_VM_ENTRY_CONTROLS, entry_ctls);

    /* Configure EPT Pointer if EPT built */
    if (vm->nested_cr3_phys) {
        uint64_t eptp = ept_create_eptp(vm->nested_cr3_phys);
        vmx_vmwrite(VMCS_EPT_POINTER, eptp);
    }

    /* ── Configure Host State in VMCS ─────────────────────────────────────── */
    uint64_t cr0, cr3, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    vmx_vmwrite(VMCS_HOST_CR0, cr0);
    vmx_vmwrite(VMCS_HOST_CR3, cr3);
    vmx_vmwrite(VMCS_HOST_CR4, cr4);

    vmx_vmwrite(VMCS_HOST_CS_SELECTOR, 0x08);
    vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_HOST_SS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_HOST_TR_SELECTOR, 0x28);

    /* ── Configure Guest State in VMCS ────────────────────────────────────── */
    vmx_vmwrite(VMCS_GUEST_CR0, vm->regs.cr0);
    vmx_vmwrite(VMCS_GUEST_CR3, vm->regs.cr3);
    vmx_vmwrite(VMCS_GUEST_CR4, vm->regs.cr4);
    vmx_vmwrite(VMCS_GUEST_DR7, 0x00000400);

    vmx_vmwrite(VMCS_GUEST_RSP, vm->regs.rsp);
    vmx_vmwrite(VMCS_GUEST_RIP, vm->regs.rip);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, vm->regs.rflags);

    /* Segment descriptors: flat 64-bit segments */
    vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, 0x08);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, 0x0000A09B); /* 64-bit Code */

    vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, 0x0000C093); /* Data */

    vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, 0x0000C093);

    vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, 0x10);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, 0x0000C093);

    vmx_vmwrite(VMCS_VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFULL);

    return 0;
}

int vmx_run_vm(vmm_vm_t *vm) {
    if (!vm || !vm->arch_cb_phys) return -1;

    /* Ensure this VM's VMCS is loaded */
    vmx_vmptrld(vm->arch_cb_phys);

    /* Update guest state from vm->regs */
    vmx_vmwrite(VMCS_GUEST_RIP, vm->regs.rip);
    vmx_vmwrite(VMCS_GUEST_RSP, vm->regs.rsp);

    vm->entry_count++;

    /* Launch or resume guest execution */
    uint8_t err;
    if (vm->entry_count == 1) {
        err = vmx_vmlaunch();
    } else {
        err = vmx_vmresume();
    }

    if (err) {
        /* VM-entry failure */
        uint64_t inst_err = vmx_vmread(VMCS_VM_INSTRUCTION_ERROR);
        vm->last_exit_reason = VMM_EXIT_UNKNOWN;
        vm->last_exit_qual = inst_err;
        return -2;
    }

    /* VM-Exit occurred: read exit details */
    uint64_t exit_reason = vmx_vmread(VMCS_VM_EXIT_REASON) & 0xFFFF;
    uint64_t exit_qual   = vmx_vmread(VMCS_EXIT_QUALIFICATION);
    uint64_t exit_rip    = vmx_vmread(VMCS_GUEST_RIP);
    uint64_t inst_len    = vmx_vmread(VMCS_VM_EXIT_INSTRUCTION_LEN);

    vm->exit_count++;
    vm->last_exit_rip  = exit_rip;
    vm->last_exit_qual = exit_qual;

    /* Map hardware exit reason to unified abstraction */
    switch (exit_reason) {
        case VMX_EXIT_CPUID:
            vm->last_exit_reason = VMM_EXIT_CPUID;
            /* Emulate CPUID: return Nyota signature and advance RIP */
            vm->regs.rax = 1;
            vm->regs.rbx = 0x746F794E; /* 'Nyot' */
            vm->regs.rdx = 0x4D4D5661; /* 'aVMM' */
            vm->regs.rcx = 0x74736575; /* 'uest' */
            vm->regs.rip = exit_rip + (inst_len ? inst_len : 2);
            break;

        case VMX_EXIT_HLT:
            vm->last_exit_reason = VMM_EXIT_HLT;
            vm->halted = true;
            vm->regs.rip = exit_rip + (inst_len ? inst_len : 1);
            break;

        case VMX_EXIT_VMCALL:
            vm->last_exit_reason = VMM_EXIT_HYPERCALL;
            vm->regs.rip = exit_rip + (inst_len ? inst_len : 3);
            break;

        case VMX_EXIT_EPT_VIOLATION:
            vm->last_exit_reason = VMM_EXIT_EPT_VIOLATION;
            break;

        case VMX_EXIT_CR_ACCESS:
            vm->last_exit_reason = VMM_EXIT_CR_ACCESS;
            vm->regs.rip = exit_rip + (inst_len ? inst_len : 3);
            break;

        default:
            vm->last_exit_reason = VMM_EXIT_UNKNOWN;
            break;
    }

    return 0;
}

int vmx_destroy_vm(vmm_vm_t *vm) {
    if (!vm || !vm->arch_cb_phys) return 0;

    vmx_vmclear(vm->arch_cb_phys);
    pmm_free_page(vm->arch_cb_virt);
    vm->arch_cb_virt = NULL;
    vm->arch_cb_phys = 0;
    return 0;
}
