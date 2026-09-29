#!/usr/bin/env python3
"""
Nyota OS — Phase 9 Part 2 Automated Verification Suite
Tests:
  - CMOS RTC Wall-Clock & monotonic clocks
  - Time syscalls: date (SYS_CLOCK_GETTIME CLOCK_REALTIME), uptime (CLOCK_MONOTONIC)
  - Service Manager (/bin/service, /sbin/init, /sbin/nyotad)
  - Service lifecycle: start, stop, restart, status, list
  - Service dependencies resolution
  - Logging subsystem: klog ring buffer, loggerd, logger utility
  - Log persistence in /var/log/ (kernel.log, system.log, services.log)
  - Crash recovery via SIGCHLD and crash loop protection -> FAILED
"""

import subprocess
import time
import sys
import socket
import re

PORT = 45482

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-P9-P2] Launching QEMU with serial on port {PORT}...")
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
    print("[TEST-P9-P2] Failed to connect to QEMU serial socket!")
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

try:
    # 1. Wait for boot and prompt
    boot_output = wait_for_prompt(s, 25).decode('latin1', errors='replace')
    checks = {}

    # Check RTC initialization
    checks["rtc_initialized"] = "RTC Wall-Clock initialized" in boot_output

    # Check services initialized on boot
    checks["boot_loggerd_started"] = "service: starting loggerd" in boot_output
    checks["boot_ttyd_started"] = "service: starting ttyd" in boot_output
    checks["boot_netd_started"] = "service: starting netd" in boot_output
    checks["boot_logind_started"] = "service: starting logind" in boot_output

    # 2. Time Subsystem checks
    date_out = send_command(s, "date\n", timeout=5)
    checks["date_format"] = bool(re.search(r"202[0-9]-[0-1][0-9]-[0-3][0-9] [0-2][0-9]:[0-5][0-9]:[0-5][0-9]", date_out))

    uptime_out = send_command(s, "uptime\n", timeout=5)
    checks["uptime_format"] = bool(re.search(r"up [0-9]{2}:[0-9]{2}:[0-9]{2}", uptime_out))

    # 3. Service Manager checks (/bin/service)
    list_out = send_command(s, "service list\n", timeout=5)
    checks["service_list_header"] = "SERVICE" in list_out and "PID" in list_out and "STATE" in list_out and "RESTARTS" in list_out
    checks["service_list_loggerd"] = "loggerd" in list_out and "RUNNING" in list_out
    checks["service_list_ttyd"] = "ttyd" in list_out and "RUNNING" in list_out
    checks["service_list_netd"] = "netd" in list_out and "RUNNING" in list_out
    checks["service_list_logind"] = "logind" in list_out and "RUNNING" in list_out

    # Status check
    status_out = send_command(s, "service status loggerd\n", timeout=5)
    checks["service_status_loggerd"] = "SERVICE:  loggerd" in status_out and "STATE:    RUNNING" in status_out

    # Stop service
    stop_out = send_command(s, "service stop netd\n", timeout=5)
    checks["service_stop_netd"] = "OK" in stop_out
    status_netd = send_command(s, "service status netd\n", timeout=5)
    checks["service_stopped_state"] = "STATE:    STOPPED" in status_netd

    # Start service
    start_out = send_command(s, "service start netd\n", timeout=5)
    checks["service_start_netd"] = "OK" in start_out
    status_netd2 = send_command(s, "service status netd\n", timeout=5)
    checks["service_started_state"] = "STATE:    RUNNING" in status_netd2

    # Restart service
    restart_out = send_command(s, "service restart netd\n", timeout=5)
    checks["service_restart_netd"] = "OK" in restart_out
    status_netd3 = send_command(s, "service status netd\n", timeout=5)
    checks["service_restarted_state"] = "STATE:    RUNNING" in status_netd3 and ("RESTARTS: 1" in status_netd3 or "RESTARTS: 2" in status_netd3)

    # 4. /sbin/nyotad utility check
    nyotad_out = send_command(s, "/sbin/nyotad list\n", timeout=5)
    checks["nyotad_utility_list"] = "SERVICE" in nyotad_out and "loggerd" in nyotad_out

    # 5. Service Logging & File Persistence
    services_log = send_command(s, "cat /var/log/services.log\n", timeout=5)
    checks["services_log_present"] = "service: starting" in services_log and "service: stopping" in services_log

    kernel_log = send_command(s, "cat /var/log/kernel.log\n", timeout=5)
    checks["kernel_log_present"] = len(kernel_log.strip()) > 0

    # 6. Logger utility check
    send_command(s, "logger 'Phase9Part2TestMarker'\n", timeout=5)
    system_log = send_command(s, "cat /var/log/system.log\n", timeout=5)
    checks["logger_utility_system_log"] = "Phase9Part2TestMarker" in system_log

    # 7. Crash Recovery & Crash Loop Protection
    # Parse PID of netd
    m = re.search(r"PID:\s+(\d+)", status_netd3)
    netd_pid = int(m.group(1)) if m else 0

    # Kill netd to trigger SIGCHLD and automatic restart
    if netd_pid > 0:
        kill_out = send_command(s, f"kill -9 {netd_pid}\n", timeout=5)
        time.sleep(0.5)
        # Check that it auto-restarted
        status_after_kill = send_command(s, "service status netd\n", timeout=5)
        m2 = re.search(r"PID:\s+(\d+)", status_after_kill)
        new_pid = int(m2.group(1)) if m2 else 0
        checks["crash_recovery_auto_restart"] = (new_pid > 0 and new_pid != netd_pid) and "STATE:    RUNNING" in status_after_kill

        # Now trigger repeated crashes to hit crash loop limit (>= 5 restarts)
        curr_pid = new_pid
        for _ in range(5):
            if curr_pid > 0:
                send_command(s, f"kill -9 {curr_pid}\n", timeout=3)
                time.sleep(0.4)
                st = send_command(s, "service status netd\n", timeout=3)
                m_next = re.search(r"PID:\s+(\d+)", st)
                curr_pid = int(m_next.group(1)) if m_next else 0

        # After 5 restarts within short window, netd must transition to FAILED
        status_failed = send_command(s, "service status netd\n", timeout=5)
        checks["crash_loop_marked_failed"] = "STATE:    FAILED" in status_failed
    else:
        checks["crash_recovery_auto_restart"] = False
        checks["crash_loop_marked_failed"] = False

    # Check services.log recorded crash loop event
    services_log_final = send_command(s, "cat /var/log/services.log\n", timeout=5)
    checks["services_log_crash_limit"] = "restart limit reached" in services_log_final

    print("\n" + "=" * 60)
    print("      PHASE 9 PART 2 SERVICE & TIME TEST REPORT")
    print("=" * 60)
    all_ok = True
    for name, ok in checks.items():
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"  [ {status} ] {name}")
    print("=" * 60)

    if all_ok:
        print("ALL PHASE 9 PART 2 CHECKS PASSED (100%)!")
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
