# Nyota OS Developer Tools & Kernel Observability (v1.8.0)

## Overview

Nyota OS `v1.8.0` introduces a native developer tools and observability toolkit that allows kernel and userland engineers to inspect, trace, debug, profile, and optimize the operating system.

The subsystem integrates:
1. **Kernel Debugging & Crash Reports**: Enhanced panic handler, stack frame unwinding via `RBP`, kernel symbol table lookup (`ksym`), top-of-stack dumps, and dual-output mirroring (VGA console and COM1 serial port).
2. **System-Call Tracing (`nytrace`)**: System call interception, argument decoding, return value capture, and microsecond latency measurement.
3. **Process & Scheduler Monitoring (`pstree`)**: Process hierarchy tree, execution states, CPU core assignment, memory consumption, and context switch tracking.
4. **Memory Diagnostics (`memdiag`)**: Heap block validation, header and canary verification, chunk count breakdown, and physical frame allocation statistics.
5. **Network Diagnostics & Packet Monitoring (`pktmon`)**: Header-only packet capture on network interfaces (`lo`, `eth0`), interface metrics (RX/TX packets/bytes), and active socket inspection.
6. **Performance Profiling (`nyprof`)**: High-precision TSC hardware cycle measurement of system call latencies, call counts, and execution metrics.
7. **GDB Integration**: Kernel debugging configuration and symbol automation via `tools/gdbinit_nyota`.

---

## 1. Kernel Debugging & Diagnostics

### Symbol Table & Stack Unwinding (`ksym`)

* **Kernel Symbol Resolution**: `kernel/debug/ksym.c` maintains a sorted symbol table containing all major kernel subsystem entry points (entry, paging, heap, scheduler, filesystem, network, virtualization).
* **Stack Frame Walking**: `stack_trace_dump(rbp, rip)` traverses the linked list of stack base pointers (`RBP`), resolving each return instruction pointer (`RIP`) to `<symbol_name> + 0x<offset>`.
* **Crash Diagnostics**: `panic_with_frame(title, frame)` and `kernel_panic(msg)` output comprehensive failure diagnostics:
  * Faulting instruction pointer (`RIP`) and symbol offset.
  * Faulting process context (PID, executable name, EUID).
  * Register state (`RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`-`R15`, `CR2`, `CR3`).
  * Full backtrace of up to 16 call frames.
  * Dump of top 8 quadwords from `RSP`.
  * Real-time mirroring of all panic lines to the serial COM1 port (`0x3F8`).

### GDB Debugging Workflows

Nyota OS supports remote debugging using QEMU's internal GDB stub:
```bash
# Launch kernel paused with GDB server on localhost:1234
make run-debug

# In a separate terminal, attach with GDB
gdb -x tools/gdbinit_nyota
```

The `tools/gdbinit_nyota` configuration automatically loads symbols from `build/kernel.elf` and sets breakpoints on critical error paths (`panic_with_frame`, `kernel_panic`).

---

## 2. System-Call Tracing (`nytrace`)

The `nytrace` utility intercepts system calls executed by a target application or attached process.

```bash
# Trace execution of a command
nytrace /bin/hello
nytrace /bin/ls

# Attach to an existing authorized process
nytrace -p <PID>
```

### Syscall: `SYS_TRACE_OP` (88)

| Operation | Value | Purpose |
| :--- | :--- | :--- |
| `TRACE_OP_ATTACH` | 1 | Attach to target PID. Restricted to root (EUID 0) or matching EUID. Rejects PID <= 1. |
| `TRACE_OP_DETACH` | 2 | Detach tracing from target PID. |
| `TRACE_OP_READ` | 3 | Read batch of recorded `trace_event_t` records from the kernel ring buffer. |
| `TRACE_OP_ENABLE_SELF` | 4 | Mark current process as a tracer, automatically tracing any spawned children. |
| `TRACE_OP_CLEAR` | 5 | Flush kernel trace event ring buffer. |

### Security & Privacy Protections
* Tracing of PID 1 (`init`) or kernel idle tasks is strictly prohibited (`SYS_ERR_EPERM`).
* Tracing unauthorized processes belonging to other users without `CAP_SYS_PTRACE` or root EUID is rejected.
* Tracing records argument values and return codes without dumping raw buffer contents to protect user credentials and secret data.

---

## 3. Process & Scheduler Monitoring (`pstree`)

