# Nyota OS Memory Protection & Kernel Hardening

**Target Release:** `v1.4.0`  
**Architecture:** x86_64  
**Hardware Features:** EFER.NXE, CR0.WP, 4-Level Paging (PML4)

---

## 1. Hardware Execution Prevention (NX / XD)

### 1.1 Enablement
Nyota OS enables the Execute Disable / No-Execute bit supported by modern x86_64 processors:
* **MSR 0xC0000080 (`IA32_EFER`):** Bit 11 (`NXE`) is explicitly set during:
  1. Bootloader stage 2 (`boot/stage2.asm`).
  2. Multi-core AP trampoline initialization (`kernel/arch/x86_64/trampoline.asm`).
  3. Kernel CPU initialization (`kernel/cpu/cpu.c`).

### 1.2 Paging Flags
The page table entry bit 63 (`1ULL << 63`, `PAGE_NX`) marks pages as non-executable. Any instruction fetch from an NX page immediately triggers a Page Fault exception (`#PF`, interrupt 14) with error code bit 4 (`I/D` instruction fetch) set.

---

## 2. $W \oplus X$ (Write XOR Execute) Policy

Under Nyota OS v1.4.0, pages are either writable or executable, but never both simultaneously:

| Memory Region | Writable | Executable | Page Flags |
|---------------|:--------:|:----------:|------------|
| **User Code (.text)** | No | Yes | `PAGE_PRESENT \| PAGE_USER` |
| **User Data (.data, .bss)** | Yes | No | `PAGE_PRESENT \| PAGE_USER \| PAGE_WRITABLE \| PAGE_NX` |
| **User Stack** | Yes | No | `PAGE_PRESENT \| PAGE_USER \| PAGE_WRITABLE \| PAGE_NX` |
| **User Heap** | Yes | No | `PAGE_PRESENT \| PAGE_USER \| PAGE_WRITABLE \| PAGE_NX` |
| **Shared Memory (SHM)**| Yes | No | `PAGE_PRESENT \| PAGE_USER \| PAGE_WRITABLE \| PAGE_NX` |
| **Kernel Code** | No | Yes | `PAGE_PRESENT` |
| **Kernel Data / Heap** | Yes | No | `PAGE_PRESENT \| PAGE_WRITABLE \| PAGE_NX` |

### 2.1 Enforcement in ELF Loading
The kernel ELF loader (`kernel/elf/elf.c`) inspects program header flags (`ph->p_flags`):
* Segments without `PF_X` or with `PF_W` are mapped with `PAGE_NX`.
* Segments with `PF_X` are mapped without `PAGE_WRITABLE`.

---

## 3. Kernel Page Protection (`CR0.WP`)

The `CR0.WP` (Write Protect) bit 16 is enabled in the CPU Control Register 0:
* Prevents the supervisor (Ring 0) from writing to pages marked read-only.
* Protects kernel read-only text, interrupt descriptor tables, and constant data structures against inadvertent overwrite or pointer corruption bugs.

---

## 4. User Pointer Validation & Safe Copy

Before any user-space pointer is accessed in a system call handler:
1. **Range Validation:** The pointer address must fall strictly within the user canonical address range:
   ```c
   uint64_t addr = (uint64_t)ptr;
   if (addr >= 0x0000800000000000ULL || (addr + size) > 0x0000800000000000ULL)
       return false;
   ```
2. **Page Table Flag Verification:** `paging_get_flags_in(pml4, page_addr)` inspects the active PML4:
   * Must have `PAGE_PRESENT`.
   * Must have `PAGE_USER`.
   * If `write == true`, must have `PAGE_WRITABLE`.
3. **Safe Copy:** `copy_from_user()` and `copy_to_user()` return `-EFAULT` if validation fails, preventing kernel crashes.

---

## 5. Heap Hardening

The kernel dynamic memory allocator (`kernel/memory/heap.c`) enforces:
* **Allocation Bounds:** Total heap size is bounded and monitored.
* **Integer Overflow Detection:** Allocation functions verify that `count * size` does not wrap around:
  ```c
  if (size_mul_overflow(count, size, &total)) return NULL;
  ```
* **Double-Free & Corruption Checks:** Chunk magic headers and free list links are checked prior to merging or freeing blocks.
