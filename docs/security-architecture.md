# Nyota OS Security Architecture

**Target Release:** `v1.4.0`  
**Architecture:** x86_64  
**Primary Subsystems:** Kernel, Virtual Memory, Process Isolation, Access Control, Auditing

---

## 1. Executive Overview

Nyota OS v1.4.0 introduces a comprehensive security architecture and kernel hardening framework designed to protect against local privilege escalation, unauthorized cross-process interference, untrusted pointer attacks, and unauthorized system reconfiguration.

The system enforces strict boundaries between:
1. **Ring 0 Kernel Space:** Fully privileged, isolated from user execution, protected by hardware Write-Protect (`CR0.WP`) and No-Execute (`EFER.NXE`).
2. **Privileged System Daemons (UID 0):** Dedicated services (`init`, `loggerd`, `netd`, `logind`) operating with least-privilege capability masks.
3. **Unprivileged User Applications (UID 1000+):** Ring 3 processes running with non-zero UID/EUID, zero default administrative capabilities, isolated address spaces, and constrained resource limits.
4. **GUI Desktop & Window Server:** Segregated window ownership and event routing preventing arbitrary framebuffer scraping or event injection.

---

## 2. Core Architectural Boundaries

```
+-----------------------------------------------------------------------+
|                           Ring 3 Userland                             |
|  +--------------------+  +--------------------+  +-----------------+  |
|  | Unprivileged Apps  |  |  GUI Applications  |  | Privileged Svcs |  |
|  | (UID 1000, CAP 0)  |  |  (Window Server)   |  | (logind, netd)  |  |
|  +---------+----------+  +---------+----------+  +--------+--------+  |
|            |                       |                      |           |
|            +-----------------------+----------------------+           |
|                                    | (syscall: int 0x80)              |
+------------------------------------|----------------------------------+
|                                    v                                  |
|                         Syscall Dispatch Engine                       |
|   +---------------------------------------------------------------+   |
|   | 1. Per-process Syscall Filtering (Allowlist/Denylist Sandbox) |   |
|   | 2. User Pointer Boundary & Safe Copy Validation               |   |
|   | 3. Capability & EUID Authorization Checks                     |   |
|   | 4. Security Event Auditing Ring Buffer                        |   |
|   +---------------------------------------------------------------+   |
|                                    |                                  |
+------------------------------------|----------------------------------+
|                                    v                                  |
|                         Ring 0 Kernel Core                            |
|  +------------------+  +------------------+  +---------------------+  |
|  | Hardware NX/W^X  |  | Credential Model |  | VFS Access Control  |  |
|  |  (EFER.NXE, WP)  |  |  (UID, EUID, GID)|  | (Mode Bits, Inodes) |  |
|  +------------------+  +------------------+  +---------------------+  |
+-----------------------------------------------------------------------+
```

---

## 3. Subsystem Security Highlights

### 3.1 Hardware Memory Protection & $W \oplus X$
* **No-Execute (`EFER.NXE`):** Bit 11 of the Extended Feature Enable Register (`MSR 0xC0000080`) is enabled during stage 2 bootloader execution, AP trampoline initialization, and CPU setup.
* **Kernel Write Protection (`CR0.WP`):** Bit 16 of `CR0` is set across all cores, ensuring Ring 0 cannot accidentally overwrite read-only mappings.
* **$W \oplus X$ Policy:** User stack pages, data segments, and shared memory allocations are mapped with `PAGE_NX`. Executable ELF code segments (`PF_X`) are mapped read-only (`!PAGE_WRITABLE`). No mapping is simultaneously writable and executable.
* **Heap Hardening:** Free-list validation, allocation boundary checking, integer overflow checks on `count * size`, and double-free detection.

### 3.2 Process Identity & Authorization
* **Dual Identifiers:** Every process structure (`process_t`) contains explicit real `uid`/`gid` and effective `euid`/`egid`.
* **Syscalls:** `getuid` (30), `setuid` (31), `getgid` (32), `setgid` (33), `geteuid` (79), `getegid` (80), `seteuid` (81), `setegid` (82).
* **Non-Root Protection:** Unprivileged processes cannot change their real or effective UID/GID to 0 or arbitrary IDs. Unauthorized attempts are rejected with `-EPERM` and logged to the audit subsystem.
* **Privilege Dropping:** `logind` drops effective privileges via `seteuid(user_uid)` / `setegid(user_gid)` before spawning user shells, ensuring spawned processes inherit unprivileged credentials and null capabilities.

### 3.3 File Access Control
* **VFS Inode Enforcement:** File read/write/execute operations verify `euid` against `inode.uid` and `egid` against `inode.gid`, falling back to "other" permission bits.
* **Root Bypass:** Processes with `euid == 0` bypass discretionary file permission restrictions.
* **Sensitive Files:** Critical credential files such as `/etc/shadow` are provisioned with mode `0600` (root read/write only).
* **System Immutability:** Non-root processes are blocked from unlinking entries under `/bin/`, `/etc/`, and `/kernel`.

### 3.4 Capability-Based Access Control
* Operations such as firewall configuration (`CAP_NET_ADMIN`, `CAP_FIREWALL`), audit log extraction (`CAP_AUDIT`), time synchronization (`CAP_SYS_TIME`), and process signaling (`CAP_KILL`) require explicit capability bits when called by non-root processes.

### 3.5 Syscall Filtering Sandbox
* Per-process configurable syscall filtering allows processes to self-sandbox into an allowlist or denylist policy (`SYS_SET_SYSCALL_FILTER` 83).
* Once enabled, the policy is locked against tampering and enforced in `syscall_dispatch`.

### 3.6 Security Audit Subsystem
* Circular in-memory audit log buffer (capacity 64 events) captures denied operations, permission rejections, capability failures, and authentication events.
* Accessible via privileged syscall `SYS_AUDIT_LOG` (84) and CLI utility `/bin/auditlog`.
