<p align="center">
  <img src="assets/NyotaLogo.svg" alt="Nyota OS Logo" width="180">
</p>

<h1 align="center">Nyota OS</h1>

<p align="center">
  <strong>A hobby operating system built from scratch for the x86_64 architecture.</strong>
</p>

<p align="center">
  Exploring boot processes, low-level architecture, 64-bit long mode, kernel development,
  and operating system fundamentals.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Phase-5%3A%20Multitasking%2C%20Scheduler%20%26%20Process%20Management-success?style=for-the-badge">
  <img src="https://img.shields.io/badge/Version-v0.5.0-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Architecture-x86__64-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20x86__64%20ASM-00599C?style=for-the-badge&logo=c&logoColor=white">
  <img src="https://img.shields.io/badge/Toolchain-NASM%20%7C%20GCC%20%7C%20Binutils-111111?style=for-the-badge">
  <img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=for-the-badge">
</p>

<p align="center">
  <a href="#-phase-5-overview-multitasking-scheduler--process-management">Phase 5 Overview</a>
  ·
  <a href="#-phase-4-overview-processes-user-mode--system-calls">Phase 4 Overview</a>
  ·
  <a href="#-phase-3-overview">Phase 3 Overview</a>
  ·
  <a href="#-memory-architecture">Memory Architecture</a>
  ·
  <a href="#-phase-2-overview">Phase 2 Overview</a>
  ·
  <a href="#-interrupt-architecture">Interrupt Architecture</a>
  ·
  <a href="#-architecture--boot-flow">Boot Flow</a>
  ·
  <a href="#-project-structure">Project Structure</a>
  ·
  <a href="#-getting-started">Getting Started</a>
  ·
  <a href="#-how-to-use--interact-with-nyota-os">How to Use</a>
  ·
  <a href="#-debugging-with-gdb">Debugging</a>
  ·
  <a href="#-roadmap">Roadmap</a>
</p>

---

## 🌟 Development Principle

> **"Build the foundation before building the illusion."**
>
> Every subsystem added to Nyota should have a clear interface, a testable implementation, and a reason to exist.
> The goal isn't to make Nyota look like an operating system.
> **The goal is to make Nyota actually behave like one.**

---

# 🔄 Phase 5 Overview: Multitasking, Scheduler & Process Management

**Current Status:** **Phase 5 — Multitasking, Scheduler & Process Management** (Completed)

Phase 5 transforms Nyota OS from a single-tasking operating system into a **fully preemptive, multi-process operating system** capable of managing multiple independent user processes, safely switching between isolated virtual address spaces, and preempting CPU-bound code using hardware timer interrupts:

```text
                    NYOTA SCHEDULER ARCHITECTURE
                                 │
                             Scheduler
                                 │
                   ┌─────────────┼─────────────┐
                   ▼             ▼             ▼
                 PID 1         PID 2         PID 3
                (prog_a)      (prog_b)      (prog_c)
                   │             │             │
                   ▼             ▼             ▼
                 CR3 A         CR3 B         CR3 C
             (Isolated VM) (Isolated VM) (Isolated VM)
                   │             │             │
                   ▼             ▼             ▼
                 User A        User B        User C
              (Yield Loop)  (Pure Compute)  (100ms Sleep)
```

---

### Key Subsystems Delivered in Phase 5:

1. **Preemptive Round-Robin CPU Scheduler**:
   - Driven by the 8254 Programmable Interval Timer (PIT) configured at **100 Hz** (10 ms resolution).
   - Time-slice quantum set to **5 ticks (50 ms)** per process.
   - Transparent preemption: compute-heavy user programs that never call `yield()` are automatically preempted and rescheduled, preventing starvation.

2. **Hardware-Enforced Context Switching via Kernel Stack Frames**:
   - Because x86_64 Long Mode interrupts push `SS`, `RSP`, `RFLAGS`, `CS`, and `RIP` automatically, and `isr_common_stub` preserves all 15 general-purpose registers, the process execution state is fully encapsulated by an `interrupt_frame_t` on the process's dedicated kernel stack.
   - Low-level stack switching: `interrupt_dispatch()` returns the active frame pointer in `RAX`. In `kernel/arch/x86_64/interrupts.asm`, `mov rsp, rax` atomically pivots the stack pointer to the scheduled process frame.
   - Data segment selectors are checked against `CS.RPL`: if returning to Ring 3 (`CS & 3 == 3`), data segments (`DS`, `ES`, `FS`, `GS`) are loaded with `0x1B` (DPL=3 user data); otherwise loaded with `0x10` (kernel data).
   - `TSS.RSP0` is updated to `next->kernel_stack_top` so subsequent interrupts from Ring 3 land on the correct kernel stack.
   - Address space transition: `CR3` is reloaded with `next->cr3`, switching the active 4-level PML4 paging table and flushing the TLB.

3. **Process State Machine & Double-Queue Architecture**:
   - **Process States**: `PROCESS_NEW`, `PROCESS_READY`, `PROCESS_RUNNING`, `PROCESS_SLEEPING`, `PROCESS_TERMINATED`, `PROCESS_IDLE`.
   - **Ready Queue**: Circular doubly-linked list (`ready_head`, `ready_tail`) providing $O(1)$ dispatch and enqueue. Terminated or sleeping processes are removed, preventing duplicate insertion.
   - **Sleep Queue**: Linked list of sleeping processes evaluated every 10 ms timer tick. When `timer_ticks() >= p->wakeup_tick`, the process transitions to `PROCESS_READY` and re-enters the ready queue.
   - **Kernel Idle Process (PID 0)**: Created with a dedicated 4 KiB kernel stack and runs `sti; hlt` in a power-saving halt loop when no user processes are runnable.

