# Nyota OS — Architectural Decisions & Changelog

## Phase 9: Advanced Userland, System Services & Service Management

This document records the architectural decisions, design rationale, subsystem modifications, bug fixes, and comparative differences between **Phase 8** (*IPC, Security Hardening & Process Isolation*) and **Phase 9** (*Advanced Userland, System Services & Service Management*).

---

## 1. High-Level Summary: Phase 8 vs. Phase 9

| Feature / Subsystem | Phase 8 (Previous Stage) | Phase 9 (Current Stage) | Why Changed / Rationale |
| :--- | :--- | :--- | :--- |
| **PID 1 (Init)** | Ad-hoc bare shell (`/bin/sh`) or test runner launched as PID 1 directly by kernel. | Dedicated Userspace Supervisor (`/init`) initialized as PID 1. | Needed true service supervision, auto-restarting of crashed services, and daemon management. |
| **Service Architecture** | None. All processes were executed manually from the prompt or test scripts. | Multi-daemon system architecture: `loggerd`, `ttyd`, `netd`, `logind`. | Modular separation of concerns for logging, networking, terminal control, and login sessions. |
| **Terminal Subsystem** | Direct raw VGA and serial mirror without terminal line discipline. | Full TTY subsystem (`/dev/tty0`..`/dev/tty3`), canonical & raw modes, backspace, echo, signals. | Standard POSIX-like terminal behavior, job control, and multi-TTY readiness. |
| **Authentication & Users** | Single root user, hardcoded or absent user context. | Multi-user support with `/etc/passwd`, `logind` login prompt, UID/GID assignment, home directories (`/home/sahil`, `/home/root`). | Secure userland separation, user privilege management, and realistic OS login workflow. |
| **System Configuration** | Static and minimal (`/etc/nyota.conf`). | Declarative configs: `/etc/init.conf`, `/etc/network.conf`, `/etc/services.conf`, `/etc/passwd`, `/etc/profile`, `/etc/hostname`. | Eliminates hardcoded kernel/userland constants; enables runtime configurability. |
| **Local IPC Sockets** | Only Anonymous Pipes and Shared Memory. | Added `AF_UNIX` (Unix Domain Sockets) supporting `SOCK_STREAM` IPC (e.g. `/run/init.sock`). | Enables daemon-to-client control protocols (such as `service list`, `service restart`). |
| **Time & Clock** | PIT timer ticks and relative system uptime only. | Hardware RTC (CMOS ports 0x70/0x71) decoding, BCD conversion, Unix epoch time (`time_t`), UTC clock. | Allows standard date/time utilities (`date`), file timestamps, and log record time-stamping. |
| **Userspace Standard Library** | Basic `printf` with `%s`, `%d`, `%x`, `%c` without width or padding support. | Enhanced `printf` with width and zero-padding (`%02d`, `%04d`, `%4d`), environment variable APIs (`getenv`, `setenv`), non-blocking `waitpid(WNOHANG)`. | Clean tabular output for `ps`, `free`, `uptime`, `date`, `sysinfo`, and non-blocking service polling. |
| **Shell Experience** | Minimal prompt `nyota$ `, basic command execution. | Interactive prompt `[sahil@nyota ~]$ `, environment variable expansion (`$VAR`), builtins (`export`, `jobs`, `fg`, `bg`). | Productive, modern interactive UNIX shell experience. |
| **System Utilities** | Minimal diagnostic set (`hello`, `memtest`, `ipctest`, `stressproc`, `crash`). | Rich userland suite: `uname`, `date`, `uptime`, `hostname`, `ps`, `free`, `df`, `sysinfo`, `service`, `logger`, `reboot`, `shutdown`. | Provides complete administrative inspection and control of kernel and userland resources. |

---

## 2. Key Architectural Decisions & Why They Were Made

### 2.1. Dedicated `/init` Process as PID 1
- **Problem in Phase 8**: The kernel was responsible for directly choosing whether to load `/bin/sh` or test harnesses. When child processes exited, there was no centralized supervisor to reap zombies or restart crashed daemons.
- **Decision in Phase 9**: The kernel exclusively launches `/init` as PID 1. `/init` parses `/etc/init.conf` and `/etc/services.conf`, spawns critical daemons (`loggerd`, `ttyd`, `netd`, `logind`), tracks their PIDs, reaps terminated processes via `waitpid()`, and auto-restarts failed services.
- **Control Socket**: `/init` binds an `AF_UNIX` domain socket at `/run/init.sock` to service requests from the userspace `service` command (e.g., `service list`, `service restart <name>`).

