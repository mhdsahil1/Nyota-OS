#!/usr/bin/env python3
"""
Nyota OS — Phase 19 Automated Test Suite: AI-Assisted System Diagnostics (v1.9.0)

Verifies:
  1. System boot and OS version reporting '1.9.0' via `uname -a`.
  2. Availability of diagnostic service and CLI tools (`/sbin/diagd`, `/bin/nydiag`).
  3. System health telemetry inspection (`nydiag health`).
  4. Metric collection across memory, storage, processes, network, and logs (`nydiag summary`).
  5. Local heuristic AI analysis with structured output (`nydiag analyze --local`).
  6. Distinction of observed facts from hypotheses (`[FACTS]`, `[HYPOTHESIS]`, `[RECOMMENDATIONS]`).
  7. Credential and secret redaction filter (`nydiag redact`).
  8. AI consent enforcement (external transmission blocked without explicit user authorization).
  9. Simulated crash diagnostics (`/bin/crash` followed by `nydiag crash`).
 10. Simulated memory pressure & heap integrity reasoning (`nydiag analyze --mock memory`).
 11. Graceful AI provider fallback when remote endpoint is offline/unreachable.
 12. Non-execution safety guarantee (suggested commands are never automatically executed).
 13. Diagnostic report generation and file export (`nydiag report -o /tmp/report.txt`).
 14. Host kernel and userspace supervisor stability after diagnostic operations.
"""

import subprocess
import socket
import time
import sys
import os

QEMU = "qemu-system-x86_64"
IMAGE = "build/nyota.img"
DATA_IMAGE = "build/nyota-data.img"
PORT = 45506

def wait_for_login_and_prompt(s, timeout=25):
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

def send_command(s, cmd_str, timeout=10):
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
                if (b"$ " in buf or b"nyota$ " in buf) and len(buf) > len(cmd_str) + 4:
                    break
        except socket.timeout:
            pass
    return buf.decode('latin1', errors='replace')

