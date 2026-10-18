# Nyota OS AI-Assisted System Diagnostics (Phase 19)

**Target Release:** `v1.9.0`  
**Architecture:** `x86_64`  
**System Services:** `/sbin/diagd`  
**User Utilities:** `/bin/nydiag` (CLI & Desktop GUI)  
**Configuration:** `/etc/diagnostics.conf`  

---

## 1. Overview & Objectives

Nyota OS Phase 19 introduces an optional, privacy-respecting **AI-Assisted System Diagnostics** framework. As operating systems grow in complexity across multicore SMP, network stacks, storage drivers, and virtual memory paging, diagnosing intermittent faults, resource starvation, and unexpected crashes requires structured reasoning.

Phase 19 provides:
1. **Telemetry Collection Service (`/sbin/diagd`)**: A supervised background service that continuously monitors system health, process execution, memory pressure, kernel heap integrity, storage capacity, and network interface status.
2. **Modular AI Engine (`/bin/nydiag`)**: A flexible diagnostic interface supporting **Local Heuristic Reasoning**, **Mock Fault Simulation**, and **External HTTP LLM** providers.
3. **Strict Privacy Safeguards**: Zero transmission of telemetry without explicit consent (`--consent`), automatic redaction of secrets, tokens, API keys, and passwords before external network egress.
4. **Factual Separation & Non-Execution Principle**: Observed telemetry (`[FACTS]`) is strictly distinguished from probabilistic reasoning (`[HYPOTHESIS]`) and advisory remediation (`[RECOMMENDATIONS]`). AI suggestions are strictly advisory and are **never** executed automatically.
5. **Desktop Diagnostics Assistant**: An interactive graphical dashboard integrated directly into the Nyota OS desktop environment.

---

## 2. Architecture & Telemetry Pipeline

```
 +-------------------------------------------------------------+
 |                    User Applications                        |
 +-------------------------------------------------------------+
        |                            |                      |
        | Process Crash              | Memory Violation     | Network Drop
        v                            v                      v
 +-------------------------------------------------------------+
 |                   Ring 0 Kernel Telemetry                   |
 |  - PMM free frames        - VFS storage free blocks         |
 |  - Heap canary checks     - e1000 link state / rx packets   |
 |  - Active process table   - /var/log/kernel.log ring buffer |
 +-------------------------------------------------------------+
        |                            |                      |
        +----------------------------+----------------------+
                                     |
                                     v
                  +--------------------------------------+
                  |   /sbin/diagd (System Daemon)        |
                  |   - Listens on /run/diag.sock        |
                  |   - Supervises telemetry gathering   |
                  +--------------------------------------+
                                     |
                    +----------------+----------------+
                    |                                 |
                    v                                 v
        +-----------------------+         +-----------------------+
        | /bin/nydiag (CLI)     |         | /bin/nydiag --gui     |
        | - summary / health    |         | - Desktop Dashboard   |
        | - analyze / crash     |         | - Real-time metrics   |
        | - redact / report     |         | - Color-coded badge   |
        +-----------------------+         +-----------------------+
                    |                                 |
                    +----------------+----------------+
                                     |
                                     v
                      +-----------------------------+
                      | Privacy & Redaction Filter  |
                      | - Redacts API keys & tokens |
                      | - Strips passwords & auth   |
                      +-----------------------------+
                                     |
                       +-------------+-------------+
                       |                           |
                       v                           v
         [Consent: Local Engine]     [Consent: Authorized]
         - Deterministic rules       - HTTP POST to endpoint
         - 0 bytes sent outside      - Strict 5000ms timeout
         - Guaranteed offline        - Graceful fallback
```

---

## 3. Telemetry Collection

Diagnostic collection pools system information from existing Ring 0 and Ring 3 facilities without introducing overhead:

| Metric Source | Telemetry Gathered | Health Criteria |
| :--- | :--- | :--- |
| **Physical Memory (PMM)** | Total frames, free frames, usage percentage | Warning if used > 85% or free < 2000 frames |
| **Kernel Heap Allocator** | Used bytes, total bytes, block canary status | **Critical** if any corrupted canary blocks detected |
| **Root Filesystem (NyotaFS)** | Total blocks, free blocks, allocation % | Warning if free space < 10% |
| **Process Scheduler** | Active process count, zombie count | Warning if zombie count > 5 |
| **Crash & Fault Detector** | Recent crash PID, faulting address, signal | **Critical** if user space process crashed via SIGSEGV |
| **Network Interface (e1000)** | Link UP/DOWN, RX/TX packet counters | Warning if eth0 link is offline |
| **Security Subsystem** | Audit denied count, last denied security event | Warning if repeated unauthorized access recorded |

---

## 4. Privacy & Secret Redaction

Nyota OS implements a mandatory redaction filter applied to all telemetry buffers prior to external transmission or log export:

* **API Keys & Bearer Tokens**: Replaces strings matching `bearer sk-...` or `api_key=...` with `[REDACTED_SECRET]`.
* **Password Fields**: Strips parameters matching `password=...`, `passwd=...`, or `secret=...` with `[REDACTED_SECRET]`.
* **Private Key Material**: Detects and redacts standard PEM block headers (`-----BEGIN ... PRIVATE KEY-----`).
* **Consent Gate**: If external AI analysis is invoked (`--ai`) without explicit user authorization (`--consent`), transmission is aborted immediately and local analysis executes instead.

---

## 5. Diagnostic Reporting Standard

Every diagnostic report generated by `nydiag` strictly adheres to a standardized tripartite format:

1. **`[FACTS]` (Observed Ground Truth)**
   Verified telemetry gathered directly from kernel system calls, process tables, and memory counters.
2. **`[HYPOTHESIS]` (Probabilistic Diagnostic Reasoning)**
   Causal explanations and probable root causes derived from the facts (e.g. NULL pointer dereference, memory exhaustion, network gateway timeout).
3. **`[RECOMMENDATIONS]` (Actionable Remediation Advice)**
   Concrete, non-destructive troubleshooting steps (e.g., inspecting logs with `/bin/cat /var/log/kernel.log`, running heap diagnostics with `/bin/memdiag -v`, or profiling processes with `/bin/nyprof`).
4. **`[NOTICE]` (Safety Guarantee)**
   Explicitly declares:
   > *"Recommendations are purely advisory and never executed automatically. All administrative remediation actions require manual verification."*

---

## 6. Utilities & Command Reference

### `/bin/nydiag` Commands

```bash
# Print high-level health status badge (HEALTHY, WARNING, CRITICAL)
nydiag health

# Display comprehensive system metrics summary
nydiag summary

# Execute local heuristic diagnostic reasoning
nydiag analyze --local

# Analyze recent process crash or faulting memory address
nydiag crash

# Test secret and credential redaction filter
nydiag redact "auth=bearer sk-secret12345 password=SuperSecret"

# Query current diagnostics and provider configuration
nydiag config

# Export diagnostic report to file
nydiag report -o /tmp/report.txt

# Simulate failure scenarios for validation
nydiag analyze --mock memory
nydiag analyze --mock crash
nydiag analyze --mock network

# Launch interactive desktop graphical interface
nydiag --gui
```

---

## 7. Configuration (`/etc/diagnostics.conf`)

```ini
[diagnostics]
enabled=true
provider=local           # local | http | mock
endpoint=http://10.0.2.2:11434/api/generate
model=nyota-diag
timeout_ms=5000
require_consent=true
redact_secrets=true
```
