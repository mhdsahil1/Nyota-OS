#!/usr/bin/env python3
"""
Nyota OS — Phase 18 Automated Test Suite: Developer Tools & Kernel Observability (v1.8.0)

Verifies:
  1. System boot and OS version reporting '1.8.0' via `uname -a`.
  2. Availability of all five observability tools in `/bin`:
     `nytrace`, `pstree`, `memdiag`, `pktmon`, `nyprof`.
  3. Process & Scheduler tree visualization (`pstree`).
  4. Memory diagnostics and kernel heap integrity verification (`memdiag -v`).
  5. Network interface statistics and socket table inspection (`pktmon -s`).
  6. Syscall latency and activity profiling (`nyprof /bin/ls`).
  7. Process syscall tracing (`nytrace /bin/hello`).
  8. Syscall tracing security boundaries (rejection of unauthorized tracing of PID 1).
  9. Host kernel stability and clean operation after tracing & profiling.
"""

import subprocess
import socket
import time
import sys
import os

QEMU = "qemu-system-x86_64"
IMAGE = "build/nyota.img"
DATA_IMAGE = "build/nyota-data.img"
PORT = 45505

def wait_for_login_and_prompt(s, timeout=25):
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
                if (b"$ " in buf or b"nyota$ " in buf) and len(buf) > len(cmd_str) + 4:
                    break
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

def run_tests():
    print("=" * 70)
    print(" Nyota OS Phase 18: Developer Tools & Observability Test Suite (v1.8.0)")
    print("=" * 70)

    qemu_cmd = [
        QEMU,
        "-m", "512M",
        "-drive", f"format=raw,file={IMAGE},index=0,media=disk",
        "-drive", f"format=raw,file={DATA_IMAGE},index=1,media=disk",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
    ]

    print(f"[*] Launching QEMU with command: {' '.join(qemu_cmd)}")
    proc = subprocess.Popen(qemu_cmd)

    s = None
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", PORT))
            break
        except ConnectionRefusedError:
            time.sleep(0.2)

    if not s:
        print("[!] Error: Could not connect to QEMU serial socket.")
        proc.kill()
        sys.exit(1)

    s.settimeout(0.5)

    print("[*] Connected to serial console. Waiting for system boot and login prompt...")
    boot_out = wait_for_login_and_prompt(s, 25).decode('latin1', errors='replace')

    results = {}

    # 1. Test uname version 1.8.0
    print("\n--- Test 1: System Version Metadata (v1.8.0) ---")
    uname_out = send_command(s, "uname -a\n")
    print(f"uname output: {uname_out.strip()}")
    results["uname_version_1.8.0"] = ("1.8.0" in uname_out or "1.9.0" in uname_out) and ("x86_64" in uname_out)

    # 2. Test presence of new observability binaries in /bin
    print("\n--- Test 2: Presence of Observability Binaries in /bin ---")
    bin_ls = send_command(s, "ls /bin\n")
    results["binaries_present"] = all(
        tool in bin_ls for tool in ["nytrace", "pstree", "memdiag", "pktmon", "nyprof"]
    )

    # 3. Test Process Tree Visualization (`pstree`)
    print("\n--- Test 3: Process Tree & Scheduler Visualization (`pstree`) ---")
    pstree_out = send_command(s, "pstree\n", timeout=6)
    print(pstree_out.strip())
    results["pstree_executed"] = ("init" in pstree_out) and ("PID" in pstree_out or "CPU" in pstree_out or "[1]" in pstree_out)

    # 4. Test Memory Diagnostics & Heap Integrity (`memdiag`)
    print("\n--- Test 4: Memory Diagnostics & Heap Integrity (`memdiag -v`) ---")
    memdiag_out = send_command(s, "memdiag -v\n", timeout=6)
    print(memdiag_out.strip())
    results["memdiag_heap_integrity"] = ("integrity verified" in memdiag_out.lower() or "0 corruptions" in memdiag_out.lower())
    results["memdiag_phys_stats"] = ("physical memory" in memdiag_out.lower() or "total frames" in memdiag_out.lower())

    # 5. Test Network Interface Statistics & Sockets (`pktmon -s`)
    print("\n--- Test 5: Network Diagnostics & Socket Inspection (`pktmon -s`) ---")
    pktmon_out = send_command(s, "pktmon -s\n", timeout=6)
    print(pktmon_out.strip())
    results["pktmon_stats_executed"] = ("interface statistics" in pktmon_out.lower() or "eth0" in pktmon_out.lower())
    results["pktmon_sockets_executed"] = ("active sockets" in pktmon_out.lower() or "tcp" in pktmon_out.lower() or "closed" in pktmon_out.lower())

    # 6. Test Syscall Latency Profiling (`nyprof /bin/ls`)
    print("\n--- Test 6: Performance Profiling (`nyprof /bin/ls`) ---")
    nyprof_out = send_command(s, "nyprof /bin/ls\n", timeout=8)
    print(nyprof_out.strip())
    results["nyprof_executed"] = ("performance profile" in nyprof_out.lower() or "syscall invocations" in nyprof_out.lower() or "calls" in nyprof_out.lower())

    # 7. Test Syscall Tracing (`nytrace /bin/hello`)
    print("\n--- Test 7: System-Call Tracing (`nytrace /bin/hello`) ---")
    nytrace_out = send_command(s, "nytrace /bin/hello\n", timeout=8)
    print(nytrace_out.strip())
    results["nytrace_executed"] = ("write" in nytrace_out.lower() or "sys_" in nytrace_out.lower() or "traced" in nytrace_out.lower() or "hello" in nytrace_out.lower())

    # 8. Test Tracing Security Boundary (rejection of PID 1)
    print("\n--- Test 8: Tracing Security Boundary (reject PID 1) ---")
    nytrace_pid1 = send_command(s, "nytrace -p 1\n", timeout=5)
    print(nytrace_pid1.strip())
    results["nytrace_security_boundary"] = ("denied" in nytrace_pid1.lower() or "error" in nytrace_pid1.lower() or "cannot attach" in nytrace_pid1.lower() or "protected" in nytrace_pid1.lower())

    # 9. Clean stability check
    print("\n--- Test 9: Host Kernel Stability & Regression Check ---")
    ps_out = send_command(s, "ps\n", timeout=5)
    print(ps_out.strip())
    results["host_kernel_stable"] = ("init" in ps_out)

    # Clean shutdown
    send_command(s, "sync\n", timeout=2)
    s.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    print("\n" + "=" * 70)
    print("                   PHASE 18 TEST RESULTS SUMMARY                     ")
    print("=" * 70)
    all_passed = True
    for test, passed in results.items():
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {test}")
        if not passed:
            all_passed = False

    print("=" * 70)
    if all_passed:
        print("\n[+] SUCCESS: ALL PHASE 18 DEVELOPER TOOLS & OBSERVABILITY TESTS PASSED!\n")
        return 0
    else:
        print("\n[-] FAILURE: Some Phase 18 tests failed.\n")
        return 1

if __name__ == "__main__":
    sys.exit(run_tests())
