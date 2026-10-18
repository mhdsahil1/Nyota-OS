p   - Verified anonymous pipe EOF on writer closure and `SIGPIPE` delivery on reader closure.
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

---

## 7. Phase 9 Part 3: TTY, Environment, Process Groups, Sessions & Login

### 7.1. Architectural Overview & Design Decisions

#### A. Process Environment Block & Inheritance
- **Problem**: Child processes in earlier phases had no way to receive or pass environment variables. `getenv()` and `setenv()` operated only on ad-hoc userspace memory that was lost upon `spawn` or `exec`.
- **Decision**: Added `char env[PROCESS_ENV_MAX_VARS][PROCESS_ENV_MAX_LEN]` and `uint32_t env_count` directly to `process_t`. Extended `SYS_SPAWN2` and `process_execve()` to accept an array of environment variable strings. Children automatically inherit parent environment blocks on spawn unless an explicit `envp` is provided. `setup_user_stack()` places `envp` pointers and strings onto the user stack following System V AMD64 ABI specifications.

#### B. Current Working Directory & Path Normalization
- **Problem**: File system paths were required to be absolute. Relative paths such as `cd ..`, `cd dir`, or relative script executions failed.
- **Decision**: Added `char cwd[128]` to `process_t`. Implemented `vfs_normalize_path()` in `kernel/fs/vfs.c`, which resolves relative paths against the process's `cwd`, evaluates `.` (current directory) and `..` (parent directory), and normalizes redundant slashes into a canonical path.

#### C. TTY Line Discipline & Canonical Mode
- **Problem**: Keystrokes were passed raw to readers without line editing, character erase, or terminal signal delivery.
- **Decision**: Enhanced `kernel/drivers/tty.c`:
  - **Line Discipline Buffer**: Stores up to 256 characters in canonical mode (`ICANON`). Characters are buffered and only committed to the input queue when `\n` or `\r` is received.
  - **Interactive Editing**: Backspace (`\b` or `0x7F`) erases the previous character from the line buffer and sends `\b \b` to VGA/serial.
  - **Signal Generation (`ISIG`)**: Intercepts `Ctrl+C` (`\x03`) to broadcast `SIGINT` and `Ctrl+Z` (`\x1A`) to broadcast `SIGTSTP` to the terminal's foreground process group (`tty->foreground_pgrp`).
  - **EOF**: `Ctrl+D` (`\x04`) flushes buffered input or returns EOF (0 bytes) if the buffer is empty.
  - **Background Input Protection**: If a process whose `pgrp` does not match `tty->foreground_pgrp` attempts to read from the terminal, `tty_read()` delivers `SIGTTIN` and blocks the process.

#### D. Process Groups, Sessions & Job Control
- **Problem**: The system had no grouping concept for processes, preventing terminal job control, shell background execution, and session management.
- **Decision**:
  - Added `pgrp`, `sid`, and `controlling_tty` fields to `process_t`.
  - Added syscalls `SYS_SETPGID` (47), `SYS_GETPGID` (48), `SYS_SETSID` (49), and `SYS_GETSID` (50).
  - Implemented `signal_send_pgrp()` to broadcast signals to all processes in a group.
  - Added `PROCESS_STOPPED` state and `WUNTRACED` option in `process_waitpid()` to report stopped children to the shell.
  - Implemented `jobs`, `fg`, and `bg` builtins in `/bin/sh`. Background commands launched with `&` run in separate process groups without capturing terminal foreground control.

#### E. Multi-User Authentication & Login Daemon (`/sbin/logind`)
- **Problem**: The shell booted directly without user authentication or initialization of user-specific environment variables.
- **Decision**: Created `/sbin/logind` service started by PID 1.
  - Configured `/etc/passwd` containing `root`, `sahil`, and `user` accounts with UID, GID, home directory, and default shell.
  - `logind` presents `tty0 login:`, reads username, reads password (with echo suppressed), verifies against `/etc/passwd`, creates a new session via `setsid()`, sets user credentials (`setuid`, `setgid`), populates user environment (`USER`, `HOME`, `SHELL`, `PWD`), and launches the user's shell as the foreground process group.

