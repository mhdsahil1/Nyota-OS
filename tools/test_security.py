import subprocess
import time
import sys
import socket

PORT = 45479

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST-SEC] Launching QEMU with serial on port {PORT}...")
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
    print("[TEST-SEC] Failed to connect to QEMU serial socket!")
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

def send_command(s, cmd_str, timeout=6):
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

# 1. Wait for initial boot
initial_out = wait_for_prompt(s, 20).decode('latin1', errors='replace')

checks = {}

# 2. Test secinfo
sec_out = send_command(s, "secinfo\n")
checks["secinfo_isolation"] = "User/kernel isolation : enabled" in sec_out
checks["secinfo_guard_pages"] = "Guard pages           : enabled" in sec_out
checks["secinfo_pointer_val"] = "Pointer validation    : enabled" in sec_out
checks["secinfo_capabilities"] = "Capabilities          : enabled" in sec_out
checks["secinfo_resource_limits"] = "Resource limits       : enabled" in sec_out
checks["secinfo_aslr"] = "ASLR foundation       : enabled" in sec_out

# 3. Test ipctest
ipc_out = send_command(s, "ipctest\n")
checks["ipc_pipe"] = "[ OK ] pipe communication" in ipc_out
checks["ipc_read"] = "[ OK ] blocking read" in ipc_out
checks["ipc_shm"] = "[ OK ] shared memory" in ipc_out
checks["ipc_perms"] = "[ OK ] permission checks" in ipc_out
checks["ipc_cleanup"] = "[ OK ] cleanup" in ipc_out

# 4. Test pipeline: ls | cat
pipe_out = send_command(s, "ls | cat\n")
checks["pipeline"] = ("bin" in pipe_out) and ("dev" in pipe_out)

# 5. Test ps
ps_out = send_command(s, "ps\n")
checks["ps_output"] = ("PID" in ps_out) and ("PPID" in ps_out) and ("init" in ps_out) and ("sh" in ps_out)

# 6. Test background execution: echo-server 8080 &
bg_out = send_command(s, "echo-server 8080 &\n")
checks["background_job"] = "[1]" in bg_out

# 7. Check ps after background execution
ps2_out = send_command(s, "ps\n")
checks["ps_has_echo_server"] = "echo-server" in ps2_out

# 8. Test userspace crash recovery: crash
crash_out = send_command(s, "crash\n")
checks["crash_page_fault"] = "page fault" in crash_out.lower()
checks["crash_sigsegv"] = "sigsegv" in crash_out.lower() or "11" in crash_out
checks["crash_kernel_survived"] = ("nyota$ " in crash_out) or ("$ " in crash_out)

# 9. Test memtest
mem_out = send_command(s, "memtest\n")
checks["mem_own"] = "[ OK ] own memory access" in mem_out
checks["mem_kernel_blocked"] = "[ OK ] kernel memory blocked" in mem_out
checks["mem_foreign_blocked"] = "[ OK ] foreign process memory blocked" in mem_out
checks["mem_unmapped_sigsegv"] = "[ OK ] unmapped memory -> SIGSEGV" in mem_out
checks["mem_survived"] = "[ OK ] process survived" in mem_out

# 10. Test kill on background process
kill_out = send_command(s, "kill -KILL 10\n")
checks["kill_command"] = ("error" not in kill_out.lower())

# 11. Test stressproc
stress_out = send_command(s, "stressproc 8\n", timeout=12)
checks["stressproc_pass"] = "[ OK ] Process stress test passed" in stress_out

s.close()
proc.terminate()

print("\n" + "=" * 60)
print("              PHASE 8 SECURITY TEST REPORT")
print("=" * 60)

all_passed = True
for name, passed in checks.items():
    status = "[ PASS ]" if passed else "[ FAIL ]"
    print(f"  {status} {name}")
    if not passed:
        all_passed = False

print("=" * 60)
if all_passed:
    print("ALL PHASE 8 SECURITY & IPC CHECKS PASSED SUCCESSFULLY!")
else:
    print("SOME CHECKS FAILED - REVIEW OUTPUT ABOVE.")
print("=" * 60)

sys.exit(0 if all_passed else 1)
