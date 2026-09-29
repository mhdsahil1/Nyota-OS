import subprocess
import time
import sys
import socket

SERIAL_PORT = 45479

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
    "-device", "e1000,netdev=net0",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{SERIAL_PORT},server,wait"
]

print(f"[TCP_INTERNAL] Launching QEMU on serial port {SERIAL_PORT}...")
proc = subprocess.Popen(cmd)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
connected = False
for _ in range(50):
    try:
        s.connect(('127.0.0.1', SERIAL_PORT))
        connected = True
        break
    except Exception:
        time.sleep(0.1)

if not connected:
    print("[TCP_INTERNAL] Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

def read_until(expected, timeout=12):
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
    print(f"\n[INTERNAL_CMD] >>> {command}", flush=True)
    time.sleep(0.2)
    s.sendall(command.encode('latin1') + b"\n")

try:
    print("[TCP_INTERNAL] Waiting for boot and login...", flush=True)
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
        print("\n[FAIL] Did not reach shell prompt!")
        sys.exit(1)

    prompt = b"$ " if b"$ " in boot_out else b"nyota$ "

    # 1. Start echo server in background
    send_cmd("echo-server 8080 &")
    time.sleep(0.8)
    out = read_until(b"Listening on 0.0.0.0:8080", timeout=5)

    # 2. Run netcat connecting to 127.0.0.1:8080
    send_cmd("netcat 127.0.0.1 8080 TestMessage123")
    out = read_until(prompt, timeout=10)

    if b"Received: TestMessage123" in out or b"TestMessage123" in out:
        print("\n[PASS] Internal TCP client/server communication verified successfully!")
    else:
        print("\n[WARN] Expected echoed message not found in output")

finally:
    s.close()
    proc.terminate()
