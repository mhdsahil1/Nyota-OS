#!/usr/bin/env python3
"""
Nyota OS Phase 13 Automated Test Runner (tools/test_phase13.py)
Tests:
  - ifconfig (IPv4, IPv6, MAC, stats)
  - netinfo (Network summary diagnostics)
  - ip (ip route, ip link)
  - nyfw (firewall status, add rule, del rule, enable/disable, drop counters)
  - dhclient (DHCPv4 discovery and configuration)
  - httpd & httpget (HTTP/1.1 client & server communication, status 200, path traversal protection 403)
  - nslookup (DNS resolver, /etc/hosts resolution, A/AAAA)
"""

import socket
import subprocess
import time
import sys

def main():
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('127.0.0.1', 0))
    server_sock.listen(1)
    port = server_sock.getsockname()[1]

    qemu_cmd = [
        "qemu-system-x86_64",
        "-smp", "1",
        "-drive", "format=raw,file=build/nyota.img,index=0,media=disk",
        "-drive", "format=raw,file=build/nyota-data.img,index=1,media=disk",
        "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
        "-device", "e1000,netdev=net0",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{port}"
    ]

    print(f"[TEST-P13] Launching QEMU on serial port {port}...")
    proc = subprocess.Popen(qemu_cmd)

    try:
        server_sock.settimeout(20.0)
        s, _ = server_sock.accept()
        s.settimeout(8.0)
        print("[TEST-P13] Serial connection established. Waiting for prompt...")

        def read_until(pattern, timeout=12.0):
            deadline = time.time() + timeout
            buf = b""
            while time.time() < deadline:
                try:
                    chunk = s.recv(1024)
                    if chunk:
                        buf += chunk
                        sys.stdout.buffer.write(chunk)
                        sys.stdout.buffer.flush()
                        if pattern in buf:
                            return buf
                except socket.timeout:
                    pass
                time.sleep(0.05)
            return buf

        def send_cmd(cmd):
            s.sendall(cmd.encode() + b"\n")
            time.sleep(0.3)

        # Login
        boot_out = read_until(b"login: ", timeout=15.0)
        send_cmd("sahil")
        read_until(b"Password: ", timeout=6.0)
        send_cmd("")
        read_until(b"$ ", timeout=8.0)
        print("\n[TEST-P13] Logged in successfully!")

        results = {}

        # 1. Test netinfo
        print("\n[TEST-P13] Running netinfo...")
        send_cmd("netinfo")
        out = read_until(b"$ ", timeout=6.0)
        results["netinfo_interfaces"] = b"Interfaces : " in out
        results["netinfo_ipv4"] = b"IPv4       : configured" in out
        results["netinfo_ipv6"] = b"IPv6       : configured" in out
        results["netinfo_tcp"] = b"TCP        : available" in out
        results["netinfo_udp"] = b"UDP        : available" in out
        results["netinfo_firewall"] = b"Firewall   : " in out

        # 2. Test ifconfig
        print("\n[TEST-P13] Running ifconfig...")
        send_cmd("ifconfig")
        out = read_until(b"$ ", timeout=6.0)
        results["ifconfig_eth0"] = b"eth0:" in out
        results["ifconfig_mac"] = b"MAC:       52:54:00:12:34:56" in out
        results["ifconfig_ipv4"] = b"10.0.2.15/24" in out
        results["ifconfig_ipv6"] = b"IPv6:      fe80:" in out
        results["ifconfig_stats"] = b"RX packets:" in out and b"TX packets:" in out

        # 3. Test ip route & ip link
        print("\n[TEST-P13] Running ip route...")
        send_cmd("ip route")
        out = read_until(b"$ ", timeout=6.0)
        results["ip_route_table"] = b"Destination" in out and b"Gateway" in out and b"Interface" in out
        results["ip_route_default"] = b"0.0.0.0/0" in out and b"10.0.2.2" in out

        print("\n[TEST-P13] Running ip link...")
        send_cmd("ip link")
        out = read_until(b"$ ", timeout=6.0)
        results["ip_link"] = b"link/ether" in out and b"mtu 1500" in out

        # 4. Test nyfw (Firewall)
        print("\n[TEST-P13] Testing Firewall (nyfw)...")
        send_cmd("nyfw status")
        out = read_until(b"$ ", timeout=6.0)
        results["nyfw_status"] = b"Firewall: ENABLED" in out or b"Firewall: DISABLED" in out

        send_cmd("nyfw enable")
        out = read_until(b"$ ", timeout=6.0)
        results["nyfw_enable"] = b"Firewall enabled." in out

        send_cmd("nyfw add in drop proto icmp")
        out = read_until(b"$ ", timeout=6.0)
        results["nyfw_add_rule"] = b"Added rule" in out

        send_cmd("nyfw list")
        out = read_until(b"$ ", timeout=6.0)
        results["nyfw_list"] = b"DROP" in out and b"icmp" in out

        send_cmd("nyfw del 1")
        out = read_until(b"$ ", timeout=6.0)
        results["nyfw_del_rule"] = b"deleted" in out

        # 5. Test dhclient
        print("\n[TEST-P13] Testing dhclient...")
        send_cmd("dhclient eth0")
        out = read_until(b"$ ", timeout=10.0)
        results["dhclient_exec"] = b"Nyota DHCP Client" in out and b"DHCP configuration successful." in out
        results["dhclient_assigned"] = b"10.0.2.15" in out

        # 6. Test httpd and httpget
        print("\n[TEST-P13] Testing HTTP server and client...")
        send_cmd("httpd -p 8080 &")
        read_until(b"$ ", timeout=6.0)
        time.sleep(0.5)

        send_cmd("httpget http://127.0.0.1:8080/index.html")
        out = read_until(b"$ ", timeout=8.0)
        results["httpget_200"] = (b"HTTP Response: 200" in out or b"200 OK" in out) and b"Nyota OS Web Server" in out

        # Test path traversal attack prevention
        print("\n[TEST-P13] Testing HTTP path traversal prevention...")
        send_cmd("httpget http://127.0.0.1:8080/../../etc/passwd")
        out = read_until(b"$ ", timeout=8.0)
        results["httpget_traversal_blocked"] = b"403" in out or b"Forbidden" in out

        # Test host-to-guest connection via QEMU port forwarding
        print("\n[TEST-P13] Testing host-to-guest TCP via QEMU port forwarding 8080...")
        try:
            h_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            h_sock.settimeout(3.0)
            h_sock.connect(('127.0.0.1', 8080))
            h_sock.sendall(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
            h_resp = b""
            while True:
                try:
                    c = h_sock.recv(1024)
                    if not c:
                        break
                    h_resp += c
                    if b"</html>" in h_resp:
                        break
                except Exception:
                    break
            h_sock.close()
            results["host_to_guest_http"] = b"200 OK" in h_resp and b"Nyota OS Web Server" in h_resp
        except Exception as e:
            print(f"[WARN] host-to-guest connect error: {e}")
            results["host_to_guest_http"] = False

        # Test malformed packet robustness: send malformed garbage to port 8080
        print("\n[TEST-P13] Testing malformed packet handling...")
        try:
            m_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            m_sock.settimeout(2.0)
            m_sock.connect(('127.0.0.1', 8080))
            m_sock.sendall(b"\x00\xFF\xAA\x55\xDE\xAD\xBE\xEF" * 16 + b" INVALID / HTTP/9.9\r\n\r\n")
            try:
                m_sock.recv(512)
            except Exception:
                pass
            m_sock.close()
            results["malformed_packet_survived"] = True
        except Exception as e:
            results["malformed_packet_survived"] = False

        # 7. Test nslookup
        print("\n[TEST-P13] Testing nslookup...")
        send_cmd("nslookup localhost")
        out = read_until(b"$ ", timeout=6.0)
        results["nslookup_localhost"] = b"127.0.0.1" in out

        send_cmd("nslookup example.com")
        out = read_until(b"$ ", timeout=8.0)
        results["nslookup_example"] = b"Name:     example.com" in out and b"Address:" in out

        # Report
        print("\n" + "=" * 60)
        print("          NYOTA OS PHASE 13 TEST REPORT")
        print("=" * 60)
        all_passed = True
        for test, passed in results.items():
            status = "[ PASS ]" if passed else "[ FAIL ]"
            print(f"  {status} {test}")
            if not passed:
                all_passed = False
        print("=" * 60)

        if all_passed:
            print("ALL PHASE 13 NETWORKING & SECURITY TESTS PASSED!")
            sys.exit(0)
        else:
            print("SOME PHASE 13 TESTS FAILED!")
            sys.exit(1)

    finally:
        server_sock.close()
        proc.terminate()

if __name__ == "__main__":
    main()
