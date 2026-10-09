#!/usr/bin/env python3
"""
Nyota OS — Phase 12 Storage & Hardware Support Automated Test Suite
Tests:
  - PCI hardware enumeration (lspci)
  - Unified block device layer and capacity inspection (lsblk)
  - Controller hardware details (diskinfo)
  - Filesystem statistics (df / statvfs)
  - AHCI SATA controller and DMA I/O
  - NVMe controller and DMA I/O
  - MBR partition discovery & boundary validation (sda1)
  - GPT partition discovery & CRC32 verification (nvme0p1)
  - Block cache consistency, dirty writeback, and flush (disktest)
  - Safe root filesystem protection
"""

import subprocess
import time
import sys
import socket
import struct
import zlib
import random
import os

def find_free_port():
    for _ in range(50):
        p = random.randint(46000, 59000)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as test_s:
            try:
                test_s.bind(('127.0.0.1', p))
                return p
            except OSError:
                continue
    return 45678

def create_mbr_image(path, size_mb=16):
    """Creates a 16MB raw disk image with a valid MBR partition table."""
    total_sectors = (size_mb * 1024 * 1024) // 512
    img = bytearray(size_mb * 1024 * 1024)

    # Sector 0: MBR
    # Partition 1: Linux Native (0x83), starting LBA 2048, count 20480 (10MB)
    part1_start = 2048
    part1_count = 20480
    part_entry = struct.pack(
        "<B3sB3sII",
        0x80,               # Bootable
        b"\x00\x02\x00",    # CHS start
        0x83,               # Type: Linux native / data
        b"\x00\x00\x00",    # CHS end
        part1_start,        # LBA start
        part1_count         # Sector count
    )
    img[446:446+16] = part_entry
    img[510:512] = b"\x55\xAA" # MBR signature

    # Fill some test data in partition 1 (LBA 2048)
    p_offset = part1_start * 512
    img[p_offset:p_offset+512] = b"NYOTA_MBR_PARTITION_DATA_VERIFIED_2026" * 12

    with open(path, "wb") as f:
        f.write(img)
    print(f"[TEST] Created MBR test image: {path} ({size_mb} MB)")

def create_gpt_image(path, size_mb=16):
    """Creates a 16MB raw disk image with a valid GPT partition table."""
    total_sectors = (size_mb * 1024 * 1024) // 512
    img = bytearray(size_mb * 1024 * 1024)

    # Sector 0: Protective MBR
    protective_entry = struct.pack(
        "<B3sB3sII",
        0x00,               # Non-bootable
        b"\x00\x02\x00",    # CHS start
        0xEE,               # Type: Protective GPT
        b"\xFF\xFF\xFF",    # CHS end
        1,                  # Starting LBA
        0xFFFFFFFF          # Sector count
    )
    img[446:446+16] = protective_entry
    img[510:512] = b"\x55\xAA"

    # Sector 2..33: Partition Entries Array (128 entries * 128 bytes = 16384 bytes = 32 sectors)
    part_entries = bytearray(128 * 128)
    part1_start = 2048
    part1_end   = 18431 # ~8MB

    # Partition 1: Linux filesystem data GUID (0FC63DAF-8483-4772-8E79-3D69D8477DE4)
    type_guid   = b"\xaf\x3d\xc6\x0f\x83\x84\x72\x47\x8e\x79\x3d\x69\xd8\x47\x7d\xe4"
    unique_guid = b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10"
    name_utf16  = "NyotaGPT".encode("utf-16le").ljust(72, b"\x00")

    part1_bytes = struct.pack(
        "<16s16sQQQ72s",
        type_guid,
        unique_guid,
        part1_start,
        part1_end,
        0,                  # Attributes
        name_utf16
    )
    part_entries[0:128] = part1_bytes
    part_array_crc = zlib.crc32(part_entries) & 0xFFFFFFFF

    # Write partition entries at LBA 2 (offset 1024)
    img[1024:1024+len(part_entries)] = part_entries

    # Sector 1: GPT Header
    disk_guid = b"\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff\x00"
    hdr_without_crc = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART",        # Signature
        0x00010000,         # Revision 1.0
        92,                 # Header size
        0,                  # Header CRC32 (zero during computation)
        0,                  # Reserved
        1,                  # Current LBA
        total_sectors - 1,  # Backup LBA
        34,                 # First usable LBA
        total_sectors - 34, # Last usable LBA
        disk_guid,          # Disk GUID
        2,                  # Partition entries starting LBA
        128,                # Number of partition entries
        128,                # Size of partition entry
        part_array_crc      # Partition array CRC32
    )
    header_crc = zlib.crc32(hdr_without_crc) & 0xFFFFFFFF
    gpt_hdr = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART",
        0x00010000,
        92,
        header_crc,
        0,
        1,
        total_sectors - 1,
        34,
        total_sectors - 34,
        disk_guid,
        2,
        128,
        128,
        part_array_crc
    )
    img[512:512+92] = gpt_hdr

    # Fill some test data in partition 1 (LBA 2048)
    p_offset = part1_start * 512
    img[p_offset:p_offset+512] = b"NYOTA_GPT_PARTITION_DATA_VERIFIED_2026" * 12

    with open(path, "wb") as f:
        f.write(img)
    print(f"[TEST] Created GPT test image: {path} ({size_mb} MB)")

