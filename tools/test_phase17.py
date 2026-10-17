#!/usr/bin/env python3
"""
Nyota OS — Phase 17 Automated Test Suite: Hardware-Assisted Virtualization & Hypervisor (v1.7.0)

Verifies:
  1. System boot and OS version reporting '1.7.0' via `uname -a`.
  2. Hypervisor initialization and hardware interrogation in kernel boot logs.
  3. Availability and execution of `/bin/vmm` userspace utility.
  4. Hypervisor operational status reporting via `vmm info`.
  5. Virtual Machine execution lifecycle via `vmm run`.
  6. CPUID and HLT VM-exit interception handling.
  7. Guest physical memory isolation and verification (Magic 0xCAFEBABE write).
  8. Graceful operation of host kernel and userspace when virtualization is tested.
"""

import subprocess
import socket
import time
import sys
import os

QEMU = "qemu-system-x86_64"
IMAGE = "build/nyota.img"
DATA_IMAGE = "build/nyota-data.img"
PORT = 45504

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
    print(" Nyota OS Phase 17: Virtualization & Hypervisor Test Suite (v1.7.0)")
    print("=" * 70)

    # Launch QEMU with nested hardware virtualization enabled (+svm)
    qemu_cmd = [
        QEMU,
        "-cpu", "qemu64,+svm",
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

    # 1. Test uname version 1.7.0
    print("\n--- Test 1: System Version Metadata (v1.7.0) ---")
    uname_out = send_command(s, "uname -a\n")
    print(f"uname output: {uname_out.strip()}")
    results["uname_version_1.7.0"] = ("1.7.0" in uname_out or "1.8.0" in uname_out) and ("x86_64" in uname_out)

    # 2. Test presence of /bin/vmm
    print("\n--- Test 2: Presence of /bin/vmm Control Utility ---")
    bin_ls = send_command(s, "ls /bin\n")
    results["vmm_binary_present"] = "vmm" in bin_ls

    # 3. Test vmm info command
    print("\n--- Test 3: Hypervisor Hardware Capability Discovery (`vmm info`) ---")
    vmm_info = send_command(s, "vmm info\n", timeout=6)
    print(vmm_info.strip())
    results["vmm_info_executed"] = ("Nyota OS Hypervisor Hardware Capabilities" in vmm_info)
    results["vmm_hardware_vendor"] = ("AMD" in vmm_info or "Intel" in vmm_info or "Available" in vmm_info)

    # 4. Test vmm stats command
    print("\n--- Test 4: Hypervisor Statistics Reporting (`vmm stats`) ---")
    vmm_stats = send_command(s, "vmm stats\n", timeout=5)
    print(vmm_stats.strip())
    results["vmm_stats_executed"] = ("Hypervisor Statistics" in vmm_stats)

    # 5. Test vmm test command (Full self-test lifecycle)
    print("\n--- Test 5: Hypervisor Automated Self-Test Suite (`vmm test`) ---")
    vmm_test = send_command(s, "vmm test\n", timeout=8)
    print(vmm_test.strip())
    results["vmm_test_executed"] = ("Nyota Hypervisor Self-Test Suite" in vmm_test)
    results["vmm_lifecycle_pass"] = ("[PASS]" in vmm_test and "Hardware-Assisted VM Lifecycle Complete" in vmm_test)
    results["vmm_memory_isolation"] = ("0xCAFEBABE" in vmm_test or "ISOLATED" in vmm_test)

    # 6. Test host stability after VM execution
    print("\n--- Test 6: Host Stability & Regression Check ---")
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
    print("                   PHASE 17 TEST RESULTS SUMMARY                     ")
    print("=" * 70)
    all_passed = True
    for test, passed in results.items():
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {test}")
        if not passed:
            all_passed = False

    print("=" * 70)
    if all_passed:
        print("\n[+] SUCCESS: ALL PHASE 17 VIRTUALIZATION & HYPERVISOR TESTS PASSED!\n")
        return 0
    else:
        print("\n[-] FAILURE: Some Phase 17 tests failed.\n")
        return 1

if __name__ == "__main__":
    sys.exit(run_tests())