### 7.2. Discovered Bugs & Critical Fixes

1. **`strncpy()` Buffer Overflow & Unsigned Underflow (`kernel/memory/memory.c`)**:
   - *Bug*: `strncpy()` used `while (n && (*dest++ = *src++)) { n--; }` followed by `while (n--) { *dest++ = '\0'; }`. If `*src == '\0'` terminated the first loop, `n--` in the first loop condition was skipped, leaving `n` unchanged while `dest` had already advanced. Then `while (n--)` padded `n` additional null bytes, exceeding the allocated buffer by 1 byte. When `n == 0`, `n--` underflowed `size_t` to `SIZE_MAX`, causing memory corruption that froze kernel initialization during PID 1 loading.
   - *Fix*: Replaced with safe indexed implementation:
     ```c
     char *strncpy(char *dest, const char *src, size_t n) {
         size_t i;
         for (i = 0; i < n && src[i] != '\0'; i++) {
             dest[i] = src[i];
         }
         for (; i < n; i++) {
             dest[i] = '\0';
         }
         return dest;
     }
     ```

2. **Idle Process (PID 0) Environment Inheritance Hang (`kernel/process/process.c`)**:
   - *Bug*: At boot time, `process_get_current()` returns `idle_proc` (`pid == 0`). `process_create_from_elf_env()` evaluated `else if (curr && curr->env_count > 0)`. Because `idle_proc_storage` had uninitialized or stale bytes in `env_count`, PID 1 tried to copy corrupt environment memory and hung.
   - *Fix*: Added `curr->pid > 0` checks before inheriting credentials, environment, or parent status:
     ```c
     else if (curr && curr->pid > 0 && curr->env_count > 0) { ... }
     if (curr && curr->pid > 0) { ... } else { /* root PID 1 defaults */ }
     ```

3. **Interactive Shell SIGINT Termination (`user/sh/main.c`)**:
   - *Bug*: When a user pressed `Ctrl+C` at an empty shell prompt or to interrupt input, `sh` received `SIGINT` with `SIG_DFL` action and terminated, closing the session.
   - *Fix*: Added `signal(SIGINT, sigint_handler)` in `main()` to print a newline and refresh the prompt, and set `signal(SIGTSTP, SIG_IGN)` to prevent the interactive shell from stopping itself on `Ctrl+Z`.

4. **Missing `SIGTSTP` Define in Userspace Library (`user/libnyota/libnyota.h`)**:
   - *Bug*: `SIGTSTP` (20) was defined in kernel headers but missing from `libnyota.h`, preventing user applications from using standard job control signal constants.
   - *Fix*: Added `#define SIGTSTP 20` to `libnyota.h`.

### 7.3. Verification & Test Results
- **`tools/test_phase9_part3.py`**: **19/19 Passed (100.0%)**
  - Boot logind prompt verification (`tty0 login:`)
  - Login authentication as `sahil`
  - Default environment setup (`USER`, `HOME`, `SHELL`, `PATH`, `PWD`)
  - Shell `export` builtin
  - Shell `cd` navigation (`..`, `/`, `~`, `-`)
  - Background process execution (`&`) and job identification (`[1] <pid>`)
  - Job control listing (`jobs`)
  - Process group and session visibility in `ps`
  - TTY line discipline: Ctrl+C interruption (`SIGINT`)
  - TTY line discipline: backspace editing
- **`tools/test_security.py`**: **25/25 Passed (100.0%)** (Full security, isolation, ASLR, capabilities, guard pages, IPC suite)
- **`tools/test_phase9_part1.py`**: **19/19 Passed (100.0%)** (Process lifecycle, blocking waits, zombie reaping, orphan reparenting)
- **`tools/test_phase9_part2.py`**: **26/26 Passed (100.0%)** (Services, daemons, loggerd, ttyd, netd, logind, RTC clock, crash recovery)

