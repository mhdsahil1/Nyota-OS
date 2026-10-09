#!/usr/bin/env python3
"""
Nyota OS — Phase 11 SMP & Multicore Kernel Test Suite
Tests:
  - ACPI MADT CPU discovery across cores
  - Local APIC timer & IPI dispatch
  - AP boot trampoline execution and initialization
  - Per-CPU state (cpu_t), independent run queues, and work stealing
  - User diagnostics (/bin/smp)
  - Multicore stress testing (/bin/smpstress)
  - Tests under -smp 1, -smp 2, -smp 4, and -smp 8
"""

import subprocess
import time
import sys
import socket
import re

import random

def find_free_port():
    for _ in range(50):
        p = random.randint(46000, 59000)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as test_s:
            try:
                test_s.bind(('127.0.0.1', p))
                return p
            except OSError:
                continue
    return 45500

def run_smp_test(smp_count, port=None):
    if port is None:
        port = find_free_port()
    print(f"\n============================================================")
    print(f"       TESTING NYOTA OS SMP WITH -smp {smp_count} CORES (port {port})")
    print(f"============================================================")

    cmd = [
        "qemu-system-x86_64",
        "-smp", str(smp_count),
        "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
        "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{port},server,wait"
    ]

    proc = subprocess.Popen(cmd)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    connected = False
    for _ in range(60):
        try:
            s.connect(('127.0.0.1', port))
            connected = True
            break
        except Exception:
            time.sleep(0.1)

    if not connected:
        print(f"[ERROR] Failed to connect to QEMU on port {port}")
        proc.kill()
        return False

    s.settimeout(0.5)

    def wait_for_prompt(s, timeout=30):
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
                    elif logged_in and not password_sent and (b"Password:" in buf or b"word:" in buf or b"d: " in buf):
                        time.sleep(0.15)
                        s.sendall(b"\n")
                        password_sent = True
                    elif (b"$ " in buf or b"nyota$ " in buf) and (password_sent or not logged_in):
                        return buf.decode('latin1', errors='replace')
            except socket.timeout:
                pass
        return buf.decode('latin1', errors='replace')

    def send_cmd(s, cmd_str, timeout=15):
        time.sleep(0.4)
        s.setblocking(False)
        try:
            while True:
                chunk = s.recv(1024)
                if not chunk: break
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
                    if cmd_seen and (b"$ " in buf or b"nyota$ " in buf):
                        break
            except socket.timeout:
                pass
        return buf.decode('latin1', errors='replace')

    boot_log = wait_for_prompt(s, timeout=50)

    # Run /bin/smp
    smp_out = send_cmd(s, "smp\n", timeout=12)
    
    # Run /bin/smpstress (give 45s for 8-core QEMU TCG emulation)
    stress_out = send_cmd(s, "smpstress\n", timeout=45)

    # Run sysinfo
    sysinfo_out = send_cmd(s, "sysinfo\n", timeout=12)

    try:
        s.close()
    except Exception:
        pass

    try:
        proc.kill()
        proc.wait(timeout=3)
    except Exception:
        pass

    # Checks
    checks = {}
    checks["smp_binary_executed"] = "Nyota SMP Information" in smp_out
    checks["smp_cores_detected"] = (f"CPUs online   : {smp_count}" in smp_out) or (f"CPUs detected : {smp_count}" in smp_out)
    checks["smpstress_passed"] = "PASSED" in stress_out
    checks["sysinfo_cores"] = f"{smp_count} online / {smp_count} detected" in sysinfo_out

    print(f"\n--- Results for -smp {smp_count} ---")
    all_ok = True
    for k, v in checks.items():
        st = "PASS" if v else "FAIL"
        if not v: all_ok = False
        print(f"  [ {st} ] {k}")
    return all_ok

if __name__ == "__main__":
    results = {}
    for count in [1, 2, 4, 8]:
        results[count] = run_smp_test(count)
        time.sleep(1)

    print("\n" + "=" * 60)
    print("           SMP & MULTICORE TEST SUMMARY")
    print("=" * 60)
    all_passed = True
    for count, res in results.items():
        st = "PASS" if res else "FAIL"
        if not res: all_passed = False
        print(f"  -smp {count} Cores: [ {st} ]")
    print("=" * 60)
    if all_passed:
        print("ALL SMP TESTS PASSED SUCCESSFULLY!")
        sys.exit(0)
    else:
        print("SOME SMP TESTS FAILED.")
        sys.exit(1)
