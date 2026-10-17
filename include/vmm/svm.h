/* =============================================================================
 * Nyota OS — AMD-V (SVM) Hardware Definitions & VMCB Structure
 * =========================================================================== */

#ifndef NYOTA_VMM_SVM_H
#define NYOTA_VMM_SVM_H

#include "types.h"
#include "vmm/vmm.h"

/* ── AMD-V MSRs ───────────────────────────────────────────────────────────── */
#define MSR_EFER                            0xC0000080
#define EFER_SVME                           (1ULL << 12)

#define MSR_VM_CR                           0xC0010114
#define VM_CR_DPD                           (1ULL << 0)
#define VM_CR_R_INIT                        (1ULL << 1)
#define VM_CR_DIS_A20M                      (1ULL << 2)
#define VM_CR_LOCK                          (1ULL << 3)
#define VM_CR_SVMDIS                        (1ULL << 4)

#define MSR_VM_HSAVE_PA                     0xC0010117

/* ── SVM VM-Exit Codes ────────────────────────────────────────────────────── */
#define SVM_EXIT_READ_CR0                   0x0000
#define SVM_EXIT_READ_CR3                   0x0003
#define SVM_EXIT_READ_CR4                   0x0004
#define SVM_EXIT_READ_CR8                   0x0008
#define SVM_EXIT_WRITE_CR0                  0x0010
#define SVM_EXIT_WRITE_CR3                  0x0013
#define SVM_EXIT_WRITE_CR4                  0x0014
#define SVM_EXIT_WRITE_CR8                  0x0018
#define SVM_EXIT_READ_DR0                   0x0020
#define SVM_EXIT_WRITE_DR0                  0x0030
#define SVM_EXIT_EXCP_DB                    0x0041
#define SVM_EXIT_EXCP_BP                    0x0043
#define SVM_EXIT_EXCP_UD                    0x0046
#define SVM_EXIT_EXCP_PF                    0x004E
#define SVM_EXIT_INTR                       0x0060
#define SVM_EXIT_NMI                        0x0061
#define SVM_EXIT_SMI                        0x0062
#define SVM_EXIT_INIT                       0x0063
#define SVM_EXIT_VINTR                      0x0064
#define SVM_EXIT_CR0_SEL_WRITE              0x0065
#define SVM_EXIT_IDTR_READ                  0x0066
#define SVM_EXIT_GDTR_READ                  0x0067
#define SVM_EXIT_LDTR_READ                  0x0068
#define SVM_EXIT_TR_READ                    0x0069
#define SVM_EXIT_IDTR_WRITE                 0x006A
#define SVM_EXIT_GDTR_WRITE                 0x006B
#define SVM_EXIT_LDTR_WRITE                 0x006C
#define SVM_EXIT_TR_WRITE                   0x006D
#define SVM_EXIT_RDTSC                      0x006E
#define SVM_EXIT_RDPMC                      0x006F
#define SVM_EXIT_PUSHF                      0x0070
#define SVM_EXIT_POPF                       0x0071
#define SVM_EXIT_CPUID                      0x0072
#define SVM_EXIT_RSM                        0x0073
#define SVM_EXIT_IRET                       0x0074
#define SVM_EXIT_SWINT                      0x0075
#define SVM_EXIT_INVD                       0x0076
#define SVM_EXIT_PAUSE                      0x0077
#define SVM_EXIT_HLT                        0x0078
#define SVM_EXIT_INVLPG                     0x0079
#define SVM_EXIT_INVLPGA                    0x007A
#define SVM_EXIT_IOIO                       0x007B
#define SVM_EXIT_MSR                        0x007C
#define SVM_EXIT_TASK_SWITCH                0x007D
#define SVM_EXIT_FERR_FREEZE                0x007E
#define SVM_EXIT_SHUTDOWN                   0x007F
#define SVM_EXIT_VMRUN                      0x0080
#define SVM_EXIT_VMMCALL                    0x0081
#define SVM_EXIT_VMLOAD                     0x0082
#define SVM_EXIT_VMSAVE                     0x0083
#define SVM_EXIT_STGI                       0x0084
#define SVM_EXIT_CLGI                       0x0085
#define SVM_EXIT_SKINIT                     0x0086
#define SVM_EXIT_RDTSCP                     0x0087
#define SVM_EXIT_ICEBP                      0x0088
#define SVM_EXIT_WBINVD                     0x0089
#define SVM_EXIT_MONITOR                    0x008A
#define SVM_EXIT_MWAIT                      0x008B
#define SVM_EXIT_MWAIT_CONDITIONAL          0x008C
#define SVM_EXIT_XSETBV                     0x008D
#define SVM_EXIT_RDPRU                      0x008E
#define SVM_EXIT_EFER_WRITE_TRAP            0x008F
#define SVM_EXIT_NPF                        0x0400  /* Nested Page Fault */
#define SVM_EXIT_AVIC_INCOMPLETE_IPI        0x0401
#define SVM_EXIT_AVIC_NOACCEL               0x0402
#define SVM_EXIT_VMGEXIT                    0x0403
#define SVM_EXIT_INVALID                    ((uint64_t)-1)