---

## 8. Phase 9 Part 4: Unix Sockets, Network Management, Shutdown & Final Integration

### 8.1. Architectural Overview & Design Decisions

#### A. Unix Domain Sockets (`AF_UNIX`) & VFS Socket Files
- **Problem**: In Phase 8 and early Phase 9, `AF_UNIX` sockets lived only inside the kernel socket table. While clients could connect to bound paths, directory listings like `ls /run` showed an empty folder, and `stat()` on socket paths failed, conflicting with standard Unix filesystem semantics.
- **Decision**:
  - Implemented `vfs_create_entry()` in `kernel/fs/vfs.c`.
  - When `socket_bind()` binds an `AF_UNIX` socket with a pathname in `sun_path`, it automatically invokes `vfs_create_entry(path, 0140666)` (`S_IFSOCK`), creating an in-memory directory entry in NyotaFS.
  - Sockets bound to `/run/init.sock`, `/run/logger.sock`, and `/run/netd.sock` are visible to `ls /run`, userland tools, and permission checks.

#### B. Runtime State Directory (`/run`) & PID Files
- **Problem**: Daemons and supervisors needed a dedicated volatile location for sockets and PID files that is separated from persistent storage (`/var`, `/etc`).
- **Decision**:
  - Created `/run` directory at filesystem root.
  - Daemons write their PID into `/run/<service>.pid` upon successful startup:
    - `/run/loggerd.pid`
    - `/run/netd.pid`
    - `/run/logind.pid`
  - Added `SYS_UNLINK` (59) syscall and userspace `unlink()` in `libnyota` to enable clean unlinking of PID files and sockets when daemons terminate or when PID 1 shuts down.

#### C. Network Management Daemon (`/sbin/netd`) & Configuration
- **Problem**: Network configuration was previously hardcoded or set ad-hoc by userspace scripts.
- **Decision**:
  - Created `/sbin/netd` daemon supervised by PID 1.
  - Parses static network settings from `/etc/network.conf`:
    - `interface=eth0`
    - `ip=10.0.2.15`
    - `netmask=255.255.255.0`
    - `gateway=10.0.2.2`
    - `dns=10.0.2.3`
  - Creates `/run/netd.pid` and listens on `AF_UNIX` socket `/run/netd.sock`.
  - Serves IPC queries (`status`, `ip`, `iface`) to administrative utilities such as `/bin/netstat`.

#### D. Filesystem Sync (`sync()`) & Buffer Flushing
- **Problem**: Before shutdown or reboot, dirty filesystem pages and cached block buffers must be flushed to persistent storage to prevent metadata or data corruption.
- **Decision**:
  - Implemented `vfs_sync()` in `kernel/fs/vfs.c`.
  - Added `SYS_SYNC` (36) syscall in `kernel/arch/x86_64/syscall.c` with userspace wrapper `sync()` in `libnyota`.
  - Created `/bin/sync` binary utility and built-in `sync` command in `/bin/sh`.
  - PID 1 automatically invokes `sync()` during the shutdown sequence before hardware halt/reset.

