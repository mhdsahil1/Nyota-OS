import subprocess
import time
import sys
import socket
import threading

SERIAL_PORT = 45480
ECHO_PORT = 8080

cmd = [
    "qemu-system-x86_64",
    "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
    "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
    "-netdev", f"user,id=net0,hostfwd=tcp::{ECHO_PORT}-:8080",
    "-device", "e1000,netdev=net0",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{SERIAL_PORT},server,wait"
]

print(f"[TCP_TEST] Launching QEMU on serial port {SERIAL_PORT}...")
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
    print("[TCP_TEST] Failed to connect to QEMU serial socket!")
    proc.terminate()
    sys.exit(1)

serial_buffer = bytearray()
running = True

def serial_reader():
    s.settimeout(0.2)
    while running:
        try:
            chunk = s.recv(1024)
            if chunk:
                serial_buffer.extend(chunk)
                sys.stdout.write(chunk.decode('latin1', errors='replace'))
                sys.stdout.flush()
        except socket.timeout:
            pass
        except Exception:
            break

reader_thread = threading.Thread(target=serial_reader, daemon=True)
reader_thread.start()

def wait_for_pattern(expected, timeout=12):
    start = time.time()
    while time.time() - start < timeout:
        if expected in serial_buffer:
            return True
        time.sleep(0.1)
    return False

def send_cmd(command):
    print(f"\n[TCP_TEST_CMD] >>> {command}", flush=True)
    time.sleep(0.2)
    s.sendall(command.encode('latin1') + b"\n")

try:
    print("[TCP_TEST] Waiting for boot and login...", flush=True)
    logged_in = False
    password_sent = False
    start = time.time()
    while time.time() - start < 20:
        if not logged_in and b"login:" in serial_buffer:
            time.sleep(0.15)
            s.sendall(b"sahil\n")
            logged_in = True
        elif logged_in and not password_sent and b"Password:" in serial_buffer:
            time.sleep(0.15)
            s.sendall(b"\n")
            password_sent = True
        elif (b"$ " in serial_buffer or b"nyota$ " in serial_buffer) and (password_sent or not logged_in):
            break
        time.sleep(0.1)

    if b"$ " not in serial_buffer and b"nyota$ " not in serial_buffer:
        print("\n[TCP_TEST FAIL] Did not reach shell prompt!")
        sys.exit(1)

    # Launch echo-server in background
    send_cmd("echo-server 8080 &")
    if not wait_for_pattern(b"Listening on 0.0.0.0:8080", timeout=5):
        print("\n[TCP_TEST WARN] Listening banner not seen")

    time.sleep(0.5)

    print("\n[TCP_TEST] Connecting from host client to 127.0.0.1:8080...", flush=True)
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.settimeout(10.0)

    tcp_connected = False
    for _ in range(25):
        try:
            client.connect(('127.0.0.1', ECHO_PORT))
            tcp_connected = True
            break
        except Exception:
            time.sleep(0.2)

    if not tcp_connected:
        print("[TCP_TEST FAIL] Failed to connect to echo-server from host!")
    else:
        print("[TCP_TEST] Connected to echo-server! Sending 'hello nyota'...", flush=True)
        time.sleep(0.5)
        client.sendall(b"hello nyota\n")
        try:
            data = client.recv(1024)
            print(f"\n[TCP_TEST] Host received: {repr(data)}", flush=True)
            if b"hello nyota" in data:
                print("[PASS] TCP Echo Server verified successfully over host network!")
            else:
                print("[WARN] Received data differed from sent payload")
        except socket.timeout:
            print("\n[TCP_TEST ERROR] client.recv() timed out waiting for echo response!")
            time.sleep(2.0)
        client.close()

finally:
    running = False
    time.sleep(0.2)
    s.close()
    proc.terminate()
