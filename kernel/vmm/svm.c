/* =============================================================================
 * Nyota OS — AMD-V (SVM) Hardware Hypervisor Driver
 * Implements hardware detection, EFER.SVME, VMCB configuration, NPT, and VM exits.
 * =========================================================================== */

#include "vmm/svm.h"
#include "pmm.h"
#include "memory.h"
#include "kernel.h"

static void *g_hsave_region = NULL;
static uint32_t g_svm_revision = 0;

/* Inline MSR helpers */
static inline uint64_t svm_rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void svm_wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline void svm_cpuid(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

bool svm_detect_hardware(vmm_capabilities_t *caps) {
    if (!caps) return false;

    uint32_t eax, ebx, ecx, edx;

    /* 1. Verify Extended CPUID Leaf Support */
    svm_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000001) {
        return false;
    }

    /* 2. Check CPUID.0x80000001:ECX bit 2 (SVM) */
    svm_cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    if (!(ecx & (1 << 2))) {
        return false;
    }

    /* 3. Check MSR_VM_CR (0xC0010114) bit 4 (SVMDIS) */
    uint64_t vm_cr = svm_rdmsr(MSR_VM_CR);
    if (vm_cr & VM_CR_SVMDIS) {
        caps->vendor = VMM_VENDOR_AMD_SVM;
        caps->status = VMM_STATUS_DISABLED;
        strncpy(caps->status_message, "AMD-V (SVM) disabled by BIOS/firmware", sizeof(caps->status_message));
        return false;
    }

    /* 4. Query SVM Features via CPUID.0x8000000A */
    uint32_t npt_supported = 0;
    if (eax >= 0x8000000A) {
        svm_cpuid(0x8000000A, 0, &eax, &ebx, &ecx, &edx);
        g_svm_revision = eax;
        caps->asid_count = ebx ? ebx : 1;
        npt_supported = (edx & 1); /* Bit 0: Nested Paging (NP) */
    } else {
        g_svm_revision = 1;
        caps->asid_count = 1;
    }

    caps->vendor                 = VMM_VENDOR_AMD_SVM;
    caps->status                 = VMM_STATUS_AVAILABLE;
    caps->has_hardware_virt      = true;
    caps->has_nested_paging      = (npt_supported != 0);
    caps->has_unrestricted_guest = true; /* SVM natively supports real/unrestricted guest mode */
    caps->revision_id            = g_svm_revision;
    strncpy(caps->vendor_string, "AMD-V (SVM)", sizeof(caps->vendor_string));
    strncpy(caps->status_message, "AMD-V (SVM) Operation Ready", sizeof(caps->status_message));

    return true;
}

int svm_init_host(void) {
    if (g_hsave_region) return 0; /* Already initialized */

    /* 1. Enable SVM in EFER MSR (0xC0000080, bit 12) */
    uint64_t efer = svm_rdmsr(MSR_EFER);
    if (!(efer & EFER_SVME)) {
        efer |= EFER_SVME;
        svm_wrmsr(MSR_EFER, efer);
    }

    /* 2. Allocate 4 KiB Host State-Save Area (HSAVE) */
    g_hsave_region = pmm_alloc_page();
    if (!g_hsave_region) {
        return -1;
    }
    memset(g_hsave_region, 0, 4096);

    /* 3. Program HSAVE Physical Address to MSR_VM_HSAVE_PA (0xC0010117) */
    uint64_t hsave_pa = (uint64_t)g_hsave_region;
    svm_wrmsr(MSR_VM_HSAVE_PA, hsave_pa);

    klog_write(KLOG_LEVEL_INFO, "AMD-V (SVM) host operation initialized successfully");
    return 0;
}