#### E. Controlled Shutdown, Reboot & PID 1 State Machine
- **Problem**: Arbitrary processes directly executing hardware reboot or halt could leave services half-terminated, processes orphaned, and files un-synced.
- **Decision**:
  - **Privilege Checking**: `SYS_REBOOT` (35) validates caller privileges, requiring UID 0 or `CAP_SYS_ADMIN`.
  - **PID 1 Shutdown Coordination**:
    - Created three-state lifecycle in PID 1 (`/init`):
      `INIT_RUNNING` $\rightarrow$ `INIT_SHUTTING_DOWN` $\rightarrow$ `INIT_HALTED`.
    - `/bin/reboot` and `/bin/shutdown` send `"reboot"` or `"shutdown"` commands over the `/run/init.sock` Unix domain socket.
    - When shutdown is triggered, PID 1 transitions to `INIT_SHUTTING_DOWN`, rejecting any new service start requests.
    - PID 1 gracefully terminates all supervised services in reverse dependency order (`logind` $\rightarrow$ `netd` $\rightarrow$ `ttyd` $\rightarrow$ `loggerd`) by sending `SIGTERM`, waiting up to 500ms, and escalating to `SIGKILL` if necessary.
    - Reaps all remaining child processes.
    - Cleans up runtime state (`/run/*.pid`, `/run/*.sock`).
    - Flushes filesystem buffers via `sync()`.
    - Transitions to `INIT_HALTED` and invokes `reboot()`.

---

### 8.2. Complete Nyota OS Architecture Diagram (Phase 9 Milestone `v0.9.0`)

```text
+---------------------------------------------------------------------------------------------------+
|                                       HARDWARE LAYER                                              |
|   x86_64 CPU (Ring 0 / Ring 3)  |  PIT / RTC (CMOS 0x70/0x71)  |  E1000 NIC  |  UART Serial  | VGA |
+---------------------------------------------------------------------------------------------------+
                                                  |
+---------------------------------------------------------------------------------------------------+
|                                      NYOTA OS KERNEL                                              |
|                                                                                                   |
|  +--------------------+  +----------------------+  +---------------------+  +------------------+  |
|  |   Memory & Paging  |  | Process & Scheduler  |  |    IPC Subsystem    |  |     VFS Layer    |  |
|  | - 4-Level Paging   |  | - Preemptive Round-R |  | - Anonymous Pipes   |  | - Inode cache    |  |
|  | - Ring 0/3 Isol.   |  | - Sleep Queues       |  | - Shared Memory     |  | - Dir entries    |  |
|  | - ASLR Foundation  |  | - waitpid(WNOHANG)   |  | - Unix Domain Sockets|  | - /dev, /etc, /run|  |
|  | - Guard Pages      |  | - SIGCHLD / SIGPIPE  |  |   (AF_UNIX stream)  |  | - sync() flush   |  |
|  +--------------------+  +----------------------+  +---------------------+  +------------------+  |
|                                                                                                   |
|  +--------------------+  +----------------------+  +---------------------+  +------------------+  |
|  |     Networking     |  |     TTY Subsystem    |  |     Time Subsystem  |  |  Security/Caps   |  |
|  | - Ethernet / ARP   |  | - Canonical ICANON   |  | - RTC Wall Clock    |  | - UID/GID checks |  |
|  | - IPv4 / ICMP      |  | - Ctrl+C / Ctrl+Z    |  | - Monotonic Ticks   |  | - CAP_SYS_ADMIN  |  |
|  | - TCP / UDP stack  |  | - Line discipline    |  | - sys_clock_gettime |  | - Guard bounds   |  |
+---------------------------------------------------------------------------------------------------+
                                                  |
+---------------------------------------------------------------------------------------------------+
|                                    USERSUPERVISOR: PID 1 (/init)                                  |
|                                                                                                   |
|  - Reads /etc/init.conf and /etc/services.conf                                                    |
|  - Dependency Ordering: filesystem -> loggerd -> ttyd -> netd -> logind                           |
|  - Crash loop protection (capped at 5 restarts)                                                   |
|  - Reaps zombie/orphaned children via waitpid(-1, WNOHANG)                                        |
|  - Listens on /run/init.sock for service control & shutdown commands                              |
|  - State Machine: INIT_RUNNING -> INIT_SHUTTING_DOWN -> INIT_HALTED                              |
+---------------------------------------------------------------------------------------------------+
           |                     |                     |                     |
           v                     v                     v                     v
   +---------------+     +---------------+     +---------------+     +---------------+
   | /sbin/loggerd |     |  /sbin/ttyd   |     |  /sbin/netd   |     | /sbin/logind  |
   | - System logs |     | - TTY driver  |     | - Net config  |     | - /etc/passwd |
   | - Log rotation|     | - Session TTY |     | - /run/netd.pid|    | - Login prompt|
   | - /run/logger.pid   | - Line disc.  |     | - /run/netd.sock    | - Sets session|
   | - /run/logger.sock  +---------------+     +---------------+     | - /run/logind.pid
   +---------------+                                                 +---------------+
           |                                                                 |
           |                                                                 v
           |                                                        +-----------------+
           |                                                        |   /bin/sh       |
           |                                                        | (Login Shell)   |
           |                                                        +-----------------+
           |                                                                 |
           +--------------------+---------------------+                      |
                                |                     |                      |
                                v                     v                      v
                        +---------------+     +---------------+      +----------------+
                        |  /bin/logger  |     |  /bin/service |      | Job Control    |
                        | (IPC Logging) |     | (Daemon Ctrl) |      | - jobs, fg, bg |
                        +---------------+     +---------------+      | - Background & |
                                                                     | - Env: $VAR    |
                                                                     +----------------+
                                                                             |
                                                      +----------------------+--------------------+
                                                      |                                           |
                                                      v                                           v
                                              +---------------+                           +---------------+
                                              | Core Utilities|                           | System Admin  |
                                              | - uname       |                           | - reboot      |
                                              | - sysinfo     |                           | - shutdown    |
                                              | - free, df    |                           | - sync        |
                                              | - date, uptime|                           | - kill        |
                                              | - ps, hostname|                           | - ifconfig    |
                                              +---------------+                           +---------------+
```

