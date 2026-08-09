<p align="center">
  <img src="assets/NyotaLogo.svg" alt="Nyota OS Logo" width="180">
</p>

<h1 align="center">Nyota OS</h1>

<p align="center">
  <strong>A hobby operating system built from scratch.</strong>
</p>

<p align="center">
  Exploring boot processes, low-level programming, kernel development,
  and operating system fundamentals.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-C-00599C?style=for-the-badge&logo=c&logoColor=white">
  <img src="https://img.shields.io/badge/Assembly-x86-525252?style=for-the-badge">
  <img src="https://img.shields.io/badge/Assembler-NASM-111111?style=for-the-badge">
  <img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=for-the-badge">
</p>

<p align="center">
  <a href="#getting-started">Getting Started</a>
  ·
  <a href="#architecture">Architecture</a>
  ·
  <a href="#roadmap">Roadmap</a>
</p>

---

### A Hobby Operating System Built From Scratch

**Nyota OS** is an experimental hobby operating system developed from the ground up to explore low-level programming, boot processes, operating system fundamentals, and direct interaction with computer hardware.

The project is written primarily in **C and Assembly** and is built using a Linux/WSL development environment. The resulting bootable image can be tested using **QEMU**.

> **Nyota** means "star" in Swahili, representing the goal of exploring the lower layers of computing one step at a time.

---

<p align="center">

<a href="#getting-started">
<img src="https://img.shields.io/badge/Get%20Started-111111?style=for-the-badge&logo=rocket&logoColor=white" alt="Get Started">
</a>
<a href="#architecture">
<img src="https://img.shields.io/badge/Architecture-111111?style=for-the-badge&logo=linux&logoColor=white" alt="Architecture">
</a>
<a href="#roadmap">
<img src="https://img.shields.io/badge/Roadmap-111111?style=for-the-badge&logo=github&logoColor=white" alt="Roadmap">
</a>

</p>

<p align="center">

<img src="https://img.shields.io/badge/Language-C-00599C?style=flat-square&logo=c&logoColor=white">
<img src="https://img.shields.io/badge/Assembly-x86-525252?style=flat-square">
<img src="https://img.shields.io/badge/Assembler-NASM-111111?style=flat-square">
<img src="https://img.shields.io/badge/Compiler-GCC-234?style=flat-square&logo=gnu&logoColor=white">
<img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=flat-square">
<img src="https://img.shields.io/badge/Build-Make-427819?style=flat-square">

</p>

---

## 📸 Project Preview

> Add a screenshot of Nyota OS running in QEMU here.

```text
docs/
└── nyota-os-qemu.png
```

Example:

![Nyota OS running in QEMU](docs/nyota-os-qemu.png)

---

# 🧠 What Is Nyota OS?

Nyota OS is a **from-scratch operating system project** created to understand what happens underneath applications and modern operating systems.

Instead of relying on an existing operating system kernel, the project explores the fundamentals involved in creating a bootable system.

The project focuses on understanding concepts such as:

* Boot processes
* Low-level programming
* Assembly
* C at the system level
* Kernel development
* Hardware interaction
* Memory
* Computer architecture
* Build systems
* Emulator-based operating system development

The project is intentionally experimental and will evolve as new operating system concepts are implemented.

---

# ✨ Current Features

Nyota OS currently includes the foundations required to produce and boot the operating system image.

### Current Capabilities

* 🥾 Bootable operating system image
* ⚙️ Custom low-level code
* 🧠 C-based system development
* 🔧 x86 Assembly
* 🔗 Custom linking/build process
* 💾 Bootable disk image generation
* 🖥️ QEMU-based execution
* 🐧 Linux/WSL development environment

> Nyota OS is an ongoing project. Features will be added incrementally as development continues.

---

# 🏗️ Architecture

At a high level, Nyota OS follows a simple low-level execution path:

```text
┌──────────────────────┐
│      Computer        │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│      Boot Process    │
│      Assembly        │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│      Kernel          │
│      C + Assembly    │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ Hardware / Memory /  │
│ System Interfaces    │
└──────────────────────┘
```

The architecture will become more sophisticated as additional kernel and hardware functionality is implemented.

---

# 🛠️ Development Environment

Nyota OS is currently developed and tested using:

| Tool             | Purpose                    |
| ---------------- | -------------------------- |
| **C**            | Kernel/system programming  |
| **x86 Assembly** | Low-level and boot code    |
| **NASM**         | Assembler                  |
| **GCC**          | C compilation              |
| **GNU LD**       | Linking                    |
| **GNU objcopy**  | Binary/image generation    |
| **GNU Make**     | Build automation           |
| **QEMU**         | Operating system emulation |
| **Linux / WSL2** | Development environment    |

---

# 📋 Prerequisites

Before building Nyota OS, install the required development tools.

On Debian/Ubuntu-based Linux or WSL:

```bash
sudo apt update
```

Install the basic toolchain:

```bash
sudo apt install build-essential nasm binutils make qemu-system-x86
```

Verify the installations:

```bash
gcc --version
nasm --version
ld --version
objcopy --version
make --version
qemu-system-i386 --version
```

You should see version information for each tool.

---

# 🚀 Getting Started

## 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/nyota-os.git
```

Move into the project:

```bash
cd nyota-os
```

---

## 2. Build Nyota OS

Run:

```bash
make
```

The Makefile will compile the required C and Assembly components and generate the bootable image.

If your project uses a specific build target, use the target defined in the repository's `Makefile`.

---

## 3. Run With QEMU

After building the image, launch it using QEMU.

For example:

```bash
qemu-system-i386 -drive format=raw,file=nyota.img
```

If the generated image has a different filename, replace `nyota.img` with the generated image.

---

# 🖥️ Running Nyota OS

A successful launch should boot the generated Nyota OS image inside QEMU.

```text
Host Operating System
        │
        ▼
      QEMU
        │
        ▼
   nyota.img
        │
        ▼
  Boot Process
        │
        ▼
    Nyota OS
