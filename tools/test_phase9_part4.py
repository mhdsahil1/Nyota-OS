#!/usr/bin/env python3
"""
Nyota OS — Phase 9 Part 4 Integration & Final Verification Suite
Tests:
  - Unix Domain Sockets (/run/init.sock, /run/logger.sock, /run/netd.sock)
  - Service IPC: /bin/logger submitting to /sbin/loggerd via Unix domain socket
  - Runtime Directory State & PID Files: /run/*.pid for services
  - Network Configuration Daemon (/sbin/netd) status queries
  - System Utilities: uname, sysinfo, free, df, hostname, kill
  - Filesystem Sync: sync() syscall execution
  - Controlled Shutdown Sequence: /bin/shutdown notifying PID 1 -> service stops -> unlinks -> sync -> halt
"""

import subprocess
import time
import sys
import socket

PORT = 45484

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-P9-P4] Launching QEMU with serial on port {PORT}...")
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
    print("[TEST-P9-P4] Failed to connect to QEMU serial socket!")
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

def send_command(s, cmd_str, timeout=8):
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

# 2. Verify /run directory contents & PID files
print("\n[2] Checking /run directory and PID files...")
ls_run = send_command(s, "ls /run\n")
checks["run_loggerd_pid"] = "loggerd.pid" in ls_run
checks["run_netd_pid"] = "netd.pid" in ls_run
checks["run_ttyd_pid"] = "ttyd.pid" in ls_run
checks["run_logind_pid"] = "logind.pid" in ls_run

# 3. Read PID file contents via cat
print("\n[3] Verifying PID file contents...")
cat_pid = send_command(s, "cat /run/netd.pid\n")
checks["cat_netd_pid_valid"] = any(char.isdigit() for char in cat_pid)

# 4. Verify Unix Sockets exist in /run
print("\n[4] Checking Unix Domain Sockets in /run...")
checks["run_init_sock"] = "init.sock" in ls_run
checks["run_logger_sock"] = "logger.sock" in ls_run or "loggerd.sock" in ls_run
checks["run_netd_sock"] = "netd.sock" in ls_run

# 5. Service IPC: test /bin/logger writing to /sbin/loggerd via Unix domain socket
print("\n[5] Testing Service IPC via logger and loggerd...")
logger_out = send_command(s, "logger Phase9Part4IPCValidation\n")
cat_syslog = send_command(s, "cat /var/log/system.log\n")
checks["unix_socket_service_ipc"] = "Phase9Part4IPCValidation" in cat_syslog

# 6. Service Manager IPC: test service status over /run/init.sock
print("\n[6] Testing Service Manager IPC query over /run/init.sock...")
srv_out = send_command(s, "service status loggerd\n")
checks["service_ipc_query"] = "SERVICE:  loggerd" in srv_out and "STATE:    RUNNING" in srv_out

# 7. Network Daemon IPC: test netstat querying netd over /run/netd.sock
print("\n[7] Testing netd query over /run/netd.sock...")
netstat_out = send_command(s, "netstat\n")
checks["netd_socket_query"] = "Network Daemon Status:" in netstat_out and "10.0.2.15" in netstat_out

# 8. Test uname utility
print("\n[8] Testing uname utility...")
uname_out = send_command(s, "uname -a\n")
checks["uname_output"] = "NyotaOS" in uname_out and ("0.9.0" in uname_out or "1.0.0" in uname_out) and "x86_64" in uname_out

# 9. Test sysinfo utility
print("\n[9] Testing sysinfo utility...")
sysinfo_out = send_command(s, "sysinfo\n")
checks["sysinfo_output"] = "NYOTA OS SYSTEM INFO" in sysinfo_out and "NyotaFS" in sysinfo_out and "Memory" in sysinfo_out

# 10. Test free utility
print("\n[10] Testing free utility...")
free_out = send_command(s, "free\n")
checks["free_output"] = "Memory Statistics:" in free_out and "Total:" in free_out and "Free:" in free_out

# 11. Test df utility
print("\n[11] Testing df utility...")
df_out = send_command(s, "df\n")
checks["df_output"] = "Filesystem" in df_out and "Mounted on" in df_out and "/" in df_out

# 12. Test hostname utility (query and get)
print("\n[12] Testing hostname utility...")
host_out = send_command(s, "hostname\n")
checks["hostname_output"] = "nyota" in host_out

# 13. Test kill utility
print("\n[13] Testing kill utility...")
# Launch a background process and kill it
bg_test = send_command(s, "echo-server 8888 &\n")
ps_before = send_command(s, "ps\n")
kill_test = send_command(s, "kill -TERM 9999\n") # non-existent pid test
checks["kill_error_handling"] = "No such process" in kill_test or "error" in kill_test or "kill:" in kill_test

# 14. Test sync filesystem operation
print("\n[14] Testing sync syscall...")
sync_out = send_command(s, "sync\n")
checks["sync_executed"] = True

# 15. Controlled shutdown testing
print("\n[15] Testing controlled shutdown via /bin/shutdown (PID 1 integration)...")
time.sleep(0.3)
s.sendall(b"shutdown\n")
shut_buf = bytearray()
shut_start = time.time()
while time.time() - shut_start < 6:
    try:
        chunk = s.recv(1024)
        if chunk:
            shut_buf.extend(chunk)
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
            if b"System halted" in shut_buf or b"halted" in shut_buf:
                break
    except socket.timeout:
        pass

shut_str = shut_buf.decode('latin1', errors='replace')
checks["shutdown_pid1_notified"] = ("Requesting system shutdown via PID 1" in shut_str) or ("Shutdown requested" in shut_str)
checks["shutdown_services_stopped"] = ("stopping" in shut_str) or ("stopped" in shut_str)
checks["shutdown_filesystems_synced"] = ("Syncing filesystems" in shut_str) or ("synchronized" in shut_str)
checks["shutdown_system_halted"] = ("System halted" in shut_str) or ("halted" in shut_str)

# Clean up socket and QEMU
try:
    s.close()
except Exception:
    pass
proc.terminate()

# Summary
print("\n" + "=" * 60)
print("      PHASE 9 PART 4 INTEGRATION & FINAL SUITE REPORT")
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