---

### 8.3. Discovered Bugs & Critical Fixes

1. **`AF_UNIX` Socket File Visibility in VFS (`kernel/net/socket.c` & `kernel/fs/vfs.c`)**:
   - *Bug*: When a daemon bound an `AF_UNIX` socket to `/run/init.sock`, `/run/logger.sock`, or `/run/netd.sock`, the socket entry was recorded in the kernel socket table, but no corresponding directory entry was created in the NyotaFS VFS inode tree. Consequently, `ls /run` showed an empty directory, and tools expecting socket files could not detect their presence.
   - *Fix*: Created `vfs_create_entry()` in `kernel/fs/vfs.c` and integrated it into `socket_bind()` in `kernel/net/socket.c` whenever `domain == AF_UNIX` and `sun_path[0] == '/'`. The socket path now appears in directory listings with file type `S_IFSOCK` (0140000).

2. **PID 1 Shutdown Socket Initialization Ordering (`user/init/main.c`)**:
   - *Bug*: `/init` previously bound `/run/init.sock` *after* starting all supervised services. If `/bin/reboot` or `/bin/service` was invoked during the startup phase or if a service start hung, the IPC socket was not yet ready, causing client utilities to fail with `connection refused`.
   - *Fix*: Moved the creation and binding of `/run/init.sock` to the very beginning of `/init` before parsing `/etc/init.conf` and spawning background services, ensuring immediate IPC availability.

3. **Stale PID and Socket File Cleanup (`user/init/main.c` & `kernel/arch/x86_64/syscall.c`)**:
   - *Bug*: The kernel did not expose a `SYS_UNLINK` syscall, preventing daemons or PID 1 from deleting old PID files (`/run/netd.pid`, `/run/loggerd.pid`) or stale socket nodes between boots or after service stops.
   - *Fix*: Added `SYS_UNLINK` (59) mapped to `vfs_unlink()`, added `unlink()` in `libnyota`, and integrated cleanup routines in PID 1's `shutdown_system()` function.

---

### 8.4. Final Regression Suite Results

All five primary regression test suites pass 100% on the final integration build:

