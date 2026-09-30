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
- [ ] Interactive visual/input walkthrough and 5+ application display stress validation