/* ── SVM Segment Register Structure ───────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t selector;
    uint16_t attrib;
    uint32_t limit;
    uint64_t base;
} svm_segment_t;

/* ── Virtual Machine Control Block (VMCB) Layout (4096 bytes) ─────────────── */
typedef struct __attribute__((packed, aligned(4096))) {
    /* Control Area (Offsets 0x000 - 0x3FF) */
    struct __attribute__((packed)) {
        uint16_t intercept_cr_read;         /* 0x000 */
        uint16_t intercept_cr_write;        /* 0x002 */
        uint16_t intercept_dr_read;         /* 0x004 */
        uint16_t intercept_dr_write;        /* 0x006 */
        uint32_t intercept_exceptions;      /* 0x008 */
        uint32_t intercept_instructions;    /* 0x00C */
        uint32_t intercept_instructions2;   /* 0x010 */
        uint8_t  reserved1[40];             /* 0x014 */
        uint16_t pause_filter_threshold;    /* 0x03C */
        uint16_t pause_filter_count;        /* 0x03E */
        uint64_t iopm_base_pa;              /* 0x040 */
        uint64_t msrpm_base_pa;             /* 0x048 */
        uint64_t tsc_offset;                /* 0x050 */
        uint32_t guest_asid;                /* 0x058 */
        uint8_t  tlb_control;               /* 0x05C */
        uint8_t  reserved2[3];              /* 0x05D */
        uint64_t vintr;                     /* 0x060 */
        uint64_t interrupt_shadow;          /* 0x068 */
        uint64_t exitcode;                  /* 0x070 */
        uint64_t exitinfo1;                 /* 0x078 */
        uint64_t exitinfo2;                 /* 0x080 */
        uint64_t exitintinfo;               /* 0x088 */
        uint64_t np_enable;                 /* 0x090: bit 0 = Nested Paging Enabled */
        uint64_t avic_control;              /* 0x098 */
        uint8_t  reserved3[16];             /* 0x0A0 */
        uint64_t event_inj;                 /* 0x0B0 */
        uint64_t n_cr3;                     /* 0x0B8: Nested Page Table CR3 */
        uint64_t lbr_virtualization_enable; /* 0x0C0 */
        uint64_t vmcb_clean_bits;           /* 0x0C8 */
        uint64_t nrip;                      /* 0x0D0: Next sequential RIP on exit */
        uint8_t  num_guest_inst_bytes;      /* 0x0D8 */
        uint8_t  guest_inst_bytes[15];      /* 0x0D9 */
        uint8_t  reserved4[800];            /* 0x0E8 - 0x3FF */
    } control;

    /* State Save Area (Offsets 0x400 - 0xFFF) */
    struct __attribute__((packed)) {
        svm_segment_t es;                   /* 0x400 */
        svm_segment_t cs;                   /* 0x410 */
        svm_segment_t ss;                   /* 0x420 */
        svm_segment_t ds;                   /* 0x430 */
        svm_segment_t fs;                   /* 0x440 */
        svm_segment_t gs;                   /* 0x450 */
        svm_segment_t gdtr;                 /* 0x460 */
        svm_segment_t ldtr;                 /* 0x470 */
        svm_segment_t idtr;                 /* 0x480 */
        svm_segment_t tr;                   /* 0x490 */
        uint8_t       reserved1[43];        /* 0x4A0 */
        uint8_t       cpl;                  /* 0x4CB */
        uint32_t      reserved2;            /* 0x4CC */
        uint64_t      efer;                 /* 0x4D0 */
        uint8_t       reserved3[112];       /* 0x4D8 */
        uint64_t      cr4;                  /* 0x548 */
        uint64_t      cr3;                  /* 0x550 */
        uint64_t      cr0;                  /* 0x558 */
        uint64_t      dr7;                  /* 0x560 */
        uint64_t      dr6;                  /* 0x568 */
        uint64_t      rflags;               /* 0x570 */
        uint64_t      rip;                  /* 0x578 */
        uint8_t       reserved4[88];        /* 0x580 */
        uint64_t      rsp;                  /* 0x5D8 */
        uint8_t       reserved5[24];        /* 0x5E0 */
        uint64_t      rax;                  /* 0x5F8 */
        uint8_t       reserved6[2560];      /* 0x600 - 0xFFF */
    } state;
} vmcb_t;

/* ── Inline SVM Assembly Instructions ─────────────────────────────────────── */

static inline void svm_clgi(void) {
    __asm__ volatile ("clgi" : : : "memory");
}

static inline void svm_stgi(void) {
    __asm__ volatile ("stgi" : : : "memory");
}

static inline void svm_vmload(uint64_t vmcb_pa) {
    __asm__ volatile ("vmload %0" : : "a"(vmcb_pa) : "memory");
}

static inline void svm_vmsave(uint64_t vmcb_pa) {
    __asm__ volatile ("vmsave %0" : : "a"(vmcb_pa) : "memory");
}

/* ── SVM Subsystem Function Declarations ──────────────────────────────────── */
bool svm_detect_hardware(vmm_capabilities_t *caps);
int  svm_init_host(void);
int  svm_create_vm(vmm_vm_t *vm);
int  svm_run_vm(vmm_vm_t *vm);
int  svm_destroy_vm(vmm_vm_t *vm);

#endif /* NYOTA_VMM_SVM_H */