| Test Suite | Subsystem Coverage | Checks | Result |
| :--- | :--- | :---: | :---: |
| **`tools/test_security.py`** | Memory isolation, guard pages, ASLR, pointer validation, caps, limits, pipes, shm, crash recovery | 25 | **25 / 25 PASS (100%)** |
| **`tools/test_phase9_part1.py`** | `waitpid(WNOHANG)`, `SIGCHLD`, zombies, orphan reparenting, PID 1, blocking TTY/pipes | 19 | **19 / 19 PASS (100%)** |
| **`tools/test_phase9_part2.py`** | Service supervisor, states, restart policies, crash loop protection, `loggerd`, RTC, uptime | 26 | **26 / 26 PASS (100%)** |
| **`tools/test_phase9_part3.py`** | Environment variables, `cwd`, canonical TTY, Ctrl+C/Z/D, sessions, PGID, job control, `logind` | 19 | **19 / 19 PASS (100%)** |
| **`tools/test_phase9_part4.py`** | `AF_UNIX` sockets, `/run`, PID files, `netd`, `sync()`, `reboot`, `shutdown`, core utilities | 24 | **24 / 24 PASS (100%)** |
| **Total Combined** | **Full System Integration Milestone `v0.9.0`** | **113** | **113 / 113 PASS (100%)** |
# Phase 10 GUI Architecture

- The kernel owns framebuffer mapping and normalized PS/2 input; window policy, composition, desktop, toolkit, and applications run in Ring 3.
- A single registered window server owns global display flips, normalized input consumption, window enumeration, backing-store reads, focus/z-order policy, and event routing. Registration is exclusive to prevent another client from taking server authority.
- Window creation and updates remain process-owned; the kernel copies validated user pixels into private per-window backing stores. Process teardown destroys all windows owned by that PID.
- PID 1 starts the desktop service after core system services. The existing login shell and text console remain available when display startup fails.
- The initial display path targets QEMU VBE/Bochs 1024x768x32 linear framebuffer. The desktop currently uses a matching 32-bit mode.

---

# Phase 11 SMP & Multicore Architecture

- **ACPI MADT Discovery**: The kernel discovers processor topology via ACPI MADT (Multiple APIC Description Table) records rather than legacy MP tables, identifying processor APIC IDs, I/O APICs, and Interrupt Source Overrides dynamically.
- **AP Startup Sequence**: Application Processors are booted from physical reset via standard INIT-SIPI-SIPI. A dedicated real-mode trampoline at physical `0x8000` transitions each core from 16-bit real mode through 32-bit protected mode into 64-bit long mode with identity-mapped kernel PML4.
- **Hardware Timer Calibration**: Local APIC periodic timers are dynamically calibrated against the 100 Hz PIT reference clock over a 20 ms interval, eliminating host clock drift across diverse hardware and emulation environments.
- **Virtual Wire Interrupt Routing**: Legacy 8259 PIC interrupts are routed via Virtual Wire Mode (`LVT_LINT0` ExtINT and `LVT_LINT1` NMI) solely to the Bootstrap Processor (BSP), with APs masking both LVT registers to eliminate interrupt bus arbitration contention.
- **Per-CPU State Structure (`cpu_t`)**: Each core maintains independent kernel stacks, per-core GDT/TSS descriptors (`tss.rsp0` isolation), independent idle tasks (`PROCESS_IDLE`), context switch counters, timer tick accumulators, and independent run queue spinlocks.
- **Deadlock-Free Work Stealing**: In `scheduler_next()`, each core releases its local run-queue lock prior to scanning remote cores for work stealing, guaranteeing that no CPU ever holds more than one run-queue lock simultaneously and eliminating cyclic lock inversion deadlocks.
- **Inter-Processor Interrupts (IPIs)**: Implemented through Local APIC ICR:
  - `IPI_VECTOR_RESCHEDULE` (`0x50`): Immediate task preemption and rescheduling on target core.
  - `IPI_VECTOR_TLB_SHOOTDOWN` (`0x51`): Synchronous multi-core page invalidation with atomic acknowledgement counting.
  - `IPI_VECTOR_STOP` (`0x52`): Core halt for emergency shutdown and kernel panics.
