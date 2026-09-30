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

## 7. Testing & Verification
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