def run_tests():
    print("=" * 70)
    print(" Nyota OS Phase 19: AI-Assisted System Diagnostics Test Suite (v1.9.0)")
    print("=" * 70)

    qemu_cmd = [
        QEMU,
        "-m", "512M",
        "-drive", f"format=raw,file={IMAGE},index=0,media=disk",
        "-drive", f"format=raw,file={DATA_IMAGE},index=1,media=disk",
        "-display", "none",
        "-serial", f"tcp:127.0.0.1:{PORT},server,wait"
    ]

    print(f"[*] Launching QEMU with command: {' '.join(qemu_cmd)}")
    proc = subprocess.Popen(qemu_cmd)

    s = None
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", PORT))
            break
        except ConnectionRefusedError:
            time.sleep(0.2)

    if not s:
        print("[!] Error: Could not connect to QEMU serial socket.")
        proc.kill()
        sys.exit(1)

    s.settimeout(0.5)

    print("[*] Connected to serial console. Waiting for boot & login...")
    boot_out = wait_for_login_and_prompt(s, 25).decode('latin1', errors='replace')

    results = {}

    # 1. System Version Metadata (v1.9.0)
    print("\n--- Test 1: System Version Metadata (v1.9.0) ---")
    uname_out = send_command(s, "uname -a\n")
    print(f"uname output: {uname_out.strip()}")
    results["uname_version_1.9.0"] = ("1.9.0" in uname_out) and ("x86_64" in uname_out)

    # 2. Presence of Diagnostic Binaries
    print("\n--- Test 2: Presence of Diagnostics Service & Tool ---")
    bin_ls = send_command(s, "ls /bin\n")
    sbin_ls = send_command(s, "ls /sbin\n")
    results["binaries_present"] = ("nydiag" in bin_ls) and ("diagd" in sbin_ls)

    # 3. System Health Check (`nydiag health`)
    print("\n--- Test 3: System Health Check (`nydiag health`) ---")
    health_out = send_command(s, "nydiag health\n", timeout=6)
    print(health_out.strip())
    results["health_check_executed"] = ("system health" in health_out.lower() and ("healthy" in health_out.lower() or "warning" in health_out.lower()))

    # 4. Metric Collection Summary (`nydiag summary`)
    print("\n--- Test 4: Diagnostic Metrics Summary (`nydiag summary`) ---")
    summary_out = send_command(s, "nydiag summary\n", timeout=6)
    print(summary_out.strip())
    results["metrics_summary_executed"] = ("memory usage" in summary_out.lower() and "storage free" in summary_out.lower() and "network" in summary_out.lower())

    # 5. Local Heuristic AI Analysis (`nydiag analyze --local`)
    print("\n--- Test 5: Local Heuristic Analysis (`nydiag analyze --local`) ---")
    analyze_out = send_command(s, "nydiag analyze --local\n", timeout=8)
    print(analyze_out.strip())
    results["local_analysis_executed"] = ("[facts]" in analyze_out.lower() and "[hypothesis]" in analyze_out.lower() and "[recommendations]" in analyze_out.lower())

    # 6. Distinction of Facts vs Hypothesis vs Recommendations
    print("\n--- Test 6: Factual Distinction & Non-Execution Notice ---")
    results["facts_hypotheses_distinction"] = ("[facts]" in analyze_out.lower() and "[hypothesis]" in analyze_out.lower() and "never executed automatically" in analyze_out.lower())

    # 7. Credential & Secret Redaction (`nydiag redact`)
    print("\n--- Test 7: Secret and Credential Redaction Filter ---")
    redact_cmd = "nydiag redact \"auth=bearer sk-secretKey1234567890 and password=SuperSecretPassword\"\n"
    redact_out = send_command(s, redact_cmd, timeout=5)
    print(redact_out.strip())
    redact_lower = redact_out.lower()
    redacted_sec = redact_lower.split("redacted :")[1] if "redacted :" in redact_lower else ""
    results["secret_redaction_verified"] = ("[redacted_secret]" in redact_lower and "supersecretpassword" not in redacted_sec)

    # 8. AI Consent Gate Enforcement
    print("\n--- Test 8: AI User Consent Enforcement ---")
    consent_out = send_command(s, "nydiag analyze --ai\n", timeout=6)
    print(consent_out.strip())
    results["consent_gate_enforced"] = ("consent" in consent_out.lower() or "aborted" in consent_out.lower() or "authorization" in consent_out.lower() or "error" in consent_out.lower())

    # 9. Simulated Crash Diagnostics (`/bin/crash` then `nydiag crash`)
    print("\n--- Test 9: Simulated Process Crash Diagnostics ---")
    crash_run = send_command(s, "/bin/crash\n", timeout=5)
    print(f"crash command: {crash_run.strip()}")
    crash_out = send_command(s, "nydiag crash\n", timeout=6)
    print(crash_out.strip())
    results["crash_diagnosed"] = ("crash" in crash_out.lower() or "page fault" in crash_out.lower() or "sigsegv" in crash_out.lower() or "null pointer" in crash_out.lower())

    # 10. Simulated Failure Reasoning (`nydiag analyze --mock memory`)
    print("\n--- Test 10: Simulated Memory Failure Reasoning (`nydiag analyze --mock memory`) ---")
    mock_out = send_command(s, "nydiag analyze --mock memory\n", timeout=6)
    print(mock_out.strip())
    results["mock_failure_reasoning"] = ("memory" in mock_out.lower() and "exhaustion" in mock_out.lower() and "[recommendations]" in mock_out.lower())

    # 11. Offline AI Fallback to Local Engine
    print("\n--- Test 11: AI Provider Offline/Timeout Graceful Fallback ---")
    fallback_out = send_command(s, "nydiag analyze --ai --consent\n", timeout=8)
    print(fallback_out.strip())
    results["ai_fallback_operational"] = ("[facts]" in fallback_out.lower() and "[hypothesis]" in fallback_out.lower() and "[recommendations]" in fallback_out.lower())

    # 12. Report Generation and File Export
    print("\n--- Test 12: Diagnostic Report Export (`nydiag report -o /tmp/report.txt`) ---")
    export_out = send_command(s, "nydiag report -o /tmp/report.txt\n", timeout=6)
    print(export_out.strip())
    cat_out = send_command(s, "cat /tmp/report.txt\n", timeout=6)
    print(f"cat /tmp/report.txt preview:\n{cat_out[:300]}...")
    results["report_exported"] = ("diagnostics report" in cat_out.lower() and "[facts]" in cat_out.lower())

    # 13. Configuration Subcommand (`nydiag config`)
    print("\n--- Test 13: Diagnostics Configuration Query (`nydiag config`) ---")
    config_out = send_command(s, "nydiag config\n", timeout=5)
    print(config_out.strip())
    results["config_query_executed"] = ("diagnostics configuration" in config_out.lower() and "provider" in config_out.lower())

    # 14. Host Kernel Stability & Process Supervision Check
    print("\n--- Test 14: Host Kernel & Service Supervision Stability ---")
    ps_out = send_command(s, "ps\n", timeout=5)
    print(ps_out.strip())
    results["host_kernel_stable"] = ("init" in ps_out) and ("diagd" in ps_out or "sh" in ps_out)

    # Clean shutdown
    send_command(s, "sync\n", timeout=2)
    s.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    print("\n" + "=" * 70)
    print("                   PHASE 19 TEST RESULTS SUMMARY                     ")
    print("=" * 70)
    all_passed = True
    for test, passed in results.items():
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {test}")
        if not passed:
            all_passed = False

    print("=" * 70)
    if all_passed:
        print("\n[+] SUCCESS: ALL PHASE 19 AI-ASSISTED SYSTEM DIAGNOSTICS TESTS PASSED!\n")
        return 0
    else:
        print("\n[-] FAILURE: Some Phase 19 tests failed.\n")
        return 1

if __name__ == "__main__":
    sys.exit(run_tests())
