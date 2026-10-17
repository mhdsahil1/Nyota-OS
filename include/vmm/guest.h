/* =============================================================================
 * Nyota OS — Hypervisor Guest Environment & Payload
 * Prepares the minimal virtual machine execution payload and registers.
 * =========================================================================== */

#ifndef NYOTA_VMM_GUEST_H
#define NYOTA_VMM_GUEST_H

#include "types.h"
#include "vmm/vmm.h"

#define GUEST_ENTRY_POINT       0x00000000ULL
#define GUEST_STACK_TOP         0x0001F000ULL
#define GUEST_MAGIC_INITIAL     0x4E594F54ULL  /* 'NYOT' */
#define GUEST_MAGIC_MEMORY      0xCAFEBABEU

int  guest_load_payload(vmm_vm_t *vm);
void guest_init_registers(vmm_vm_t *vm);
bool guest_verify_execution(vmm_vm_t *vm);

#endif /* NYOTA_VMM_GUEST_H */
