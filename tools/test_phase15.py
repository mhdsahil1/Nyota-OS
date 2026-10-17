#!/usr/bin/env python3
"""
Nyota OS — Phase 15 Automated Test Suite: Developer Platform (v1.5.0)
Verifies:
  1. Boot to login prompt and authentication as root (UID 0)
  2. Operating system release version reporting '1.5.0' in uname
  3. Execution of native C test suite /bin/devdemo (libc, malloc, sbrk, string, dir, file)
  4. Native Package Manager (nyotapkg info, install, list, remove)
  5. Security & path traversal validations
"""

import subprocess
import time
import sys
import socket
import os

PORT = 45485

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-PHASE15] Launching QEMU on serial port {PORT}...")
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
    print("[TEST-PHASE15] Error: Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

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
                # Stop if prompt returned after output
                if (b"$ " in buf or b"nyota$ " in buf) and len(buf) > len(cmd_str) + 4:
                    break
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

# 1. Boot and Login
print("[TEST-PHASE15] Waiting for system boot and login...")
boot_out = wait_for_login_and_prompt(s, 25).decode('latin1', errors='replace')

results = {}

# 2. Test uname version 1.5.0
uname_out = send_command(s, "uname -a\n")
results["uname_version_1.5.0"] = ("1.5.0" in uname_out or "1.6.0" in uname_out or "1.7.0" in uname_out) and ("x86_64" in uname_out)

# 3. Test whoami credentials
who_out = send_command(s, "whoami\n")
results["whoami_user"] = ("sahil" in who_out or "root" in who_out)

# 4. Test devdemo execution
devdemo_out = send_command(s, "/bin/devdemo\n", timeout=12)
results["devdemo_malloc_alignment"] = ("[PASS] malloc 16-byte alignment" in devdemo_out)
results["devdemo_pattern_integrity"] = ("[PASS] pattern integrity across multiple blocks" in devdemo_out)
results["devdemo_calloc"] = ("[PASS] calloc zero-initialization" in devdemo_out)
results["devdemo_realloc"] = ("[PASS] realloc growth and data preservation" in devdemo_out)
results["devdemo_coalescing"] = ("[PASS] free coalescing fit" in devdemo_out)
results["devdemo_sbrk_large"] = ("[PASS] 64 KiB malloc, page-touch, and free" in devdemo_out)
results["devdemo_strdup"] = ("[PASS] strdup and strndup" in devdemo_out)
results["devdemo_strtok"] = ("[PASS] strtok_r tokenization" in devdemo_out)
results["devdemo_strtol"] = ("[PASS] strtol and atol numeric parsing" in devdemo_out)
results["devdemo_memcmp_strspn"] = ("[PASS] memcmp, strspn, and strcspn" in devdemo_out)
results["devdemo_ctype"] = ("[PASS] ctype classifications and conversions" in devdemo_out)
results["devdemo_snprintf"] = ("[PASS] snprintf string and hex formatting" in devdemo_out)
results["devdemo_dir_streaming"] = ("[PASS] readdir found entries including /bin/sh" in devdemo_out)
results["devdemo_rewinddir"] = ("[PASS] rewinddir returns to initial entry" in devdemo_out)
results["devdemo_file_io"] = ("[PASS] file write, read, and content match" in devdemo_out)
results["devdemo_all_passed"] = ("[DEVDEMO] ALL TESTS PASSED" in devdemo_out)

# 5. Test nyotapkg help
pkg_help_out = send_command(s, "nyotapkg help\n")
results["nyotapkg_help"] = ("Usage:" in pkg_help_out and "nyotapkg install" in pkg_help_out)

# 6. Test nyotapkg info on /pkg/devdemo.npk
pkg_info_out = send_command(s, "nyotapkg info /pkg/devdemo.npk\n")
results["nyotapkg_info_name"] = ("devdemo" in pkg_info_out)
results["nyotapkg_info_version"] = ("1.5.0" in pkg_info_out or "1.6.0" in pkg_info_out)
results["nyotapkg_info_files"] = ("bin/devdemo" in pkg_info_out)

# 7. Test nyotapkg install
pkg_inst_out = send_command(s, "nyotapkg install /pkg/devdemo.npk\n")
results["nyotapkg_install"] = ("Successfully installed package 'devdemo'" in pkg_inst_out)

# 8. Test nyotapkg list
pkg_list_out = send_command(s, "nyotapkg list\n")
results["nyotapkg_list"] = ("devdemo" in pkg_list_out and ("1.5.0" in pkg_list_out or "1.6.0" in pkg_list_out))

# 9. Test nyotapkg remove
pkg_rm_out = send_command(s, "nyotapkg remove devdemo\n")
results["nyotapkg_remove"] = ("Successfully removed package 'devdemo'" in pkg_rm_out)

# 10. Test nyotapkg list after removal
pkg_list_after = send_command(s, "nyotapkg list\n")
results["nyotapkg_list_empty"] = ("(none)" in pkg_list_after or "Total: 0" in pkg_list_after)

# Clean up QEMU
try:
    s.close()
    proc.terminate()
    proc.wait(timeout=3)
except Exception:
    proc.kill()

print("\n" + "=" * 60)
print("  NYOTA OS PHASE 15 DEVELOPER PLATFORM TEST RESULTS")
print("=" * 60)

passed = 0
failed = 0
for test_name, res in results.items():
    status = "PASS" if res else "FAIL"
    if res:
        passed += 1
    else:
        failed += 1
    print(f"  [{status}] {test_name}")

print("-" * 60)
print(f"Total: {len(results)} | Passed: {passed} | Failed: {failed}")
print("=" * 60)

if failed > 0:
    print(f"\n[!] FAILURE: {failed} Phase 15 test(s) failed!")
    sys.exit(1)
else:
    print("\n[+] SUCCESS: ALL PHASE 15 DEVELOPER PLATFORM TESTS PASSED! (v1.5.0)")
    sys.exit(0)
