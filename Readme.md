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
  <img src="https://img.shields.io/badge/Phase-2%3A%20Interrupts%20%26%20Input-success?style=for-the-badge">
  <img src="https://img.shields.io/badge/Version-v0.2.0-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Architecture-x86__64-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20x86__64%20ASM-00599C?style=for-the-badge&logo=c&logoColor=white">
  <img src="https://img.shields.io/badge/Toolchain-NASM%20%7C%20GCC%20%7C%20Binutils-111111?style=for-the-badge">
  <img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=for-the-badge">
</p>

<p align="center">
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

# 🚀 Phase 2 Overview

**Current Status:** **Phase 2 — Interrupts, Exceptions, Timer & Keyboard** (Completed)

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
│   └── stage2.asm           # Stage 2: A20, CPUID, Paging, GDT, Long Mode switch
│
├── kernel/
│   ├── kernel.c             # C kernel entry (kernel_main) and logging
│   ├── kernel_entry.asm     # 64-bit entry point, stack setup, calls kernel_main
│   ├── console.c            # Interactive kernel console and line editing
│   │
│   ├── arch/
│   │   └── x86_64/
│   │       ├── idt.c        # 256-entry 64-bit IDT initialization & gate setup
│   │       ├── interrupts.asm # 256 ISR stubs, stack frame setup, iretq
│   │       ├── dispatcher.c # Centralized interrupt dispatcher & handler table
│   │       ├── exceptions.c # CPU exception handlers (0..31) & diagnostic panic
│   │       ├── pic.c        # 8259 PIC initialization, IRQ remapping, EOI
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
│   ├── kernel.h             # Logging macros, version info, kernel_panic
│   ├── cpu.h                # CPU capabilities and CPUID interface
│   ├── gdt.h                # GDT constants and initialization prototype
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
│   └── memory.h             # Memory and string function declarations
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

### 2. Interactive Console Commands (Phase 2)

Nyota OS v0.2.0 features a functional interactive kernel console. Both your graphical keyboard (in the QEMU window) and host terminal stdin (via serial) are active:

| Command | Action |
| :--- | :--- |
| `any text` | Press <kbd>Enter</kbd> to echo back input text |
| `uptime` | Display live system uptime calculated from 100 Hz PIT timer ticks |
| `cpu` | Interrogate CPUID hardware vendor and architecture capabilities |
| `clear` | Clear the VGA screen and reset hardware cursor to (0,0) |
| `help` | List available built-in commands |

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
| `make debug` | Build with debug symbols enabled (`-g`) |
| `make clean` | Remove all generated binaries and build artifacts |
| `make rebuild` | Perform a clean build from scratch |
| `make help` | Display available build targets |

---

# ⚠️ Current Limitations (Phase 2)

Phase 2 successfully implements the complete interrupt pipeline, exception handling, PIT timer, PS/2 keyboard driver, and interactive console. The following subsystems belong to subsequent phases:

- Dynamic memory management (Physical frame allocator, virtual memory manager, heap `kmalloc`/`kfree`) is deferred to Phase 3.
- Multitasking, Task State Segment (TSS), context switching, and scheduler are deferred to Phase 4.
- Storage controller drivers (IDE/ATA) and Virtual File System (VFS) are deferred to Phase 5.
- Ring 3 User Space transition, system calls (`syscall`/`sysret`), and userland binaries are deferred to Phase 6.

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

Phase 3: Memory Management                       ◄ [NEXT]
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