```text
                    PROCESS LIFECYCLE
                      ┌─────────────┐
                      │     NEW     │
                      └──────┬──────┘
                             │
                             ▼
                      ┌─────────────┐
                      │    READY    │◄────────┐
                      └──────┬──────┘         │
                             │ schedule       │ wake
                             ▼                │
                      ┌─────────────┐         │
                      │   RUNNING   │         │
                      └───┬─────┬───┘         │
                          │     │             │
                   yield  │     │ sleep       │
               preemption │     ▼             │
                          │  SLEEPING ────────┘
                          │
                    exit  ▼
                      TERMINATED
```

4. **Expanded System Call Architecture**:
   - `SYS_YIELD` (Vector 3): Allows user processes to voluntarily release their remaining time slice to other runnable tasks.
   - `SYS_SLEEP` (Vector 4): Places the calling process into `PROCESS_SLEEPING` state for a requested duration in milliseconds, removing it from the ready queue until the target timer tick.
   - Syscall wrapper library providing clean C functions: `sys_write()`, `sys_exit()`, `sys_getpid()`, `sys_yield()`, `sys_sleep()`.

5. **Multi-Program Validation Suite**:
   - **Program A (`prog_a`, PID 1)**: Cooperative counter demonstrating multiple `sys_yield()` handoffs.
   - **Program B (`prog_b`, PID 2)**: Heavy compute loop with zero voluntary yields, demonstrating hardware timer preemption.
   - **Program C (`prog_c`, PID 3)**: Sleep/wake demonstration calling `sys_sleep(100)` and resuming execution after 100 ms.
   - **Rogue Process (`rogue_proc`, PID 4)**: Hostile process attempting to write to kernel memory at `0x100000`. Hardware #PF protection terminates only the offending process (`SIGSEGV -11`), leaving the kernel and all other processes completely unharmed.

6. **Interactive Process Inspection (`ps` command)**:
   - Console command `ps` displays live process states, total runtime ticks, context switch metrics, and active PID.

---

# 🛡️ Phase 4 Overview: Processes, User Mode & System Calls

**Current Status:** **Phase 4 — Processes, User Mode & System Calls** (Completed)

Phase 4 introduces true hardware privilege separation to Nyota OS, transitioning the architecture from a monolithic Ring 0 environment to a secure operating system capable of executing untrusted code in **Ring 3 User Mode** with managed entry back into **Ring 0 Kernel Mode** via **System Calls**:

```text
                    NYOTA OS ARCHITECTURE
                              │
          ┌───────────────────┴───────────────────┐
          │                                       │
     Kernel Space                            User Space
    (Ring 0 - CPL=0)                      (Ring 3 - CPL=3)
          │                                       │
          │                                ┌──────┴──────┐
          │                                │ User Program│
          │                                │   (PID 1)   │
          │                                └──────┬──────┘
          │                                       │
          │                               int 0x80 / syscall
          │                                       │
          ▼                                       ▼
       Kernel ◄───────────────────────────────────┘
   (Switch to RSP0)
          │
          ▼
   Syscall Dispatcher
          │
   ┌──────┼──────┐
   ▼      ▼      ▼
 WRITE   EXIT  GETPID
```

### Key Subsystems Delivered in Phase 4:

1. **Hardware Ring 0 / Ring 3 Privilege Separation**:
   - Extended Global Descriptor Table (GDT) with 64-bit User Data (`0x18 | 3 = 0x1B`, DPL=3) and User Code (`0x20 | 3 = 0x23`, DPL=3) segment descriptors.
   - User program executes with `CS.RPL = 3` and `SS.RPL = 3`.
   - Kernel transition helper (`user_enter_ring3`) loads user data segments (`DS`, `ES`, `FS`, `GS`), constructs an architectural 5-quadword `iretq` stack frame (`SS`, `RSP`, `RFLAGS` with IF=1, `CS`, `RIP`), and performs hardware Ring 3 transition.

2. **64-bit Task State Segment (TSS)**:
   - Full 104-byte x86_64 Task State Segment structure (`tss_t`) with `iomap_base` set to 104 (disabling I/O port bitmap).
   - Installed in GDT as a 16-byte system descriptor (`0x28`) and activated via CPU `ltr 0x28`.
   - Dynamic `RSP0` pointer switching via `tss_set_rsp0()` ensures every user process switches to its dedicated 16 KiB kernel stack upon interrupt or syscall entry.

3. **Isolated User Virtual Address Spaces (VMM)**:
   - User space is quarantined in PML4 index 1 (512 GB mark: `0x0000008000000000ULL` to `0x0000010000000000ULL`).
   - `paging_create_address_space()` clones the kernel identity map (0..128 MB) and kernel heap (`0xFFFFFFFF90000000ULL`) as supervisor-only (`USER = 0`), preventing any Ring 3 read, write, or execution of kernel code or structures.
   - User code page mapped at `USER_CODE_BASE` (`0x0000008000000000ULL`) with `PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE`.
   - Dedicated 16 KiB user stack mapped below `USER_STACK_TOP` (`0x0000008000010000ULL`) with `PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE`.

