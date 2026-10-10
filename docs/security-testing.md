# Nyota OS Security Testing & Verification

**Target Release:** `v1.4.0`  
**Test Suites:** `tools/test_security.py` (Phase 8 Baseline), `tools/test_phase14.py` (Hardening Suite), `user/sectest/main.c` (Kernel Verification Suite)

---

## 1. Automated Test Suites

### 1.1 Baseline Security Suite (`tools/test_security.py`)
Preserves the established 25/25 test suite from Phase 8 to prevent functional regressions across core OS features:
* `secinfo` feature enumeration.
* IPC pipe data transfer, blocking reads, shared memory, and cleanup.
* Process tree hierarchy (`ps`), background jobs, and pipelines (`ls | cat`).
* Crash handling and safe user fault recovery (`SIGSEGV` delivered, kernel intact).
* Memory isolation (`memtest`: private address spaces, kernel memory blocked).
* Process stress testing (`stressproc 8`).

**Status: 25 / 25 Passed (100%)**

---

### 1.2 Phase 14 Hardening Verification Suite (`tools/test_phase14.py`)
Comprehensive automated end-to-end regression test suite executing inside QEMU over TCP serial communication:

| Test Name | Category | Verification Method | Result |
|-----------|----------|---------------------|:------:|
| `uname_version_1.4.0` | Metadata | Checks kernel version string matches 1.4.0 | PASS |
| `id_uid_1000` | Identity | Verifies unprivileged shell runs with UID 1000 | PASS |
| `id_gid_1000` | Identity | Verifies unprivileged shell runs with GID 1000 | PASS |
| `id_euid_1000` | Identity | Verifies effective UID is 1000 | PASS |
| `id_egid_1000` | Identity | Verifies effective GID is 1000 | PASS |
| `whoami_sahil` | Identity | Verifies `/bin/whoami` reports `sahil` | PASS |
| `secinfo_isolation` | Kernel | Checks user/kernel isolation reporting | PASS |
| `secinfo_guard_pages`| Memory | Checks guard page enforcement reporting | PASS |
| `secinfo_pointer_val`| Syscalls | Checks user pointer validation reporting | PASS |
| `secinfo_capabilities`| Access | Checks capability engine status | PASS |
| `auditlog_restricted` | Audit | Verifies `/bin/auditlog` is denied to non-root | PASS |
| `shadow_file_protected`| Credentials | Verifies `/etc/shadow` (mode 0600) cannot be read by UID 1000 | PASS |
| `sectest_identity_uid` | ABI | Kernel verifies `getuid() == 1000` | PASS |
| `sectest_identity_euid`| ABI | Kernel verifies `geteuid() == 1000` | PASS |
| `sectest_unauthorized_seteuid` | Escalation | `seteuid(0)` fails with `-EPERM` | PASS |
| `sectest_unauthorized_setuid` | Escalation | `setuid(0)` fails with `-EPERM` | PASS |
| `sectest_unauthorized_setegid` | Escalation | `setegid(0)` fails with `-EPERM` | PASS |
| `sectest_shadow_read_blocked` | VFS | `open("/etc/shadow", O_RDONLY)` fails with `-EACCES` | PASS |
| `sectest_chmod_sys_blocked` | VFS | Non-owner cannot `chmod` `/etc/passwd` | PASS |
| `sectest_chown_sys_blocked` | VFS | Non-root cannot `chown` `/etc/passwd` | PASS |
| `sectest_user_create_file` | VFS | User can create files in `/home/sahil` | PASS |
| `sectest_owner_chmod_file` | VFS | Owner can `chmod` own file | PASS |
| `sectest_auditlog_denied` | Capability | Calling `audit_log_op(READ)` without `CAP_AUDIT` returns `-EPERM` | PASS |
| `sectest_kernel_ptr_efault` | Syscall | Kernel pointer `0xFFFFFFFF80000000` returns `-EFAULT` | PASS |
| `sectest_unmapped_ptr_efault`| Syscall | Unmapped pointer returns `-EFAULT` | PASS |
| `sectest_null_ptr_efault` | Syscall | NULL pointer returns `-EFAULT` | PASS |
| `sectest_overflow_len_rejected`| Arithmetic | `0xFFFFFFFFFFFFFFFFULL` count returns `-EFAULT` | PASS |
| `sectest_sandbox_filter` | Sandbox | Sandboxed child blocked from denied syscall | PASS |
| `crash_fault_handled` | Exceptions | Userspace `#PF` exception delivered safely | PASS |
| `crash_kernel_alive` | Reliability | Kernel survives user crash | PASS |
| `memtest_kernel_blocked` | Isolation | Direct read of kernel memory blocked | PASS |
| `memtest_foreign_blocked`| Isolation | Direct read of foreign process memory blocked | PASS |
| `memtest_process_survived`| Reliability | Calling process survives memory test | PASS |

**Status: 33 / 33 Passed (100%)**

---

## 2. Test Execution Commands

```bash
# Build bootable kernel and disk images
make all

# Run baseline Phase 8 security tests
python tools/test_security.py

# Run comprehensive Phase 14 hardening test suite
python tools/test_phase14.py
```
