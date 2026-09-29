import subprocess
import time
import sys
import socket

PORT = 45473

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[TEST] Launching QEMU on port {PORT}...")
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

output = bytearray()
start = time.time()

# Read until we see login prompt, log in, and reach shell prompt
logged_in = False
password_sent = False
while time.time() - start < 20:
    try:
        chunk = s.recv(1024)
        if chunk:
            output.extend(chunk)
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
            if not logged_in and b"login:" in output:
                time.sleep(0.15)
                s.sendall(b"sahil\n")
                logged_in = True
            elif logged_in and not password_sent and b"Password:" in output:
                time.sleep(0.15)
                s.sendall(b"\n")
                password_sent = True
            elif (b"$ " in output or b"nyota$ " in output) and (password_sent or not logged_in):
                print("\n[TEST] Saw prompt! Sending 'help\\n'...", flush=True)
                time.sleep(0.1)
                s.sendall(b"help\n")
                break
    except socket.timeout:
        pass

# Read response for 5 seconds
start = time.time()
while time.time() - start < 5:
    try:
        chunk = s.recv(1024)
        if chunk:
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
    except socket.timeout:
        pass

s.close()
proc.terminate()
print("\n[TEST] Done.")
