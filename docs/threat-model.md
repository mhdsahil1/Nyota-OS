# Nyota OS Threat Model

**Target Release:** `v1.4.0`  
**Scope:** Kernel, Address Space Boundaries, Filesystem, Syscalls, Network, IPC, Credentials

---

## 1. Threat Categories & Mitigation Matrix

| # | Threat Category | Threat Description | Nyota OS v1.4.0 Enforcement & Mitigation |
|---|-----------------|--------------------|------------------------------------------|
| 1 | **Malicious User Applications** | Rogue Ring 3 binaries attempting privilege escalation or kernel control hijack. | Ring 0/Ring 3 separation via GDT/TSS, hardware NX (`EFER.NXE`), $W \oplus X$, capability checks, and unprivileged user identities (UID 1000). |
| 2 | **Unauthorized Privileged Operations** | Unprivileged users invoking administrative functions (firewall rule mutation, system shutdown, raw devices). | Checked in syscall handlers via `curr->euid == 0` and capability masks (`CAP_NET_ADMIN`, `CAP_FIREWALL`, `CAP_SYS_ADMIN`, `CAP_AUDIT`). |
| 3 | **Invalid Syscall Arguments** | User pointers targeting kernel space (`0xFFFFFFFF...`) or unmapped regions passed to kernel handlers. | `user_validate_pointer()` verifies pointers reside in user canonical range `< 0x0000800000000000ULL`, checking `PAGE_PRESENT`, `PAGE_USER`, and `PAGE_WRITABLE`. Safe copy routines (`copy_from_user`, `copy_to_user`) return `-EFAULT`. |
| 4 | **Malformed Network Packets** | Crafted Ethernet, IPv4, IPv6, TCP, or UDP frames triggering memory corruption or OOB reads. | Strict packet length verification, header boundary checks in E1000 and IP layers, checksum checks, and firewall inspection. |
| 5 | **Malformed Filesystem Metadata** | Corrupted superblock or inode data causing kernel panics or arbitrary writes. | Inode block indexing validation, direct and indirect block bounds checks, allocation bitmap verification in NyotaFS. |
| 6 | **Memory Corruption** | Stack buffer overflows, heap metadata tampering, double-frees, or integer wraps. | Heap chunk header verification, double-free detection, arithmetic overflow checks (`size_mul_overflow`), and hardware `CR0.WP`. |
| 7 | **Resource Exhaustion** | Fork bombing, open descriptor exhaustion, socket flooding, or kernel memory starvation. | Enforced per-process limits: `max_open_files` (64), `max_sockets` (16), `max_processes` (16), and `max_memory` (64 MiB). |
| 8 | **Cross-Process Data Access** | Process reading/writing private address space of another process or snooping IPC. | Separate PML4 page tables per process, isolated user virtual spaces, and permission checks on IPC pipes/shared memory. |
| 9 | **Credential Exposure** | Plaintext password storage, world-readable credential files, or memory leakage. | Standard SHA-256 password hashing, `/etc/shadow` restricted to mode `0600`, constant-time comparison (`crypto_memcmp`), zeroing of buffers. |
| 10 | **Kernel Crashes from Untrusted Input** | Unhandled page faults or invalid parameters inducing kernel panics. | Structured exception handlers recover user faults by delivering `SIGSEGV` or terminating faulty Ring 3 threads; kernel remains stable. |

---

## 2. Trust Boundaries

```
[ UNTRUSTED ] User Space Processes (UID 1000)
      |
      |  System Call Interface (int 0x80)
      v  [ Boundary Check: Pointer Validation, Argument Validation, Filter Mask ]
[ TRUSTED ]   Syscall Dispatcher & Capabilities Engine
      |
      v
[ PRIVILEGED ] Kernel Executive (Ring 0, PML4 Kernel Identity Mappings)
```

1. **User $\rightarrow$ Kernel Boundary:** All data crossing this boundary is untrusted. Must use `copy_from_user()` / `copy_to_user()`. Direct pointer dereference is strictly forbidden.
2. **Process $\rightarrow$ Process Boundary:** Address spaces are mutually inaccessible. Shared memory requires explicit creation (`shm_get`) and attachment (`shm_at`).
3. **Daemon $\rightarrow$ Daemon Boundary:** UNIX-domain sockets and service endpoints validate requestor PID and UID before executing administrative RPC commands.