### 2.2. Session Login Daemon (`logind`) and User Separation
- **Problem in Phase 8**: Shell had full access immediately upon boot without authentication or credential setup.
- **Decision in Phase 9**: `/init` starts `logind` on `tty0`. `logind` presents `tty0 login:`, verifies credentials against `/etc/passwd`, resolves user metadata (UID, GID, home directory, preferred shell), sets terminal ownership via `tty_ctrl()`, and executes the user shell (`/bin/sh`).

### 2.3. TTY Terminal Driver and Line Discipline
- **Problem in Phase 8**: VGA output and serial input were coupled with no canonical editing buffer. Backspace and control characters were not handled cleanly.
- **Decision in Phase 9**: Created `kernel/drivers/tty.c` and `include/drivers/tty.h` providing 4 virtual terminals (`tty0`..`tty3`). Features include:
  - **Canonical Mode (`ICANON`)**: Line editing buffer that commits only on Enter (`\n` or `\r`), correctly processing backspaces (`\b`, `0x7F`).
  - **Echo Control (`ECHO`)**: Automatically mirrors typed characters to VGA/serial console.
  - **Signal Generation (`ISIG`)**: Intercepts `Ctrl+C` (ASCII 3) to deliver `SIGINT` and `Ctrl+\` (ASCII 28) to deliver `SIGQUIT` to the active foreground process group.

### 2.4. `AF_UNIX` Domain Sockets
- **Problem in Phase 8**: Local process-to-process streaming required anonymous pipes or shared memory, neither of which allowed client processes to dynamically rendezvous with a named service without an inherited file descriptor.
- **Decision in Phase 9**: Extended the socket engine in `kernel/net/socket.c` to support `AF_UNIX` with `struct sockaddr_un`. Clients can connect to named file-based endpoints like `/run/init.sock` or `/dev/log`.

### 2.5. CMOS RTC Real-Time Wall Clock
- **Problem in Phase 8**: The system only understood PIT timer ticks and relative seconds since boot. There was no real wall-clock date or calendar time.
- **Decision in Phase 9**: Implemented `kernel/time/rtc.c` to read CMOS registers via I/O ports `0x70` and `0x71`. Handled BCD decoding, Century/Status register inspection, leap year calculations, and converted the hardware clock into Unix Epoch seconds (`time_t`). Exposed via syscalls `SYS_GETTIMEOFDAY` and `SYS_CLOCK_GETTIME`.

---

## 3. Subsystem Modifications & Bug Fixes

### 3.1. Kernel Driver & Syscall Layer Fixes

#### A. TTY Read Sleep Loop (`kernel/drivers/tty.c`)
- **Bug**: `tty_read()` set `tty->waiters = curr;` and then cleared `tty->waiters = NULL;` immediately upon yielding. If a keyboard interrupt fired after yielding, the waiter was already wiped out, leaving the reading process asleep permanently.
- **Fix**: Updated `tty_read()` to keep the waiter registered while sleeping:
  ```c
  tty->waiters = curr;
  curr->state = PROCESS_SLEEPING;
  scheduler_remove(curr);
  scheduler_request_reschedule();

  while (curr->state == PROCESS_SLEEPING && tty->in_count == 0) {
      __asm__ volatile ("sti; hlt");
  }
  tty->waiters = NULL;
  curr->state = PROCESS_RUNNING;
  ```

#### B. Pipe Waiter Sleep Loop (`kernel/ipc/pipe.c`)
- **Bug**: Identical to the TTY bug, `pipe_read()` and `pipe_write()` prematurely set their waiter pointers to `NULL`, leading to hanging threads during blocking pipe operations.
- **Fix**: Synchronized writer and reader waiter pointers with loop guards and waking transitions.

#### C. `AF_UNIX` Syscall Buffer Truncation (`kernel/arch/x86_64/syscall.c`)
- **Bug**: `sys_handle_bind` and `sys_handle_connect` copied only `sizeof(struct sockaddr_in)` (16 bytes) from user space. Because `struct sockaddr_un` starts its path string at offset 2 (`sun_path[108]`), a 15-byte path like `/run/init.sock` was truncated to 14 bytes (`/run/init.soc`), breaking all socket bindings.
- **Fix**: Changed the copy size in `sys_handle_bind` and `sys_handle_connect` to `sizeof(struct sockaddr_un)` (110 bytes).

### 3.2. Userspace C Library Enhancements (`user/libnyota/`)

#### A. `printf()` Width & Zero-Padding (`user/libnyota/io.c`)
- **Bug**: `printf()` did not support width or padding flags (e.g., `%02d`, `%04d`, `%4d`). System utilities like `date` (e.g. `2026-9-29 4:5:7` instead of `2026-09-29 04:05:07`) and `free`/`uptime` displayed unaligned and malformed output.
- **Fix**: Implemented `print_dec_padded()` and parsed width and leading `'0'` flags in `printf()`.

#### B. Non-blocking `waitpid(WNOHANG)` (`user/libnyota/syscall.c`)
- **Bug**: `waitpid(pid, status)` looped indefinitely in user space waiting for a process to terminate. When `/init` called `waitpid(-1, &status)` to poll for dead child services, it locked up PID 1 because all services were still running.
- **Fix**: Implemented non-blocking check:
  ```c
  int waitpid(int pid, int *status) {
      if (pid == -1) {
          return (int)syscall3(15, (uint64_t)pid, (uint64_t)status, 1 /* WNOHANG */);
      }
      while (1) {
          int64_t ret = syscall3(15, (uint64_t)pid, (uint64_t)status, 0);
          if (ret != 0) return (int)ret;
          sleep(20);
      }
  }
  ```

---

## 4. Test Suite Modernization & Results

All automated test suites were updated to account for the Phase 9 login prompt and userspace supervisor environment:

1. **`tools/test_services.py` (Phase 9 Primary Suite)**:
   - **35 checks executed**:
     - Kernel boot banner, version (`v0.9.0`), architecture (`x86_64`), RTC, TTY, security checks.
     - Init supervisor launch and daemon spawning (`loggerd`, `ttyd`, `netd`, `logind`).
     - Login prompt reception, credentials acceptance, session launch.
     - System utility verification: `uname`, `date`, `uptime`, `hostname`, `ps`, `free`, `df`, `sysinfo`, `service list`.
     - Shell features: `export`, `$VAR` expansion, `pwd`, `cd`, `ls /etc`, `cat /etc/hostname`.
     - Background job control (`echo-server 8080 &`, `ps`).
     - Phase 8 regressions: `secinfo`, crash recovery after `SIGSEGV`.
   - **Result**: **35 / 35 PASS (100%)**.

2. **`tools/test_network.py` (Phase 7 Regression Suite)**:
   - Verified `ifconfig`, loopback ping (`127.0.0.1`), gateway ping (`10.0.2.2`), `netstat`, `nslookup`.
   - **Result**: **ALL CHECKS PASS**.

3. **`tools/test_security.py` (Phase 8 Regression Suite)**:
   - Verified kernel/user isolation, guard pages, pointer validation, capabilities, limits, pipes, shared memory, pipeline execution (`ls | cat`), background jobs, crash containment, memtest, and `stressproc`.
   - **Result**: **ALL 25 CHECKS PASS (100%)**.

4. **`tools/test_runner.py` (General Userspace Regression Suite)**:
   - Verified Ring 3 process execution, VFS file creation in `/tmp`, stat, spawn arguments, and memory safety.
   - **Result**: **ALL 13 CHECKS PASS (100%)**.

5. **`tools/test_internal_tcp.py` (TCP Loopback Regression Suite)**:
   - Verified loopback TCP echo server and `netcat` payload exchange.
   - **Result**: **PASS**.

6. **`tools/test_shell.py` (Interactive Shell Regression Suite)**:
   - Verified login flow, prompt arrival, and builtin `help` command.
   - **Result**: **PASS**.

7. **`tools/test_phase9_part1.py` (Phase 9 Part 1 Primary Suite)**:
   - Verified `waitpid(WNOHANG)` on running child (returns 0 without blocking).
   - Verified `waitpid(WNOHANG)` on exited child (reaps zombie child and returns exit status).
   - Verified `waitpid` on invalid/nonexistent PID (returns -ECHILD / -ESRCH).
   - Verified `waitpid(-1, WNOHANG)` reaping multiple children.
   - Verified zombie process lifecycle (child enters `PROCESS_ZOMBIE`, reaped, slot freed).
   - Verified `SIGCHLD` signal delivery to parent on child termination.
   - Verified orphan process reparenting to PID 1 (`/sbin/init`) and subsequent reaping by PID 1.
   - Verified anonymous pipe blocking read and wakeup on writer activity.
   - Verified anonymous pipe EOF on writer closure and `SIGPIPE` delivery on reader closure.
   - Verified TTY blocking on empty read and wakeup on keyboard input.
   - Verified blocked processes enter `PROCESS_SLEEPING` without busy waiting or CPU waste.
   - **Result**: **ALL 19 CHECKS PASS (100%)**.

---

## 5. Phase 9 Part 1: Process Lifecycle, Blocking, SIGCHLD & PID 1

### 5.1. Non-Blocking `waitpid(WNOHANG)` and Kernel Sleep Wait Queues
- **Rationale**: Userland previously relied on a `sleep(20)` loop in `libnyota` to poll child processes. This burned cycles and delayed reaping.
- **Implementation**:
  - Added `#define WNOHANG 1` in `syscall.h`, `process.h`, and `libnyota.h`.
  - Overloaded `waitpid(pid, status, [options])` in userspace using C variadic macros so legacy 2-argument calls compile transparently without warnings while supporting 3-argument `WNOHANG` calls.
  - Implemented kernel wait queues in `process_waitpid()`: when `WNOHANG` is NOT passed, the calling process transitions to `PROCESS_SLEEPING` and yields CPU. When child exits, `process_exit()` wakes parent via `scheduler_wake(parent)`.
  - When `WNOHANG` is passed, `process_waitpid()` immediately returns `0` if children exist but none are zombies, or `-ECHILD` (-10) if no matching children exist.

