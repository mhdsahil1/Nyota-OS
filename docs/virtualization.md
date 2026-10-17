# Nyota OS Virtualization & Hypervisor Architecture (v1.7.0)

## Overview

Nyota OS Phase 17 introduces a hardware-assisted hypervisor (Virtual Machine Monitor, or VMM) subsystem designed to run isolated virtual machines directly on the host processor. The architecture is vendor-agnostic and supports both **Intel VT-x (VMX)** and **AMD-V (SVM)** hardware virtualization extensions.

---

## 1. Subsystem Architecture

The hypervisor consists of three primary layers:

```
+--------------------------------------------------------------+
|                Userspace Control & Utilities                 |
|          /bin/vmm (info | run | stats | test)                |
|          Desktop Settings (System Virtualization Panel)      |
+--------------------------------------------------------------+
                               |
                        SYS_VMM_OP (87)
                               |
+--------------------------------------------------------------+
|             Unified Hypervisor Engine (kernel/vmm)           |
|   vmm.c        - Subsystem init, VM descriptors, cycle loops |
|   guest.c      - Flat 64-bit guest payloads & memory tests   |
|   ept.c        - Extended (EPT) and Nested (NPT) Page Tables |
+--------------------------------------------------------------+
            |                                      |
+-----------------------+              +-----------------------+
|   Intel VT-x Driver   |              |     AMD-V Driver      |
|   (kernel/vmm/vmx.c)  |              |  (kernel/vmm/svm.c)   |
|   VMXON / VMCS        |              |  EFER.SVME / VMCB     |
+-----------------------+              +-----------------------+
            |                                      |
+--------------------------------------------------------------+
|                     Host CPU Hardware                        |
+--------------------------------------------------------------+
```

---

## 2. Hardware Support & Detection

1. **Vendor Detection:**
   - Evaluates standard CPUID leaves (`CPUID.0:EBX/EDX/ECX`) to distinguish between `AuthenticAMD` and `GenuineIntel`.
2. **Intel VT-x (VMX):**
   - Verified via `CPUID.1:ECX[5]` (VMX feature flag).
   - Validates firmware enablement in `IA32_FEATURE_CONTROL_MSR` (0x3A): lock bit (bit 0) and `VMXON outside SMX` (bit 2).
   - Reads `IA32_VMX_BASIC_MSR` (0x480) for VMCS revision ID and region sizing (4096 bytes).
   - Queries `IA32_VMX_EPT_VPID_CAP_MSR` for 4-level nested paging and page-walk length capabilities.
3. **AMD-V (SVM):**
   - Verified via `CPUID.0x80000001:ECX[2]` (SVM feature flag).
   - Inspects `MSR_VM_CR` (0xC0010114) bit 4 (`SVMDIS`) to confirm firmware has not locked out virtualization.
   - Enables `EFER.SVME` (bit 12) across executing CPU cores.
   - Allocates and configures the host state-save physical region (`MSR_VM_HSAVE_PA`, 0xC0010117).

---

## 3. Virtual Machine Control Block (VMCB / VMCS)

### AMD-V VMCB (`include/vmm/svm.h`)
- Structured as a 4 KiB aligned physical block split into:
  - **Control Area (0x000 - 0x3FF):** Intercept masks for instructions (`CPUID`, `HLT`, `VMRUN`, `VMMCALL`), guest ASID, and nested paging configurations (`n_cr3`, `np_enable`).
  - **State Save Area (0x400 - 0xFFF):** Flat 64-bit segment descriptors (`CS`, `DS`, `ES`, `SS`, `FS`, `GS`, `TR`, `GDTR`, `IDTR`), control registers (`CR0`, `CR3`, `CR4`), `EFER`, and general registers (`RIP`, `RSP`, `RAX`).

### Intel VT-x VMCS (`include/vmm/vmx.h`)
- 4 KiB physical structure containing:
  - Guest-state fields (registers, segments, CR0/CR3/CR4, RIP, RSP).
  - Host-state fields (host kernel code, stack, CR0/CR3/CR4, IDTR, GDTR).
  - VM-execution control fields (pin-based, processor-based, secondary controls including EPT).
  - VM-exit and VM-entry controls.

---

## 4. Guest Environment & Memory Isolation

- **Payload (`kernel/vmm/guest.c`):**
  - Minimal 64-bit flat long-mode micro-payload testing CPUID interception, isolated memory writes to GPA `0x1000` (`0xCAFEBABE`), and clean termination via `HLT`.
- **Page Tables:**
  - 4-level identity page tables (PML4, PDPT, PD) established in guest memory.
- **Isolation Verification:**
  - The guest memory buffer is allocated via `pmm_alloc_page()` and isolated from host kernel memory.
  - The host verifies guest writes by checking GPA `0x1000` for magic `0xCAFEBABE`.

---

## 5. Userspace Interface (`/bin/vmm`)

The `/bin/vmm` binary provides command-line control of the hypervisor:

| Command | Description |
| :--- | :--- |
| `vmm info` | Displays detected hardware virtualization vendor, operational status, revision, and capabilities. |
| `vmm stats` | Reports global hypervisor telemetry: total VM entries, exits, CPUID intercepts, HLT exits, and faults. |
| `vmm test` | Executes the complete hardware-assisted guest VM lifecycle and memory isolation verification test. |
| `vmm run` | Launches a guest virtual machine cycle and outputs the diagnostic report. |
| `vmm help` | Displays available commands and usage instructions. |

---

## 6. System Call Interface

**Syscall Number:** `SYS_VMM_OP` (87)

- `sys_vmm_op(op, a1, a2, a3)`:
  - `VMM_OP_GET_INFO` (1): Copies hypervisor capabilities and status to userspace buffer.
  - `VMM_OP_RUN_VM` (2): Executes guest VM cycle and returns formatted diagnostics.
  - `VMM_OP_GET_STATS` (3): Copies operational statistics structure to userspace.
