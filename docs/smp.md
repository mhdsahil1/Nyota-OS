# Nyota OS — Phase 11: Symmetric Multiprocessing (SMP) & Multicore Kernel Architecture

**Kernel Release**: Nyota OS v1.1.0  
**Target Architecture**: x86_64 Multiprocessor  
**Component**: Hardware Topology, Local APIC, Trampoline, SMP Scheduler, Work Stealing, Ticket Spinlocks, IPIs  

---

## 1. Executive Summary

Phase 11 introduces true Symmetric Multiprocessing (SMP) to Nyota OS, transitioning the operating system from a single-core preemptive kernel to a multicore system capable of scheduling and executing workloads simultaneously across up to 32 x86_64 processor cores.

Nyota OS v1.1.0 preserves the entire subsystem surface established across Phases 1–10 (VBE framebuffer graphics, desktop compositor, multi-windowing, PS/2 mouse and keyboard drivers, Berkeley sockets TCP/IP network stack, ELF execution, and Unix-domain security model) while introducing hardware-driven multicore discovery, per-core CPU isolation, atomic synchronization primitives, Inter-Processor Interrupts (IPIs), distributed run queues, and work-stealing scheduling.

---

## 2. Hardware CPU Discovery (ACPI MADT)

Nyota OS dynamically discovers platform topology using the Advanced Configuration and Power Interface (ACPI):

1. **RSDP Discovery**: Locates the Root System Description Pointer (RSDP) in the Extended BIOS Data Area (EBDA: `0x80000`–`0x9FFFF`) or BIOS ROM memory window (`0xE0000`–`0xFFFFF`) by validating the `"RSD PTR "` 8-byte signature and checksum.
2. **RSDT / XSDT Parsing**: Traverses description table physical pointers to identify the **MADT** (Multiple APIC Description Table) with signature `"APIC"`.
3. **MADT Topology Traversal**:
   - Parses Entry Type `0` (**Local APIC** entries): Records processor IDs, APIC IDs, and validates the `ENABLED` flag bit (`flags & 1`).
   - Parses Entry Type `1` (**I/O APIC** entries): Detects I/O APIC base physical addresses and GSI bases.
   - Parses Entry Type `2` (**Interrupt Source Overrides**): Determines IRQ-to-GSI remap entries (such as PIT IRQ 0 mapped to GSI 2).
4. **BSP Identification**: Discovers the Bootstrap Processor (BSP) APIC ID via `IA32_APIC_BASE_MSR` (`0x1B`) and stores discovered processor states in `acpi_cpu_t`.

---

## 3. AP Startup Sequence & Real Mode Trampoline

Application Processors (APs) are booted from reset into 64-bit long mode using the industry-standard **INIT-SIPI-SIPI** protocol:

```
[BSP Core 0]                                    [AP Core i]
     |                                               |
     |--- 1. Copy Trampoline Blob to 0x8000 -------->|
     |--- 2. Write Target Stack & Entry in Mailbox ->|
     |--- 3. Send INIT IPI (Assert/Deassert) ------->| (Reset vector 0xFFFF0)
     |--- 4. Delay 10 ms --------------------------->|
     |--- 5. Send Startup IPI (SIPI, Vector 0x08) -->| (Starts at phys 0x8000)
     |                                               |--- 16-bit Real Mode
     |                                               |--- Load Temporary GDT32
     |                                               |--- Enable CR0.PE -> 32-bit Protected
     |                                               |--- Load CR3 (Kernel PML4)
     |                                               |--- Enable CR4.PAE + IA32_EFER.LME
     |                                               |--- Enable CR0.PG -> 64-bit Long Mode
     |                                               |--- Load 64-bit GDT & Data Segments
     |                                               |--- Switch to Core Kernel Stack
     |                                               |--- Jump to ap_kernel_entry()
     |<-- 6. AP Writes Status=1 in Mailbox ----------|
     |--- 7. BSP Acknowledges AP Online ------------>|
```

### Trampoline Implementation
- **Location**: Physical page `0x00008000` (within conventional low memory below 1 MiB).
- **Embedded Blob**: Built via NASM (`trampoline.asm`) into a flat binary payload linked into the kernel image (`trampoline_blob_start` to `trampoline_blob_end`).
- **Mailbox Protocol**:
  - `0x7FF0`: Target 64-bit Kernel Stack Top (`rsp`).
  - `0x7FF8`: 64-bit Kernel C Entry Point (`ap_kernel_entry`).
  - `0x7FE8`: Core CPU Identifier (`uint32_t`).
  - `0x7FEC`: Online status handshake flag (`uint32_t`).