4. **System Call Interface & Dispatcher (Vector 0x80)**:
   - Vector `0x80` configured as a User Trap Gate (`IDT_GATE_USER_TRAP` = `0xEE`, DPL=3), callable from Ring 3 without triggering a `#GP`.
   - **System V AMD64 Syscall ABI**:
     * `RAX`: Syscall Number / Return Value
     * `RDI`: Argument 1 (e.g., buffer pointer, status)
     * `RSI`: Argument 2 (e.g., length)
     * `RDX`: Argument 3
     * `R10`: Argument 4
     * `R8` : Argument 5
   - Core System Calls:
     * `SYS_WRITE` (0): Outputs validated user buffer to VGA text console and serial COM1.
     * `SYS_EXIT` (1): Terminates user process with status code and restores kernel CR3.
     * `SYS_GETPID` (2): Returns active process PID.

5. **Defensive Pointer Validation & Safe Memory Copying**:
   - All user-supplied pointers are treated as hostile.
   - `user_validate_pointer()` verifies:
     * Pointer is non-NULL.
     * Virtual range `[ptr, ptr + len)` is strictly contained within `USER_SPACE_BASE` and `USER_SPACE_END`.
     * No integer overflow / arithmetic wrap-around.
     * All targeted pages are present in the active process page tables.
   - `copy_from_user()` and `copy_to_user()` prevent malicious pointers from triggering arbitrary kernel memory reads or writes.
   - Unknown syscall numbers return `-ENOSYS` safely without jumping through unchecked pointers.

6. **Process Abstraction & PID System**:
   - Process Control Block (`process_t`): tracks PID, CR3 page table physical base, entry point, user stack top, dedicated kernel stack top, execution state (`READY`, `RUNNING`, `TERMINATED`), and exit status.
   - Sequential PID allocation starting at PID 1.
   - Position-independent initial user program blob executes in Ring 3, performs `SYS_WRITE` ("Hello from user space!"), retrieves PID via `SYS_GETPID` (1), formats and writes the PID, and cleanly exits via `SYS_EXIT(0)`.
   - Ring 3 page fault recovery: if user code touches kernel memory, the page fault handler cleanly kills the offending process with `SIGSEGV` (-11) and drops back to the interactive console without halting or crashing the kernel.