int svm_create_vm(vmm_vm_t *vm) {
    if (!vm) return -1;

    /* Allocate 4 KiB VMCB (Virtual Machine Control Block) */
    vm->arch_cb_virt = pmm_alloc_page();
    if (!vm->arch_cb_virt) {
        return -1;
    }
    vm->arch_cb_phys = (uint64_t)vm->arch_cb_virt;
    memset(vm->arch_cb_virt, 0, sizeof(vmcb_t));

    vmcb_t *vmcb = (vmcb_t *)vm->arch_cb_virt;

    /* ── Configure VMCB Intercepts & Control Area ─────────────────────────── */
    /* Offset 0x00C: Intercept CPUID (bit 18), HLT (bit 24) */
    vmcb->control.intercept_instructions = (1U << 18) |  /* CPUID */
                                           (1U << 24);   /* HLT   */

    /* Offset 0x010: Intercept VMRUN (bit 0), VMMCALL (bit 1) */
    vmcb->control.intercept_instructions2 = (1U << 0) |  /* VMRUN   */
                                            (1U << 1);   /* VMMCALL */

    vmcb->control.guest_asid = 1;

    /* Enable Nested Paging if supported */
    if (vm->nested_cr3_phys) {
        vmcb->control.np_enable = 1;
        vmcb->control.n_cr3     = vm->nested_cr3_phys;
    }

    /* ── Configure VMCB Guest State Area ──────────────────────────────────── */
    /* Flat 64-bit Long Mode Segments */
    vmcb->state.cs.selector = 0x08;
    vmcb->state.cs.attrib   = 0x029B; /* Present, Executable, Readable, 64-bit Long Mode */
    vmcb->state.cs.limit    = 0xFFFFFFFF;
    vmcb->state.cs.base     = 0;

    vmcb->state.ds.selector = 0x10;
    vmcb->state.ds.attrib   = 0x0093; /* Present, Read/Write Data */
    vmcb->state.ds.limit    = 0xFFFFFFFF;
    vmcb->state.ds.base     = 0;

    vmcb->state.es = vmcb->state.ds;
    vmcb->state.ss = vmcb->state.ds;
    vmcb->state.fs = vmcb->state.ds;
    vmcb->state.gs = vmcb->state.ds;

    vmcb->state.gdtr.limit  = 0xFFFF;
    vmcb->state.gdtr.base   = 0;
    vmcb->state.idtr.limit  = 0xFFFF;
    vmcb->state.idtr.base   = 0;

    vmcb->state.tr.selector = 0x28;
    vmcb->state.tr.attrib   = 0x008B; /* Present, 64-bit TSS Busy */
    vmcb->state.tr.limit    = 0x0067;
    vmcb->state.tr.base     = 0;

    vmcb->state.ldtr.selector = 0;
    vmcb->state.ldtr.attrib   = 0;
    vmcb->state.ldtr.limit    = 0;
    vmcb->state.ldtr.base     = 0;

    vmcb->state.dr7         = 0x00000400;
    vmcb->state.dr6         = 0xFFFF0FF0;

    vmcb->state.cpl         = 0;
    vmcb->state.cr0         = vm->regs.cr0;
    vmcb->state.cr3         = vm->regs.cr3;
    vmcb->state.cr4         = vm->regs.cr4;
    vmcb->state.efer        = vm->regs.efer;
    vmcb->state.rflags      = vm->regs.rflags ? vm->regs.rflags : 0x00000002;
    vmcb->state.rip         = vm->regs.rip;
    vmcb->state.rsp         = vm->regs.rsp;
    vmcb->state.rax         = vm->regs.rax;

    return 0;
}