---

## 4. Local APIC & Hardware Timer Calibration

Each CPU core interacts with its dedicated Local APIC mapped at physical base address `0xFEE00000` (configured via `IA32_APIC_BASE_MSR` `0x1B`):

1. **Virtual Wire Mode Initialization**:
   - The BSP configures `LVT_LINT0` as `ExtINT` (`0x00000700`) and `LVT_LINT1` as `NMI` (`0x00000400`) to route legacy 8259 PIC interrupts exclusively to CPU 0.
   - All APs mask both `LVT_LINT0` and `LVT_LINT1` (`0x00010000`), preventing interrupt arbitration conflicts on the system bus.
2. **Spurious Vector**: Set to vector `0xFF` with APIC Software Enable bit (`1 << 8`) set in `LAPIC_REG_SVR`.
3. **Hardware Timer Calibration**:
   - Rather than relying on hardcoded constants that drift between physical CPUs and emulation environments, Nyota OS calibrates the Local APIC timer dynamically against the hardware PIT reference timer over a 20 ms window.
   - The Local APIC timer is configured with divider 16 in periodic mode at `LAPIC_TIMER_VECTOR` (`0x40`) at exactly 100 Hz per core.

---

## 5. Inter-Processor Interrupts (IPIs)

Nyota OS implements low-latency hardware signaling across cores via the Local APIC Interrupt Command Register (ICR):

| Vector | Symbolic Name | Function |
|---|---|---|
| `0x50` | `IPI_VECTOR_RESCHEDULE` | Triggers immediate preemption and run queue re-evaluation on target core. |
| `0x51` | `IPI_VECTOR_TLB_SHOOTDOWN` | Synchronous inter-core TLB page invalidation (`invlpg`) across remote cores. |
| `0x52` | `IPI_VECTOR_STOP` | Emergency core halt for system shutdown or unrecoverable kernel panics. |

### TLB Shootdown Algorithm
When a page mapping is modified or unmapped on one core:
1. Core acquires `tlb_shootdown_lock`.
2. Sets `tlb_shootdown_addr` to target virtual address and zeroes `tlb_shootdown_ack`.
3. Invalidates local TLB via `local_invlpg(addr)`.
4. Broadcasts `IPI_VECTOR_TLB_SHOOTDOWN` via LAPIC ICR to all other online CPUs.
5. Remote cores execute `local_invlpg(addr)`, atomically incrementing `tlb_shootdown_ack`.
6. Initiating core spins with `pause` until all online remote cores have acknowledged before releasing the lock.

---

## 6. Per-CPU State Architecture (`cpu_t`)

To ensure thread safety and eliminate contention on core critical paths, Nyota OS structures per-CPU data in independent records:

```c
typedef struct cpu {
    uint32_t id;                      /* Nyota logical CPU ID (0..MAX_CPUS-1) */
    uint32_t apic_id;                 /* Physical hardware APIC ID */
    bool online;                      /* True if CPU initialized & running */
    bool is_bsp;                      /* True if Bootstrap Processor */

    uint64_t kernel_stack;            /* Base of dedicated kernel stack */
    uint64_t kernel_stack_top;        /* Top of dedicated kernel stack */

    gdt_entry_t gdt[GDT_ENTRY_COUNT]; /* Per-CPU GDT table */
    gdt_ptr_t gdt_ptr;                /* Per-CPU GDTR descriptor */
    tss_t tss;                        /* Per-CPU Task State Segment */

    process_t *current_proc;          /* Currently executing process on this core */
    process_t idle_proc;              /* Dedicated per-core idle process */
    uint8_t idle_stack[PAGE_SIZE * 2];/* Dedicated per-core idle stack */

    process_t *ready_head;            /* Head of local ready queue */
    process_t *ready_tail;            /* Tail of local ready queue */
    spinlock_t lock;                  /* Per-CPU ready queue spinlock */

    uint32_t current_time_slice;      /* Remaining ticks in quantum */
    bool reschedule_requested;        /* Core preemption flag */

    uint64_t ticks;                   /* Local APIC timer ticks received */
    uint64_t context_switches;        /* Total switches executed by core */
    uint64_t ipi_count;               /* Total IPIs handled by core */
    uint64_t syscall_count;           /* Total syscalls handled by core */
} cpu_t;
```

