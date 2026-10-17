/* =============================================================================
 * Nyota OS — Hypervisor Guest Environment & Micro-Payload
 * Minimal guest virtual machine code verifying CPUID, memory writes, and VM-exits.
 * =========================================================================== */

#include "vmm/guest.h"
#include "vmm/vmm.h"
#include "memory.h"
#include "kernel.h"

/* ── Minimal Guest Bytecode ─────────────────────────────────────────────────
 * 64-bit Flat Mode Guest Payload:
 *
 * 1. Load Initial Signature:
 *    b8 54 4f 59 4e          mov eax, 0x4E594F54  ('NYOT')
 *    bb 00 07 01 00          mov ebx, 0x00010700  (v1.7.0)
 *
 * 2. CPUID Interception Test:
 *    31 c0                   xor eax, eax
 *    0f a2                   cpuid                ; Intercepted -> returns signature
 *
 * 3. Isolated Memory Write Test:
 *    c7 04 25 00 10 00 00    mov dword [0x1000], 0xCAFEBABE
 *    be ba fe ca
 *
 * 4. Completion Signal (HLT):
 *    f4                      hlt                  ; Intercepted -> terminates cleanly
 * ─────────────────────────────────────────────────────────────────────────── */
static const uint8_t guest_bytecode[] = {
    0xb8, 0x54, 0x4f, 0x59, 0x4e,       /* mov eax, 0x4E594F54 */
    0xbb, 0x00, 0x07, 0x01, 0x00,       /* mov ebx, 0x00010700 */
    0x31, 0xc0,                         /* xor eax, eax        */
    0x0f, 0xa2,                         /* cpuid               */
    0xc7, 0x04, 0x25, 0x00, 0x10, 0x00,
    0x00, 0xbe, 0xba, 0xfe, 0xca,       /* mov dword [0x1000], 0xCAFEBABE */
    0xf4                                /* hlt                 */
};

int guest_load_payload(vmm_vm_t *vm) {
    if (!vm || !vm->guest_mem_virt || vm->guest_mem_size < 0x2000) {
        return -1;
    }

    /* Zero the first 64 KiB of guest memory */
    memset(vm->guest_mem_virt, 0, (vm->guest_mem_size < 0x10000) ? vm->guest_mem_size : 0x10000);

    /* Copy micro-payload to entry point (GPA 0x0) */
    memcpy(vm->guest_mem_virt, guest_bytecode, sizeof(guest_bytecode));

    /* Build identity page tables in guest memory:
     * PML4 at GPA 0x2000
     * PDPT at GPA 0x3000
     * PD   at GPA 0x4000 (maps 0..2MB using 2MB huge page with Present|Writable|Huge)
     */
    uint64_t *guest_pml4 = (uint64_t *)((uintptr_t)vm->guest_mem_virt + 0x2000);
    uint64_t *guest_pdpt = (uint64_t *)((uintptr_t)vm->guest_mem_virt + 0x3000);
    uint64_t *guest_pd   = (uint64_t *)((uintptr_t)vm->guest_mem_virt + 0x4000);

    memset(guest_pml4, 0, 4096);
    memset(guest_pdpt, 0, 4096);
    memset(guest_pd, 0, 4096);

    uint64_t phys = vm->guest_mem_phys;

    guest_pml4[0] = (phys + 0x3000) | 0x07; /* Points to guest PDPT, Present|Writable|User */
    guest_pdpt[0] = (phys + 0x4000) | 0x07; /* Points to guest PD, Present|Writable|User */
    guest_pd[0]   = (phys + 0x0000) | 0x87; /* Maps 0..2MB identity, Present|Writable|User|PageSize=2MB */

    return 0;
}

void guest_init_registers(vmm_vm_t *vm) {
    if (!vm) return;

    memset(&vm->regs, 0, sizeof(vmm_guest_regs_t));
    vm->regs.rip    = GUEST_ENTRY_POINT;
    vm->regs.rsp    = GUEST_STACK_TOP;
    vm->regs.rflags = 0x00000002;           /* Bit 1 always 1 */
    vm->regs.rax    = GUEST_MAGIC_INITIAL;

    /* 64-bit Long Mode Control Registers */
    vm->regs.cr0    = 0x80000011;           /* PG | ET | PE */
    vm->regs.cr4    = 0x00000020;           /* PAE */
    vm->regs.cr3    = vm->guest_mem_phys + 0x2000; /* Guest PML4 */
    vm->regs.efer   = 0x00000500;           /* LMA | LME */
}

bool guest_verify_execution(vmm_vm_t *vm) {
    if (!vm || !vm->guest_mem_virt || vm->guest_mem_size < 0x2000) {
        return false;
    }

    /* Verify isolated memory write at GPA 0x1000 */
    volatile uint32_t *target = (volatile uint32_t *)((uintptr_t)vm->guest_mem_virt + 0x1000);
    return (*target == GUEST_MAGIC_MEMORY);
}