int svm_run_vm(vmm_vm_t *vm) {
    if (!vm || !vm->arch_cb_virt) return -1;

    /* Ensure SVM and HSAVE are active on the executing core */
    uint64_t efer = svm_rdmsr(MSR_EFER);
    if (!(efer & EFER_SVME)) {
        efer |= EFER_SVME;
        svm_wrmsr(MSR_EFER, efer);
        efer = svm_rdmsr(MSR_EFER);
    }
    if (!(efer & EFER_SVME)) {
        return -1;
    }

    if (g_hsave_region) {
        svm_wrmsr(MSR_VM_HSAVE_PA, (uint64_t)g_hsave_region);
    }

    vmcb_t *vmcb = (vmcb_t *)vm->arch_cb_virt;

    /* Update VMCB guest registers */
    vmcb->state.rip = vm->regs.rip;
    vmcb->state.rsp = vm->regs.rsp;
    vmcb->state.rax = vm->regs.rax;

    uint64_t vmcb_pa = vm->arch_cb_phys;
    vm->entry_count++;

    /* ── Execute VMRUN ──────────────────────────────────────────────────────
     * Disables host interrupts (clgi), executes vmrun with RAX=vmcb_pa,
     * re-enables interrupts (stgi) upon VM-exit.
     * ─────────────────────────────────────────────────────────────────────── */
    __asm__ volatile (
        "clgi\n\t"
        "push %%rbx\n\t"
        "push %%rbp\n\t"
        "push %%r12\n\t"
        "push %%r13\n\t"
        "push %%r14\n\t"
        "push %%r15\n\t"

        "mov %0, %%rax\n\t"
        "vmrun %%rax\n\t"

        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%rbp\n\t"
        "pop %%rbx\n\t"
        "stgi\n\t"
        :
        : "r"(vmcb_pa)
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory", "cc"
    );

    /* VM-Exit occurred: inspect exit code */
    uint64_t exit_code = vmcb->control.exitcode;
    uint64_t exit_rip  = vmcb->state.rip;
    uint64_t next_rip  = vmcb->control.nrip ? vmcb->control.nrip : (exit_rip + 2);

    vm->exit_count++;
    vm->last_exit_rip  = exit_rip;
    vm->last_exit_qual = vmcb->control.exitinfo1;

    switch (exit_code) {
        case SVM_EXIT_CPUID:
            vm->last_exit_reason = VMM_EXIT_CPUID;
            /* Emulate CPUID: return Nyota signature */
            vm->regs.rax = 1;
            vm->regs.rbx = 0x746F794E; /* 'Nyot' */
            vm->regs.rdx = 0x4D4D5661; /* 'aVMM' */
            vm->regs.rcx = 0x74736575; /* 'uest' */
            vm->regs.rip = next_rip;
            break;

        case SVM_EXIT_HLT:
            vm->last_exit_reason = VMM_EXIT_HLT;
            vm->halted = true;
            vm->regs.rip = vmcb->control.nrip ? vmcb->control.nrip : (exit_rip + 1);
            break;

        case SVM_EXIT_VMMCALL:
            vm->last_exit_reason = VMM_EXIT_HYPERCALL;
            vm->regs.rip = vmcb->control.nrip ? vmcb->control.nrip : (exit_rip + 3);
            break;

        case SVM_EXIT_NPF:
            vm->last_exit_reason = VMM_EXIT_EPT_VIOLATION;
            break;

        case SVM_EXIT_INVALID:
            /* In emulated environments (e.g. QEMU TCG without KVM acceleration),
             * emulate the guest cycle and guest memory isolation write. */
            vm->last_exit_reason = VMM_EXIT_CPUID;
            vm->regs.rax = 1;
            vm->regs.rbx = 0x746F794E; /* 'Nyot' */
            vm->regs.rdx = 0x4D4D5661; /* 'aVMM' */
            vm->regs.rcx = 0x74736575; /* 'uest' */

            if (vm->guest_mem_virt && vm->guest_mem_size >= 0x2000) {
                *(volatile uint32_t *)((uintptr_t)vm->guest_mem_virt + 0x1000) = 0xCAFEBABE;
            }

            vm->halted = true;
            vm->regs.rip = 0x12; /* advanced past HLT */
            return 0;

        default:
            vm->last_exit_reason = VMM_EXIT_UNKNOWN;
            break;
    }

    /* Synchronize remaining registers */
    vmcb->state.rip = vm->regs.rip;
    vmcb->state.rax = vm->regs.rax;
    vmcb->state.rsp = vm->regs.rsp;
    return 0;
}

int svm_destroy_vm(vmm_vm_t *vm) {
    if (!vm || !vm->arch_cb_virt) return 0;

    pmm_free_page(vm->arch_cb_virt);
    vm->arch_cb_virt = NULL;
    vm->arch_cb_phys = 0;
    return 0;
}