- **Multicore System Calls & Diagnostic Utilities**:
  - `SYS_SMP_INFO` (64): Global CPU discovery and per-core telemetry.
  - `SYS_SCHED_SETAFFINITY` (65) / `SYS_SCHED_GETAFFINITY` (66): CPU affinity mask controls.
  - `/bin/smp`: Real-time multicore diagnostics and per-core process placement display.
  - `/bin/smpstress`: Multicore stress utility spawning workers pinned to distinct cores executing 1,000,000 shared memory operations with zero failures.

---

# Phase 19 AI-Assisted System Diagnostics Architecture

- **Kernel Entry BSS Zeroing (`kernel/kernel_entry.asm`)**:
  - *Context*: Flattened binary images created via `objcopy -O binary build/kernel.elf build/kernel.bin` do not record BSS section contents on disk. If the bootloader or physical RAM contains unzeroed memory, global and static uninitialized variables (such as `process_table_storage` and `trace_global_target_pid`) can boot with dirty non-zero state.
  - *Decision*: Added 64-bit BSS zeroing in `kernel_entry.asm` immediately after entering 64-bit long mode (`mov rdi, _bss_start; mov rcx, _bss_end; sub rcx, rdi; xor rax, rax; rep stosb`) before invoking `kernel_main`.
- **Non-Blocking TCP Connection Polling (`kernel/net/socket.c`)**:
  - *Context*: `socket_wait()` puts the calling process to sleep until explicitly woken by an incoming packet via `socket_wake()`. If an external endpoint is offline and sends neither SYN-ACK nor RST, the process remained blocked in `socket_wait()` indefinitely because the timeout check was never reached.
  - *Decision*: In `socket_connect()`, replaced `socket_wait()` with an interrupt-enabled poll loop (`e1000_poll_rx(); __asm__ volatile ("sti; hlt");`) bounded by a 500ms timeout (`timer_uptime_ms() - start_ms > 500`). When timed out, `sock->state` transitions to `TCP_STATE_CLOSED` and cleanly returns `-1`.
- **Shell Argument Tokenizer Quote Parsing (`user/sh/main.c`)**:
  - *Context*: The shell split input strictly on spaces and tabs, breaking multi-word string arguments (e.g. `nydiag redact "auth=bearer sk-... password=..."`).
  - *Decision*: Enhanced `tokenize()` in `user/sh/main.c` to parse `'` and `"` quote boundaries, stripping the outer quotes and bundling enclosed whitespace into a single token argument.
- **Privacy Redaction Filter (`user/diagd/diag_engine.h`)**:
  - *Design*: All telemetry data destined for display or external transmission passes through `redact_sensitive_data()`. Tokens matching `bearer sk-...`, `api_key=...`, `password=...`, `passwd=...`, `secret=...`, and PEM private key blocks are replaced with `[REDACTED_SECRET]`.
- **Tripartite Factual Separation & Non-Execution Principle**:
  - *Design*: All diagnostics outputs are strictly partitioned into `[FACTS]` (verified kernel telemetry), `[HYPOTHESIS]` (probabilistic root-cause analysis), and `[RECOMMENDATIONS]` (advisory troubleshooting suggestions).
  - *Non-Execution Safety*: Suggested commands (e.g. `/bin/memdiag`, `/bin/pstree`) are purely advisory and never executed automatically by the AI system.
- **Consent-Gated Provider Architecture (`/etc/diagnostics.conf`)**:
  - *Design*: External AI providers (`AI_PROVIDER_HTTP`) are strictly prohibited from transmitting data over the network without explicit user consent (`--consent`, `-y`). When consent is omitted, or if the external endpoint is unreachable/timed out, the engine automatically falls back to the deterministic **Local Heuristic Engine** without error or disruption to OS operations.

---

# Phase 20 Nyota OS 2.0 Stability, Integration & Final Release Architecture

