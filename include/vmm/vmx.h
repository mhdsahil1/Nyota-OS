/* =============================================================================
 * Nyota OS — Intel VT-x (VMX) Hardware Definitions & Encodings
 * =========================================================================== */

#ifndef NYOTA_VMM_VMX_H
#define NYOTA_VMM_VMX_H

#include "types.h"
#include "vmm/vmm.h"

/* ── Intel VMX MSRs ───────────────────────────────────────────────────────── */
#define IA32_FEATURE_CONTROL_MSR            0x0000003A
#define IA32_VMX_BASIC_MSR                  0x00000480
#define IA32_VMX_PINBASED_CTLS_MSR          0x00000481
#define IA32_VMX_PROCBASED_CTLS_MSR         0x00000482
#define IA32_VMX_EXIT_CTLS_MSR              0x00000483
#define IA32_VMX_ENTRY_CTLS_MSR             0x00000484
#define IA32_VMX_MISC_MSR                   0x00000485
#define IA32_VMX_CR0_FIXED0_MSR             0x00000486
#define IA32_VMX_CR0_FIXED1_MSR             0x00000487
#define IA32_VMX_CR4_FIXED0_MSR             0x00000488
#define IA32_VMX_CR4_FIXED1_MSR             0x00000489
#define IA32_VMX_VMCS_ENUM_MSR              0x0000048A
#define IA32_VMX_PROCBASED_CTLS2_MSR        0x0000048B
#define IA32_VMX_EPT_VPID_CAP_MSR           0x0000048C
#define IA32_VMX_TRUE_PINBASED_CTLS_MSR     0x0000048D
#define IA32_VMX_TRUE_PROCBASED_CTLS_MSR    0x0000048E
#define IA32_VMX_TRUE_EXIT_CTLS_MSR         0x0000048F
#define IA32_VMX_TRUE_ENTRY_CTLS_MSR        0x00000490

/* IA32_FEATURE_CONTROL Bits */
#define IA32_FEATURE_CONTROL_LOCK           (1ULL << 0)
#define IA32_FEATURE_CONTROL_VMXON_INSIDE_SMX (1ULL << 1)
#define IA32_FEATURE_CONTROL_VMXON_OUTSIDE_SMX (1ULL << 2)

/* ── VMCS Field Encodings ─────────────────────────────────────────────────── */
/* 16-Bit Guest State Fields */
#define VMCS_GUEST_ES_SELECTOR              0x00000800
#define VMCS_GUEST_CS_SELECTOR              0x00000802
#define VMCS_GUEST_SS_SELECTOR              0x00000804
#define VMCS_GUEST_DS_SELECTOR              0x00000806
#define VMCS_GUEST_FS_SELECTOR              0x00000808
#define VMCS_GUEST_GS_SELECTOR              0x0000080A
#define VMCS_GUEST_LDTR_SELECTOR            0x0000080C
#define VMCS_GUEST_TR_SELECTOR              0x0000080E

/* 16-Bit Host State Fields */
#define VMCS_HOST_ES_SELECTOR               0x00000C00
#define VMCS_HOST_CS_SELECTOR               0x00000C02
#define VMCS_HOST_SS_SELECTOR               0x00000C04
#define VMCS_HOST_DS_SELECTOR               0x00000C06
#define VMCS_HOST_FS_SELECTOR               0x00000C08
#define VMCS_HOST_GS_SELECTOR               0x00000C0A
#define VMCS_HOST_TR_SELECTOR               0x00000C0C

/* 64-Bit Control Fields */
#define VMCS_IO_BITMAP_A                    0x00002000
#define VMCS_IO_BITMAP_B                    0x00002002
#define VMCS_MSR_BITMAP                     0x00002004
#define VMCS_VM_EXIT_MSR_STORE_ADDR         0x00002006
#define VMCS_VM_EXIT_MSR_LOAD_ADDR          0x00002008
#define VMCS_VM_ENTRY_MSR_LOAD_ADDR         0x0000200A
#define VMCS_EXECUTIVE_VMCS_POINTER         0x0000200C
#define VMCS_TSC_OFFSET                     0x00002010
#define VMCS_EPT_POINTER                    0x0000201A

/* 64-Bit Read-Only Fields */
#define VMCS_GUEST_PHYSICAL_ADDRESS         0x00002400

