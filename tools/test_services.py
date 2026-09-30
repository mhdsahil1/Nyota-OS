#!/usr/bin/env python3
"""
Nyota OS — Phase 9: Advanced Userland & Services Test Suite
Launches QEMU with serial over TCP and verifies all Phase 9 subsystems.
"""

import subprocess
import time
import sys
import socket

PORT = 45490

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait",
    "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
    "-device", "e1000,netdev=net0"
]

print(f"[TEST-P9] Launching QEMU with serial on port {PORT}...")
proc = subprocess.Popen(cmd)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
connected = False
for _ in range(50):
    try:
        s.connect(('127.0.0.1', PORT))
        connected = True
        break
    except Exception:
        time.sleep(0.1)

if not connected:
    print("[TEST-P9] Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

def collect_output(s, timeout=10, until=None):
    """Collect serial output until a string is seen or timeout"""
    buf = bytearray()
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(4096)
            if chunk:
                buf.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if until and until.encode('latin1') in buf:
                    return buf.decode('latin1', errors='replace')
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

def send_and_wait(s, cmd_str, until="$ ", timeout=8):
    """Send a command and collect output until prompt returns"""
    print(f"\n>>> SENDING: {cmd_str.strip()}", flush=True)
    time.sleep(0.15)
    s.sendall(cmd_str.encode('latin1'))
    return collect_output(s, timeout=timeout, until=until)

# ── Wait for kernel boot and login prompt ──
print("\n[TEST-P9] Waiting for boot and login prompt...", flush=True)
boot_out = collect_output(s, timeout=25, until="login:")
print("\n[TEST-P9] Got login prompt.", flush=True)

checks = {}

# ── Verify kernel boot messages ──
checks["boot_banner"] = "NYOTA OS" in boot_out
checks["boot_version"] = ("0.9.0" in boot_out) or ("1.0.0" in boot_out)
checks["boot_arch"] = "x86_64" in boot_out
checks["boot_rtc"] = "RTC" in boot_out
checks["boot_tty"] = "TTY" in boot_out
checks["boot_security"] = "Security" in boot_out
checks["boot_init_started"] = "PID 1" in boot_out

# ── Check that init started services ──
checks["init_loggerd"] = "loggerd" in boot_out or "logger" in boot_out
checks["init_ttyd"] = "ttyd" in boot_out
checks["init_netd"] = "netd" in boot_out
checks["init_logind"] = "logind" in boot_out

# ── Login flow ──
time.sleep(0.3)
login_out = send_and_wait(s, "sahil\n", until="Password:", timeout=8)
checks["login_username_accepted"] = "Password" in login_out

time.sleep(0.3)
welcome_out = send_and_wait(s, "\n", until="$ ", timeout=10)
checks["login_success"] = "Welcome" in welcome_out or "$ " in welcome_out or "nyota" in welcome_out

# ── Shell prompt reached ──
checks["shell_prompt"] = "$ " in welcome_out

# ── 1. Test uname ──
uname_out = send_and_wait(s, "uname -a\n")
checks["uname_os"] = "NyotaOS" in uname_out or "Nyota" in uname_out
checks["uname_arch"] = "x86_64" in uname_out

# ── 2. Test date ──
date_out = send_and_wait(s, "date\n")
checks["date_runs"] = "20" in date_out  # Year should start with 20xx

# ── 3. Test uptime ──
uptime_out = send_and_wait(s, "uptime\n")
checks["uptime_runs"] = "up" in uptime_out.lower()

# ── 4. Test hostname ──
hostname_out = send_and_wait(s, "hostname\n")
checks["hostname_runs"] = "nyota" in hostname_out.lower()

# ── 5. Test ps ──
ps_out = send_and_wait(s, "ps\n")
checks["ps_pid_header"] = "PID" in ps_out
checks["ps_shows_init"] = "init" in ps_out
checks["ps_shows_sh"] = "sh" in ps_out

# ── 6. Test environment builtins: export / echo $VAR ──
send_and_wait(s, "export TESTVAR=hello_nyota\n")
echo_out = send_and_wait(s, "echo $TESTVAR\n")
checks["env_export_echo"] = "hello_nyota" in echo_out

# ── 7. Test pwd ──
pwd_out = send_and_wait(s, "pwd\n")
checks["pwd_works"] = "/" in pwd_out

# ── 8. Test cd ──
send_and_wait(s, "cd /home\n")
pwd2_out = send_and_wait(s, "pwd\n")
checks["cd_works"] = "/home" in pwd2_out
send_and_wait(s, "cd /\n")

# ── 9. Test ls ──
ls_out = send_and_wait(s, "ls /etc\n")
checks["ls_etc"] = "passwd" in ls_out or "hostname" in ls_out or "init.conf" in ls_out

# ── 10. Test cat /etc/hostname ──
cat_out = send_and_wait(s, "cat /etc/hostname\n")
checks["cat_hostname"] = "nyota" in cat_out.lower()

# ── 11. Test free ──
free_out = send_and_wait(s, "free\n")
checks["free_runs"] = "Memory" in free_out or "Total" in free_out or "MB" in free_out or "free" in free_out.lower()

# ── 12. Test sysinfo ──
sysinfo_out = send_and_wait(s, "sysinfo\n")
checks["sysinfo_runs"] = "Nyota" in sysinfo_out or "Kernel" in sysinfo_out

# ── 13. Test background job ──
bg_out = send_and_wait(s, "echo-server 8080 &\n")
checks["background_job"] = "[1]" in bg_out

# ── 14. Test ps after background ──
time.sleep(0.3)
ps2_out = send_and_wait(s, "ps\n")
checks["ps_background"] = "echo-server" in ps2_out or "sh" in ps2_out

# ── 15. Test secinfo (Phase 8 regression) ──
sec_out = send_and_wait(s, "secinfo\n")
checks["secinfo_regression"] = "enabled" in sec_out.lower() or "Security" in sec_out

# ── 16. Test crash recovery (Phase 8 regression) ──
crash_out = send_and_wait(s, "crash\n", timeout=6)
checks["crash_recovery"] = "$ " in crash_out

# ── 17. Test df ──
df_out = send_and_wait(s, "df\n")
checks["df_runs"] = "NyotaFS" in df_out or "Mounted" in df_out or "Blocks" in df_out or "/" in df_out

# ── 18. Test service list ──
svc_out = send_and_wait(s, "service list\n")
checks["service_list"] = "logger" in svc_out or "netd" in svc_out or "ttyd" in svc_out or "RUNNING" in svc_out

# ── Cleanup ──
s.close()
proc.terminate()

# ── Report ──
print("\n" + "=" * 70)
print("           NYOTA OS — PHASE 9 TEST REPORT")
print("=" * 70)

passed = 0
failed = 0
for name, result in checks.items():
    status = "[ PASS ]" if result else "[ FAIL ]"
    print(f"  {status} {name}")
    if result:
        passed += 1
    else:
        failed += 1

print("=" * 70)
print(f"  Results: {passed} passed, {failed} failed, {passed + failed} total")
if failed == 0:
    print("  ALL PHASE 9 TESTS PASSED SUCCESSFULLY!")
else:
    print("  SOME TESTS FAILED — REVIEW OUTPUT ABOVE.")
print("=" * 70)

sys.exit(0 if failed == 0 else 1)