- **Storage Block Cache Concurrency Race Fix (`kernel/storage/cache.c`)**:
  - *Context*: In `block_cache_read()`, when a cache miss occurred, `get_victim_block()` was invoked to choose a slot to evict or reuse. The function marked `slot->flags = 0` and released `cache_lock` before calling the underlying device driver `block_device_read()`. In multi-process workloads (such as shell pipelines `ls | cat` where both executables are spawned and read from disk simultaneously), the second process also called `block_cache_read()`, saw the first slot with `flags == 0` (unmarked as valid), and selected the exact same victim slot. Both threads then wrote conflicting sector contents into the same buffer without synchronization, corrupting ELF headers and causing sporadic `elf_load: read_file ehdr failed`.
  - *Decision*: Decoupled hardware sector reads on cache misses from the internal cache slot buffer. `block_cache_read()` now reads directly from the block device into the caller's target buffer while the spinlock is released. After verified read completion, `cache_lock` is re-acquired and the cache slot is updated atomically. This completely eliminated disk race conditions and restored 100% reliability for concurrent processes and pipelines.
- **Service Logging Decoupling & Interactive Prompt Preservation (`user/diagd/main.c`)**:
  - *Context*: Background daemons spawned by `init` (`loggerd`, `ttyd`, `netd`, `logind`, `desktop`, `diagd`) run concurrently. When `diagd` wrote startup greetings directly to `stdout`, its output interleaved with `logind`'s `tty0 login:` and `Password:` prompts on the serial console, causing race conditions in automated test harnesses and terminal login screens.
  - *Decision*: Removed direct `printf` to `stdout` from `diagd` initialization. Telemetry services log through Unix domain sockets (`/run/diag.sock`) or system log files (`/var/log/system.log`), keeping the serial console cleanly dedicated to the interactive user session.
- **Phase 20 Master Release & Acceptance Test Suite (`tools/test_phase20.py`)**:
  - *Design*: Author an overarching automated end-to-end acceptance suite executing 14 critical verification checks:
    1. Kernel boot & OS version metadata (`v2.0.0`, `x86_64`, `nyota`) via `uname -a`.
    2. Kernel security architecture via `secinfo` (isolation, guard pages, pointer validation, capabilities).
    3. Userland service supervision ecosystem via `ps` (`init`, `loggerd`, `ttyd`, `netd`, `logind`, `desktop`, `diagd`, `sh`).
    4. Storage subsystem & NyotaFS read/write persistence via `lsblk`, `nydiag report`, and `cat`.
    5. Concurrency & IPC integrity: pipeline `ls /bin | cat` and `ipctest`.
    6. Memory protection & user crash isolation via `/bin/crash` (SIGSEGV handled without panic).
    7. Network stack operational status via `ifconfig` (eth0 UP, IP 10.0.2.15) and `pktmon -s`.
    8. Memory allocator canary verification via `memdiag` (0 heap corruptions).
    9. Observability & performance profiling via `nytrace` and `nyprof`.
    10. AI-assisted diagnostics operational baseline via `nydiag health`, `nydiag summary`, and `nydiag redact`.
    11. Native desktop GUI environment application suite presence in `/bin`.
    12. Post-stress system stability and process accounting.
    13. Multi-core SMP boot and core diagnostics under `-smp 2` with `/bin/smp`.
  - *Result*: 14/14 tests passing (100%).
- **Version Unification (`v2.0.0`)**:
  - All version strings across the kernel (`include/kernel.h`, `kernel/arch/x86_64/syscall.c`), boot drivers (`drivers/gfx.c`), system daemons (`user/init/main.c`, `user/diagd/diag_engine.h`), desktop applications (`user/desktop/main.c`, `user/about/main.c`, `user/settings/main.c`, `user/sysmon/main.c`, `user/term/main.c`), and diagnostic tools (`user/nydiag/main.c`, `user/uname/main.c`) were synchronized to `v2.0.0`.



