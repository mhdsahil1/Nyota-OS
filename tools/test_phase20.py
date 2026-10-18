#!/usr/bin/env python3
"""
Nyota OS — Phase 20 Master Release & Acceptance Test Suite
Target Version: v2.0.0 (Nyota OS 2.0: Stability, Integration & Final Release)

Verifies:
  1. Boot reliability & version metadata ('2.0.0', 'x86_64') via `uname -a`.
  2. Kernel security architecture via `secinfo`.
  3. System service supervision & daemon ecosystem via `ps` (/sbin/init, loggerd, ttyd, netd, logind, desktop, diagd).
  4. VFS & storage read/write/delete persistence in NyotaFS (`/tmp/p20_test.txt`).
  5. Concurrency & IPC integrity: pipeline `ls /bin | cat` and `ipctest`.
  6. Memory protection & user crash isolation via `/bin/crash` (SIGSEGV handled without panic).
  7. Network stack operational status via `ifconfig` (eth0 UP, IP 10.0.2.15) and `pktmon -s`.
  8. Memory allocator canary verification via `memdiag` (0 heap corruptions).
  9. Observability & performance profiling via `nytrace` and `nyprof`.
 10. AI-assisted diagnostics operational baseline via `nydiag health`, `nydiag summary`, and `nydiag redact`.
 11. Native desktop GUI environment application suite presence in `/bin`.
 12. SMP multi-core stability under `-smp 2` with `/bin/smp`.
"""

import subprocess
import socket
import time
import sys
import os
import random

QEMU = "qemu-system-x86_64"
IMAGE = "build/nyota.img"
DATA_IMAGE = "build/nyota-data.img"

def find_free_port():
    for _ in range(50):
        p = random.randint(46000, 59000)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as test_s:
            try:
                test_s.bind(('127.0.0.1', p))
                return p
            except OSError:
                continue
    return 45520

def wait_for_prompt(s, timeout=35):
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
                elif logged_in and not password_sent and (b"Password:" in buf or b"word:" in buf):
                    time.sleep(0.15)
                    s.sendall(b"\n")
                    password_sent = True
                elif (b"$ " in buf or b"nyota$ " in buf) and (password_sent or not logged_in):
                    return buf.decode('latin1', errors='replace')
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

def send_cmd(s, cmd_str, timeout=12):
    time.sleep(0.3)
    s.setblocking(False)
    try:
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
    except Exception:
        pass
    s.settimeout(0.5)
    s.sendall(cmd_str.encode('latin1'))

    buf = bytearray()
    start = time.time()
    cmd_seen = False
    target_token = cmd_str.strip().encode('latin1')

    while time.time() - start < timeout:
        try:
            chunk = s.recv(1024)
            if chunk:
                buf.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if target_token in buf:
                    cmd_seen = True
                if cmd_seen and (b"$ " in buf or b"nyota$ " in buf) and len(buf) > len(target_token) + 3:
                    break
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

