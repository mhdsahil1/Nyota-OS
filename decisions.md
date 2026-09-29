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

---

## 6. Phase 9 Part 2: Service Management, Logging & Time Subsystem

### 6.1. Service Supervision & Lifecycle Management
- **Architecture**:
  - Clean service supervision embedded inside PID 1 (`/sbin/init`), companion CLI daemon `/sbin/nyotad`, and control utility `/bin/service`.
  - Service metadata tracking: `name`, `command` (path), `PID`, `state` (`STOPPED`, `STARTING`, `RUNNING`, `STOPPING`, `FAILED`), `exit_status`, `start_time`, `last_start_time`, `last_exit_time`, `restart_count`, `restart_policy` (`never`, `always`, `on-failure`), `depends`.
- **Dynamic Configuration (`/etc/init.conf`)**:
  - Parsed dynamically at boot with automatic default fallback.
  - Supports syntax: `service <name> <path> [restart=<never|always|on-failure>] [depends=<dep_service>]`.
- **Dependency Resolution**:
  - Before starting a service, dependencies declared via `depends=` are validated.
  - If a dependency is not running, the supervisor attempts to start it first. If the dependency fails to start, the dependent service is aborted and marked `FAILED`.
- **Graceful Termination & Stopping**:
  - `stop_service()` sends `SIGTERM`, waits via `waitpid(WNOHANG)` across a polling window, escalates to `SIGKILL` if uncooperative, and updates state to `STOPPED`. Manually stopped services are prevented from auto-restarting.

### 6.2. Crash Loop Protection & Autonomous Recovery
- **Autonomous Recovery**:
  - Terminated children generate `SIGCHLD` and wake PID 1.
  - Child exit status is captured via `waitpid(-1, &status, WNOHANG)`.
  - Supervisor evaluates restart policy: `RESTART_ALWAYS` or `RESTART_ON_FAILURE` (triggered when `exit_status != 0`).
- **Crash Loop Protection**:
  - Rapidly failing services increment `restart_count` on each crash cycle.
  - If a service exceeds 5 restarts within a short window, the supervisor transitions the service state to `FAILED`, logs `restart limit reached`, and prevents infinite restart loops that consume CPU.
  - Stable runs (>30s) automatically reset `restart_count` to zero.
  - `reap_children()` is executed immediately upon receiving IPC control messages, guaranteeing that queries (`service list`, `service status`) always reflect the freshest state.

### 6.3. Kernel Logging Subsystem & Userland Daemons
- **Kernel Ring Buffer (`klog`)**:
  - Bounded circular buffer (`kernel/logging.c`) with levels `DEBUG`, `INFO`, `WARN`, `ERROR`, `PANIC` and monotonic millisecond timestamps.
  - Safe wraparound overwrites oldest entries without unbounded memory consumption.
- **System Logger Daemon (`/sbin/loggerd`)**:
  - Periodically drains `klog()` ring buffer into `/var/log/kernel.log`.
  - Listens on `/run/logger.sock` (`AF_UNIX`) and records system messages into `/var/log/system.log`.
  - Ensures initial creation of `/var/log/kernel.log`, `/var/log/system.log`, and `/var/log/services.log`.
- **Bounded Log Rotation**:
  - When log files exceed 16 KiB, `loggerd` rotates `<file>` to `<file>.old` and truncates `<file>` with `[LOG ROTATED]`, preventing disk exhaustion.
- **Service Lifecycle Event Logging**:
  - Lifecycle state transitions (`starting`, `running pid=`, `stopped`, `exited status=`, `restarting`, `restart limit reached`) are formatted as `[HH:MM:SS] <LEVEL> service: <msg>` and written directly to the console, `klog()`, and `/var/log/services.log`.
- **Userland `logger` Utility (`/bin/logger`)**:
  - Submits log entries via `/run/logger.sock`, directly appends to `/var/log/system.log`, and mirrors to `klog()`.

