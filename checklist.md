# Nyota OS — Phase 9 Part 3 Implementation Checklist

## 1. Process Environment Variables
- [x] Safe kernel representation for `NAME=value` environment storage in `process_t` (`env[PROCESS_ENV_MAX_VARS][PROCESS_ENV_MAX_LEN]`)
- [x] Pointer validation using `user_validate_pointer()` in `sys_handle_execve()` and `sys_handle_spawn2()`
- [x] Environment inheritance across `process_execve()` and `process_spawn_elf()`
- [x] Userland standard C library functions in `libnyota`: `getenv()`, `setenv()`, `unsetenv()`, `environ`
- [x] Shell environment builtins: `export`, `unset`, `env`
- [x] Shell parameter expansion: `$VAR` in command arguments
- [x] Environment propagation to child executables (`export NAME=Nyota; prog` receives `NAME=Nyota`)

## 2. Current Working Directory (`cwd`)
- [x] Per-process `cwd` stored in `process_t`
- [x] Syscalls `SYS_CHDIR` (47) and `SYS_GETCWD` (48) with user pointer validation
- [x] Path resolution for relative paths (including `.` and `..`) across VFS and ELF loader
- [x] Shell builtins: `cd` (supporting `~`, `-`, relative paths) and `pwd`

## 3. TTY Subsystem & Canonical Terminal Input
- [x] Hardware-decoupled TTY abstraction (`tty_t`)
- [x] Canonical line editing: line buffer, echo, backspace (`\b \b`), Enter (`\n`)
- [x] Terminal signals:
  - `Ctrl+C` (ASCII 0x03) $\rightarrow$ `SIGINT`
  - `Ctrl+Z` (ASCII 0x1A) $\rightarrow$ `SIGTSTP`
  - `Ctrl+D` (ASCII 0x04) $\rightarrow$ EOF (returns 0 bytes on empty buffer)
- [x] Delivery of terminal signals to foreground process group (`tty->foreground_pgrp`)
- [x] Background process isolation from stealing terminal input

## 4. Process Groups (`PGID`) & Sessions (`SID`)
- [x] `process_t` fields: `pgrp`, `sid`, `controlling_tty`
- [x] Syscalls:
  - `SYS_SETPGID` (55): `setpgid(pid, pgid)`
  - `SYS_GETPGID` (56): `getpgid(pid)`
  - `SYS_SETSID` (57): `setsid()`
  - `SYS_GETSID` (58): `getsid(pid)`
  - `SYS_TTY_CTRL` (53): `TTY_CTRL_GET_PGRP`, `TTY_CTRL_SET_PGRP`
- [x] TTY tracking of `foreground_pgrp` and `session_id`
- [x] `signal_send_pgrp()` to broadcast signals to all processes in group

## 5. Shell Job Control
- [x] Background job launching: `cmd &`
- [x] Job table tracking: Job ID, PID, PGID, State (`RUNNING`, `STOPPED`, `TERMINATED`), Command
- [x] Shell builtin `jobs`: displays active jobs with status
- [x] Shell builtin `fg [id]`: transfers TTY foreground PGID, resumes with `SIGCONT`, waits with `waitpid()`, regains TTY on completion/stop
- [x] Shell builtin `bg [id]`: resumes stopped job with `SIGCONT` in background without seizing TTY
- [x] `waitpid()` with `WUNTRACED`: detects child state transitions (`PROCESS_STOPPED`) without reaping

## 6. Login Daemon (`/sbin/logind`) & Multi-User Environment
- [x] `/etc/passwd` configuration with `user`, `sahil`, and `root`
- [x] Home directories: `/home/user`, `/home/sahil`, `/root`
- [x] `/sbin/logind` session lifecycle:
  - Prompts `tty0 login:` and `Password:`
  - Validates credentials against `/etc/passwd`
  - Creates user session with `setsid()`
  - Sets user environment: `USER`, `HOME`, `SHELL`, `PWD`, `PATH`
  - Changes directory to home directory
  - Binds controlling TTY and launches login shell
  - Reaps session on shell exit and re-prompts for login

