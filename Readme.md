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
  <img src="https://img.shields.io/badge/Phase-8%3A%20IPC%2C%20Security%20Hardening%20%26%20Process%20Isolation-success?style=for-the-badge">
  <img src="https://img.shields.io/badge/Version-v0.8.0-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Architecture-x86__64-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/Language-C%20%2B%20x86__64%20ASM-00599C?style=for-the-badge&logo=c&logoColor=white">
  <img src="https://img.shields.io/badge/Toolchain-NASM%20%7C%20GCC%20%7C%20Binutils-111111?style=for-the-badge">
  <img src="https://img.shields.io/badge/Emulator-QEMU-FF6600?style=for-the-badge">
</p>

<p align="center">
  <a href="#-phase-8-overview-ipc-security-hardening--process-isolation">Phase 8 Overview</a>
  ·
  <a href="#-phase-7-overview-networking-tcpip--sockets">Phase 7 Overview</a>
  ·
  <a href="#-phase-6-overview-filesystem-elf-loader--real-userland">Phase 6 Overview</a>
  ·
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

# 🔒 Phase 8 Overview: IPC, Security Hardening & Process Isolation

**Current Status:** **Phase 8 — IPC, Security Hardening & Process Isolation** (Completed)

Phase 8 fortifies Nyota OS with a defense-in-depth security model, robust process isolation, safe inter-process communication, and resilient failure recovery. The kernel now assumes all user input, pointers, lengths, file descriptors, and IPC requests are untrusted. Malformed requests are rejected cleanly with `-EFAULT` / `-EINVAL`, user CPU faults (#DE, #UD, #GP, #PF) are converted into POSIX signals without kernel panics, and isolated processes communicate safely via anonymous pipes and page-aligned shared memory:

```text
                                USERLAND (Ring 3)
         ┌──────────────────────────────┼──────────────────────────────┐
         ▼                              ▼                              ▼
  Process Control                      IPC                          Signals
   (waitpid, kill)             (Pipes, Shared Mem)             (sigaction, masks)
         │                              │                              │
         └──────────────────────────────┼──────────────────────────────┘
                                        ▼
                               SYSCALL BOUNDARY
                          (Vector 0x80 Dispatcher)
                                        │
                         ┌──────────────┴──────────────┐
                         ▼                             ▼
                 Validation Engine             Security Checks
              (Pointer / Buffer Check)     (UID/GID & Capabilities)
              (Integer Overflow Guard)     (Resource Limits Check)
                         │                             │
                         └──────────────┬──────────────┘
                                        ▼
                               KERNEL SUBSYSTEMS
         ┌──────────────────────────────┼──────────────────────────────┐
         ▼                              ▼                              ▼
  Memory Isolation                  Scheduler                         VFS
  (Stack Guard Pages)         (Parent/Child Trees)              (Pipes & Nodes)
  (Shared Memory Maps)        (Zombies & Orphans)               (Chmod & Chown)
         │                              │                              │
         └──────────────────────────────┼──────────────────────────────┘
                                        ▼
                               Hardware (Ring 0)
```

---

### Key Subsystems Delivered in Phase 8:

1. **Process Hierarchy & Safe Lifecycle Management (`kernel/process/process.c`)**:
   - **Parent/Child Relationships**: Every PCB (`process_t`) tracks its `parent_pid`, pointer to `parent`, and a linked list of `children`. Provides `process_add_child()` and `process_remove_child()`.
   - **Non-Spinning Process Waiting (`waitpid`)**: `process_waitpid()` integrates with scheduler wait queues. If the target child is still running, the parent transitions to `PROCESS_SLEEPING` and yields CPU time without spinning.
   - **Zombie Process Lifecycle**: When a child process terminates, it transitions to `PROCESS_TERMINATED` (zombie), retaining its PID, exit status, and termination signal. Upon `waitpid()`, the parent reaps the exit status and frees the remaining PCB resources.
   - **Automatic Orphan Adoption**: When a parent process terminates while children are alive, all orphaned children are automatically reparented to PID 1 (`/init`), preventing un-reapable zombies.
   - **Clean Process Exit (`process_exit`)**: Systematically closes all open file descriptors and sockets, releases active shared memory attachments, unmaps user memory regions, sends `SIGCHLD` to the parent, wakes waiting parents, and logs termination diagnostics.

2. **POSIX Signal Subsystem & Return-to-User Delivery Gate (`kernel/process/signal.c`)**:
   - **Signal Representation**: 64-bit signal bitmasks (`signal_set_t`) for `pending` and `blocked` signals.
   - **Core Signals**: `SIGTERM` (15), `SIGKILL` (9), `SIGSTOP` (19), `SIGCONT` (18), `SIGCHLD` (17), `SIGSEGV` (11), `SIGILL` (4), `SIGFPE` (8), `SIGPIPE` (13).
   - **Actions & Handlers**: Supports `SIG_DFL`, `SIG_IGN`, and custom user handlers registered via `sys_sigaction`. Enforces POSIX rules: `SIGKILL` and `SIGSTOP` cannot be caught or ignored.
   - **Safe Delivery Gate**: Signal checks occur exclusively at safe execution boundaries—right before returning to Ring 3 in `syscall_dispatch`. If a pending, unmasked signal is present, default actions (termination, ignore, stop, continue) or user handler frames are executed safely.

3. **CPU Exception-to-Signal Dispatch & Crash Containment (`kernel/arch/x86_64/exceptions.c`)**:
   - User-mode processor exceptions are caught by the kernel IDT stubs and converted directly into signals:
     - Divide by Zero (`#DE`, Vector 0) -> `SIGFPE`
     - Invalid Opcode (`#UD`, Vector 6) -> `SIGILL`
     - General Protection Fault (`#GP`, Vector 13) -> `SIGSEGV`
     - Page Fault (`#PF`, Vector 14) -> `SIGSEGV`
   - **Crash Containment**: A crashing Ring 3 process (e.g., dereferencing `0x10` in `/bin/crash` or executing an illegal instruction) is cleanly terminated with a security diagnostic log (`[SEC] PID X exception: page fault, signal: SIGSEGV`). The kernel, interactive shell, and background services remain 100% stable and operational.

4. **Anonymous Pipes & Stream Redirection (`kernel/ipc/pipe.c`, `kernel/fs/vfs.c`)**:
   - **Circular Buffer Architecture**: Bounded 4096-byte circular FIFO buffer (`pipe_t`) protected by separate read and write positions, byte counts, and reference counters.
   - **Blocking I/O**: Reads on empty pipes sleep until data is written; writes to full pipes sleep until space is drained.
   - **EOF & Broken Pipe Semantics**: When all writers close, readers receive `0` (EOF). When all readers close, writers receive `-EPIPE` and a `SIGPIPE` signal.
   - **VFS Integration**: Pipes are exposed as standard file descriptors (`FILE_TYPE_PIPE`). Supports `sys_pipe` and `sys_dup2`.
   - **Shell Pipelines**: Enables pipeline streaming in `/bin/sh` (`ls | cat`, `cmd1 | cmd2`) and standard stream redirection (`>`, `<`).

5. **Controlled Shared Memory IPC (`kernel/ipc/shm.c`)**:
   - **System V-Style Shared Memory**: Implements `sys_shmget`, `sys_shmat`, `sys_shmdt`, and `sys_shmctl`.
   - **Page-Aligned Virtual Aperture**: Maps shared segments starting at `0x0000008000180000ULL`, dynamically mapping physical frames into calling process page tables with user read/write attributes.
   - **Security & Permissions**: Shared segments enforce owner UID/GID and permission mode flags (`0666`, `0600`). Non-owners without permission receive `-EACCES`.
   - **Reference Counting & Automatic Cleanup**: Segments track active attachment counts. Upon process exit, all attached segments are automatically detached and unmapped.

6. **User Identity, Permissions & Capabilities (`kernel/security/capability.c`)**:
   - **UID/GID Tracking**: Every process possesses effective UID and GID (PID 1 / root: UID 0; normal user: UID 1000).
   - **Capability Model**: Granular 64-bit capability bitmask:
     - `CAP_NET_ADMIN` (1 << 0): Network interface configuration and routing.
     - `CAP_NET_RAW` (1 << 1): Raw ICMP and packet socket creation.
     - `CAP_SYS_ADMIN` (1 << 2): System configuration and hardware control.
     - `CAP_IPC_OWNER` (1 << 3): Override IPC ownership and permissions.
     - `CAP_KILL` (1 << 4): Signal arbitrary processes regardless of UID.
   - **Filesystem Permissions**: VFS checks read, write, and execute permissions on inodes (`vfs_chmod`, `vfs_chown`). System directories (`/bin`, `/etc`, `/kernel`) are protected against unauthorized modification.

7. **Per-Process Resource Limits (`include/process.h`)**:
   - Each process enforces configurable resource limits (`limits`):
     - `max_memory`: Upper bound on allocated address space.
     - `max_open_files`: Enforced maximum open file descriptors (default: 16).
     - `max_processes`: Limit on child processes.
     - `max_sockets`: Limit on open network sockets.
   - Limit violations return clean errors (`-EMFILE`, `-ENOMEM`) rather than exhausting kernel resources.

8. **Kernel Hardening & Memory Isolation**:
   - **Centralized Pointer Validation**: `validate_user_pointer()` and `validate_user_buffer()` guarantee that all userspace-supplied buffers reside strictly below `USER_SPACE_END` (`0x0000800000000000ULL`) and do not wrap around address boundaries.
   - **Integer Overflow Protection**: Checked arithmetic helpers (`size_add_overflow`, `size_mul_overflow`) prevent buffer length and offset overflow exploits.
   - **Stack Guard Pages**: An unmapped 4 KiB guard page is placed immediately below the user stack base (`0x800000F000`). Stack overflow attempts trigger an immediate, controlled page fault (`SIGSEGV`) rather than corrupting adjacent mappings.
   - **Hardware-Seeded PRNG**: SplitMix64 pseudorandom generator (`kernel_random()`) seeded via CPU `rdtsc`, exposed to userspace via `sys_getrandom` (Vector 43).
   - **ASLR Foundation**: Randomized user stack base and shared memory attachment offsets.
   - **Security Audit Logging**: Kernel security event mechanism (`security_log()`) generating structured audit records for invalid syscalls, privilege violations, and Ring 3 exceptions.

9. **New System Calls in Phase 8 (Vectors 26..43)**:

| Vector | Syscall Name | Description |
| :---: | :--- | :--- |
| `26` | `SYS_WAITPID` | Wait for child process state change or termination |
| `27` | `SYS_KILL` | Send a signal to a process |
| `28` | `SYS_SIGACTION` | Examine and change a signal action |
| `29` | `SYS_SIGPROCMASK` | Examine and change blocked signals |
| `30` | `SYS_PIPE` | Create an anonymous unidirectional data channel |
| `31` | `SYS_DUP2` | Duplicate an open file descriptor onto another |
| `32` | `SYS_SHMGET` | Allocates a System V shared memory segment |
| `33` | `SYS_SHMAT` | Attach shared memory segment into process address space |
| `34` | `SYS_SHMDT` | Detach shared memory segment from process address space |
| `35` | `SYS_SHMCTL` | Control shared memory segment (stat, remove) |
| `36` | `SYS_GETUID` | Get real user identity |
| `37` | `SYS_SETUID` | Set real user identity (privileged) |
| `38` | `SYS_GETGID` | Get real group identity |
| `39` | `SYS_SETGID` | Set real group identity (privileged) |
| `40` | `SYS_CHMOD` | Change file permission mode bits |
| `41` | `SYS_CHOWN` | Change file owner and group |
| `42` | `SYS_SECINFO` | Query kernel security feature status & diagnostic counters |
| `43` | `SYS_GETRANDOM` | Obtain cryptographic/system entropy bytes |

10. **Ring 3 Security & Diagnostic Suite**:
    - **`/bin/secinfo`**: Displays kernel security posture (isolation, guard pages, ASLR, capabilities, active IPC objects).
    - **`/bin/kill`**: User signal utility supporting `-TERM`, `-KILL`, `-STOP`, `-CONT`.
    - **`/bin/ipctest`**: Automated validation of pipe communication, blocking I/O, shared memory, and permission enforcement.
    - **`/bin/memtest`**: Validates user vs. kernel memory isolation, foreign address space protection, and unmapped access faulting.
    - **`/bin/crash`**: Intentionally triggers user-space `#PF` (null dereference) to demonstrate clean exception-to-signal crash containment.
    - **`/bin/stressproc`**: Spawns multiple processes communicating through pipes to stress lifecycle and resource management.
    - **`/bin/ps`**: Extended to display `PPID`, `UID`, process states, and an interactive process tree (`ps tree`).
    - **`/bin/sh`**: Enhanced with command pipelines (`|`), background job execution (`&`), and stream redirection (`>`, `<`).

---

# 🌐 Phase 7 Overview: Networking, TCP/IP & Sockets

**Current Status:** **Phase 7 — Networking, TCP/IP & Sockets** (Completed)

Phase 7 delivers a complete, modular, and standards-compliant networking subsystem that enables Nyota OS to communicate over virtual Ethernet networks and with the outside world. From hardware discovery of PCI network adapters through an Intel 82540EM (E1000) driver, to an RFC-compliant protocol stack (Ethernet II, ARP, IPv4, ICMP, UDP, TCP) and a full BSD-style Socket API integrated into the VFS and preemptive scheduler:

```text
                    User Applications (/bin/ping, /bin/netcat, /bin/echo-server, etc.)
                                           │
                                           ▼
                                      Socket API
                           (libnyota: socket, bind, listen, ...)
                                           │
                                           ▼
                                    Network Syscalls
                              (SYS_SOCKET..SYS_SHUTDOWN)
                                           │
                                           ▼
                                      Socket Layer
                               (wait queues, circular FIFO)
                                           │
                              ┌────────────┼────────────┐
                              ▼            ▼            ▼
                             TCP          UDP          ICMP
                              │            │            │
                              └───────┬────┴────────────┘
                                      ▼
                                     IPv4
                              (Checksums, Routing)
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
                        ARP                     Loopback
                  (Cache, Request/Reply)       (127.0.0.1)
                         │
                         ▼
                      Ethernet
                  (Ethernet II Frames)
                         │
                         ▼
                     NIC Driver
                 (Intel 82540EM / E1000)
                         │
                         ▼
                    QEMU / SLIRP
             (10.0.2.15 -> Gateway 10.0.2.2)
                         │
                         ▼
                    Host Network
```

---

### Key Subsystems Delivered in Phase 7:

1. **PCI Bus Subsystem & Enumeration (`kernel/drivers/pci/pci.c`)**:
   - Accesses standard PCI configuration space via I/O ports `0xCF8` (Address) and `0xCFC` (Data).
   - Recursively enumerates buses, device slots, and functions, extracting Vendor ID, Device ID, Class, Subclass, and Base Address Registers (BARs).
   - Detects the Intel 82540EM Gigabit Ethernet Controller (`0x8086:0x100E`, Class `0x02:0x00`), locating its 128 KiB Memory-Mapped I/O BAR0 (`0xFEBC0000`) and assigned interrupt line (IRQ 11).

2. **Intel 82540EM (E1000) Gigabit NIC Driver (`kernel/drivers/net/e1000.c`)**:
   - **MMIO Access**: Maps BAR0 into kernel virtual memory using 4-level paging (`paging_map_page()`) to access device control, status, and descriptor ring registers.
   - **Physical DMA Descriptor Rings**:
     - Allocates physical frames via `pmm_alloc_page()` to ensure physical-address DMA safety.
     - Sets up 32 Receive descriptors (`e1000_rx_desc_t`) and 8 Transmit descriptors (`e1000_tx_desc_t`).
     - Configures receive control (`RCTL`: Broadcast accept, 2048-byte buffer size, strip CRC) and transmit control (`TCTL`: Enable, Pad short packets, Collision threshold 15, Collision distance 64).
   - **MAC Address Retrieval**: Reads factory MAC `52:54:00:12:34:56` directly from EEPROM registers and programs the Receive Address Filter (`RAL[0]` / `RAH[0]`).
   - **Dual-Mode Packet Reception**: Interrupt-driven handling on IRQ 11 (`dispatcher_register_handler(IRQ_TO_VECTOR(11), e1000_irq_handler)`) paired with deferred worker polling (`e1000_poll_rx()`) ensuring zero packet loss during heavy socket traffic.

3. **Packet Buffer Architecture (`packet_t`, `kernel/net/packet.c`)**:
   - Flexible packet abstraction with reserved headroom and tailroom.
   - Dynamic encapsulation/decapsulation primitives: `packet_alloc()`, `packet_free()`, `packet_push_header()`, and `packet_pull_header()`.
   - Strict bounds validation preventing buffer overruns or malformed packet panics.

4. **Ethernet II Layer & Network Device Model (`kernel/net/ethernet.c`, `kernel/net/netdev.c`)**:
   - Universal network device interface (`net_device_t`) abstracting physical Ethernet (`eth0` at `10.0.2.15`, MTU 1500) and virtual loopback (`lo` at `127.0.0.1`, MTU 65536).
   - Ethernet II frame encapsulation, EtherType demultiplexing (`0x0800` IPv4, `0x0806` ARP), and frame validation (length, destination MAC, source MAC).

5. **Dynamic ARP (Address Resolution Protocol, `kernel/net/arp.c`)**:
   - Implements RFC 826 ARP requests and replies.
   - Dynamic ARP Cache storing IPv4-to-MAC associations with automated query broadcast, cache updates, and entry timeouts.
   - Transparent resolution for outgoing IPv4 packets destined for local subnet hosts.

6. **IPv4 & Routing Subsystem (`kernel/net/ipv4.c`, `kernel/net/route.c`)**:
   - RFC 791 IPv4 parser and transmitter with RFC 1071 16-bit one's complement checksum calculation and verification.
   - Protocol dispatching: `1` (ICMP), `6` (TCP), `17` (UDP).
   - Longest-prefix-match routing table with route addition and lookup (`route_lookup()`):
     - `127.0.0.0/8` -> `lo` (127.0.0.1)
     - `10.0.2.0/24` -> `eth0` (local subnet)
     - `0.0.0.0/0` -> Gateway `10.0.2.2` via `eth0`

7. **ICMP Diagnostics & Echo Engine (`kernel/net/icmp.c`)**:
   - Implements ICMP Echo Request (`Type 8`) and Echo Reply (`Type 0`) with checksum verification.
   - Automatic kernel Echo Reply generation for incoming pings.
   - Raw ICMP socket support enabling userspace diagnostic utilities (`/bin/ping`).

8. **User Datagram Protocol (UDP, `kernel/net/udp.c`)**:
   - RFC 768 UDP header construction, parsing, and IPv4 pseudo-header checksum validation.
   - Port multiplexing delivering incoming datagrams into per-socket FIFO buffers.
   - Supports connectionless messaging for applications such as DNS (`nslookup`).

9. **RFC 793 TCP Engine & State Machine (`kernel/net/tcp.c`)**:
   - Explicit connection state machine: `CLOSED`, `LISTEN`, `SYN_SENT`, `SYN_RECEIVED`, `ESTABLISHED`, `FIN_WAIT_1`, `FIN_WAIT_2`, `CLOSE_WAIT`, `LAST_ACK`, `TIME_WAIT`.
   - Full 3-way handshake: Client `SYN` -> Server `SYN+ACK` -> Client `ACK`.
   - Bidirectional stream transfer with sequence numbering, acknowledgment tracking, receive window flow control (`8192` bytes advertised), and PSH flag propagation.
   - Graceful termination via `FIN`/`ACK` sequences and immediate `RST` generation for invalid packets or unopened ports.
   - Checksum calculation with RFC 793 pseudo-header and QEMU SLIRP checksum-offload tolerance.

10. **BSD Socket Abstraction & VFS Integration (`kernel/net/socket.c`, `kernel/fs/vfs.c`)**:
    - Sockets implemented as first-class kernel objects (`socket_t`) and exposed through standard VFS file descriptors as `FILE_TYPE_SOCKET`.
    - Ten new network system calls (Vectors 16..25):
      - `SYS_SOCKET` (16), `SYS_BIND` (17), `SYS_LISTEN` (18), `SYS_ACCEPT` (19), `SYS_CONNECT` (20), `SYS_SEND` (21), `SYS_RECV` (22), `SYS_SENDTO` (23), `SYS_RECVFROM` (24), `SYS_SHUTDOWN` (25).
    - Circular FIFO buffering (`SOCKET_BUFFER_SIZE = 8192`) preventing unbounded memory growth.
    - Sockets transparently integrate with `vfs_read()` and `vfs_write()` for standard stream redirection.

11. **Non-Spinning Process Sleeping & Network Wait Queues**:
    - Complete integration with the Phase 5 preemptive scheduler.
    - Blocking network calls (`accept()`, `recv()`, `connect()`) transition the calling process to `PROCESS_SLEEPING` and enroll it in the socket's wait queue.
    - Incoming packet processing immediately wakes sleeping tasks to `PROCESS_READY`, eliminating CPU-wasting spin loops.

12. **Real Userspace Network Utilities & Services**:
    - **`/bin/ifconfig`**: Displays network interface hardware and IP configuration, netmask, broadcast, and MTU.
    - **`/bin/ping`**: Network latency diagnostic tool measuring round-trip times over both loopback and QEMU gateway.
    - **`/bin/netstat`**: Network status tool enumerating active TCP, UDP, and RAW sockets.
    - **`/bin/nslookup`**: DNS resolution tool querying gateway DNS (`10.0.2.3:53`) over UDP for IPv4 `A` records.
    - **`/bin/netcat`**: Interactive TCP stream client for connecting to arbitrary hosts and ports.
    - **`/bin/echo-server`**: Ring 3 TCP echo daemon listening on port `8080`, supporting simultaneous internal and host connections.

---

# 💾 Phase 6 Overview: Filesystem, ELF Loader & Real Userland

**Current Status:** **Phase 6 — Filesystem, ELF Loader & Real Userland** (Completed)

Phase 6 marks the defining architectural milestone where Nyota OS transitions from executing kernel-embedded test code into a **genuine, autonomous operating system**. The kernel now detects and reads from physical ATA block devices, mounts its own native filesystem (**NyotaFS**), maintains an extensible Virtual Filesystem (**VFS**) layer with per-process file descriptors, dynamically parses and maps **ELF64 executables**, launches **`/init` (PID 1)**, and hosts an interactive **Ring 3 user-space shell (`/bin/sh`)** alongside modular utility programs:

```text
                                 NYOTA OS SUBSYSTEM ARCHITECTURE
                                                │
                                            Hardware
                                     (ATA Controller / Disk)
                                                │
                                                ▼
                                       Block Device Layer
                                     (block_device_t API)
                                                │
                                                ▼
                                         Storage Driver
                                         (ATA PIO Driver)
                                                │
                                                ▼
                                             NyotaFS
                                   (Superblock / Inodes / Extents)
                                                │
                                                ▼
                                     Virtual Filesystem (VFS)
                                 (File Objects / Descriptors / Devs)
                                                │
                                                ▼
                                        System Call Engine
                                   (Vector 0x80 / Dispatcher)
                                                │
                       ┌────────────────────────┴────────────────────────┐
                       ▼                                                 ▼
                  ELF64 Loader                                  Process Management
             (Segments / Permissions)                        (Address Spaces / Contexts)
                       │                                                 │
                       └────────────────────────┬────────────────────────┘
                                                │
                                                ▼
                                         Userland (Ring 3)
                            ┌───────────────────┼───────────────────┐
                            ▼                   ▼                   ▼
                          /init               /bin/sh           Utilities
                         (PID 1)              (Shell)      (hello, echo, ls, cat, ps, test)
```

---

### Key Subsystems Delivered in Phase 6:

1. **Generic Block Device Abstraction & Storage Driver**:
   - `block_device_t` structure providing clean read/write primitives (`block_device_read()`, `block_device_write()`) decoupled from underlying hardware drivers.
   - **ATA PIO Driver** (`kernel/storage/ata.c`):
     - Hardware initialization and drive interrogation via `ATA IDENTIFY`.
     - LBA28 512-byte sector reads and writes on Primary and Secondary ATA channels.
     - Dual-disk configuration: Primary Master (`build/nyota.img` boot disk) and Primary Slave (`build/nyota-data.img` persistent filesystem disk).

2. **Native Filesystem (NyotaFS)**:
   - On-disk layout: `Superblock` -> `Block Bitmap` -> `Inode Bitmap` -> `Inode Table` -> `Data Blocks`.
   - **Superblock (`nyota_superblock_t`)**: Magic `0x4E594F53` ("NYOS"), version 1, 1024-byte block size, 16,384 total blocks (16 MiB disk), inode table bounds, and root inode identifier (inode 1).
   - **Inode Allocation & Management (`nyota_inode_t`)**: 128-byte inodes tracking permissions, file sizes, creation/modification timestamps, 8 direct data blocks, and 1 indirect block pointer.
   - **Directory Mapping (`nyota_dirent_t`)**: Mapping file and directory names to inode numbers with standard `.` and `..` support.
   - **Path Resolution Engine (`nyotafs_resolve_path`)**: Resolves hierarchical paths (e.g. `/bin/sh`, `/etc/nyota.conf`, `/home/welcome.txt`) across nested directory levels.

3. **Virtual Filesystem (VFS) Layer & Kernel File Objects**:
   - Generic VFS abstraction dispatching file system operations (`vfs_open`, `vfs_close`, `vfs_read`, `vfs_write`, `vfs_seek`, `vfs_stat`, `vfs_readdir`, `vfs_mkdir`).
   - Reference-counted global file table tracking open file instances (`file_t`), current seek offsets, and access modes (`O_RDONLY`, `O_WRONLY`, `O_CREAT`, `O_APPEND`).
   - **Per-Process File Descriptors**: Process structures contain isolated descriptor tables initialized with standard streams:
     - `FD 0 (stdin)`: Connected to `/dev/console` (interactive keyboard and serial input).
     - `FD 1 (stdout)`: Connected to `/dev/console` (VGA terminal mirrored to COM1 serial).
     - `FD 2 (stderr)`: Connected to `/dev/console` (diagnostic logging).
   - **Device Files**: Direct VFS routing for `/dev/console` and `/dev/null`.

4. **Freestanding ELF64 Loader**:
   - Zero-dependency ELF parser validating ELF magic (`0x7F 'E' 'L' 'F'`), 64-bit class, LSB endianness, and `EM_X86_64` architecture.
   - Segment loader inspecting Program Headers for `PT_LOAD` segments, dynamically allocating physical frames, mapping them into user virtual memory at their target `p_vaddr`, and copying executable code/data from disk.
   - Segment permissions enforced: readable (`PF_R`), writable (`PF_W`), and executable (`PF_X`).
   - Automatic BSS zeroing for uninitialized global and static data.
   - Extraction of entry point address (`e_entry`) to set the initial instruction pointer (`RIP`).

5. **Process Creation from Disk & Execution (`exec` / `spawn`)**:
   - `process_create_from_elf()` / `process_spawn_elf()`:
     - Opens ELF binary from disk, creates an isolated 4-level PML4 address space, loads segments, maps a 16 KiB user stack, fabricates an interrupt frame, and enqueues the process in the scheduler.
     - Places command-line arguments (`argc`, `argv`) onto the user stack according to System V ABI standards.
   - `process_exec()`: In-place address-space replacement allowing a process to execute a new program image.
   - Process termination cleanup: closes open file descriptors, reclaims address spaces, records exit status, and wakes parent tasks waiting in `waitpid()`.

6. **Userspace Standard C Library (`libnyota`)**:
   - Freestanding userland runtime compiled into `build/libnyota.a`:
     - `crt0.asm`: Assembly entry stub setting up the stack frame, calling `main(argc, argv)`, and invoking `exit(status)` syscall upon return.
     - `syscall.c`: Vector 0x80 syscall wrappers (`open`, `close`, `read`, `write`, `seek`, `stat`, `getdents`, `mkdir`, `exec`, `spawn`, `waitpid`, `getpid`, `yield`, `sleep`, `exit`).
     - `io.c`: Formatted `printf`, `putchar`, `puts`, `getchar`, and line-buffered `getline`.
     - `string.c`: String and memory utilities (`strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `memcpy`, `memset`).

7. **Real Userland Environment & Interactive Shell**:
   - **`/init` (PID 1)**: First real user process launched by the kernel after mounting root. Executes the automated self-test suite and supervises the user shell.
   - **`/bin/sh`**: Interactive Ring 3 command shell supporting commands:
     - `help`: Displays available shell commands.
     - `ls [dir]`: Directory inspection via VFS directory enumeration.
     - `cat <file>`: Reads and displays file contents from persistent storage.
     - `echo [args...]`: Echoes command-line arguments to stdout.
     - `ps`: Queries active system processes, states, and execution metrics.
     - `pwd`: Displays current working directory.
     - `clear`: Clears terminal screen.
     - `run <path> [args]`: Executes arbitrary ELF64 programs from disk.
     - Automatic PATH resolution: typing `hello` resolves to `/bin/hello`.
   - **Modular User Binaries**: `/bin/hello`, `/bin/echo`, `/bin/ls`, `/bin/cat`, `/bin/ps`, and `/bin/test`.

8. **User Memory Validation & Security Protection**:
   - `user_validate_pointer()`: Ensures all user-supplied pointers reside strictly within user space (`0x8000000000` to `0x8000000000 + 512GB`) and refer to present user pages before dereferencing.
   - Safe data transfers: `copy_from_user()`, `copy_to_user()`, and `copy_string_from_user()` reject illegal addresses with `-EFAULT` without triggering kernel crashes.
   - Attempted user access to kernel memory generates hardware `#PF` exceptions, terminating the offending user process while keeping the kernel and remaining processes fully operational.

---

# 🔄 Phase 5 Overview: Multitasking, Scheduler & Process Management

**Status:** **Phase 5 — Multitasking, Scheduler & Process Management** (Completed)

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
│   │       ├── exceptions.c # CPU exception handlers (0..31) & Ring 3 signal dispatch
│   │       ├── pic.c        # 8259 PIC initialization, IRQ remapping, EOI
│   │       ├── paging.c     # 4-level paging (PML4, PDPT, PD, PT), map/unmap, guard pages
│   │       └── syscall.c    # Vector 0x80 syscall dispatcher (44 syscalls, vectors 0..43)
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
│   ├── process/
│   │   ├── process.c        # Process control blocks (PCB), parent/child, waitpid, zombies
│   │   ├── scheduler.c      # Preemptive round-robin scheduler & time-slice preemption
│   │   ├── signal.c         # POSIX signal delivery, registration, and exception mapping
│   │   └── usertest.c       # Ring 3 security tests & privilege violation verification
│   │
│   ├── ipc/
│   │   ├── pipe.c           # Anonymous pipe circular buffer, blocking I/O, EOF, SIGPIPE
│   │   └── shm.c            # Controlled shared memory segments, permissions, refcounts
│   │
│   ├── security/
│   │   ├── capability.c     # User identity, capability bitmasks, and privilege checks
│   │   ├── random.c         # Hardware-seeded SplitMix64 PRNG and sys_getrandom
│   │   └── security.c       # Kernel security posture querying and audit logging
│   │
│   ├── storage/
│   │   ├── ata.c            # ATA PIO disk controller driver (LBA28 read/write)
│   │   └── block.c          # Generic block device abstraction
│   │
│   ├── fs/
│   │   ├── nyotafs.c        # Native filesystem implementation (inodes, extents, dirs)
│   │   └── vfs.c            # Virtual filesystem (open, read, write, close, pipes, sockets)
│   │
│   ├── elf/
│   │   └── elf.c            # Freestanding 64-bit ELF executable parser & segment loader
│   │
│   ├── drivers/
│   │   ├── pci/
│   │   │   └── pci.c        # PCI configuration space enumeration & device discovery
│   │   └── net/
│   │       └── e1000.c      # Intel 82540EM Gigabit Ethernet NIC driver (MMIO, DMA rings)
│   │
│   └── net/
│       ├── netdev.c         # Universal network device layer (eth0, lo)
│       ├── packet.c         # Dynamic packet buffer management (headroom/tailroom)
│       ├── ethernet.c       # Ethernet II frame parser and serializer
│       ├── arp.c            # Dynamic ARP request/reply and cache management
│       ├── ipv4.c           # IPv4 packet processor & RFC 1071 internet checksum
│       ├── route.c          # Longest-prefix-match IP routing table
│       ├── icmp.c           # ICMP Echo Request and Echo Reply engine
│       ├── udp.c            # UDP header processing, checksum, and port multiplexing
│       ├── tcp.c            # RFC 793 TCP state machine (handshake, sequence, close)
│       └── socket.c         # BSD Socket abstraction, circular FIFO, and wait queues
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
│   ├── syscall.h            # Syscall numbers (0..43), ABI constants, pointer validation
│   ├── process.h            # Process hierarchy, states, limits, lifecycle APIs
│   ├── signal.h             # POSIX signal definitions, signal_set_t, sigaction
│   ├── ipc/                 # Pipe circular buffer and shared memory headers
│   ├── security/            # Capability bitmasks, security info, and PRNG headers
│   ├── storage/             # Block device and ATA driver headers
│   ├── fs/                  # NyotaFS and VFS layer headers
│   ├── elf/                 # ELF64 header structures and loader prototypes
│   ├── drivers/             # PCI and Intel E1000 NIC driver headers
│   └── net/                 # Protocol headers (ethernet, arp, ipv4, icmp, udp, tcp, socket)
│
├── user/
│   ├── libnyota/            # Ring 3 C library (crt0, syscalls, sockets, IPC, signals)
│   ├── init/                # System init process (PID 1, orphan reaper)
│   ├── sh/                  # Interactive user command shell (pipelines, backgrounding)
│   ├── hello/               # Hello world user program
│   ├── echo/                # Argument echoing utility
│   ├── ls/                  # Directory listing utility
│   ├── cat/                 # File and standard stream concatenation utility
│   ├── ps/                  # Active process status and process tree utility
│   ├── test/                # Ring 3 automated verification suite
│   ├── ifconfig/            # Network interface configuration utility
│   ├── ping/                # ICMP Echo round-trip diagnostic tool
│   ├── netstat/             # Active socket and protocol table inspector
│   ├── nslookup/            # UDP DNS address resolution utility
│   ├── netcat/              # Interactive TCP stream client
│   ├── echo-server/         # Concurrent TCP echo daemon on port 8080
│   ├── secinfo/             # Kernel security posture query utility
│   ├── kill/                # Signal transmission utility (-TERM, -KILL, etc.)
│   ├── ipctest/             # Pipes, blocking I/O, and shared memory test suite
│   ├── memtest/             # Memory isolation and protection fault test
│   ├── crash/               # Controlled userspace #PF crash containment demo
│   └── stressproc/          # Process lifecycle, IPC, and waitpid stress tool
│
├── tools/
│   ├── mkimage.c            # Cross-platform bootable disk image generator
│   ├── mknyotafs.c          # Host tool creating NyotaFS filesystem images
│   ├── test_runner.py       # Automated QEMU userspace test suite
│   ├── test_network.py      # Automated network verification test suite
│   ├── test_internal_tcp.py # Internal loopback TCP echo server/client test
│   ├── test_tcp.py          # Host-to-guest external TCP connection test
│   └── test_security.py     # Automated Phase 8 security, IPC, and isolation suite
│
├── linker.ld                # 64-bit kernel linker script (load address 0x100000)
├── user.ld                  # Ring 3 user ELF linker script (virtual load 0x400000)
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

Kernel       : v0.8.0
Architecture : x86_64

[ OK ] GDT
[ OK ] IDT
[ OK ] Exceptions
[ OK ] PIC
[ OK ] Timer
[ OK ] Keyboard
[ OK ] Interrupts

[ OK ] Memory
[ OK ] Paging
[ OK ] Kernel Heap
[ OK ] TSS
[ OK ] User Segments
[ OK ] Syscalls

[ OK ] Scheduler
[INFO]  Initializing storage
[ OK ]  ATA controller detected
[ OK ]  Disk detected (Primary Master: QEMU HARDDISK)
[INFO]  Sector size: 512
[INFO]  Disk sectors: 2880
[ OK ]  Disk detected (Primary Slave: QEMU HARDDISK)
[INFO]  Sector size: 512
[INFO]  Disk sectors: 32768
[ OK ] Storage
[INFO]  Mounting root filesystem
[FS]    Mounting NyotaFS
[ OK ]  NyotaFS valid (total blocks: 16384)
[FS]    Root inode: 1
[ OK ]  / mounted
[ OK ] NyotaFS
[ OK ] VFS
[ OK ] ELF Loader

[PCI] 0x0000000000000000:0x0000000000000003.0 Device [0x0000000000008086:0x000000000000100E] Class 0x0000000000000002:0x0000000000000000 IRQ 11 BAR0: 0x00000000FEBC0000
[ OK ] PCI (6 devices)
[NET] Registered interface: lo
[ OK ] ARP
[ OK ] IPv4
[ OK ] ICMP
[ OK ] UDP
[ OK ] TCP
[ OK ] Sockets
[NET] Registered interface: eth0
[ OK ] E1000
[E1000] Initialized eth0 MAC: 52:54:00:12:34:56 IRQ: 11
[INFO]  eth0
[INFO]  MAC: 52:54:00:12:34:56
[INFO]  IP:  10.0.2.15
[INFO]  Gateway: 10.0.2.2

[ OK ] IPC
[ OK ] Signals
[ OK ] Permissions
[ OK ] Capabilities
[ OK ] Resource Limits

[INFO]  Loading /init
[ OK ]  ELF loaded
[ OK ]  PID 1 started

========================================
          NYOTA OS INIT (PID 1)         
========================================
[INIT] System initialization complete.
[INIT] Starting userspace interactive shell (/bin/sh)...

nyota$ ps
PID   PPID  UID   STATE       NAME
1     0     0     RUNNING     init
2     1     1000  RUNNING     sh

nyota$ secinfo
Kernel security features
------------------------
User/kernel isolation : enabled
Guard pages           : enabled
User pointer checks   : enabled
Capabilities          : enabled
Resource limits       : enabled
ASLR foundation       : enabled
Active shared memory  : 0 segments

nyota$ ipctest
[ OK ] pipe communication
[ OK ] blocking read
[ OK ] shared memory
[ OK ] permission checks
[ OK ] cleanup
All IPC tests passed successfully!

nyota$ ls | cat
bin
dev
etc
home
tmp

nyota$ crash
[SEC] PID 8 exception: page fault, address: 0x0000000000000010, signal: SIGSEGV
[PROC] PID 8 terminated

nyota$ ps
PID   PPID  UID   STATE       NAME
1     0     0     RUNNING     init
2     1     1000  RUNNING     sh
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

# ⚠️ Current Limitations (Phase 8)

Phase 8 successfully implements process hierarchy (parent/child trees, orphan adoption to PID 1), non-spinning process waiting (`waitpid`), zombie process lifecycle & reaping, full POSIX-style signals (`SIGTERM`, `SIGKILL`, `SIGSTOP`, `SIGCONT`, `SIGCHLD`, `SIGSEGV`, `SIGILL`, `SIGFPE`, `SIGPIPE`), safe Ring 3 CPU exception-to-signal conversion, anonymous pipes with blocking I/O and EOF/SIGPIPE semantics, VFS pipe integration and shell pipelines (`ls | cat`), controlled shared memory IPC (`shm_get/at/dt/ctl`), user identity (UID/GID), capability model (`CAP_NET_ADMIN`, `CAP_NET_RAW`, `CAP_SYS_ADMIN`, `CAP_IPC_OWNER`, `CAP_KILL`), filesystem permissions (`vfs_chmod`, `vfs_chown`, protection of `/bin`, `/etc`, `/kernel`), per-process resource limits, centralized user pointer validation, stack guard pages, hardware-seeded PRNG, basic ASLR foundation, security audit logging, and 18 new system calls (Vectors 26..43). The following subsystems belong to subsequent phases:

- Pseudo-terminals (PTYs), line disciplines, and POSIX `termios` control are deferred to Phase 9.
- Advanced shell job control (foreground/background process groups, `tcsetpgrp`) and session management belong to Phase 9.
- System timekeeping (RTC CMOS clock, wall-clock time, `gettimeofday`) and daemon/service managers are planned for Phase 9.

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

Phase 6: Filesystem, ELF Loader & Real Userland  ◄ [COMPLETED]
   ├── Generic Block Device Abstraction (block_device_t)
   ├── ATA PIO Storage Driver (IDENTIFY, LBA28 sector I/O)
   ├── Dual-Drive Architecture (boot image + NyotaFS persistent disk)
   ├── Native Filesystem (NyotaFS: Superblock, Bitmaps, Inodes, Extents)
   ├── Directory Hierarchies & Recursive Path Resolution Engine
   ├── Virtual Filesystem Layer (VFS: open, close, read, write, seek, stat, readdir, mkdir)
   ├── Kernel File Objects (file_t) & Per-Process File Descriptor Tables
   ├── Standard Streams (stdin, stdout, stderr) & Device Files (/dev/console, /dev/null)
   ├── Freestanding ELF64 Loader (PT_LOAD segments, memory mapping, permissions)
   ├── Process Creation from Disk & Execution (spawn, exec, System V ABI argc/argv stack)
   ├── Userspace Standard C Library (libnyota: crt0, syscalls, stdio printf, string)
   ├── Autonomous Init Process (PID 1 /init) & Interactive Userspace Shell (/bin/sh)
   ├── Userspace Utilities (/bin/hello, /bin/echo, /bin/ls, /bin/cat, /bin/ps, /bin/test)
   └── Strict User Memory Validation (user_validate_pointer, copy_from/to_user, safe EFAULT)

Phase 7: Networking, TCP/IP & Sockets            ◄ [COMPLETED]
   ├── PCI Bus Subsystem & Enumeration (Config space 0xCF8/0xCFC)
   ├── Intel 82540EM (E1000) Gigabit NIC Driver (MMIO BAR0, IRQ 11)
   ├── Physical DMA Circular Descriptor Rings (32 RX / 8 TX descriptors)
   ├── Hardware MAC Retrieval & Filtering (52:54:00:12:34:56)
   ├── Network Device Abstraction (net_device_t: eth0, lo)
   ├── Dynamic Packet Buffer Architecture (packet_t headroom/tailroom)
   ├── Ethernet II Frame Processing & EtherType Demultiplexing
   ├── Dynamic ARP Subsystem (RFC 826 Request/Reply & Cache Management)
   ├── IPv4 Layer & RFC 1071 Internet Checksum Engine
   ├── Longest-Prefix Routing Table (127.0.0.0/8, 10.0.2.0/24, 0.0.0.0/0)
   ├── ICMP Echo Request & Reply Processing (/bin/ping)
   ├── User Datagram Protocol (UDP) & DNS Client (/bin/nslookup)
   ├── RFC 793 TCP State Machine (Handshake, Windowing, ACK/FIN/RST)
   ├── BSD Socket Architecture (socket_t, Circular FIFO Buffers)
   ├── 10 Network Syscalls (SYS_SOCKET..SYS_SHUTDOWN) & VFS Integration
   ├── Preemptive Scheduler Wait Queues & Non-Spinning Process Sleeping
   └── Ring 3 Network Utilities (ifconfig, ping, netstat, nslookup, netcat, echo-server)

Phase 8: IPC, Security Hardening & Process Isolation ◄ [COMPLETED]
   ├── Process Hierarchy (parent/child trees, orphan adoption to PID 1 init)
   ├── Non-Spinning Process Waiting (waitpid, zombie process lifecycle & reaping)
   ├── POSIX Signal Subsystem (SIGTERM, SIGKILL, SIGSTOP, SIGCONT, SIGCHLD, SIGSEGV, SIGILL, SIGFPE, SIGPIPE)
   ├── Safe Ring 3 Exception-to-Signal Dispatch (#DE -> SIGFPE, #UD -> SIGILL, #GP/#PF -> SIGSEGV)
   ├── Anonymous Pipes (pipe_t 4096-byte circular buffer, blocking read/write queues, EOF, SIGPIPE)
   ├── VFS Pipe Integration & Shell Pipelines (ls | cat, cmd1 | cmd2)
   ├── Controlled Shared Memory IPC (shm_get/at/dt/ctl, page-aligned, ref-counted, permission-checked)
   ├── User Identity & Ownership (UID/GID, root UID 0 vs unprivileged user UID 1000)
   ├── Capability System (CAP_NET_ADMIN, CAP_NET_RAW, CAP_SYS_ADMIN, CAP_IPC_OWNER, CAP_KILL)
   ├── Filesystem Permission Enforcement (vfs_chmod, vfs_chown, protected /bin, /etc, /kernel)
   ├── Resource Limits (max open files, memory, processes, sockets per task)
   ├── Centralized Pointer & Buffer Validation (validate_user_pointer, copy_from/to_user, safe EFAULT)
   ├── Integer Overflow Checks (size_add_overflow, size_mul_overflow)
   ├── Stack Protection & Guard Pages (unmapped guard page below user stack base)
   ├── Hardware-Seeded PRNG (kernel_random, SYS_GETRANDOM / getrandom) & ASLR Foundation
   ├── Security Event Logging (security_log audit trail)
   └── Ring 3 Security Suite (/bin/secinfo, /bin/kill, /bin/ipctest, /bin/memtest, /bin/crash, /bin/stressproc)

Phase 9: Advanced Userland & System Services     ◄ [NEXT]
   ├── Advanced Init Daemon & Service Manager (daemons, respawning, runlevels)
   ├── Pseudo-Terminal Subsystem (PTYs, line disciplines, termios)
   ├── Advanced Shell & Job Control (tcgetattr, foreground/background process groups)
   └── System Timekeeping & Real-Time Clock (RTC CMOS, uptime, gettimeofday)
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