```

QEMU allows development and testing without installing Nyota OS directly onto physical hardware.

**Do not write experimental OS images directly to a physical disk unless you fully understand the consequences.**

One typo in a command and suddenly your storage device becomes a very expensive coaster. QEMU exists for a reason.

---

# 📁 Project Structure

The exact structure may evolve as the operating system grows.

A typical structure is:

```text
nyota-os/
│
├── boot/              # Boot-related code
│
├── kernel/            # Kernel implementation
│
├── src/               # C source files
│
├── include/           # Header files
│
├── Makefile           # Build automation
│
├── linker.ld          # Linker script
│
├── README.md
│
└── .gitignore
```

Generated build artifacts should not be committed to the main source tree.

---

# 🔬 Development Philosophy

Nyota OS is being developed primarily as a **learning and experimentation project**.

The goal is not to immediately reproduce Linux or another production operating system.

Instead, development follows a gradual approach:

```text
Boot
 │
 ▼
Low-Level Initialization
 │
 ▼
Kernel
 │
 ▼
Memory
 │
 ▼
Interrupts
 │
 ▼
Hardware Interaction
 │
 ▼
Drivers
 │
 ▼
Processes
 │
 ▼
File System
 │
 ▼
User Space
```

Each stage provides a deeper understanding of how operating systems interact with hardware.

---

# 🗺️ Roadmap

Nyota OS is an ongoing project.

### Phase 1 — Boot Foundation

* [x] Bootable image
* [x] Assembly boot code
* [x] Basic build pipeline
* [x] QEMU boot testing

### Phase 2 — Kernel Foundation

* [x] Kernel entry
* [x] C-based kernel development
* [x] Linker configuration
* [x] Kernel/image integration

### Phase 3 — System Interaction

* [x] Initial system interaction
* [ ] Keyboard input improvements
* [ ] Interrupt handling
* [ ] IDT
* [ ] GDT improvements

### Phase 4 — Memory

* [ ] Memory map
* [ ] Physical memory management
* [ ] Heap
* [ ] Paging
* [ ] Virtual memory

### Phase 5 — Hardware

* [ ] Keyboard driver
* [ ] Timer
* [ ] VGA/framebuffer improvements
* [ ] Device abstractions

### Phase 6 — Processes

* [ ] Process management
* [ ] Context switching
* [ ] Scheduling
* [ ] User/kernel separation

### Phase 7 — Storage

* [ ] File-system design
* [ ] Disk abstraction
* [ ] File operations

### Phase 8 — User Space

* [ ] System calls
* [ ] Shell
* [ ] Basic user programs
* [ ] User-space memory

> Roadmap items are experimental goals and may change as the project evolves.

---

# 🧪 Testing

Nyota OS is primarily tested through QEMU.

Build:

```bash
make
```

Run:

```bash
qemu-system-i386 -drive format=raw,file=nyota.img
```

The emulator provides a safe environment for testing boot and kernel changes without modifying the host operating system.

---

# 🧰 Build Commands

Common commands:

```bash
# Build
make

# Clean generated files
make clean

# Build again
make clean && make
```

If additional Makefile targets are available, they should be documented here.

---

# 📷 Screenshots

Screenshots and development captures will be added as the project evolves.

Recommended:

```text
docs/
├── boot.png
├── qemu.png
├── kernel.png
└── architecture.png
```

---

# 🎯 Project Goals

Nyota OS exists to answer a simple question:

> **What actually happens between turning on a computer and running software?**

Through building the system from the ground up, the project explores:

* How machines boot
* How processors execute instructions
* How C interacts with hardware
* How Assembly fits into system software
* How kernels are structured
* How memory is managed
* How hardware communicates with software
* How operating systems provide abstractions

---

# 🔐 Security Perspective

Because Nyota OS is part of a broader cybersecurity and systems-learning journey, the project also provides a foundation for understanding security at a lower level.

Future areas of exploration may include:

* Memory isolation
* Privilege levels
* Kernel attack surfaces
* Secure boot concepts
* Process isolation
* System-call security
* Memory corruption
* Hardware security boundaries

These areas will be explored as the operating system becomes more capable.

---

# ⚠️ Disclaimer

Nyota OS is an **experimental educational operating system project**.

It is not intended to replace a production operating system and should not be considered production-ready.

Run the system inside an emulator such as **QEMU** during development.

Do not install experimental builds directly onto important physical storage devices.

---

# 🤝 Contributing

Nyota OS is primarily a personal learning project, but ideas, discussions, and educational contributions are welcome.

If you find an issue or have an interesting idea:

1. Open an issue.
2. Explain the problem or proposal.
3. Include reproduction steps where applicable.
4. Provide relevant logs or screenshots.

Pull requests should remain focused and clearly documented.

---

# 📚 Learning Resources

Nyota OS development involves concepts from:

* Operating systems
* Computer architecture
* C programming
* Assembly programming
* x86 architecture
* Linkers and loaders
* Boot processes
* Kernel development

The project itself is intended to be a practical way of learning these concepts rather than simply reading about them.

---

# 👨‍💻 Author

**Sahil**

Computer Science Engineering Student
Cybersecurity

Interested in:

**Cybersecurity • Software Engineering • Systems • Networking • Low-Level Programming**

---

<p align="center">

**Built from the ground up. One instruction at a time.**

</p>

---

<p align="center">

⭐ If you find Nyota OS interesting, consider starring the repository.

</p>
