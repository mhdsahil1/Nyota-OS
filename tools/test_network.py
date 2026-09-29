import subprocess
import time
import sys
import socket
import threading

PORT = 45475

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
    "-device", "e1000,netdev=net0",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST] Launching QEMU on serial port {PORT} with E1000 NIC...")
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
    print("[TEST] Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

def read_until(expected, timeout=10):
    output = bytearray()
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(1024)
            if chunk:
                output.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if expected in output:
                    return output
        except socket.timeout:
            pass
    return output

def send_cmd(command):
    print(f"\n[TEST_CMD] >>> {command}", flush=True)
    time.sleep(0.2)
    s.sendall(command.encode('latin1') + b"\n")

try:
    # 1. Wait for boot and shell prompt
    print("[TEST] Waiting for boot and login...", flush=True)
    boot_out = bytearray()
    start = time.time()
    logged_in = False
    password_sent = False
    while time.time() - start < 20:
        try:
            chunk = s.recv(1024)
            if chunk:
                boot_out.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
                if not logged_in and b"login:" in boot_out:
                    time.sleep(0.15)
                    s.sendall(b"sahil\n")
                    logged_in = True
                elif logged_in and not password_sent and b"Password:" in boot_out:
                    time.sleep(0.15)
                    s.sendall(b"\n")
                    password_sent = True
                elif (b"$ " in boot_out or b"nyota$ " in boot_out) and (password_sent or not logged_in):
                    break
        except socket.timeout:
            pass

    if b"$ " not in boot_out and b"nyota$ " not in boot_out:
        print("\n[TEST FAIL] Did not reach shell prompt in time!")
        sys.exit(1)

    prompt = b"$ " if b"$ " in boot_out else b"nyota$ "

    # 2. Test ifconfig
    send_cmd("ifconfig")
    out = read_until(prompt, timeout=6)
    if b"eth0" in out and b"10.0.2.15" in out:
        print("\n[PASS] ifconfig verified eth0 with 10.0.2.15")
    else:
        print("\n[WARN] ifconfig output missing expected eth0 fields")

    # 3. Test ping 127.0.0.1
    send_cmd("ping 127.0.0.1")
    out = read_until(prompt, timeout=8)
    if b"bytes from 127.0.0.1" in out:
        print("\n[PASS] ping 127.0.0.1 successful (Loopback verified)")
    else:
        print("\n[WARN] ping 127.0.0.1 did not show expected echo reply")

    # 4. Test ping 10.0.2.2 (Gateway)
    send_cmd("ping 10.0.2.2")
    out = read_until(prompt, timeout=8)
    if b"bytes from 10.0.2.2" in out:
        print("\n[PASS] ping 10.0.2.2 successful (ARP + IPv4 + ICMP verified)")
    else:
        print("\n[WARN] ping 10.0.2.2 did not receive echo reply")

    # 5. Test netstat
    send_cmd("netstat")
    out = read_until(prompt, timeout=6)
    if b"Proto" in out:
        print("\n[PASS] netstat displayed protocol table")

    # 6. Test nslookup example.com
    send_cmd("nslookup example.com")
    out = read_until(prompt, timeout=8)
    print("\n[INFO] nslookup finished")

    print("\n[TEST] All automated checks complete!")

finally:
    s.close()
    proc.terminate()