## 7. Testing & Verification (Part 3)
- [x] `test_security.py` passes 25/25 (100%)
- [x] `test_phase9_part1.py` passes 19/19 (100%)
- [x] `test_phase9_part2.py` passes 26/26 (100%)
- [x] New `test_phase9_part3.py` suite covering:
  - Environment variables & inheritance
  - Working directory & relative paths
  - Canonical TTY input & control characters (`Ctrl+C`, `Ctrl+Z`, `Ctrl+D`)
  - Job control (`jobs`, `fg`, `bg`, background `&`)
  - Sessions & process groups
  - Multi-user login & `/etc/passwd`
  - Failure recovery (foreground process crash, background exit)

---

# Nyota OS — Phase 9 Part 4 Implementation Checklist

## 1. Unix-Domain Sockets (`AF_UNIX`) & Service IPC
- [x] `AF_UNIX` / `AF_LOCAL` socket support with `SOCK_STREAM` in `kernel/net/socket.c`
- [x] Filesystem-style socket addressing: `/run/init.sock`, `/run/logger.sock`, `/run/netd.sock`
- [x] `vfs_create_entry()` upon `socket_bind()` so bound sockets are visible in VFS directories (`ls /run`, `stat`)
- [x] Real service interaction via Unix socket:
  - `/bin/logger` $\rightarrow$ `/run/logger.sock` $\rightarrow$ `/sbin/loggerd` $\rightarrow$ `/var/log/system.log`
  - `/bin/service` $\rightarrow$ `/run/init.sock` $\rightarrow$ `/init` (status, list, start, stop, restart)
  - `/bin/reboot` & `/bin/shutdown` $\rightarrow$ `/run/init.sock` $\rightarrow$ `/init` (controlled shutdown)
  - `/bin/netstat` $\rightarrow$ `/run/netd.sock` $\rightarrow$ `/sbin/netd` (network status query)

## 2. Runtime State (`/run`) & PID Files
- [x] `/run` directory created for volatile runtime state (sockets, PID files)
- [x] Daemon PID file creation:
  - `/run/loggerd.pid` created by `loggerd`
  - `/run/netd.pid` created by `netd`
  - `/run/logind.pid` created by `logind`
- [x] Syscall `SYS_UNLINK` (59) and userspace wrapper `unlink()` to remove stale files
- [x] PID file and socket cleanup on service shutdown and system halt

## 3. Network Management Daemon (`netd`) & Configuration
- [x] `/sbin/netd` userland network management daemon
- [x] Static network configuration parsing via `/etc/network.conf` (`iface`, `ip`, `netmask`, `gateway`, `dns`)
- [x] Listens on Unix domain socket `/run/netd.sock`
- [x] Responds to IPC queries with network status and interface telemetry
- [x] Enhanced `/bin/netstat` to query `/run/netd.sock`

## 4. Filesystem Sync (`sync()`) & Buffer Flushing
- [x] Syscall `SYS_SYNC` (36) implemented in `kernel/arch/x86_64/syscall.c`
- [x] `vfs_sync()` in `kernel/fs/vfs.c` flushes pending filesystem blocks
- [x] Userspace library wrapper `sync()` in `user/libnyota/syscall.c`
- [x] Dedicated `/bin/sync` binary utility and shell builtin `sync`
- [x] Automatic `sync()` invocation during controlled system shutdown

## 5. Controlled Shutdown, Reboot & PID 1 State Machine
- [x] `SYS_REBOOT` (35) privilege checking (UID 0 / `CAP_SYS_ADMIN` required)
- [x] PID 1 (`/init`) shutdown state transitions: `INIT_RUNNING` $\rightarrow$ `INIT_SHUTTING_DOWN` $\rightarrow$ `INIT_HALTED`
- [x] New services forbidden from starting during `INIT_SHUTTING_DOWN`
- [x] Orderly service termination in reverse dependency order (`logind` $\rightarrow$ `netd` $\rightarrow$ `ttyd` $\rightarrow$ `loggerd`)
- [x] Session and user process termination (`SIGTERM`, wait, `SIGKILL`)
- [x] Unlinking runtime PID files and domain sockets in `/run`
- [x] Filesystem sync and log flushing prior to hardware reset / halt
- [x] `/bin/reboot` and `/bin/shutdown` utilities coordinating through PID 1 socket

