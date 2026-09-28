import subprocess
import time
import sys
import socket

PORT = 45472

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
]

print(f"[RUNNER] Starting QEMU with TCP serial on port {PORT}...")
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
    print("[RUNNER] Error: Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)

output = bytearray()
commands = [
    b"echo Hello Nyota Userspace\n",
    b"hello\n",
    b"ls /bin\n",
    b"cat /etc/nyota.conf\n",
    b"ps\n",
    b"/bin/test\n"
]
cmd_idx = 0
start = time.time()

while time.time() - start < 40:
    try:
        chunk = s.recv(1024)
        if not chunk:
            break
        output.extend(chunk)
        sys.stdout.write(chunk.decode('latin1', errors='replace'))
        sys.stdout.flush()
    except socket.timeout:
        pass

    if output.count(b"nyota$ ") > cmd_idx:
        if cmd_idx < len(commands):
            c = commands[cmd_idx]
            cmd_idx += 1
            print(f"\n[RUNNER] ---> Sending command [{cmd_idx}/{len(commands)}]: {c.decode('latin1').strip()}", flush=True)
            time.sleep(0.05)
            s.sendall(c)
            start = time.time()
        else:
            print("\n[RUNNER] All commands executed successfully!", flush=True)
            time.sleep(0.5)
            break

s.close()
proc.terminate()
print("\n[RUNNER] Done.")