/* 64-Bit Guest State Fields */
#define VMCS_VMCS_LINK_POINTER              0x00002800
#define VMCS_GUEST_IA32_DEBUGCTL            0x00002802
#define VMCS_GUEST_IA32_PAT                 0x00002804
#define VMCS_GUEST_IA32_EFER                0x00002806

/* 64-Bit Host State Fields */
#define VMCS_HOST_IA32_PAT                  0x00002C00
#define VMCS_HOST_IA32_EFER                 0x00002C02

/* 32-Bit Control Fields */
#define VMCS_PIN_BASED_VM_EXEC_CONTROL      0x00004000
#define VMCS_PROC_BASED_VM_EXEC_CONTROL     0x00004002
#define VMCS_EXCEPTION_BITMAP               0x00004004
#define VMCS_PAGE_FAULT_ERROR_CODE_MASK     0x00004006
#define VMCS_PAGE_FAULT_ERROR_CODE_MATCH    0x00004008
#define VMCS_CR3_TARGET_COUNT               0x0000400A
#define VMCS_VM_EXIT_CONTROLS               0x0000400C
#define VMCS_VM_EXIT_MSR_STORE_COUNT        0x0000400E
#define VMCS_VM_EXIT_MSR_LOAD_COUNT         0x00004010
#define VMCS_VM_ENTRY_CONTROLS              0x00004012
#define VMCS_VM_ENTRY_MSR_LOAD_COUNT        0x00004014
#define VMCS_VM_ENTRY_INTR_INFO_FIELD       0x00004016
#define VMCS_SECONDARY_VM_EXEC_CONTROL      0x0000401E

/* 32-Bit Read-Only Fields */
#define VMCS_VM_INSTRUCTION_ERROR           0x00004400
#define VMCS_VM_EXIT_REASON                 0x00004402
#define VMCS_VM_EXIT_INTR_INFO              0x00004404
#define VMCS_VM_EXIT_INTR_ERROR_CODE        0x00004406
#define VMCS_IDT_VECTORING_INFO_FIELD       0x00004408
#define VMCS_IDT_VECTORING_ERROR_CODE       0x0000440A
#define VMCS_VM_EXIT_INSTRUCTION_LEN        0x0000440C
#define VMCS_VMX_INSTRUCTION_INFO           0x0000440E

/* 32-Bit Guest State Fields */
#define VMCS_GUEST_ES_LIMIT                 0x00004800
#define VMCS_GUEST_CS_LIMIT                 0x00004802
#define VMCS_GUEST_SS_LIMIT                 0x00004804
#define VMCS_GUEST_DS_LIMIT                 0x00004806
#define VMCS_GUEST_FS_LIMIT                 0x00004808
#define VMCS_GUEST_GS_LIMIT                 0x0000480A
#define VMCS_GUEST_LDTR_LIMIT               0x0000480C
#define VMCS_GUEST_TR_LIMIT                 0x0000480E
#define VMCS_GUEST_GDTR_LIMIT               0x00004810
#define VMCS_GUEST_IDTR_LIMIT               0x00004812
#define VMCS_GUEST_ES_ACCESS_RIGHTS         0x00004814
#define VMCS_GUEST_CS_ACCESS_RIGHTS         0x00004816
#define VMCS_GUEST_SS_ACCESS_RIGHTS         0x00004818
#define VMCS_GUEST_DS_ACCESS_RIGHTS         0x0000481A
#define VMCS_GUEST_FS_ACCESS_RIGHTS         0x0000481C
#define VMCS_GUEST_GS_ACCESS_RIGHTS         0x0000481E
#define VMCS_GUEST_LDTR_ACCESS_RIGHTS       0x00004820
#define VMCS_GUEST_TR_ACCESS_RIGHTS         0x00004822
#define VMCS_GUEST_INTERRUPTIBILITY_INFO    0x00004824
#define VMCS_GUEST_ACTIVITY_STATE           0x00004826
#define VMCS_GUEST_SMBASE                   0x00004828
#define VMCS_GUEST_SYSENTER_CS              0x0000482A

/* 32-Bit Host State Fields */
#define VMCS_HOST_IA32_SYSENTER_CS          0x00004C00

/* Natural-Width Control Fields */
#define VMCS_CR0_GUEST_HOST_MASK            0x00006000
#define VMCS_CR4_GUEST_HOST_MASK            0x00006002
#define VMCS_CR0_READ_SHADOW                0x00006004
#define VMCS_CR4_READ_SHADOW                0x00006006