The `pstree` tool renders the hierarchical relationship between running processes.

```bash
pstree
```

### Example Output
```
================================================================================
                    NYOTA OS PROCESS HIERARCHY TREE
================================================================================
[1] init (EUID=0, CPU=0, State=SLEEPING, Sw=4, Mem=128 KB)
  +-- [2] loggerd (EUID=0, CPU=0, State=SLEEPING, Sw=1, Mem=128 KB)
  +-- [3] ttyd (EUID=0, CPU=0, State=SLEEPING, Sw=1, Mem=128 KB)
  +-- [4] netd (EUID=0, CPU=0, State=SLEEPING, Sw=1, Mem=128 KB)
  +-- [6] logind (EUID=0, CPU=0, State=SLEEPING, Sw=1, Mem=128 KB)
  |     +-- [9] sh (EUID=1000, CPU=0, State=SLEEPING, Sw=8, Mem=128 KB)
  |           +-- [12] pstree (EUID=1000, CPU=0, State=RUNNING, Sw=1, Mem=128 KB)
  +-- [7] desktop (EUID=0, CPU=0, State=SLEEPING, Sw=3, Mem=128 KB)
        +-- [8] about (EUID=0, CPU=0, State=SLEEPING, Sw=1, Mem=128 KB)
================================================================================
Total Processes: 9
```

---

## 4. Memory Diagnostics (`memdiag`)

The `memdiag` tool provides allocator integrity checking, heap fragmentation analysis, and physical memory accounting.

```bash
# Summary of physical memory and kernel heap
memdiag

# Validate allocator block canaries and headers
memdiag -v

# Analyze heap fragmentation and free chunks
memdiag -l
```

### Syscall: `SYS_MEM_DIAG` (89)

* `MEM_DIAG_SUMMARY` (1): Populates `mem_diag_info_t` with frame statistics from PMM, virtual heap boundaries, total/used/free byte counts, chunk counts, and block health.
* `MEM_DIAG_VERIFY` (2): Performs an in-line integrity audit over the kernel heap linked-list, verifying magic canaries (`0xAA55AA55`), chunk sizes, and header alignment, returning the count of corrupted chunks.

---

## 5. Network Diagnostics & Socket Inspection (`pktmon`)

The `pktmon` utility provides network telemetry and packet monitoring.

```bash
# Display interface statistics and active socket table
pktmon -s

# Capture packet headers on network interfaces (root / CAP_NET_ADMIN required)
pktmon -c 10
```

### Syscall: `SYS_PKT_MON` (90)

* `PKTMON_OP_START` (1): Enables the kernel packet tap ring buffer (`CAP_NET_ADMIN` required).
* `PKTMON_OP_STOP` (2): Disables packet capture.
* `PKTMON_OP_READ` (3): Reads captured packet header metadata (Ethernet frame type, source/dest IP, protocols, ports, packet length, timestamp).
* `PKTMON_OP_STATS` (4): Retrieves interface packet/byte counters and active TCP/UDP/UNIX sockets.

---

## 6. Performance Profiling (`nyprof`)

The `nyprof` utility profiles system call latency and execution distribution using hardware Time Stamp Counter (`RDTSC`).

```bash
# Profile syscall latencies for a specific command
nyprof /bin/ls
nyprof /bin/sysinfo

# Query active profiling statistics
nyprof -s

# Reset profiler counters
nyprof -r
```

### Syscall: `SYS_PROF_OP` (91)

* `PROF_OP_START` (1): Activates hardware cycle timing on syscall dispatch.
* `PROF_OP_STOP` (2): Suspends syscall profiling.
* `PROF_OP_GET_STATS` (3): Copies `prof_report_t` containing call counts, min/max/average cycle durations per syscall.
* `PROF_OP_RESET` (4): Zeroes latency tracking tables.

---

## Verification & Automated Testing

The complete observability suite is validated via `tools/test_phase18.py`:
```bash
python tools/test_phase18.py
```
Validates:
* OS version `v1.8.0` reporting.
* Userspace binary availability (`nytrace`, `pstree`, `memdiag`, `pktmon`, `nyprof`).
* Process tree hierarchy generation.
* Heap canary and physical frame diagnostics.
* Network interface statistics and active socket table inspection.
* Syscall performance profiling metrics.
* Process syscall tracing and microsecond latency measurement.
* Permission boundaries (rejection of PID 1 tracing).
* Host kernel stability.
