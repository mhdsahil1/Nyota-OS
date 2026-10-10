# Nyota OS Access Control & Authorization

**Target Release:** `v1.4.0`  
**Subsystems:** User & Group Credentials, File Permissions (VFS), Capabilities, Syscall Filtering

---

## 1. Identity & Process Credentials

Every process in Nyota OS possesses an explicit security context represented in `process_t`:
* **Real User ID (`uid`):** Represents the actual account executing the process.
* **Effective User ID (`euid`):** Used by the kernel for all access control decisions (file access, privileged syscalls, service administration).
* **Real Group ID (`gid`):** Primary group identity.
* **Effective Group ID (`egid`):** Group identity used for permission resolution.
* **Capability Mask (`capabilities`):** 64-bit mask defining fine-grained administrative rights.

### 1.1 Syscall ABI

```c
uint32_t getuid(void);    /* Syscall 30 */
int      setuid(uint32_t uid);   /* Syscall 31 */
uint32_t getgid(void);    /* Syscall 32 */
int      setgid(uint32_t gid);   /* Syscall 33 */
uint32_t geteuid(void);   /* Syscall 79 */
uint32_t getegid(void);   /* Syscall 80 */
int      seteuid(uint32_t euid); /* Syscall 81 */
int      setegid(uint32_t egid); /* Syscall 82 */
```

### 1.2 Authorization Rules
* Processes with `uid == 0` or `euid == 0` or possessing `CAP_SETUID` may transition their credentials arbitrarily.
* Unprivileged processes (`euid != 0`) cannot change their credentials to 0 or another user ID.
* Unauthorized credential modifications return `-EPERM` and generate an audit event in the security ring buffer.

---

## 2. File Ownership & Permissions

### 2.1 NyotaFS Inode Permissions
Each file and directory inode records:
* `uid` (32-bit user owner).
* `gid` (32-bit group owner).
* `mode` (Standard permission bits):
  * Owner: Read (`0400`), Write (`0200`), Execute (`0100`).
  * Group: Read (`0040`), Write (`0020`), Execute (`0010`).
  * Other: Read (`0004`), Write (`0002`), Execute (`0001`).

### 2.2 Kernel Enforcement Rules
When a process attempts `vfs_open()`, `vfs_read()`, or `vfs_write()`:
1. **Root Bypass:** If `curr->euid == 0`, access is granted unconditionally.
2. **Owner Matching:** If `curr->euid == inode.uid`, owner permission bits apply.
3. **Group Matching:** If `curr->egid == inode.gid`, group permission bits apply.
4. **Other Matching:** Otherwise, "other" permission bits apply.

If the requested access is not permitted by the applicable class, the call returns `-EACCES` and emits a security denial record.

### 2.3 Management Interfaces
* `chmod <octal_mode> <path>`: Only the file owner (`euid == inode.uid`) or root (`euid == 0`) can modify file mode bits.
* `chown <uid>[:<gid>] <path>`: Restrictive ownership transfer requires root (`euid == 0`) or `CAP_CHOWN`.
* `id`: Displays current `uid`, `gid`, `euid`, `egid`.
* `whoami`: Displays effective username.

---

## 3. Capability Model

Administrative privileges are segmented into discrete 64-bit capability flags:

| Capability | Bit | Protected Operations |
|------------|:---:|----------------------|
| `CAP_SYS_ADMIN` | 0 | System shutdown, reboot, mount operations |
| `CAP_NET_ADMIN` | 1 | Interface configuration, routing table mutation |
| `CAP_NET_RAW`   | 2 | Raw socket creation (`SOCK_RAW`) |
| `CAP_KILL`      | 3 | Signaling processes belonging to other users |
| `CAP_SETUID`    | 4 | Setting arbitrary process UID / EUID |
| `CAP_CHOWN`     | 5 | Modifying arbitrary file ownership |
| `CAP_FIREWALL`  | 6 | Packet firewall rule addition, deletion, policy changes |
| `CAP_SYS_TIME`  | 7 | Modifying hardware RTC and system wall-clock |
| `CAP_AUDIT`     | 8 | Reading security audit event ring buffer |
| `CAP_DEV_RAW`   | 9 | Direct block device I/O (`sys_blk_read`, `sys_blk_write`)|

---

## 4. Syscall Filtering Sandbox

Applications can voluntarily drop access to unnecessary system calls via `set_syscall_filter()`:
* **Allowlist Mode (`mode = 1`):** Only syscalls whose bit is set in the 128-bit filter mask are permitted (with `exit` and `yield` implicitly allowed).
* **Denylist Mode (`mode = 2`):** Syscalls with their bit set are blocked with `-EPERM`.
* **Locking:** Once configured, the filter is locked (`syscall_filter_locked = true`) and cannot be disabled or widened by the sandboxed process.