### 5.2. Zombie Process Lifecycle & Child Reaping
- **Rationale**: When a child process terminates, its PID and exit status must persist until reaped by its parent, while all other resources (file descriptors, sockets, shared memory) must be reclaimed immediately.
- **Implementation**:
  - `process_exit()` marks process as `PROCESS_ZOMBIE`, preserves `exit_status`, closes open descriptors via `vfs_close_process_fds()`, detaches shared memory via `shm_process_cleanup()`, delivers `SIGCHLD` to parent, and wakes waiting parent.
  - `process_waitpid()` extracts `exit_status`, unlinks child from parent's children tree, and frees the process table slot.

### 5.3. Orphan Reparenting to PID 1 (`/sbin/init`)
- **Rationale**: If a parent process terminates while children are still executing, those children must not become unreapable zombies with dangling pointers.
- **Implementation**:
  - `process_exit()` iterates through `curr->children`, reparents each child to PID 1 (`init_proc`), updates `child->parent_pid = 1`, and if the child is already a zombie, wakes PID 1.
  - PID 1 (`/sbin/init`) supervisor loop reaps all terminated children non-blockingly using `while ((pid = waitpid(-1, &status, WNOHANG)) > 0)`.

### 5.4. Signal Stack Alignment & Return Delivery Fix
- **Bug Identified**: Custom signal delivery was decrementing user stack pointer by 16 bytes (`sp -= 2`) before writing `frame->rip`. Because `ret` pops 8 bytes, the user stack was corrupted by 8 bytes across signal handler invocation, causing a page fault at `0x0` upon returning from the interrupted function.
- **Fix**: Adjusted to `sp -= 1` (`rsp - 8`) in `signal_check_and_deliver()`, matching standard x86_64 `CALL` / `RET` ABI semantics and maintaining 16-byte stack alignment.
- **Scheduler Signal Delivery**: Added `signal_check_and_deliver(frame)` to `interrupt_dispatch()` in `dispatcher.c` whenever returning to Ring 3 User Mode, ensuring asynchronous signals delivered while a thread was asleep are handled immediately upon resumption.

### 5.5. Pipe Blocking & Error Sign Correction
- **Implementation**:
  - `pipe_read()` transitions caller to `PROCESS_SLEEPING` when pipe buffer is empty, waking upon write activity or EOF.
  - `pipe_write()` transitions caller to `PROCESS_SLEEPING` when pipe buffer is full (`PIPE_CAPACITY`), waking upon read activity or closed readers (`SIGPIPE`).
  - Fixed erroneous sign negation on `SYS_ERR_*` constants in `kernel/ipc/pipe.c` (which were already defined negative in `syscall.h`).