## 6. Core System Utilities
- [x] `/bin/uname`: Displays OS name, kernel version (`1.0.0`), architecture (`x86_64`), hostname
- [x] `/bin/sysinfo`: Displays kernel version, architecture, CPU, memory stats, process count, uptime, filesystem, and network status
- [x] `/bin/free`: Formatted memory display (total, used, free) from memory manager statistics
- [x] `/bin/df`: Formatted filesystem usage (blocks, used, free) from VFS metadata
- [x] `/bin/hostname`: Displays or sets system hostname (`/etc/hostname`)
- [x] `/bin/kill`: Sends signals (`SIGTERM`, `SIGKILL`, etc.) with security/permission checks
- [x] `/bin/sync`: Synchronizes cached filesystem data to storage

## 7. Final Boot Sequence & Service Ordering
- [x] Verified boot ordering: Filesystem $\rightarrow$ `loggerd` $\rightarrow$ `ttyd` $\rightarrow$ `netd` $\rightarrow$ `logind` $\rightarrow$ Login Shell
- [x] Respects declared service dependencies in `/etc/init.conf`
- [x] Full failure recovery: process crashes reaped by PID 1; crash loops bounded to 5 restarts; shell stable across foreground/background crashes

## 8. Final Regression Testing Suite
- [x] `test_security.py` passes 25/25 (100%)
- [x] `test_phase9_part1.py` passes 19/19 (100%)
- [x] `test_phase9_part2.py` passes 26/26 (100%)
- [x] `test_phase9_part3.py` passes 19/19 (100%)
- [x] `test_phase9_part4.py` passes 24/24 (100%)

## Phase 10: Graphical User Interface
- [x] VBE framebuffer discovery, supervisor mapping, safe geometry checks, 32-bit mode fallback to text login
- [x] Clipped 2D primitives, bitmap font, graphics test screen, and back-buffer display flips
- [x] PS/2 mouse and keyboard normalized input events with bounded queues
- [x] Private process-owned window buffers, focus/Z-order server interface, close and lifecycle cleanup
- [x] Desktop compositor, panel, launcher, clock, cursor, Alt+Tab, and Ctrl+Alt+T terminal shortcut
- [x] Native GUI programs and toolkit integrated into the NyotaFS image
- [x] GUI global input/framebuffer/window-server calls restricted to the PID 1 desktop service
- [x] `tools/test_phase10.py` passes 24/24; `tools/test_security.py` passes 25/25
- [x] Interactive visual/input walkthrough and 5+ application display stress validation

## Phase 11: Symmetric Multiprocessing (SMP) & Multicore Kernel
- [x] Hardware CPU discovery via ACPI RSDP, RSDT/XSDT, and MADT (Local APIC entries, I/O APIC, GSI overrides)
- [x] Application Processor (AP) startup sequence using standard INIT-SIPI-SIPI protocol
- [x] 16-bit to 64-bit real mode trampoline assembled at physical `0x8000` with shared communication mailbox
- [x] Local APIC configuration: Virtual wire mode, Spurious vector, Task Priority Register (TPR = 0)
- [x] Calibrated Local APIC periodic timer against PIT reference clock (100 Hz per core)
- [x] Inter-Processor Interrupts (IPIs): Reschedule (`0x50`), TLB Shootdown (`0x51`), Emergency Stop (`0x52`)
- [x] Per-CPU state structure (`cpu_t`): dedicated kernel stacks, per-core GDT/TSS (`tss.rsp0` isolation), independent idle tasks
- [x] Ticket spinlocks (`spinlock_t`) with fair FIFO acquisition and interrupt-safe variants (`spinlock_acquire_irqsave`)
- [x] Multiprocessor scheduler: distributed per-CPU ready queues, lowest-load placement, and deadlock-free work stealing
- [x] Multicore system calls: `SYS_SMP_INFO` (64), `SYS_SCHED_SETAFFINITY` (65), `SYS_SCHED_GETAFFINITY` (66)
- [x] Diagnostic tooling: `/bin/smp` (core status, ticks, processes) and `/bin/smpstress` (1,000,000 operation multicore stress test)
- [x] System info utility `/bin/sysinfo` updated for multicore topology reporting
- [x] Automated test suite `tools/test_smp.py` passing 100% across `-smp 1`, `-smp 2`, `-smp 4`, and `-smp 8` cores
- [x] Complete regression validation: `test_security.py` (25/25), `test_phase10.py` (24/24), `test_network.py` (100%)
- [x] Comprehensive architectural documentation in `docs/smp.md`

