#!/usr/bin/env python3
"""
Nyota OS — Phase 14 Automated Security & Kernel Hardening Test Suite
Target Version: v1.4.0
Tests user/group identities, credentials, file permissions, capability boundaries,
NX & memory isolation, safe user pointers, integer overflow protection,
and syscall filtering sandbox.
"""

import subprocess
import time
import sys
import socket

PORT = 45480

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-PHASE14] Launching QEMU on serial port {PORT}...")
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
    print("[TEST-PHASE14] Error: Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

def wait_for_prompt(s, timeout=20):
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

# 1. Wait for system boot and login prompt
initial_out = wait_for_prompt(s, 20).decode('latin1', errors='replace')

results = {}

# 2. Test uname version
uname_out = send_command(s, "uname -a\n")
results["uname_version_1.4.0"] = ("1.4.0" in uname_out or "1.5.0" in uname_out) and ("x86_64" in uname_out)

# 3. Test id utility
id_out = send_command(s, "id\n")
results["id_uid_1000"] = ("uid=1000" in id_out)
results["id_gid_1000"] = ("gid=1000" in id_out)
results["id_euid_1000"] = ("euid=1000" in id_out)
results["id_egid_1000"] = ("egid=1000" in id_out)

# 4. Test whoami utility
whoami_out = send_command(s, "whoami\n")
results["whoami_sahil"] = ("sahil" in whoami_out)

# 5. Test secinfo
sec_out = send_command(s, "secinfo\n")
results["secinfo_isolation"] = ("User/kernel isolation : enabled" in sec_out)
results["secinfo_guard_pages"] = ("Guard pages           : enabled" in sec_out)
results["secinfo_pointer_val"] = ("Pointer validation    : enabled" in sec_out)
results["secinfo_capabilities"] = ("Capabilities          : enabled" in sec_out)

# 6. Test auditlog restricted access
audit_out = send_command(s, "auditlog\n")
results["auditlog_restricted"] = ("permission denied" in audit_out.lower() or "requires root" in audit_out.lower())

# 7. Test shadow file permissions via cat
shadow_out = send_command(s, "cat /etc/shadow\n")
results["shadow_file_protected"] = ("denied" in shadow_out.lower() or "failed" in shadow_out.lower() or "no such file" in shadow_out.lower())

# 8. Test comprehensive Phase 14 verification suite: sectest
sectest_out = send_command(s, "sectest\n", timeout=12)
results["sectest_identity_uid"] = "[ PASS ] identity_uid_is_1000" in sectest_out
results["sectest_identity_euid"] = "[ PASS ] identity_euid_is_1000" in sectest_out
results["sectest_unauthorized_seteuid"] = "[ PASS ] unauthorized_seteuid_rejected" in sectest_out
results["sectest_unauthorized_setuid"] = "[ PASS ] unauthorized_setuid_rejected" in sectest_out
results["sectest_unauthorized_setegid"] = "[ PASS ] unauthorized_setegid_rejected" in sectest_out
results["sectest_shadow_read_blocked"] = "[ PASS ] shadow_file_read_blocked_mode_0600" in sectest_out
results["sectest_chmod_sys_blocked"] = "[ PASS ] unauthorized_chmod_system_file_blocked" in sectest_out
results["sectest_chown_sys_blocked"] = "[ PASS ] unauthorized_chown_system_file_blocked" in sectest_out
results["sectest_user_create_file"] = "[ PASS ] user_can_create_own_file" in sectest_out
results["sectest_owner_chmod_file"] = "[ PASS ] owner_can_chmod_own_file" in sectest_out
results["sectest_auditlog_denied"] = "[ PASS ] auditlog_read_denied_without_cap" in sectest_out
results["sectest_kernel_ptr_efault"] = "[ PASS ] kernel_pointer_in_syscall_returns_efault" in sectest_out
results["sectest_unmapped_ptr_efault"] = "[ PASS ] unmapped_pointer_in_syscall_returns_efault" in sectest_out
results["sectest_null_ptr_efault"] = "[ PASS ] null_pointer_in_syscall_returns_efault" in sectest_out
results["sectest_overflow_len_rejected"] = "[ PASS ] integer_overflow_length_rejected" in sectest_out
results["sectest_sandbox_filter"] = "[ PASS ] syscall_filtering_sandbox_enforced" in sectest_out

# 9. Test crash recovery and isolation
crash_out = send_command(s, "crash\n")
results["crash_fault_handled"] = ("page fault" in crash_out.lower() or "sigsegv" in crash_out.lower())
results["crash_kernel_alive"] = ("nyota" in crash_out or "$ " in crash_out)

# 10. Test memory isolation
mem_out = send_command(s, "memtest\n")
results["memtest_kernel_blocked"] = "[ OK ] kernel memory blocked" in mem_out
results["memtest_foreign_blocked"] = "[ OK ] foreign process memory blocked" in mem_out
results["memtest_process_survived"] = "[ OK ] process survived" in mem_out

s.close()
proc.terminate()

print("\n" + "=" * 65)
print("         PHASE 14 SECURITY ARCHITECTURE & HARDENING REPORT       ")
print("=" * 65)

all_passed = True
pass_count = 0
fail_count = 0
for name, passed in results.items():
    status = "[ PASS ]" if passed else "[ FAIL ]"
    print(f"  {status} {name}")
    if passed:
        pass_count += 1
    else:
        fail_count += 1
        all_passed = False

print("=" * 65)
print(f"Total: {len(results)} | Passed: {pass_count} | Failed: {fail_count} | Skipped: 0")
if all_passed:
    print("ALL PHASE 14 SECURITY HARDENING TESTS PASSED SUCCESSFULLY (v1.4.0)!")
else:
    print("SOME TESTS FAILED - REVIEW OUTPUT ABOVE.")
print("=" * 65)

sys.exit(0 if all_passed else 1)