### Hardware Isolation Guarantees
- **Independent TSS & RSP0**: Every CPU has its own Task State Segment (TSS) descriptor in its local GDT. On Ring 3 $\rightarrow$ Ring 0 transitions, the CPU hardware automatically loads `tss.rsp0`, guaranteeing user privilege switches on different cores never collide on kernel stacks.
- **Dedicated Idle Tasks**: Each core runs its own `idle_proc` with a private stack when no user processes are ready, issuing `sti; hlt` to conserve power and reduce bus bandwidth.

---

## 7. Multiprocessor Scheduling & Work Stealing

Nyota OS implements a hybrid distributed scheduling model:

1. **Local Run Queues**: Each CPU maintains an independent, lock-protected doubly linked ready queue. Processes execute on their home core with cache affinity.
2. **Initial Process Placement**: When a process is spawned or unblocked (`scheduler_add`), the scheduler evaluates all online CPUs allowed by `proc->cpu_affinity` and assigns the process to the core with the lowest instantaneous run queue load.
3. **Deadlock-Free Work Stealing**: When a core drains its local ready queue:
   - The core releases its local queue lock (`cpu->lock`) prior to inspecting other cores.
   - It iterates through remote victim cores, acquiring only one victim lock at a time.
   - If a victim core possesses $\ge 2$ ready processes, the stealing core unlinks a candidate process matching affinity, assigns `proc->current_cpu = cpu->id`, releases the victim lock, and context switches to the stolen process immediately.
   - Releasing the local lock prior to remote inspection mathematically guarantees that cyclic lock inversions between cores are impossible.
4. **Preemption Preservation**: Preempted processes remain on their local core's run queue rather than being redistributed, preserving CPU register cache state and avoiding inter-core stack invalidation.

---

## 8. System Calls & User Diagnostic Suite

Phase 11 introduces three core multicore system calls:

| Syscall Number | Function Name | Description |
|---|---|---|
| 64 | `SYS_SMP_INFO` | Retrieves global multicore topology and per-CPU runtime telemetry. |
| 65 | `SYS_SCHED_SETAFFINITY` | Sets allowed CPU mask bitfield (`uint64_t`) for target PID. |
| 66 | `SYS_SCHED_GETAFFINITY` | Retrieves current CPU affinity mask bitfield for target PID. |

### Userspace Utilities
- `/bin/smp`: Displays real-time multicore topology, APIC mappings, timer ticks, context switch counts, syscall counts, and active process allocations across all cores.
- `/bin/smpstress`: Spawns multiple worker processes pinned across all detected CPU cores, executing 1,000,000 shared memory operations, verifying atomic operations, mutual exclusion, and zero data corruption across cores.
- `/bin/sysinfo`: System telemetry utility updated to report discovered and active SMP cores.

---

## 9. Verification & Test Matrix

Nyota OS v1.1.0 passes comprehensive testing across all supported configurations:

```
============================================================
           SMP & MULTICORE TEST SUMMARY (tools/test_smp.py)
============================================================
  -smp 1 Cores: [ PASS ] (smp, smpstress, sysinfo)
  -smp 2 Cores: [ PASS ] (smp, smpstress, sysinfo)
  -smp 4 Cores: [ PASS ] (smp, smpstress, sysinfo)
  -smp 8 Cores: [ PASS ] (smp, smpstress, sysinfo)
============================================================
ALL SMP TESTS PASSED SUCCESSFULLY!
```

### Full Regression Test Results
- `tools/test_security.py`: **25/25 PASSED (100%)** — User/kernel isolation, capabilities, guard pages, ASLR foundation, pipes, shared memory, and permission barriers.
- `tools/test_phase10.py`: **24/24 PASSED (100%)** — Framebuffer initialization, window server boundary, compositor, mouse input, multi-app desktop lifecycle, crash recovery.
- `tools/test_network.py`: **100% PASSED** — Intel E1000 Ethernet driver, ARP, loopback, gateway ping, DNS client (`nslookup`), and TCP socket connections (`netstat`).