### 6.4. Hardware Time & Timekeeping Subsystem
- **CMOS Real-Time Clock (`kernel/time/rtc.c`)**:
  - Interfaces directly with Motorola 146818 CMOS RTC via I/O ports `0x70` and `0x71`.
  - Detects Binary vs. BCD encoding and 12-hour vs. 24-hour formats; converts to UTC civil date.
  - Computes Unix epoch seconds from civil year, month, day, hour, minute, second.
- **Monotonic Clock (`kernel/time/clock.c`)**:
  - Maintained independently from wall-clock time using PIT timer ticks.
  - Guaranteed never to go backwards; used for scheduler timing, sleeps, timeouts, and uptime.
- **Time Syscalls**:
  - `SYS_TIME` (44): Returns Unix epoch seconds.
  - `SYS_CLOCK_GETTIME` (45): Supports `CLOCK_REALTIME` and `CLOCK_MONOTONIC` into `struct timespec`.
  - `SYS_NANOSLEEP` (46): Validates user pointers and yields CPU via `scheduler_sleep()`.
- **Userland Date & Uptime Utilities**:
  - `/bin/date`: Displays calendar date and time formatted as `YYYY-MM-DD HH:MM:SS`.
  - `/bin/uptime`: Displays system uptime formatted as `up HH:MM:SS`.

### 6.5. Subsystem Bug Fixes & Discovered Edge Cases
1. **Sleep Queue Corruption in `signal_send()` (`kernel/process/signal.c`)**:
   - *Bug*: When waking a process from `PROCESS_SLEEPING`, `signal_send()` directly modified `state = PROCESS_READY` and called `scheduler_add(target)` without unlinking from `sleep_queue`. This corrupted `sleep_queue`, causing timer ticks to corrupt the ready queue and freeze sleeping processes (such as `sh` or `init`).
   - *Fix*: Changed `signal_send()` to call `scheduler_wake(target)`, which cleanly unlinks the target from `sleep_queue` before enrolling into the scheduler ready queue.
2. **`process_exit()` Non-Halting Execution (`kernel/process/process.c`)**:
   - *Bug*: `process_exit()` marked the process as `PROCESS_ZOMBIE`, removed it from the scheduler, and returned to `signal_check_and_deliver()`. Because it returned to `syscall_handler()`, the CPU IRETed back to Ring 3 userspace, allowing the terminated process to resume executing and overwrite its state.
   - *Fix*: Added `while (1) { __asm__ volatile ("sti; hlt"); }` at the end of `process_exit()`, ensuring terminated processes never resume execution in userland and cleanly wait for context switch.
3. **`sys_handle_nanosleep()` Non-Spinning Sleep (`kernel/arch/x86_64/syscall.c`)**:
   - *Bug*: `sys_handle_nanosleep()` invoked `timer_sleep()`, which spun on `hlt` without yielding CPU.
   - *Fix*: Switched to `scheduler_sleep(ms)`, transitioning calling process to `PROCESS_SLEEPING` and yielding the CPU to other ready processes.

### 6.6. Verification & Regression Suites
1. **`tools/test_phase9_part2.py` (Phase 9 Part 2 Service & Time Suite)**:
   - Verified RTC initialization and UTC wall-clock time decoding.
   - Verified `/bin/date` format (`YYYY-MM-DD HH:MM:SS`) and `/bin/uptime` format (`up HH:MM:SS`).
   - Verified `/bin/service list` formatted table (`SERVICE`, `PID`, `STATE`, `RESTARTS`).
   - Verified `service status`, `service stop`, `service start`, and `service restart`.
   - Verified `/sbin/nyotad` service manager companion interface.
   - Verified persistent logs in `/var/log/services.log`, `/var/log/kernel.log`, `/var/log/system.log`.
   - Verified `/bin/logger` utility writes to `/var/log/system.log`.
   - Verified SIGCHLD crash recovery with automatic restarting.
   - Verified crash loop protection: repeated failures cap at 5 restarts and transition to `FAILED`.
   - **Result**: **ALL 26 CHECKS PASS (100%)**.
2. **`tools/test_phase9_part1.py`**:
   - **Result**: **ALL 19 CHECKS PASS (100%)**.
3. **`tools/test_security.py`**:
   - **Result**: **ALL 25 CHECKS PASS (100%)**.


