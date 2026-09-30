#!/usr/bin/env python3
"""Boot Nyota in QEMU, log in, open a terminal window, and save a desktop PNG."""

import socket
import subprocess
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SERIAL_PORT = 45486
MONITOR_PORT = 45487
PPM_PATH = ROOT / "build" / "nyota-desktop.ppm"
PNG_PATH = ROOT / "assets" / "nyota-desktop.png"

qemu = subprocess.Popen([
    "qemu-system-x86_64",
    "-drive", f"format=raw,file={ROOT / 'build' / 'nyota.img'},index=0,media=disk",
    "-drive", f"format=raw,file={ROOT / 'build' / 'nyota-data.img'},index=1,media=disk",
    "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
    "-device", "e1000,netdev=net0",
    "-display", "none",
    "-serial", f"tcp:127.0.0.1:{SERIAL_PORT},server,wait",
    "-monitor", f"tcp:127.0.0.1:{MONITOR_PORT},server,nowait",
], cwd=ROOT)

def connect(port, attempts=100):
    for _ in range(attempts):
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            sock.settimeout(0.25)
            return sock
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"QEMU socket {port} did not open")

def read_until(sock, marker, timeout=30):
    data = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if chunk:
                data.extend(chunk)
                if marker in data:
                    return bytes(data)
        except socket.timeout:
            pass
    raise RuntimeError(f"QEMU did not emit {marker!r}")

serial = monitor = None
try:
    serial = connect(SERIAL_PORT)
    monitor = connect(MONITOR_PORT)
    read_until(serial, b"login:")
    serial.sendall(b"sahil\n")
    read_until(serial, b"Password:")
    serial.sendall(b"\n")
    session_output = read_until(serial, b"$ ")
    if b"[DESKTOP] Window Server initialized" not in session_output:
        session_output += read_until(serial, b"[DESKTOP] Window Server initialized", timeout=10)
    print("Desktop server started; launching Terminal.")
    serial.sendall(b"term &\n")
    read_until(serial, b"$ ")
    time.sleep(1.5)

    read_until(monitor, b"(qemu)", timeout=5)
    for key in ["u", "n", "a", "m", "e", "ret"]:
        monitor.sendall(f"sendkey {key}\n".encode())
        read_until(monitor, b"(qemu)", timeout=3)
        time.sleep(0.05)
    time.sleep(0.5)
    monitor.sendall(b"screendump build/nyota-desktop.ppm\n")
    monitor_reply = read_until(monitor, b"(qemu)", timeout=10)
    if not PPM_PATH.exists():
        raise RuntimeError(f"QEMU screendump failed: {monitor_reply.decode(errors='replace')}")
    Image.open(PPM_PATH).convert("RGB").save(PNG_PATH, optimize=True)
    print(f"Captured {PNG_PATH.relative_to(ROOT)}")
finally:
    if serial:
        serial.close()
    if monitor:
        monitor.close()
    qemu.terminate()
    try:
        qemu.wait(timeout=3)
    except subprocess.TimeoutExpired:
        qemu.kill()
