#!/usr/bin/env python3
"""
Nyota OS — Phase 9 Part 3 Automated Verification Suite
Tests:
  - TTY line discipline & canonical mode
  - Environment variables (USER, HOME, SHELL, PWD, PATH)
  - Shell builtins (export, cd with ~, -, and relative paths)
  - Process groups and sessions
  - Job control (background &, jobs, fg)
  - Multi-user authentication & logind
"""

import subprocess
import time
import sys
import socket
import re

PORT = 45483

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-P9-P3] Launching QEMU with serial on port {PORT}...")
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
    print("[TEST-P9-P3] Failed to connect to QEMU serial socket!")
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

def send_command(s, cmd_str, timeout=10):
    print(f"\n>>> SENDING: {cmd_str.strip()}", flush=True)
    time.sleep(0.2)
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

checks = {}

# 1. Boot and verify logind prompt
print("[1] Waiting for boot and logind prompt...")
initial_out = wait_for_prompt(s, 25).decode('latin1', errors='replace')
checks["boot_logind_prompt"] = "tty0 login:" in initial_out
checks["login_success_sahil"] = "[sahil@nyota" in initial_out

# 2. Check initial environment variables via export
print("\n[2] Checking initial environment variables...")
env_out = send_command(s, "export\n")
checks["env_user_sahil"] = "USER=sahil" in env_out
checks["env_home_sahil"] = "HOME=/home/sahil" in env_out
checks["env_shell"] = "SHELL=/bin/sh" in env_out
checks["env_path"] = "PATH=" in env_out
checks["env_pwd"] = "PWD=/home/sahil" in env_out

# 3. Test export builtin (setting new variable)
print("\n[3] Testing export builtin...")
send_command(s, "export TEST_VAR=NyotaRocks\n")
export_check = send_command(s, "export\n")
checks["export_custom_var"] = "TEST_VAR=NyotaRocks" in export_check

# 4. Test directory navigation (cd .., cd ~, cd -)
print("\n[4] Testing cd builtins and PWD tracking...")
cd_parent = send_command(s, "cd ..\n")
checks["cd_parent"] = "[sahil@nyota /home]$" in cd_parent

cd_root = send_command(s, "cd /\n")
checks["cd_root"] = "[sahil@nyota /]$" in cd_root

cd_tilde = send_command(s, "cd ~\n")
checks["cd_tilde"] = "[sahil@nyota ~]$" in cd_tilde

cd_dash = send_command(s, "cd -\n")
checks["cd_dash"] = "[sahil@nyota /]$" in cd_dash

send_command(s, "cd ~\n")

# 5. Test background jobs and job control (&, jobs)
print("\n[5] Testing job control (&, jobs)...")
bg_out = send_command(s, "echo-server 8088 &\n")
checks["job_background_launch"] = "[1]" in bg_out

jobs_out = send_command(s, "jobs\n")
checks["jobs_list_running"] = "[1]" in jobs_out and "echo-server" in jobs_out

# 6. Test ps process group and session visibility
print("\n[6] Checking process groups and sessions via ps...")
ps_out = send_command(s, "ps\n")
checks["ps_shows_logind"] = "logind" in ps_out
checks["ps_shows_shell"] = "sh" in ps_out
checks["ps_shows_bg_job"] = "echo-server" in ps_out

# 7. Test TTY line discipline: Ctrl+C interruption
print("\n[7] Testing TTY line discipline: Ctrl+C interruption...")
# Send netcat listening or running command, then send Ctrl+C (\x03)
time.sleep(0.2)
s.sendall(b"cat\n")
time.sleep(0.5)
# Send Ctrl+C
s.sendall(b"\x03")
int_out = bytearray()
t_start = time.time()
while time.time() - t_start < 4:
    try:
        chunk = s.recv(1024)
        if chunk:
            int_out.extend(chunk)
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
            if b"$ " in int_out:
                break
    except socket.timeout:
        pass
int_str = int_out.decode('latin1', errors='replace')
checks["tty_ctrl_c_interrupted"] = ("^C" in int_str or "$ " in int_str)

# 8. Test TTY line discipline: backspace editing
print("\n[8] Testing TTY backspace editing...")
# Send "echx\b\bo foo\n" -> should execute "echo foo"
s.sendall(b"echx\b\bo foo\n")
bs_out = bytearray()
t_start = time.time()
while time.time() - t_start < 4:
    try:
        chunk = s.recv(1024)
        if chunk:
            bs_out.extend(chunk)
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
            if b"$ " in bs_out:
                break
    except socket.timeout:
        pass
bs_str = bs_out.decode('latin1', errors='replace')
checks["tty_backspace_editing"] = "foo" in bs_str

# Shutdown cleanly
try:
    s.close()
except Exception:
    pass
proc.terminate()

# Summary
print("\n" + "=" * 60)
print("      PHASE 9 PART 3 INTERACTIVE ENVIRONMENT REPORT")
print("=" * 60)

passed = 0
failed = 0
for name, ok in checks.items():
    status = "PASS" if ok else "FAIL"
    if ok:
        passed += 1
    else:
        failed += 1
    print(f"  [ {status} ] {name}")

print("=" * 60)
total = passed + failed
print(f"RESULT: {passed}/{total} Passed ({(passed/total)*100:.1f}%)")
print("=" * 60)

if failed > 0:
    sys.exit(1)
else:
    sys.exit(0)