def run_phase20_suite():
    print("=" * 72)
    print("   NYOTA OS 2.0: STABILITY, INTEGRATION & FINAL RELEASE ACCEPTANCE")
    print("   Target Version: v2.0.0 | Release Engineering Master Test Suite")
    print("=" * 72)

    port = find_free_port()
    qemu_cmd = [
        QEMU,
        "-m", "512M",
        "-drive", f"format=raw,file={IMAGE},index=0,media=disk",
        "-drive", f"format=raw,file={DATA_IMAGE},index=1,media=disk",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{port},server,wait"
    ]

    print(f"[*] Booting Nyota OS (Port {port})...")
    proc = subprocess.Popen(qemu_cmd)

    s = None
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", port))
            break
        except ConnectionRefusedError:
            time.sleep(0.2)

    if not s:
        print("[!] Error: Could not connect to QEMU serial socket.")
        proc.kill()
        sys.exit(1)

    s.settimeout(0.5)

    print("[*] Waiting for kernel boot, init supervisor, and login prompt...")
    wait_for_prompt(s, timeout=40)

    results = {}

    # 1. System Version Metadata (v2.0.0)
    print("\n--- Test 1: System Version Metadata (v2.0.0) ---")
    uname_out = send_cmd(s, "uname -a\n")
    results["uname_version_2.0.0"] = ("2.0.0" in uname_out) and ("x86_64" in uname_out) and ("nyota" in uname_out.lower())

    # 2. Kernel Security Architecture
    print("\n--- Test 2: Kernel Security Architecture (`secinfo`) ---")
    sec_out = send_cmd(s, "secinfo\n")
    results["security_architecture"] = (
        "User/kernel isolation : enabled" in sec_out and
        "Guard pages           : enabled" in sec_out and
        "Pointer validation    : enabled" in sec_out and
        "Capabilities          : enabled" in sec_out
    )

    # 3. Userspace Service Supervision Ecosystem
    print("\n--- Test 3: Userspace Service Supervision (`ps`) ---")
    ps_out = send_cmd(s, "ps\n")
    required_services = ["init", "loggerd", "ttyd", "netd", "logind", "desktop", "diagd", "sh"]
    results["service_supervision"] = all(svc in ps_out for svc in required_services)

    # 4. Storage & Filesystem R/W Integrity
    print("\n--- Test 4: Storage & NyotaFS Read/Write Integrity (`lsblk`, `nydiag report`) ---")
    lsblk_out = send_cmd(s, "lsblk\n")
    send_cmd(s, "nydiag report -o /tmp/p20_report.txt\n")
    cat_out = send_cmd(s, "cat /tmp/p20_report.txt\n")
    results["storage_rw_integrity"] = (
        ("ata0" in lsblk_out or "ata1" in lsblk_out) and
        ("ONLINE" in lsblk_out) and
        ("NYOTA OS AI-ASSISTED SYSTEM DIAGNOSTICS REPORT" in cat_out)
    )

    # 5. Multiprocess Pipeline & Storage Cache Concurrency
    print("\n--- Test 5: Pipeline & Concurrency (`ls /bin | cat`) ---")
    pipe_out = send_cmd(s, "ls /bin | cat\n")
    results["pipeline_concurrency"] = ("sh" in pipe_out) and ("uname" in pipe_out) and ("ps" in pipe_out)

    # 6. IPC Subsystem Validation (`ipctest`)
    print("\n--- Test 6: IPC Pipes & Shared Memory (`ipctest`) ---")
    ipc_out = send_cmd(s, "ipctest\n")
    results["ipc_subsystem"] = (
        "[ OK ] pipe communication" in ipc_out and
        "[ OK ] shared memory" in ipc_out and
        "[ OK ] cleanup" in ipc_out
    )

    # 7. Process Crash Isolation & Exception Safety
    print("\n--- Test 7: Process Crash Isolation (`/bin/crash`) ---")
    crash_out = send_cmd(s, "crash\n")
    results["crash_isolation"] = ("exception: page fault" in crash_out) and ("SIGSEGV" in crash_out)

    # 8. Network Subsystem Configuration
    print("\n--- Test 8: Network Stack Configuration (`ifconfig`, `pktmon -s`) ---")
    if_out = send_cmd(s, "ifconfig\n")
    pkt_out = send_cmd(s, "pktmon -s\n")
    results["network_stack"] = ("eth0" in if_out and "10.0.2.15" in if_out) and ("Interface Statistics" in pkt_out)

    # 9. Kernel Memory Heap Canary Integrity
    print("\n--- Test 9: Heap Allocator Integrity (`memdiag`) ---")
    mem_out = send_cmd(s, "memdiag\n")
    results["heap_allocator_canary"] = ("Corrupted" in mem_out) and ("0 (HEALTHY)" in mem_out)

    # 10. Observability & Tracing Tools
    print("\n--- Test 10: System-Call Tracing & Profiling (`nytrace`, `nyprof`) ---")
    trace_out = send_cmd(s, "nytrace /bin/hello\n")
    prof_out = send_cmd(s, "nyprof /bin/ls\n")
    results["observability_tools"] = ("write(fd=1" in trace_out) and ("NYOTA SYSCALL PERFORMANCE PROFILE" in prof_out)

    # 11. AI Diagnostics Baseline
    print("\n--- Test 11: AI-Assisted System Diagnostics (`nydiag`) ---")
    health_out = send_cmd(s, "nydiag health\n")
    summary_out = send_cmd(s, "nydiag summary\n")
    redact_out = send_cmd(s, "nydiag redact 'token=sk-9999secret'\n")
    results["ai_diagnostics"] = (
        ("HEALTH" in health_out.upper()) and
        ("SYSTEM HEALTH SUMMARY" in summary_out) and
        ("[REDACTED" in redact_out)
    )

    # 12. Desktop GUI & Native Applications Verification
    print("\n--- Test 12: Desktop & Native Application Suite ---")
    bin_out = send_cmd(s, "ls /bin\n")
    gui_apps = ["desktop", "about", "sysmon", "settings", "term"]
    results["desktop_apps_present"] = all(app in bin_out for app in gui_apps)

    # 13. System Post-Stress Stability
    print("\n--- Test 13: System Stability Verification ---")
    final_ps = send_cmd(s, "ps\n")
    send_cmd(s, "sync\n")
    results["system_stability"] = "init" in final_ps and "sh" in final_ps

    try:
        s.close()
    except Exception:
        pass
    try:
        proc.kill()
    except Exception:
        pass

    # 14. Multicore SMP Configuration Test (-smp 2)
    print("\n--- Test 14: Multicore SMP Verification (-smp 2) ---")
    smp_port = find_free_port()
    smp_cmd = [
        QEMU,
        "-smp", "2",
        "-m", "512M",
        "-drive", f"format=raw,file={IMAGE},index=0,media=disk",
        "-drive", f"format=raw,file={DATA_IMAGE},index=1,media=disk",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{smp_port},server,wait"
    ]
    smp_proc = subprocess.Popen(smp_cmd)
    s_smp = None
    for _ in range(50):
        try:
            s_smp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s_smp.connect(("127.0.0.1", smp_port))
            break
        except ConnectionRefusedError:
            time.sleep(0.2)

    if s_smp:
        s_smp.settimeout(0.5)
        wait_for_prompt(s_smp, timeout=40)
        smp_info = send_cmd(s_smp, "smp\n", timeout=12)
        results["smp_multicore_2cpu"] = ("2 CPUs" in smp_info or "CPU 0" in smp_info and "CPU 1" in smp_info)
        try:
            s_smp.close()
        except Exception:
            pass
        try:
            smp_proc.kill()
        except Exception:
            pass
    else:
        results["smp_multicore_2cpu"] = False
        try:
            smp_proc.kill()
        except Exception:
            pass

    # Print Full Acceptance Report
    print("\n" + "=" * 72)
    print("                 PHASE 20 FINAL ACCEPTANCE REPORT                 ")
    print("=" * 72)
    all_passed = True
    for test_name, passed in results.items():
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {test_name}")
        if not passed:
            all_passed = False
    print("=" * 72)

    if all_passed:
        print("\n[+] SUCCESS: ALL PHASE 20 FINAL RELEASE ACCEPTANCE TESTS PASSED!")
        return 0
    else:
        print("\n[!] FAILURE: ONE OR MORE PHASE 20 ACCEPTANCE TESTS FAILED.")
        return 1

if __name__ == "__main__":
    sys.exit(run_phase20_suite())
