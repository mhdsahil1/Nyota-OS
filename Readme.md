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
  <img src="https://img.shields.io/badge/Phase-1%3A%20Kernel%20Foundation-success?style=for-the-badge">
  <img src="https://img.shields.io/badge/Architecture-x86__64-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20x86__64%20ASM-00599C?style=for-the-badge&logo=c&logoColor=white">
  <img src="https://img.shields.io/badge/Toolchain-NASM%20%7C%20GCC%20%7C%20Binutils-111111?style=for-the-badge">
  <img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=for-the-badge">
</p>

<p align="center">
  <a href="#-phase-1-overview">Phase 1 Overview</a>
  ·
  <a href="#-architecture--boot-flow">Architecture</a>
  ·
  <a href="#-project-structure">Project Structure</a>
  ·
  <a href="#-getting-started">Getting Started</a>
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

# 🚀 Phase 1 Overview

**Current Status:** **Phase 1 — Kernel Foundation & Boot Architecture** (Completed)

Phase 1 establishes a clean, bootable, maintainable 64-bit kernel foundation for Nyota OS:

- Boots from a raw disk image via standard BIOS in QEMU.
- Executes two-stage bootloader:
  - **Stage 1 (MBR)**: Initializes segments, validates BIOS boot drive, loads Stage 2 from disk.
  - **Stage 2**: Enables A20 gate, verifies CPUID & 64-bit Long Mode support, sets up 4-level PML4 paging (identity-mapping 0–16 MB using 2 MB huge pages), builds 64-bit GDT, enables PAE/LME/PG, transitions to 64-bit Long Mode, relocates the kernel to `0x100000` (1 MB mark), and transfers control.
- Executes 64-bit C kernel at entry point `0x100000` with an aligned stack.
- Configures 64-bit kernel Global Descriptor Table (GDT).
- Inspects CPU vendor string and hardware features using CPUID (SSE, SSE2, SSE3, APIC, PAE, Long Mode, NX).
- Provides VGA 80x25 text-mode console with hardware cursor, color support, and automatic scrolling.
- Mirrors console output to COM1 serial port (`0x3F8`) for instant host terminal diagnostics.
- Implements kernel logging interface (`kprint`, `kprintln`, `klog`, `kinfo`, `kwarn`, `kerror`) and safe kernel panic system (`kernel_panic`).
- Enters safe idle loop (`hlt`).
- Reproducible cross-platform build system generating `build/nyota.img` in one command.

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
│   └── stage2.asm           # Stage 2: A20, CPUID, Paging, GDT, Long Mode switch
│
├── kernel/
│   ├── kernel.c             # C kernel entry (kernel_main) and logging
│   ├── kernel_entry.asm     # 64-bit entry point, stack setup, calls kernel_main
│   │
│   ├── arch/
│   │   └── x86_64/
│   │       └── io.h         # Architecture port I/O wrappers
│   │
│   ├── cpu/
│   │   ├── cpu.c            # CPUID hardware feature detection & vendor query
│   │   ├── gdt.c            # 64-bit Global Descriptor Table setup
│   │   └── gdt_flush.asm    # 64-bit GDTR reload and CS/DS refresh
│   │
│   └── memory/
│       └── memory.c         # Freestanding memset, memcpy, memmove, memcmp, strlen
│
├── include/
│   ├── types.h              # Freestanding fixed-width types (uint64_t, bool, etc.)
│   ├── kernel.h             # Logging macros (kinfo, kwarn, kerror) and kernel_panic
│   ├── cpu.h                # CPU capabilities and CPUID interface
│   ├── gdt.h                # GDT constants and initialization prototype
│   ├── vga.h                # VGA colors, cursor positioning, and print APIs
│   ├── serial.h             # COM1 serial driver interface
│   ├── io.h                 # Port I/O (inb, outb, inw, outw, inl, outl)
│   └── memory.h             # Memory and string function declarations
│
├── drivers/
│   ├── vga.c                # 80x25 text-mode driver at 0xB8000 with scrolling
│   └── serial.c             # 16550 UART serial driver (115200 8N1)
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

[INFO]  Bootloader initialized
[INFO]  CPU: x86_64
[INFO]  GDT initialized
[INFO]  Paging enabled
[INFO]  Kernel loaded
[INFO]  Kernel initialization complete

CPU Information
-------------------------
Vendor   : AuthenticAMD (or GenuineIntel)
Mode     : x86_64
Features :
  SSE
  SSE2
  SSE3
  APIC
  PAE
  Long Mode (x86_64)
  NX (No-Execute)

Bootloader       : OK
CPU              : x86_64
Long Mode        : OK
Paging           : OK
GDT              : OK
Kernel           : OK

----------------------------------------

Nyota Kernel v0.1
System initialized successfully.

nyota kernel is running...
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
| `make debug` | Build with debug symbols enabled (`-g`) |
| `make clean` | Remove all generated binaries and build artifacts |
| `make rebuild` | Perform a clean build from scratch |
| `make help` | Display available build targets |

---

# ⚠️ Current Limitations (Phase 1)

Phase 1 deliberately focuses exclusively on establishing a rock-solid boot architecture and 64-bit kernel foundation. The following are intentionally deferred to future phases:

- Hardware interrupts are disabled (`cli`).
- Interrupt Descriptor Table (IDT) and exception handling are deferred to Phase 2.
- Interactive keyboard input driver is deferred to Phase 2.
- Dynamic physical and virtual memory allocation (heap / malloc) is deferred to Phase 3.
- Multitasking, scheduler, filesystems, and userland applications belong to later phases.

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

Phase 2: Interrupts & Input Architecture         ◄ [NEXT]
   ├── Interrupt Descriptor Table (IDT)
   ├── CPU Exception Handlers (Page Fault, GPF, etc.)
   ├── 8259 PIC / APIC Configuration
   ├── Programmable Interval Timer (PIT)
   ├── PS/2 Keyboard Controller Driver
   └── Interactive Kernel Shell

Phase 3: Memory Management
   ├── Physical Memory Allocator (Bitmap / Buddy)
   ├── Virtual Memory Manager & Dynamic Paging
   └── Kernel Heap (kmalloc / kfree)

Phase 4: Multitasking & Processes
   ├── Task State Segment (TSS)
   ├── Context Switching
   └── Round-Robin Cooperative/Preemptive Scheduler

Phase 5: Filesystem & Storage
   ├── IDE / ATA Disk Driver
   └── Virtual File System (VFS) & FAT32 / TAR FS

Phase 6: User Space & System Calls
   ├── Ring 3 User Mode Transition
   ├── Syscall Interface (syscall / sysret)
   └── Basic Userland Shell & Utilities
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