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

# Read until we see "nyota$ "
while time.time() - start < 15:
    try:
        chunk = s.recv(1024)
        if chunk:
            output.extend(chunk)
            sys.stdout.write(chunk.decode('latin1', errors='replace'))
            sys.stdout.flush()
            if b"nyota$ " in output:
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