## Phase 12: Modern Storage Architecture, Block Layer & PCI Discovery
- [x] Unified Block Device Layer (`block_device_t`) with standardized bio read/write/flush interfaces
- [x] PCI Bus Enumeration (Configuration Space ports `0xCF8`/`0xCFC`) and device class identification
- [x] AHCI SATA Controller Driver (`drivers/ahci.c`) with ABAR MMIO, HBA port detection, and FIS command issuance
- [x] NVMe Controller Driver (`drivers/nvme.c`) with Admin & I/O submission/completion queue pairs and PRP entry handling
- [x] Partition Discovery Engine (`kernel/storage/partition.c`) supporting both legacy MBR and modern GUID Partition Tables (GPT)
- [x] LRU Block Buffer Cache Subsystem (`kernel/storage/cache.c`) with dirty writeback and synchronized flushing
- [x] Storage Diagnostic Utilities: `/bin/lspci`, `/bin/lsblk`, `/bin/diskinfo`, and `/bin/disktest`
- [x] System Calls: `SYS_PCI_INFO` (71), `SYS_BLK_INFO` (72), `SYS_BLK_READ` (73), `SYS_BLK_WRITE` (74), `SYS_BLK_FLUSH` (75), `SYS_STATVFS` (76)
- [x] Automated test suite `tools/test_phase12.py` passing 28/28 (100%)

## Phase 13: Advanced Networking & Network Security
- [x] Dynamic DHCPv4 Client (`/bin/dhclient`) implementing RFC 2131 (DISCOVER, OFFER, REQUEST, ACK) with transaction-ID validation
- [x] Native IPv6 Subsystem with 128-bit address structures, SLAAC EUI-64 generation, ICMPv6 Echo, NDP (NS/NA), and multicast Ethernet mapping
- [x] TCP Reliability & Flow Control: connection retransmission queues with exponential backoff timers, 3-duplicate-ACK Fast Retransmit, and sliding window advertisement
- [x] Dynamic Kernel Routing Engine (`kernel/net/route.c`) supporting LPM route lookup and dynamic manipulation via `/bin/ip route`
- [x] Packet-Filtering Firewall Subsystem (`kernel/net/firewall.c`) with atomic rule evaluation, connection state tracking, and management via `/bin/nyfw`
- [x] Enhanced DNS resolver parsing `/etc/hosts` and `/etc/resolv.conf` with compression pointer loop defenses in `/bin/nslookup`
- [x] Automated test suite `tools/test_phase13.py` passing 33/33 (100%)

## Phase 14: Security Architecture & Kernel Hardening
- [x] Hardware Memory Protection: `EFER.NXE` enabled, `CR0.WP` supervisor write-protection active, strict $W \oplus X$ policy
- [x] User & Group Credentials: `uid`, `euid`, `gid`, `egid` tracking, privilege drop in `logind` to UID 1000, permission checks across syscalls
- [x] Discretionary Access Control (DAC): VFS inode mode bits (`0777`) verification, root bypass for EUID 0, protected `/etc/shadow` (`0600`)
- [x] Discrete Capability System: 64-bit capability masks authorizing privileged operations (`CAP_NET_ADMIN`, `CAP_FIREWALL`, etc.)
- [x] Syscall Filtering Sandbox (`SYS_SET_SYSCALL_FILTER` 83) with per-process locked allowlist/denylist bitmasks
- [x] Secure Authentication: SHA-256 password hashing in `/etc/shadow` and constant-time comparison (`crypto_memcmp`)
- [x] Security Event Auditing: circular ring buffer capturing denials and unauthorized privilege operations (`/bin/auditlog`)
- [x] Verification: `/bin/sectest` (19/19), `tools/test_phase14.py` (33/33), `tools/test_security.py` (25/25)

