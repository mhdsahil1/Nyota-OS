#!/usr/bin/env python3
"""
Nyota OS — Phase 9 Part 1 Automated Verification Suite
Tests:
  - waitpid(WNOHANG) on running child (returns 0)
  - waitpid(WNOHANG) on exited child (returns PID + exit status)
  - waitpid on non-existent / invalid PID (returns error)
  - waitpid(-1, WNOHANG) reaping multiple children
  - Zombie process lifecycle & child reaping
  - SIGCHLD signal delivery to parent on child termination
  - Orphan process reparenting to PID 1 & reaping by PID 1
  - Pipe blocking read (sleeps until data arrives) & writer wakeup
  - Pipe EOF when all writers close (returns 0)
  - Pipe SIGPIPE delivery and -EPIPE when all readers close
  - TTY blocking read & keyboard wakeup
  - PID 1 (/sbin/init) supervision and responsiveness
"""

import subprocess
import time
import sys
import socket

PORT = 45481

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-P9-P1] Launching QEMU with serial on port {PORT}...")
proc = subprocess.Popen(cmd)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
connected = False
for _ in range(60):
    try:
        s.connect(('127.0.0.1', PORT))
        connected = True
        break
    except Exception:
        time.sleep(0.1)

if not connected:
    print("[TEST-P9-P1] Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

def wait_for_prompt(s, timeout=25):
    buf = bytearray()
    start = time.time()
    logged_in = False
    password_sent = False
    while time.time() - start < timeout:
        try:
            chunk = s.recv(1024)
            if chunk:
                buf.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if not logged_in and b"login:" in buf:
                    time.sleep(0.15)
                    s.sendall(b"sahil\n")
                    logged_in = True
                elif logged_in and not password_sent and b"Password:" in buf:
                    time.sleep(0.15)
                    s.sendall(b"\n")
                    password_sent = True
                elif (b"$ " in buf or b"nyota$ " in buf) and (password_sent or not logged_in):
                    return buf
        except socket.timeout:
            pass
    return buf

def send_command(s, cmd_str, timeout=12):
    print(f"\n>>> SENDING: {cmd_str.strip()}", flush=True)
    time.sleep(0.15)
    s.sendall(cmd_str.encode('latin1'))
    buf = bytearray()
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(1024)
            if chunk:
                buf.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if b"$ " in buf or b"nyota$ " in buf:
                    break
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

try:
    # 1. Wait for boot & prompt
    boot_output = wait_for_prompt(s, 25).decode('latin1', errors='replace')
    checks = {}

    checks["pid1_sbin_init_loaded"] = "Loading /sbin/init" in boot_output and "PID 1 started" in boot_output

    # 2. Check process table
    ps_out = send_command(s, "ps\n", timeout=6)
    checks["pid1_running"] = "1 " in ps_out and "init" in ps_out

    # 3. Run comprehensive lifecycletest
    lifecycle_out = send_command(s, "lifecycletest\n", timeout=12)

    checks["waitpid_wnohang_running"] = "[PASS] waitpid(WNOHANG) on running child returns 0 immediately" in lifecycle_out
    checks["waitpid_blocking_collect"] = "[PASS] blocking waitpid collects child termination" in lifecycle_out
    checks["waitpid_wnohang_zombie"] = "[PASS] waitpid(WNOHANG) reaps zombie child" in lifecycle_out
    checks["waitpid_reaped_echild"] = "[PASS] waitpid on already reaped child returns -ECHILD" in lifecycle_out
    checks["waitpid_invalid_pid"] = "[PASS] waitpid on non-existent PID returns error (< 0)" in lifecycle_out
    checks["waitpid_multiple_children"] = "[PASS] waitpid(-1, WNOHANG) reaped all 3 children" in lifecycle_out
    checks["sigchld_delivery"] = "[PASS] parent received SIGCHLD on child exit" in lifecycle_out
    checks["sigchld_reaped"] = "[PASS] child reaped after SIGCHLD" in lifecycle_out
    checks["pipe_blocking_read"] = "[PASS] blocked reader woke up, read data, and exited 0" in lifecycle_out
    checks["pipe_eof_closed_writers"] = "[PASS] read from pipe with all writers closed returns 0 (EOF)" in lifecycle_out
    checks["pipe_sigpipe_closed_readers"] = "[PASS] write to pipe with all readers closed returns -EPIPE" in lifecycle_out
    checks["pipe_sigpipe_delivered"] = "[PASS] SIGPIPE signal delivered to writer" in lifecycle_out
    checks["orphan_reparenting"] = "[PASS] orphan creator exited, grandchild reparented to PID 1" in lifecycle_out
    checks["pid1_reaped_orphan"] = "[PASS] PID 1 supervision loop remains active and reaps orphan" in lifecycle_out
    checks["lifecycle_all_passed"] = "0 Failed" in lifecycle_out

    # 4. Test TTY blocking / echoing with interactive input
    echo_out = send_command(s, "echo 'Testing TTY wakeups'\n", timeout=5)
    checks["tty_echo"] = "Testing TTY wakeups" in echo_out

    # 5. Check process table again to verify no leaked unreaped zombies
    ps_after = send_command(s, "ps\n", timeout=5)
    checks["no_zombie_leak"] = "ZOMBIE" not in ps_after

    print("\n" + "=" * 60)
    print("      PHASE 9 PART 1 PROCESS LIFECYCLE TEST REPORT")
    print("=" * 60)
    all_ok = True
    for name, ok in checks.items():
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"  [ {status} ] {name}")
    print("=" * 60)

    if all_ok:
        print("ALL PHASE 9 PART 1 PROCESS LIFECYCLE CHECKS PASSED (100%)!")
        sys.exit(0)
    else:
        print("SOME CHECKS FAILED!")
        sys.exit(1)

finally:
    try:
        s.close()
    except Exception:
        pass
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except Exception:
        proc.kill()