/* Natural-Width Read-Only Fields */
#define VMCS_EXIT_QUALIFICATION             0x00006400
#define VMCS_IO_RCX                         0x00006402
#define VMCS_IO_RSI                         0x00006404
#define VMCS_IO_RDI                         0x00006406
#define VMCS_IO_RIP                         0x00006408
#define VMCS_GUEST_LINEAR_ADDRESS           0x0000640A

/* Natural-Width Guest State Fields */
#define VMCS_GUEST_CR0                      0x00006800
#define VMCS_GUEST_CR3                      0x00006802
#define VMCS_GUEST_CR4                      0x00006804
#define VMCS_GUEST_ES_BASE                  0x00006806
#define VMCS_GUEST_CS_BASE                  0x00006808
#define VMCS_GUEST_SS_BASE                  0x0000680A
#define VMCS_GUEST_DS_BASE                  0x0000680C
#define VMCS_GUEST_FS_BASE                  0x0000680E
#define VMCS_GUEST_GS_BASE                  0x00006810
#define VMCS_GUEST_LDTR_BASE                0x00006812
#define VMCS_GUEST_TR_BASE                  0x00006814
#define VMCS_GUEST_GDTR_BASE                0x00006816
#define VMCS_GUEST_IDTR_BASE                0x00006818
#define VMCS_GUEST_DR7                      0x0000681A
#define VMCS_GUEST_RSP                      0x0000681C
#define VMCS_GUEST_RIP                      0x0000681E
#define VMCS_GUEST_RFLAGS                   0x00006820
#define VMCS_GUEST_PENDING_DBG_EXCEPTIONS   0x00006822
#define VMCS_GUEST_SYSENTER_ESP             0x00006824
#define VMCS_GUEST_SYSENTER_EIP             0x00006826

/* Natural-Width Host State Fields */
#define VMCS_HOST_CR0                       0x00006C00
#define VMCS_HOST_CR3                       0x00006C02
#define VMCS_HOST_CR4                       0x00006C04
#define VMCS_HOST_FS_BASE                   0x00006C06
#define VMCS_HOST_GS_BASE                   0x00006C08
#define VMCS_HOST_TR_BASE                   0x00006C0A
#define VMCS_HOST_GDTR_BASE                 0x00006C0C
#define VMCS_HOST_IDTR_BASE                 0x00006C0E
#define VMCS_HOST_IA32_SYSENTER_ESP         0x00006C10
#define VMCS_HOST_IA32_SYSENTER_EIP         0x00006C12
#define VMCS_HOST_RSP                       0x00006C14
#define VMCS_HOST_RIP                       0x00006C16