## Phase 15: Developer Platform
- [x] C Standard Library (`libnyota`): 16-byte aligned `malloc()`, `free()`, `calloc()`, and `realloc()` with chunk header splitting and bidirectional coalescing
- [x] POSIX-Inspired Directory Streaming: `opendir()`, `readdir()`, `closedir()`, and `rewinddir()` with anonymous union `dirent_t` compatibility
- [x] String & CType Utilities: `strdup()`, `strndup()`, `strtok_r()`, `strtol()`, `atol()`, `memcmp()`, `strspn()`, `strcspn()`, and complete ctype suite
- [x] Formatted I/O & File Operations: `vsnprintf()`, `snprintf()`, `fprintf()`, `dprintf()`, and `access()`
- [x] User Heap Syscall Interface: `SYS_BRK` (85) and `SYS_SBRK` (86) with page-aligned PMM allocation/mapping (`PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE | PAGE_NX`)
- [x] Automatic User Heap Reclamation: physical frames systematically freed on `process_exit()`
- [x] Hardened Static 64-bit ELF Loading: entry point verification in executable segments (`PF_X`) and strict BSS zeroing
- [x] Developer SDK: populated standalone SDK tree (`sdk/`) with headers, libraries, linker script, and `make sdk` target
- [x] Cross-Compiler Driver: `tools/nyotacc` driver with `.py`, shell, and batch wrappers for reproducible native application compilation
- [x] Native Package Manager: `/bin/nyotapkg` command suite (`info`, `install`, `list`, `remove`) and host packager `tools/mkpkg.py`
- [x] Package Security: path traversal sanitization (rejection of `..`, leading `/`, drive colons) and target prefix enforcement (`bin/`, `etc/`, `share/`, `lib/`, `usr/`)
- [x] Package Database: persistent manifest registration under `/var/lib/pkg/installed/<name>.list`
- [x] Native Sample Verification Application: `/bin/devdemo` testing memory allocation, string utilities, directory streaming, and file I/O
- [x] Automated Test Suite: `tools/test_phase15.py` validating system boot, `v1.5.0` uname, `devdemo` execution, and package manager operations
- [x] Version Bump: system metadata, headers, drivers, and user applications updated to `v1.5.0`

## Phase 16: Native Application Ecosystem (v1.6.0)
- [x] Nyota SDK Enhancement: Added `gui.h` and `theme.h` to `sdk/include/`, exported `libgui.a` to `sdk/lib/`, updated `tools/exportsdk.py` and `tools/nyotacc.py`
- [x] Standalone SDK Examples: Created `sdk/examples/hello_cli.c`, `sdk/examples/hello_gui.c`, and `sdk/examples/Makefile`
- [x] SDK Documentation: Updated `sdk/README.md` with complete GUI Toolkit, widget hierarchy, design tokens, and compilation guides
- [x] Native Calculator Application: `/bin/calc` with 64-bit fixed-point arithmetic engine, interactive button grid, decimal handling, sign toggle, clear, and keyboard input
- [x] Native Task Manager Application: `/bin/taskmgr` with process selection, real-time memory usage bars, interactive kill capability, auto-refresh, and keyboard navigation
- [x] Native Network Manager Application: `/bin/netmgr` with tabbed interface (Interfaces, Routing table, DNS config, Firewall toggle & stats, Diagnostics ping)
- [x] Core Native Applications: Full suite in `/bin` — `/bin/term`, `/bin/files`, `/bin/editor`, `/bin/calc`, `/bin/sysmon`, `/bin/taskmgr`, `/bin/settings`, `/bin/netmgr`, `/bin/about`
- [x] Desktop Launcher & Application Management: Integrated all applications into desktop menu launcher with shortcut badges, process spawning, and window server event dispatch
- [x] Application Crash & Lifecycle Resilience: Isolated Ring 3 address spaces, safe window teardown on process exit, capability-checked process termination
- [x] Automated Test Suite: `tools/test_phase16.py` verifying system boot, `v1.6.0` uname metadata, native application presence, background process spawning, process management, package manager integration, and SDK headers
- [x] Version Bump: System metadata, kernel headers (`NYOTA_OS_VERSION "1.6.0"`), syscalls, init supervisor, drivers, desktop, and user applications updated to `v1.6.0`