def run_storage_tests():
    os.makedirs("build", exist_ok=True)
    sata_img = "build/test_sata_mbr.img"
    nvme_img = "build/test_nvme_gpt.img"

    create_mbr_image(sata_img, 16)
    create_gpt_image(nvme_img, 16)

    port = find_free_port()
    print(f"\n============================================================")
    print(f"       TESTING NYOTA OS STORAGE & HARDWARE (port {port})")
    print(f"============================================================")

    cmd = [
        "qemu-system-x86_64",
        "-smp", "4",
        "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
        "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
        # AHCI SATA Controller with disposable MBR disk
        "-device", "ahci,id=ahci",
        "-drive", f"id=sata0,file={sata_img},if=none,format=raw",
        "-device", "ide-hd,drive=sata0,bus=ahci.0",
        # NVMe Controller with disposable GPT disk
        "-drive", f"id=nvme0,file={nvme_img},if=none,format=raw",
        "-device", "nvme,drive=nvme0,serial=NYOTA_NVME01",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{port},server,wait"
    ]

    proc = subprocess.Popen(cmd)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    connected = False
    for _ in range(60):
        try:
            s.connect(('127.0.0.1', port))
            connected = True
            break
        except Exception:
            time.sleep(0.1)

    if not connected:
        print(f"[ERROR] Failed to connect to QEMU on port {port}")
        proc.kill()
        return False

    s.settimeout(0.5)

    def wait_for_prompt(s, timeout=30):
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
                        time.sleep(0.2)
                        s.sendall(b"sahil\n")
                        logged_in = True
                    elif logged_in and not password_sent and (b"Password:" in buf or b"d: " in buf):
                        time.sleep(0.2)
                        s.sendall(b"\n")
                        password_sent = True
                    elif password_sent and (b"$ " in buf or b"[sahil@nyota" in buf):
                        time.sleep(0.3)
                        return buf.decode('latin1', errors='replace')
            except socket.timeout:
                pass
        return buf.decode('latin1', errors='replace')

    def send_cmd(s, cmd_str, timeout=15):
        time.sleep(0.3)
        s.setblocking(False)
        try:
            while True:
                chunk = s.recv(1024)
                if not chunk: break
        except Exception:
            pass
        s.settimeout(0.5)
        s.sendall(cmd_str.encode('ascii') + b"\n")
        buf = bytearray()
        start = time.time()
        cmd_seen = False
        target_token = cmd_str.strip().encode('ascii')
        while time.time() - start < timeout:
            try:
                chunk = s.recv(1024)
                if chunk:
                    buf.extend(chunk)
                    sys.stdout.write(chunk.decode('latin1', errors='replace'))
                    sys.stdout.flush()
                    if target_token in buf:
                        cmd_seen = True
                    if cmd_seen and (b"$ " in buf or b"[sahil@nyota" in buf):
                        break
            except socket.timeout:
                pass
        return buf.decode('latin1', errors='replace')

    print("[TEST] Waiting for Nyota OS login prompt...")
    wait_for_prompt(s, 30)

    test_results = {}

    # Test 1: lspci
    print("\n>>> Running: lspci -v")
    out = send_cmd(s, "lspci -v")
    pci_ok = ("Nyota PCI Devices" in out) and \
             ("Host bridge" in out) and \
             ("Ethernet controller" in out or "VGA" in out) and \
             ("SATA controller" in out or "NVMe controller" in out or "IDE controller" in out)
    test_results["lspci_enumeration"] = pci_ok

    # Test 2: lsblk
    print("\n>>> Running: lsblk")
    out_lsblk = send_cmd(s, "lsblk")
    lsblk_ok = ("Nyota Storage Devices" in out_lsblk) and \
               ("/dev/ata0" in out_lsblk or "/dev/ata1" in out_lsblk) and \
               ("ONLINE" in out_lsblk)
    test_results["lsblk_listing"] = lsblk_ok

    # Test 3: diskinfo
    print("\n>>> Running: diskinfo")
    out_info = send_cmd(s, "diskinfo")
    diskinfo_ok = ("Nyota Storage Architecture" in out_info) and \
                  ("Capacity:" in out_info) and \
                  ("Sector Size:" in out_info)
    test_results["diskinfo_inspection"] = diskinfo_ok

    # Test 4: df (real statvfs metrics)
    print("\n>>> Running: df")
    out_df = send_cmd(s, "df")
    df_ok = ("Filesystem" in out_df) and ("nyotafs" in out_df) and ("/" in out_df)
    test_results["df_statvfs"] = df_ok

    # Test 5: disktest on disposable device (default safe mode on ata0)
    print("\n>>> Running: disktest")
    out_test = send_cmd(s, "disktest")
    disktest_ok = ("Device discovery" in out_test and "PASS" in out_test) and \
                  ("Sector reads" in out_test and "PASS" in out_test) and \
                  ("Boundary validation" in out_test and "PASS" in out_test) and \
                  ("Result: PASS" in out_test)
    test_results["disktest_safe_suite"] = disktest_ok

    # Test 6: disktest on sda (AHCI SATA)
    print("\n>>> Running: disktest sda")
    out_sda = send_cmd(s, "disktest sda")
    test_results["disktest_sda"] = ("Result: PASS" in out_sda) and \
                                   ("Partition validation   PASS" in out_sda)

    # Test 7: disktest on nvme0 (NVMe)
    print("\n>>> Running: disktest nvme0")
    out_nvme = send_cmd(s, "disktest nvme0")
    test_results["disktest_nvme"] = ("Result: PASS" in out_nvme) and \
                                    ("Partition validation   PASS" in out_nvme)

    # Test 8: Protection test - root disk ata1 cannot be written by disktest
    print("\n>>> Running: disktest ata1 (root disk protection check)")
    out_root = send_cmd(s, "disktest ata1")
    test_results["root_disk_protection"] = ("protected root filesystem disk" in out_root or "FAIL" in out_root)

    # Clean shutdown
    send_cmd(s, "sync")
    time.sleep(1)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()

    print("\n============================================================")
    print("           NYOTA OS STORAGE TEST REPORT")
    print("============================================================")
    all_passed = True
    for name, res in test_results.items():
        status = "PASS" if res else "FAIL"
        if not res: all_passed = False
        print(f"  [ {status} ] {name}")
    print("============================================================")

    if all_passed:
        print("ALL STORAGE & HARDWARE TESTS PASSED SUCCESSFULLY!")
        return True
    else:
        print("SOME STORAGE TESTS FAILED!")
        return False

if __name__ == "__main__":
    success = run_storage_tests()
    sys.exit(0 if success else 1)