7. **Automated User Security & Syscall Test Suite**:
   - Built-in `testuser` command runs 4 automated tests verifying TSS/TR registers, unknown syscall rejection, hostile pointer rejection (NULL, kernel code `0x100000`, kernel heap), and `copy_from_user` boundary enforcement.
   - `viol_write` command spawns a rogue Ring 3 process attempting to write to `0x100000`, demonstrating genuine CPU page-protection enforcement (#PF).

---

# 🧠 Phase 3 Overview

**Current Status:** **Phase 3 — Memory Management** (Completed)

Phase 3 transitions Nyota OS from using raw unmanaged physical memory to an enterprise-grade, multi-tier memory architecture with hardware-backed paging, page-frame allocation, dynamic heap management, and comprehensive page-fault diagnostics:

- **BIOS E820 Memory Map Parser**: Queries BIOS `INT 15h, AX=E820h` during Stage 2 bootloader to discover real hardware memory regions (`USABLE`, `RESERVED`, `ACPI`, `ACPI_RECLAIMABLE`, `BAD`).
- **Physical Memory Manager (PMM)**: Implements a 4 KiB frame bitmap allocator tracking physical pages, reserving firmware, bootloader, kernel code/data/BSS, page tables, and bitmap structures.
- **4-Level x86_64 Paging Architecture (VMM)**: Manages PML4, PDPT, Page Directory, and Page Table structures with fine-grained entry permissions (`PRESENT`, `WRITABLE`, `USER`, `NX`).
- **Dynamic Virtual Page Mapping**: Provides `paging_map_page()`, `paging_unmap_page()`, and `paging_get_physical()` with on-demand allocation of intermediate page table levels.
- **CR3 & TLB Management**: Direct hardware control of CR3 register and precise per-page TLB invalidation via `invlpg`.
- **Page Fault Diagnostics (#PF, Vector 14)**: Reads `CR2`, decodes architectural error code flags (Present/Protection, Read/Write, User/Kernel, Instruction Fetch, Reserved Bit, Protection Key), and renders a structured diagnostic box.
- **Kernel Dynamic Heap**: Dynamic memory allocator providing `kmalloc()`, `kfree()`, `kcalloc()`, and `krealloc()`. Operates on high virtual memory (`0xFFFFFFFF90000000`), guarantees 16-byte alignment, implements free-block coalescing, and dynamically expands by mapping additional physical frames through the VMM.
- **Lightweight Corruption Detection**: Employs magic values (`0x4E594F5441` "NYOTA" and `0xDEADBEEF`) for heap block header validation.
- **Interactive Memory Diagnostics & Stress Suite**: Console commands `mem` (RAM and heap statistics), `mmap` (E820 memory map table), `memtest` (automated PMM, VMM, heap, and 100-block stress test), and `crashpf` (controlled page-fault verification).

---

# 🗺️ Memory Architecture

```text
                    NYOTA MEMORY SYSTEM
                           │
             ┌─────────────┴─────────────┐
             │                           │
      Physical Memory              Virtual Memory
             │                           │
             ▼                           ▼
      Memory Map Parser             Page Tables
        (BIOS E820)             (PML4/PDPT/PD/PT)
             │                           │
             ▼                           ▼
      Page Frame Allocator       Virtual Mapping
       (4 KiB Bitmap)          (paging_map_page)
             │                           │
             └─────────────┬─────────────┘
                           ▼
                     Kernel Heap
               (0xFFFFFFFF90000000)
                           │
                 ┌─────────┴─────────┐
                 ▼                   ▼
              kmalloc()           kfree()
```

### 4-Level x86_64 Page Translation Hierarchy

```text
                 Virtual Address
                       │
                       ▼
                    PML4  (Page Map Level 4)
                       │
                       ▼
                    PDPT  (Page Directory Pointer Table)
                       │
                       ▼
                     PD   (Page Directory)
                       │
                       ▼
                     PT   (Page Table)
                       │
                       ▼
                Physical Page (4 KiB Frame)
                       │
                       ▼
                 Physical RAM
```

### Virtual & Physical Memory Layout

| Virtual Address Range | Size | Description | Attributes |
| :--- | :--- | :--- | :--- |
| `0x0000000000000000` - `0x000000000009FFFF` | 640 KB | Conventional RAM (IVT, BDA, Bootloader, Page Tables, Stack) | Present, Writable |
| `0x00000000000A0000` - `0x00000000000FFFFF` | 384 KB | Video Memory (VGA `0xB8000`) & Motherboard BIOS ROM | Present, Writable |
| `0x0000000000100000` - `_kernel_end` | ~40 KB | Kernel Code (`.text`), Read-Only Data (`.rodata`), Data, BSS | Present, Writable |
| `_kernel_end` - `+4KB` | 4 KB | PMM Page Frame Allocation Bitmap | Present, Writable |
| `0x0000000000100000` - `0x0000000008000000` | 128 MB | Identity-Mapped Physical RAM (Backed by 64x 2MB Pages in PD) | Present, Writable |
| `0xFFFFFFFF90000000` - `0xFFFFFFFF90100000` | 1 MB+ | Kernel Dynamic Heap (Expands dynamically on demand) | Present, Writable |

---

# 🚀 Phase 2 Overview

**Phase 2 — Interrupts, Exceptions, Timer & Keyboard** (Completed)

Phase 2 transforms Nyota OS from a static kernel that boots and halts into a reactive, event-driven operating system responding dynamically to hardware events, CPU exceptions, clock ticks, and keyboard strokes:

- **Interrupt Descriptor Table (IDT)**: Complete 256-entry 64-bit IDT loaded via `lidt`.
- **CPU Exception Handlers**: Robust handlers for all 32 x86_64 exception vectors (Divide Error, Breakpoint, Invalid Opcode, Double Fault, GPF, Page Fault, etc.).
- **Diagnostic Kernel Panic**: Rich crash report displaying exception name, vector, error code, faulting address (`CR2`), `RIP`, `RFLAGS`, stack pointer, and full register dump across both VGA text mode and COM1 serial console.
- **Assembly Interrupt Layer**: Uniform ISR stubs normalizing stack frames for exceptions with and without CPU error codes, preserving all 15 general-purpose registers, maintaining 16-byte System V AMD64 ABI alignment, and returning cleanly via `iretq`.
- **Centralized Interrupt Dispatcher**: Routes traps and IRQs to registered C callbacks with automatic End Of Interrupt (EOI) signaling.
- **8259 PIC Driver**: Remaps hardware IRQs 0–15 to vectors 32–47 (`0x20`–`0x2F`), manages master/slave cascade wiring, and dynamic IRQ masking.
- **Programmable Interval Timer (PIT 8254)**: Configured at 100 Hz (10 ms per tick), driving monotonic uptime counters and interrupt-driven `timer_sleep()`.
- **PS/2 Keyboard Driver**: Scancode Set 1 decoder handling make/break codes, modifier tracking (Shift, Ctrl, Alt, Caps Lock), navigation keys, and an asynchronous lock-free circular event buffer.
- **Interactive Kernel Console**: Command-line shell prompt (`nyota> `) supporting character input, backspace, enter, and built-in commands (`uptime`, `cpu`, `clear`, `help`).
- **Dual-Input Capability**: Simultaneously receives input from the graphical PS/2 keyboard and COM1 serial port (`-serial stdio`).

---

# ⚡ Interrupt Architecture

```text
                        CPU Hardware
                             │
              ┌──────────────┴──────────────┐
              │                             │
        CPU Exceptions                Hardware IRQs
        (Vectors 0..31)              (IRQs 0..15)
              │                             │
              ▼                             ▼
       Exception Handler               8259 PIC
      (Panic & Diagnostics)          (Remap 32..47)
                                            │
                                            ▼
                                   Central IRQ Dispatcher
                                     │               │
                                     ▼               ▼
                               PIT Timer ISR   PS/2 Keyboard ISR
                               (Vector 32)       (Vector 33)
                                     │               │
                                     ▼               ▼
                                Monotonic      Scancode Decoder
                               Tick Counter          │
                                     │               ▼
                                     │         Key Event Buffer
                                     │               │
                                     └───────┬───────┘
                                             ▼
                                   Interactive Console
                                        (nyota> )
```

---

# 🏗️ Architecture & Boot Flow

```text
BIOS (Real Mode, 16-bit)
 │
 ▼
Stage 1 Boot Sector (boot/boot.asm, loaded at 0x7C00)
 │  - Reads Stage 2 (4 sectors) to 0x8000
 │  - Verifies disk read and transfers control
 ▼
Stage 2 Bootloader (boot/stage2.asm, loaded at 0x8000)
 │  - Loads Kernel binary from disk into buffer (0x10000)
 │  - Enables A20 line (Fast A20 + BIOS fallback)
 │  - Verifies CPUID and 64-bit Long Mode capability
 │  - Prepares 4-level PML4 Page Tables (identity maps 0 - 16 MB)
 │  - Loads 64-bit GDT (Code & Data selectors)
 │  - Enables PAE (CR4.PAE = 1)
 │  - Enables Long Mode (EFER.LME = 1)
 │  - Enables Paging & Protection (CR0.PG = 1, CR0.PE = 1)
 │  - Far jumps into 64-bit Long Mode
 ▼
64-bit Long Mode Transition
 │  - Relocates kernel from buffer to 0x100000 (1 MB mark)
 │  - Jumps to 0x100000
 ▼
Kernel Entry (kernel/kernel_entry.asm)
 │  - Establishes 64-bit stack (RSP = 0x90000)
 │  - Clears CPU state and direction flag (DF = 0)
 │  - Calls kernel_main()
 ▼
Kernel Main (kernel/kernel.c)
 │  - Brings up Serial console (COM1, 115200 baud)
 │  - Brings up VGA text-mode driver (0xB8000)
 │  - Installs kernel GDT
 │  - Interrogates CPUID hardware features
 │  - Emits boot banner and subsystem verification status
 │  - Enters safe idle loop (hlt)
```

---

# 📁 Project Structure

```text
nyota-os/
│
├── boot/
│   ├── boot.asm             # Stage 1 MBR boot sector (512 bytes, 0xAA55)
│   └── stage2.asm           # Stage 2: A20, CPUID, Paging, GDT, E820 mmap, Long Mode
│
├── kernel/
│   ├── kernel.c             # C kernel entry (kernel_main) and subsystem bring-up
│   ├── kernel_entry.asm     # 64-bit entry point, stack setup, calls kernel_main
│   ├── console.c            # Interactive kernel console and line editing shell
│   │
│   ├── arch/
│   │   └── x86_64/
│   │       ├── idt.c        # 256-entry 64-bit IDT initialization & trap/user gates
│   │       ├── interrupts.asm # 256 ISR stubs, uniform stack frames, iretq
│   │       ├── dispatcher.c # Centralized interrupt dispatcher & handler table
│   │       ├── exceptions.c # CPU exception handlers (0..31) & page fault diagnostics
│   │       ├── pic.c        # 8259 PIC initialization, IRQ remapping, EOI
│   │       ├── paging.c     # 4-level paging (PML4, PDPT, PD, PT), map/unmap, VMM
│   │       └── syscall.c    # Vector 0x80 syscall dispatcher & pointer validation
│   │
│   ├── cpu/
│   │   ├── cpu.c            # CPUID hardware feature detection & vendor query
│   │   ├── gdt.c            # 64-bit GDT with Kernel & Ring 3 User segment descriptors
│   │   ├── gdt_flush.asm    # 64-bit GDTR reload and CS/DS refresh
│   │   ├── tss.c            # 64-bit Task State Segment (TSS) initialization & RSP0
│   │   └── user_jump.asm    # iretq-based Ring 3 user privilege transition
│   │
│   ├── memory/
│   │   ├── memory.c         # Freestanding memset, memcpy, memmove, memcmp, strlen
│   │   ├── pmm.c            # Physical Memory Manager (4 KiB page frame bitmap)
│   │   ├── heap.c           # Kernel dynamic heap (kmalloc, kfree, kcalloc, krealloc)
│   │   └── memtest.c        # Automated PMM, VMM, and heap stress validation suite
│   │
│   └── process/
│       ├── process.c        # Process control blocks (PCB), PID allocator, execution
│       └── usertest.c       # Ring 3 security tests & privilege violation verification
│
├── include/
│   ├── types.h              # Freestanding fixed-width types (uint64_t, bool, etc.)
│   ├── kernel.h             # Logging macros, version info, kernel_panic
│   ├── cpu.h                # CPU capabilities and CPUID interface
│   ├── gdt.h                # GDT selectors (Kernel/User Code/Data, TSS)
│   ├── tss.h                # 64-bit TSS descriptor structure and RSP0 APIs
│   ├── idt.h                # IDT descriptors, attributes, and gate APIs
│   ├── interrupts.h         # Interrupt frame structure, IRQ mappings, dispatcher
│   ├── exceptions.h         # Exception vectors, panic_with_frame prototypes
│   ├── pic.h                # 8259 PIC port definitions and commands
│   ├── timer.h              # PIT 8254 timer, uptime, and sleep interface
│   ├── keyboard.h           # Key event structure, scancodes, ring buffer
│   ├── console.h            # Interactive kernel console interface
│   ├── vga.h                # VGA colors, cursor positioning, and print APIs
│   ├── serial.h             # COM1 serial driver interface (tx/rx)
│   ├── io.h                 # Port I/O (inb, outb, inw, outw, inl, outl)
│   ├── memory.h             # Memory and string function declarations
│   ├── pmm.h                # Physical memory frame allocator definitions
│   ├── paging.h             # 4-level paging and address space management
│   ├── heap.h               # Dynamic heap allocator API
│   ├── memtest.h            # Memory diagnostic and stress testing
│   ├── syscall.h            # Syscall numbers, ABI constants, pointer validation
│   ├── process.h            # Process structure, states, lifecycle APIs
│   └── usertest.h           # User mode security validation test suite
│
├── drivers/
│   ├── vga.c                # 80x25 text-mode driver at 0xB8000 with scrolling
│   ├── serial.c             # 16550 UART serial driver (115200 8N1 tx/rx)
│   ├── timer.c              # 8254 PIT driver (100 Hz, uptime tracking, sleep)
│   └── keyboard.c           # PS/2 keyboard driver, scancode decoder, ring buffer
│
├── tools/
│   └── mkimage.c            # Cross-platform disk image builder (creates nyota.img)
│
├── linker.ld                # 64-bit kernel linker script (load address 0x100000)
├── Makefile                 # Reproducible build system
└── README.md                # Project documentation
```

---

# 🛠️ Toolchain

The following tools are required to build and run Nyota OS:

| Tool | Recommended Version | Purpose |
| :--- | :--- | :--- |
| **NASM** | 2.15+ / 3.02 | 16-bit and 64-bit Assembler |
| **GCC** | 9.0+ / 16.x | Freestanding C compiler (`-m64 -mabi=sysv`) |
| **GNU Binutils** | 2.34+ | Linker (`ld`) and binary extractor (`objcopy`) |
| **GNU Make** | 4.0+ | Build automation |
| **QEMU** | 7.0+ / 11.x | System emulator (`qemu-system-x86_64`) |
| **GDB** | 10.0+ | Remote kernel debugger |
| **Git** | 2.30+ | Version control |

Verify your toolchain:

```bash
nasm -v
gcc --version
ld --version
objcopy --version
make --version
qemu-system-x86_64 --version
gdb --version
git --version
```

---

# 🚀 Getting Started

Nyota OS builds natively on **Windows (MinGW-w64 / MSYS2)** as well as **Linux / WSL2**.

### 1. Build the OS Image

```bash
make
```

Sample output:

```text
[BUILD] boot/boot.asm
[BUILD] boot/stage2.asm
[BUILD] kernel/kernel_entry.asm
[BUILD] kernel/cpu/gdt_flush.asm
[BUILD] kernel/kernel.c
[BUILD] kernel/memory/memory.c
[BUILD] kernel/cpu/cpu.c
[BUILD] kernel/cpu/gdt.c
[BUILD] drivers/vga.c
[BUILD] drivers/serial.c
[LINK]  build/kernel.elf
[STRIP] build/kernel.bin
[HOST]  tools/mkimage.c
[IMAGE] build/nyota.img

  [IMAGE] build/nyota.img created successfully (1440 KB / 2880 sectors)
    Sector 0      (0x000000): boot.bin   (512 bytes)
    Sectors 1..4  (0x000200): stage2.bin (2048 bytes)
    Sectors 5..15 (0x000A00): kernel.bin (5632 bytes, 11 sectors)

==========================================================
  Build successful: build/nyota.img
  Launch in QEMU with: make run
==========================================================
```

### 2. Run in QEMU

```bash
make run
```

This launches QEMU with graphical VGA window and interactive terminal serial output.

### 3. Headless Run (Serial Output Only)

For quick tests or CI environments without a graphical window:

```bash
make run-serial
```

### Expected Boot Output

```text
========================================
              NYOTA OS                  
========================================

Kernel       : v0.2.0
Architecture : x86_64

[ OK ] GDT
[ OK ] IDT
[ OK ] Exceptions
[ OK ] PIC
[ OK ] Timer
[ OK ] Keyboard
[ OK ] Interrupts

Uptime: 00:00:00

nyota> hello nyota
hello nyota

nyota> uptime
Uptime: 00:00:15

nyota> cpu
CPU Information
-------------------------
Vendor   : AuthenticAMD
Mode     : x86_64
Features :
  SSE
  SSE2
  SSE3
  APIC
  PAE
  Long Mode (x86_64)
  NX (No-Execute)

nyota> 
```

---

# 🎮 How to Use & Interact with Nyota OS

### 1. Launching Nyota OS

To build and start Nyota OS in the QEMU emulator:

```bash
make run
```

When you execute `make run`, Nyota OS boots using a **dual-output architecture**:

1. **VGA Graphics Window**: A QEMU window opens displaying the 80x25 text-mode console (`0xB8000`) with colored status badges, hardware CPUID feature logs, and the system checklist.
2. **Serial Terminal Stream**: The 16550 UART COM1 serial port (`0x3F8`) mirrors all kernel diagnostics directly to your active host terminal in real time.

---

### 2. Interactive Console Commands (v0.5.0)

Nyota OS features a functional interactive kernel console (`nyota> ` prompt). Both your graphical keyboard (in the QEMU window) and host terminal stdin (via serial) are active:

| Command | Subsystem | Action |
| :--- | :--- | :--- |
| `ps` | Phase 5 Process | Display active and terminated process table, states, runtime ticks, and context switches |
| `multitask` | Phase 5 Process | Spawn three concurrent independent user processes (`prog_a`, `prog_b`, `prog_c`) |
| `testsched` | Phase 5 Scheduler | Run Phase 5 automated validation suite (ready queue, idle fallback, syscalls, metrics) |
| `viol_iso` | Phase 5 Security | Spawn rogue process attempting to write kernel memory, testing page fault isolation |
| `uptime` | Phase 2 Timer | Display live system uptime calculated from 100 Hz PIT timer ticks |
| `cpu` | Phase 1 CPUID | Interrogate CPUID hardware vendor and architecture capabilities |
| `mem` | Phase 3 Memory | Display physical memory statistics (total/used/free frames) and heap metrics |
| `mmap` | Phase 3 Memory | Display BIOS E820 physical memory map table |
| `memtest` | Phase 3 Memory | Run automated memory validation suite (PMM, VMM, heap, and 100-block stress test) |
| `crashpf` | Phase 3 Memory | Trigger a controlled kernel page fault to test architectural #PF diagnostic dump |
| `user` | Phase 4 Processes | Spawn and execute Ring 3 user process PID 1 (`SYS_WRITE`, `SYS_GETPID`, `SYS_EXIT`) |
| `testuser` | Phase 4 Security | Run Phase 4 user security & syscall test suite (TSS/TR, bounds, hostile pointer rejection) |
| `viol_write`| Phase 4 Security | Spawn rogue Ring 3 process attempting to write to `0x100000`, testing hardware #PF protection |
| `clear` | Phase 1 VGA | Clear the VGA screen and reset hardware cursor to (0,0) |
| `help` | Console | List all available built-in commands |

Line editing supports:
- Printable ASCII characters (`A-Z`, `a-z`, `0-9`, symbols, space)
- <kbd>Shift</kbd> and <kbd>Caps Lock</kbd> modifiers
- <kbd>Backspace</kbd> with character erase and hardware cursor repositioning
- <kbd>Enter</kbd> to execute and produce new prompt `nyota> `

---

### 3. How to Control & Exit QEMU

| Action | Shortcut / Method | Description |
| :--- | :--- | :--- |
| **Exit QEMU (Window)** | Click the window **Close (X)** button | Shuts down emulator immediately |
| **Exit QEMU (Terminal)** | Press <kbd>Ctrl</kbd> + <kbd>C</kbd> in your terminal | Stops QEMU process cleanly |
| **Release Mouse Cursor** | Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>G</kbd> (or <kbd>Ctrl</kbd> + <kbd>Alt</kbd>) | Un-grabs mouse if cursor is locked in QEMU |
| **Open QEMU Monitor** | Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>2</kbd> inside QEMU | Opens interactive hardware monitor |
| **Return to OS Display** | Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>1</kbd> | Returns to the Nyota VGA console |

---

### 4. Interactive Live Inspection via QEMU Monitor

Even before the Phase 2 keyboard shell, you can interact directly with the running hardware state using QEMU's built-in **Monitor**:

1. Inside the QEMU window, press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>2</kbd> to open the `(qemu)` monitor prompt.
2. Run any of the following live hardware inspection commands:

- **Inspect 64-bit CPU registers:**
  ```text
  (qemu) info registers
  ```
  *Dumps live registers (`RAX`, `RBX`, `RIP`, `RSP`, `CR0`, `CR3`, `CR4`, `EFER`). Verifies that 64-bit Long Mode is active (`CR0.PG=1`, `CR4.PAE=1`, `EFER.LME=1`).*

- **Inspect physical memory at kernel entry:**
  ```text
  (qemu) xp /16x 0x100000
  ```
  *Dumps physical memory at `0x100000` (1 MB mark), showing the raw machine code of the loaded Nyota kernel.*

- **Inspect VGA text video memory:**
  ```text
  (qemu) xp /24cx 0xB8000
  ```
  *Dumps raw VGA buffer memory displaying characters and color attributes currently rendered on screen.*

- **Inspect active memory mappings:**
  ```text
  (qemu) info mem
  ```
  *Displays the active virtual memory page tables (confirming PML4 2MB identity-mapped pages).*

- **Quit emulator:**
  ```text
  (qemu) quit
  ```

3. Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>1</kbd> to switch back to the Nyota OS screen.

---

### 5. Running in Different Modes

Depending on your workflow or environment:

#### 🖥️ Standard Interactive Mode (VGA Window + Terminal Serial)
```bash
make run
```
*Best for visual inspection and development.*

#### ⚡ Headless Serial Mode (Terminal Only)
```bash
make run-serial
```
*Best for CI/CD pipelines, remote SSH sessions, or fast terminal-only checks without opening a GUI window. Press <kbd>Ctrl</kbd> + <kbd>C</kbd> to exit.*

#### 🐞 Source-Level GDB Debugging Mode
```bash
make run-debug
```
*Starts QEMU paused at the boot vector and opens GDB stub on port `1234`. Connect with `gdb build/kernel.elf` in another terminal (see [Debugging with GDB](#-debugging-with-gdb)).*

---

### 6. Hands-On: Modifying and Testing Your Own Kernel Code

You can easily experiment with Nyota OS by adding your own logic to the kernel:

1. Open [`kernel/kernel.c`](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/kernel.c).
2. Inside `kernel_main()`, add custom log messages or change VGA text colors:

```c
/* Custom logging test */
kinfo("My custom feature initialized successfully!");
kwarn("Warning: low memory simulation test.");
kerror("Error test message.");

/* Custom colored text */
vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
vga_println("Hello from Nyota OS kernel hack!");
```

3. To test the kernel panic handler, add:

```c
kernel_panic("Kernel panic test triggered intentionally.");
```

4. Recompile and run in one command:

```bash
make rebuild && make run
```

---

# 🐞 Debugging with GDB

Nyota OS includes built-in support for source-level kernel debugging via QEMU's GDB server.

### 1. Launch QEMU in Debug Mode

```bash
make run-debug
```

QEMU will start, freeze the CPU at the reset vector, and listen for GDB connections on TCP port `1234`.

### 2. Connect GDB

In another terminal window:

```bash
gdb build/kernel.elf
```

Inside GDB:

```gdb
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
(gdb) info registers rip rsp rax
(gdb) step
```

---

# 🧰 Build Commands Reference

| Command | Description |
| :--- | :--- |
| `make` / `make all` | Build complete bootable image (`build/nyota.img`) |
| `make run` | Launch OS in QEMU with VGA display & serial console |
| `make run-serial` | Run in QEMU headless (prints serial output directly to terminal) |
| `make run-debug` | Launch QEMU with GDB stub paused on port 1234 |
| `make memtest` | Run headless QEMU for automated memory verification |
| `make debug` | Build with debug symbols enabled (`-g`) |
| `make clean` | Remove all generated binaries and build artifacts |
| `make rebuild` | Perform a clean build from scratch |
| `make help` | Display available build targets |

---

# ⚠️ Current Limitations (Phase 4)

Phase 4 successfully implements hardware Ring 3 user mode execution, GDT user code/data descriptors, Task State Segment (TSS) with `RSP0` stack switching, isolated user address spaces in PML4[1], dedicated user/kernel stacks, Vector 0x80 System Call dispatcher (`SYS_WRITE`, `SYS_EXIT`, `SYS_GETPID`), strict hostile user memory validation, process abstraction (`process_t`), and graceful user-space segfault recovery. The following subsystems belong to subsequent phases:

- Preemptive multitasking, thread contexts, and round-robin scheduler are deferred to Phase 5.
- Storage controller drivers (IDE/ATA) and Virtual File System (VFS) are deferred to Phase 6.
- Executable and Linkable Format (ELF-64) binary loader is deferred to Phase 7.
- Inter-Process Communication (IPC), signals, and pipes are deferred to Phase 8.

---

# 🗺️ Roadmap

```text
Phase 1: Kernel Foundation & Boot Architecture  ◄ [COMPLETED]
   ├── BIOS MBR Bootloader (16-bit)
   ├── Stage 2 Bootloader & A20 Gate
   ├── 4-Level Paging (Identity Mapping 0-16MB)
   ├── 64-bit Long Mode Transition
   ├── 64-bit Kernel Entry & C Runtime
   ├── Global Descriptor Table (GDT)
   ├── CPUID Feature Interrogation
   ├── VGA Text Mode Driver & Scrolling
   ├── Serial COM1 Diagnostics
   ├── Kernel Logging & Panic System
   └── Automated Bootable Image Generation

Phase 2: Interrupts & Input Architecture         ◄ [COMPLETED]
   ├── Interrupt Descriptor Table (IDT, 256 gates)
   ├── CPU Exception Handlers (0..31) & Panic Dump
   ├── Assembly ISR Stubs & Uniform Stack Frames
   ├── Centralized Interrupt Dispatcher
   ├── 8259 PIC Remapping (Vectors 32..47)
   ├── PIT Timer (100 Hz, Uptime, timer_sleep)
   ├── PS/2 Keyboard Driver & Scancode Set 1 Decoder
   ├── Lock-Free Keyboard Event Circular Buffer
   └── Interactive Kernel Console (nyota> shell)

Phase 3: Memory Management                       ◄ [COMPLETED]
   ├── BIOS E820 Physical Memory Map Parser
   ├── Physical Memory Manager (PMM) & Bitmap Allocator
   ├── 4-Level x86_64 Paging Architecture (PML4, PDPT, PD, PT)
   ├── Dynamic Page Mapping & Unmapping (paging_map_page)
   ├── CR3 Control & TLB Invalidation (invlpg)
   ├── Architectural Page Fault Diagnostics (#PF Vector 14, CR2)
   ├── Kernel Dynamic Heap (kmalloc, kfree, kcalloc, krealloc)
   ├── Free-Block Coalescing & Dynamic Heap Expansion
   └── Memory Test Suite (PMM, VMM, Heap, 100-Block Stress Test)

Phase 4: Processes, User Mode & System Calls     ◄ [COMPLETED]
   ├── Hardware Ring 0 / Ring 3 Privilege Separation
   ├── GDT User Code & Data Descriptors (0x23 / 0x1B)
   ├── 64-bit Task State Segment (TSS, RSP0 Stack Transition)
   ├── Isolated User Address Space (PML4[1] at 512 GB mark)
   ├── Dedicated User Stack (0x8000010000) & Kernel Stacks
   ├── System Call Vector 0x80 (IDT_GATE_USER_TRAP)
   ├── System V AMD64 Syscall ABI (RAX, RDI, RSI, RDX, R10, R8)
   ├── Syscall Dispatcher (SYS_WRITE, SYS_EXIT, SYS_GETPID)
   ├── Hostile User Pointer Validation & copy_from_user
   ├── Process Control Block (process_t) & PID Allocator
   ├── Initial Ring 3 User Process Execution & Clean Exit
   ├── Graceful User Page Fault Recovery (SIGSEGV -11)
   └── Automated User Security & Privilege Test Suite

Phase 5: Preemptive Multitasking & Scheduling   ◄ [COMPLETED]
   ├── Hardware-Driven Preemptive CPU Scheduler (100 Hz PIT)
   ├── Round-Robin Time-Slice Preemption (50 ms Quantum)
   ├── Process Lifecycle (NEW, READY, RUNNING, SLEEPING, TERMINATED, IDLE)
   ├── Circular Doubly-Linked Ready Queue with O(1) Operations
   ├── Real-Time Sleep Queue with Automatic Timer-Tick Wakeup
   ├── Stack-Based Context Switch Engine via isr_common_stub (mov rsp, rax)
   ├── Ring 3/Ring 0 Data Segment Selector Restoration (0x1B / 0x10)
   ├── Dynamic Address Space (CR3) & TSS RSP0 Stack Switching
   ├── Expanded Syscalls: SYS_YIELD (3) and SYS_SLEEP (4)
   ├── Multi-Process Suite (prog_a yield, prog_b compute, prog_c sleeper)
   ├── Fault Isolation & Protection (rogue process #PF containment)
   ├── Kernel Idle Task (PID 0) with Power-Saving hlt Loop
   └── Interactive Process Manager (ps, multitask, testsched, viol_iso)

Phase 6: Filesystem & Storage                     ◄ [NEXT]
   ├── IDE / ATA Sector I/O Driver
   └── Virtual File System (VFS) & FAT32 / TAR FS

Phase 7: Executable Formats & Shell
   ├── 64-bit ELF Binary Loader
   └── User-Space Command-Line Shell & Utilities
```

---

# 👨‍💻 Author

**Sahil**  
Computer Science Engineering Student  
Focus: **Cybersecurity • Systems Programming • Operating Systems • Computer Architecture**

---

<p align="center">
  ⭐ <strong>Nyota OS — Built from the ground up. One instruction at a time.</strong>
</p>