## Phase 17: Virtualization & Hypervisor (v1.7.0)
- [x] Hardware Support & Vendor Detection: Architecture-aware hypervisor engine with runtime detection for Intel VT-x (`kernel/vmm/vmx.c`) and AMD-V SVM (`kernel/vmm/svm.c`)
- [x] Intel VT-x Driver: VMX capability checking, `IA32_FEATURE_CONTROL_MSR` verification, 4 KiB aligned VMXON region, VMCS setup with full host/guest register state, and VM-exit handling loop
- [x] AMD-V (SVM) Driver: `EFER.SVME` activation, 4 KiB host save area (`MSR_VM_HSAVE_PA`), VMCB control area with instruction interception (CPUID, HLT, VMRUN, VMMCALL), and `clgi`/`vmrun`/`stgi` loop
- [x] Guest Memory Management & Isolation: Extended Page Tables (EPT) and Nested Page Tables (NPT) mapping guest physical addresses (GPA) to host physical addresses (HPA), with memory boundary isolation
- [x] Virtual CPU & Guest Execution: Minimal 64-bit flat guest environment (`kernel/vmm/guest.c`) with PML4/PDPT/PD identity tables, verifying CPUID interception, isolated memory write to GPA `0x1000` (`0xCAFEBABE`), and clean HLT termination
- [x] Hypervisor System Call: `SYS_VMM_OP` (87) supporting `VMM_OP_GET_INFO`, `VMM_OP_RUN_VM`, and `VMM_OP_GET_STATS`
- [x] Userspace Hypervisor Utility: Native `/bin/vmm` application supporting `info`, `run`, `stats`, `test`, and `help` commands
- [x] Desktop Settings Integration: System tab updated with live virtualization hardware capability detection and operational status
- [x] System Version Bump: Updated system metadata, kernel headers (`NYOTA_OS_VERSION "1.7.0"`), userland utilities (`uname`, `init`, `sysinfo`, `sysmon`, `about`, `desktop`, `term`), and drivers to `v1.7.0`
- [x] Automated Test Suite: `tools/test_phase17.py` validating system boot, `v1.7.0` metadata, `/bin/vmm` discovery, hardware capability discovery, VM lifecycle execution, guest memory isolation, and host kernel stability
- [x] Documentation: Created `docs/virtualization.md` detailing Intel VT-x and AMD-V architecture, VMCB/VMCS layout, EPT/NPT, syscall API, and `/bin/vmm` usage

## Phase 18: Developer Tools & Kernel Observability (v1.8.0)
- [x] Kernel Debugging: Enhanced crash diagnostics and panic reporting with running process context (PID, name, EUID), register dumps, top-of-stack dumps, and dual-output mirroring to VGA console and serial COM1
- [x] Symbol Resolution & Stack Unwinding: `kernel/debug/ksym.c` symbol table resolving instruction pointers to symbol names and offsets, with `RBP` stack frame walking up to 16 call levels
- [x] GDB Remote Debugging Integration: `tools/gdbinit_nyota` configuration script automating symbol loading and breakpoint hooks for QEMU GDB stub
- [x] System-Call Tracing (`/bin/nytrace`): Intercepts syscall operations, arguments, return codes, and microsecond latencies via `SYS_TRACE_OP` (88); enforces security boundaries (prohibits tracing PID 1; checks EUID/root; protects sensitive payload data)
- [x] Process & Scheduler Monitoring (`/bin/pstree`): Visualizes hierarchical process trees with execution states, CPU assignments, memory usage, and context switch counts
- [x] Memory Diagnostics (`/bin/memdiag`): Allocator integrity checking via `SYS_MEM_DIAG` (89), block header canary magic (`0xAA55AA55`) validation, chunk classification, and physical memory frame accounting
- [x] Network Diagnostics & Packet Tap (`/bin/pktmon`): Network interface statistics (RX/TX packets and bytes), active TCP/UDP/UNIX socket diagnostics, and ring-buffer packet header tapping via `SYS_PKT_MON` (90) restricted to `CAP_NET_ADMIN` / EUID 0
- [x] Performance Profiling (`/bin/nyprof`): Hardware TSC cycle measurement of syscall latencies and call frequencies via `SYS_PROF_OP` (91) with low-overhead sampling
- [x] Automated Test Suite: `tools/test_phase18.py` validating system boot, `v1.8.0` metadata, binary presence, process tree visualizer, heap integrity verification, network interface statistics, syscall tracing, security boundary enforcement, and host kernel stability
- [x] System Version Bump: Updated system metadata, kernel headers (`NYOTA_OS_VERSION "1.8.0"`), userland utilities (`uname`, `init`, `sysinfo`, `sysmon`, `about`, `desktop`, `term`), and graphics driver to `v1.8.0`
- [x] Documentation: Created `docs/observability.md` detailing debugging workflows, symbol table unwinding, GDB integration, tracing ABI, and userspace diagnostics tools