/* ── Intel Basic VM-Exit Reasons ──────────────────────────────────────────── */
#define VMX_EXIT_EXCEPTION_OR_NMI           0
#define VMX_EXIT_EXTERNAL_INTERRUPT         1
#define VMX_EXIT_TRIPLE_FAULT               2
#define VMX_EXIT_INIT_SIGNAL                3
#define VMX_EXIT_SIPI                       4
#define VMX_EXIT_IO_SMI                     5
#define VMX_EXIT_OTHER_SMI                  6
#define VMX_EXIT_INTERRUPT_WINDOW           7
#define VMX_EXIT_NMI_WINDOW                 8
#define VMX_EXIT_TASK_SWITCH                9
#define VMX_EXIT_CPUID                      10
#define VMX_EXIT_GETSEC                     11
#define VMX_EXIT_HLT                        12
#define VMX_EXIT_INVD                       13
#define VMX_EXIT_INVLPG                     14
#define VMX_EXIT_RDPMC                      15
#define VMX_EXIT_RDTSC                      16
#define VMX_EXIT_RSM                        17
#define VMX_EXIT_VMCALL                     18
#define VMX_EXIT_VMCLEAR                    19
#define VMX_EXIT_VMLAUNCH                   20
#define VMX_EXIT_VMPTRLD                    21
#define VMX_EXIT_VMPTRST                    22
#define VMX_EXIT_VMREAD                     23
#define VMX_EXIT_VMRESUME                   24
#define VMX_EXIT_VMWRITE                    25
#define VMX_EXIT_VMXOFF                     26
#define VMX_EXIT_VMXON                      27
#define VMX_EXIT_CR_ACCESS                  28
#define VMX_EXIT_MOV_DR                     29
#define VMX_EXIT_IO_INSTRUCTION             30
#define VMX_EXIT_RDMSR                      31
#define VMX_EXIT_WRMSR                      32
#define VMX_EXIT_ENTRY_FAILED_GUEST_STATE   33
#define VMX_EXIT_ENTRY_FAILED_MSR_LOAD      34
#define VMX_EXIT_MWAIT                      36
#define VMX_EXIT_MONITOR_TRAP_FLAG          37
#define VMX_EXIT_MONITOR                    39
#define VMX_EXIT_PAUSE                      40
#define VMX_EXIT_ENTRY_FAILED_MACHINE_CHECK 41
#define VMX_EXIT_TPR_BELOW_THRESHOLD        43
#define VMX_EXIT_APIC_ACCESS                44
#define VMX_EXIT_VIRTUALIZED_EIO            45
#define VMX_EXIT_ACCESS_GDTR_OR_IDTR        46
#define VMX_EXIT_ACCESS_LDTR_OR_TR          47
#define VMX_EXIT_EPT_VIOLATION              48
#define VMX_EXIT_EPT_MISCONFIG              49
#define VMX_EXIT_INVEPT                     50
#define VMX_EXIT_RDTSCP                     51
#define VMX_EXIT_VMX_PREEMPTION_TIMER       52
#define VMX_EXIT_INVVPID                    53
#define VMX_EXIT_WBINVD                     54
#define VMX_EXIT_XSETBV                     55
#define VMX_EXIT_APIC_WRITE                 56
#define VMX_EXIT_RDRAND                     57
#define VMX_EXIT_INVPCID                    58
#define VMX_EXIT_VMFUNC                     59
#define VMX_EXIT_ENCLS                      60
#define VMX_EXIT_RDSEED                     61
#define VMX_EXIT_PML_FULL                   62
#define VMX_EXIT_XSAVES                     63
#define VMX_EXIT_XRSTORS                    64

/* ── Inline VMX Instructions ──────────────────────────────────────────────── */

static inline uint8_t vmx_vmxon(uint64_t vmxon_pa) {
    uint8_t error;
    __asm__ volatile ("vmxon %1; setc %0" : "=q"(error) : "m"(vmxon_pa) : "cc", "memory");
    return error;
}

static inline void vmx_vmxoff(void) {
    __asm__ volatile ("vmxoff" : : : "cc", "memory");
}

static inline uint8_t vmx_vmclear(uint64_t vmcs_pa) {
    uint8_t error;
    __asm__ volatile ("vmclear %1; setc %0" : "=q"(error) : "m"(vmcs_pa) : "cc", "memory");
    return error;
}

static inline uint8_t vmx_vmptrld(uint64_t vmcs_pa) {
    uint8_t error;
    __asm__ volatile ("vmptrld %1; setc %0" : "=q"(error) : "m"(vmcs_pa) : "cc", "memory");
    return error;
}

static inline uint64_t vmx_vmread(uint64_t field) {
    uint64_t val = 0;
    __asm__ volatile ("vmread %1, %0" : "=r"(val) : "r"(field) : "cc");
    return val;
}

static inline uint8_t vmx_vmwrite(uint64_t field, uint64_t val) {
    uint8_t error;
    __asm__ volatile ("vmwrite %2, %1; setc %0" : "=q"(error) : "r"(field), "r"(val) : "cc");
    return error;
}

static inline uint8_t vmx_vmlaunch(void) {
    uint8_t error;
    __asm__ volatile ("vmlaunch; setc %0" : "=q"(error) : : "cc", "memory");
    return error;
}

static inline uint8_t vmx_vmresume(void) {
    uint8_t error;
    __asm__ volatile ("vmresume; setc %0" : "=q"(error) : : "cc", "memory");
    return error;
}

/* ── VMX Subsystem Function Declarations ──────────────────────────────────── */
bool vmx_detect_hardware(vmm_capabilities_t *caps);
int  vmx_init_host(void);
int  vmx_create_vm(vmm_vm_t *vm);
int  vmx_run_vm(vmm_vm_t *vm);
int  vmx_destroy_vm(vmm_vm_t *vm);

#endif /* NYOTA_VMM_VMX_H */
