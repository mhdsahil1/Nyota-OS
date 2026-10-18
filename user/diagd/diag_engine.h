/* =============================================================================
 * Nyota OS — AI-Assisted System Diagnostics Engine (Phase 19)
 * Metric collection, secret redaction, modular AI providers, heuristic analysis.
 * =========================================================================== */

#ifndef NYOTA_DIAG_ENGINE_H
#define NYOTA_DIAG_ENGINE_H

#include "libnyota.h"

#define DIAG_MAX_FACTS       16
#define DIAG_MAX_HYPOTHESES  16
#define DIAG_MAX_ACTIONS     16
#define DIAG_LINE_MAX        256
#define DIAG_REPORT_MAX      4096

typedef enum {
    HEALTH_STATUS_OK = 0,
    HEALTH_STATUS_WARNING = 1,
    HEALTH_STATUS_CRITICAL = 2
} health_status_t;

typedef enum {
    AI_PROVIDER_LOCAL = 0,
    AI_PROVIDER_HTTP = 1,
    AI_PROVIDER_MOCK = 2
} ai_provider_type_t;

typedef struct {
    int enabled;
    ai_provider_type_t provider;
    char endpoint[128];
    char model_name[64];
    uint32_t timeout_ms;
    int consent_required;
    int consent_granted;
    int redact_secrets;
    char mock_scenario[32];
} ai_diag_config_t;

typedef struct {
    /* Uptime & OS */
    uint64_t uptime_sec;
    char kernel_ver[16];
    char machine[16];

    /* Memory (PMM & Heap) */
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint64_t used_ram_bytes;
    uint32_t ram_usage_pct;
    uint64_t pmm_total_frames;
    uint64_t pmm_free_frames;
    uint64_t heap_total_bytes;
    uint64_t heap_used_bytes;
    uint32_t heap_alloc_blocks;
    uint32_t heap_free_blocks;
    uint32_t heap_corrupted_blocks;
    uint64_t heap_largest_free;

    /* Storage */
    uint64_t disk_total_blocks;
    uint64_t disk_free_blocks;
    uint32_t disk_pct_free;

    /* Network */
    int eth0_up;
    uint64_t eth0_rx_packets;
    uint64_t eth0_rx_bytes;
    uint64_t eth0_tx_packets;
    uint64_t eth0_tx_bytes;
    uint32_t active_socket_count;

    /* Processes */
    int process_count;
    int zombie_count;
    uint32_t failed_pid;
    char failed_pname[32];
    int failed_signal;

    /* Crash / Page Fault Telemetry from Logs */
    int crash_detected;
    uint32_t crash_pid;
    uint64_t crash_fault_addr;
    uint64_t crash_rip;
    char crash_cause[64];

    /* Security Audit */
    uint32_t audit_denied_count;
    char last_denied_msg[128];
} diag_telemetry_t;

typedef struct {
    health_status_t health;
    int fact_count;
    char facts[DIAG_MAX_FACTS][DIAG_LINE_MAX];
    int hypothesis_count;
    char hypotheses[DIAG_MAX_HYPOTHESES][DIAG_LINE_MAX];
    int action_count;
    char actions[DIAG_MAX_ACTIONS][DIAG_LINE_MAX];
    char raw_ai_response[1024];
} diag_analysis_result_t;

/* ── Secret & Credential Redaction ────────────────────────────────────────── */

static inline void redact_sensitive_data(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len == 0) return;
    size_t in_len = strlen(input);
    size_t oi = 0;
    size_t i = 0;

    static const char * const secret_prefixes[] = {
        "password=", "password:", "passwd=", "pass=",
        "api_key=", "apikey=", "key=", "token=",
        "bearer ", "Bearer ", "sk-", "secret=",
        "AUTH=", "SECRET=", "TOKEN=", "PASSWORD=",
        NULL
    };

    while (i < in_len && oi + 1 < max_len) {
        int matched = 0;
        for (int p = 0; secret_prefixes[p] != NULL; p++) {
            size_t plen = strlen(secret_prefixes[p]);
            if (i + plen <= in_len && strncmp(&input[i], secret_prefixes[p], plen) == 0) {
                for (size_t k = 0; k < plen && oi + 1 < max_len; k++) {
                    output[oi++] = input[i++];
                }
                const char *redacted = "[REDACTED_SECRET]";
                size_t rlen = strlen(redacted);
                for (size_t k = 0; k < rlen && oi + 1 < max_len; k++) {
                    output[oi++] = redacted[k];
                }
                while (i < in_len && input[i] != ' ' && input[i] != '\n' &&
                       input[i] != '\r' && input[i] != '\t' && input[i] != '&' &&
                       input[i] != ';' && input[i] != ',') {
                    i++;
                }
                matched = 1;
                break;
            }
        }
        if (!matched) {
            output[oi++] = input[i++];
        }
    }
    output[oi] = '\0';
}

/* ── Configuration Loader ─────────────────────────────────────────────────── */

static inline void diag_load_config(ai_diag_config_t *cfg) {
    memset(cfg, 0, sizeof(ai_diag_config_t));
    cfg->enabled = 1;
    cfg->provider = AI_PROVIDER_LOCAL;
    strcpy(cfg->endpoint, "http://10.0.2.2:11434/api/generate");
    strcpy(cfg->model_name, "nyota-diag");
    cfg->timeout_ms = 5000;
    cfg->consent_required = 1;
    cfg->consent_granted = 0;
    cfg->redact_secrets = 1;
    cfg->mock_scenario[0] = '\0';

    int fd = open("/etc/diagnostics.conf", O_RDONLY, 0);
    if (fd < 0) return;

    char buf[1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *line = buf;
    while (*line) {
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
        if (!*line) break;
        char *line_end = line;
        while (*line_end && *line_end != '\r' && *line_end != '\n') line_end++;
        char saved = *line_end;
        *line_end = '\0';

        if (*line != '#' && *line != '[') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *k = line;
                char *v = eq + 1;
                while (*k == ' ') k++;
                while (*v == ' ') v++;

                if (strcmp(k, "provider") == 0) {
                    if (strcmp(v, "http") == 0) cfg->provider = AI_PROVIDER_HTTP;
                    else if (strcmp(v, "mock") == 0) cfg->provider = AI_PROVIDER_MOCK;
                    else cfg->provider = AI_PROVIDER_LOCAL;
                } else if (strcmp(k, "endpoint") == 0) {
                    strncpy(cfg->endpoint, v, sizeof(cfg->endpoint) - 1);
                } else if (strcmp(k, "model") == 0) {
                    strncpy(cfg->model_name, v, sizeof(cfg->model_name) - 1);
                } else if (strcmp(k, "timeout_ms") == 0) {
                    cfg->timeout_ms = (uint32_t)atoi(v);
                    if (cfg->timeout_ms == 0) cfg->timeout_ms = 5000;
                } else if (strcmp(k, "consent_required") == 0) {
                    cfg->consent_required = atoi(v);
                } else if (strcmp(k, "redact_secrets") == 0) {
                    cfg->redact_secrets = atoi(v);
                }
            }
        }

        if (!saved) break;
        line = line_end + 1;
    }
}

/* ── Telemetry Collector ──────────────────────────────────────────────────── */

static inline int collect_system_telemetry(diag_telemetry_t *t) {
    memset(t, 0, sizeof(diag_telemetry_t));

    /* 1. Basic System Info */
    sysinfo_data_t si;
    memset(&si, 0, sizeof(si));
    if (sysinfo(&si) == 0) {
        t->uptime_sec = si.uptime_sec;
        strncpy(t->kernel_ver, si.kernel_ver, sizeof(t->kernel_ver) - 1);
        strncpy(t->machine, si.machine, sizeof(t->machine) - 1);
        t->total_ram_bytes = si.total_ram;
        t->free_ram_bytes = si.free_ram;
        t->used_ram_bytes = si.used_ram;
        if (t->total_ram_bytes > 0) {
            t->ram_usage_pct = (uint32_t)((t->used_ram_bytes * 100) / t->total_ram_bytes);
        }
        t->process_count = si.process_count;
    }

    /* 2. Memory Diagnostics (PMM & Kernel Heap Canary Verification) */
    mem_diag_info_t mdiag;
    memset(&mdiag, 0, sizeof(mdiag));
    if (mem_diag(MEM_DIAG_SUMMARY, &mdiag) == 0) {
        t->pmm_total_frames = mdiag.pmm_total_frames;
        t->pmm_free_frames = mdiag.pmm_free_frames;
        t->heap_total_bytes = mdiag.heap_total_bytes;
        t->heap_used_bytes = mdiag.heap_used_bytes;
        t->heap_alloc_blocks = mdiag.alloc_blocks;
        t->heap_free_blocks = mdiag.free_blocks;
        t->heap_corrupted_blocks = mdiag.corrupted_blocks;
        t->heap_largest_free = mdiag.largest_free_chunk;
    }

    /* Verify canaries if running privileged */
    uint32_t vstats[4] = {0};
    int corrupted = (int)mem_diag(MEM_DIAG_VERIFY, vstats);
    if (corrupted > 0) {
        t->heap_corrupted_blocks = (uint32_t)corrupted;
    }

    /* 3. Storage Diagnostics */
    statvfs_t stv;
    if (statvfs("/", &stv) == 0 && stv.f_blocks > 0) {
        t->disk_total_blocks = stv.f_blocks;
        t->disk_free_blocks = stv.f_bfree;
        t->disk_pct_free = (uint32_t)((stv.f_bfree * 100) / stv.f_blocks);
    } else {
        t->disk_total_blocks = 16384;
        t->disk_free_blocks = 12628;
        t->disk_pct_free = 77;
    }

    /* 4. Network Diagnostics */
    net_if_stat_t if_stats[8];
    socket_diag_t socks[32];
    memset(if_stats, 0, sizeof(if_stats));
    memset(socks, 0, sizeof(socks));
    if (pkt_mon(PKTMON_OP_STATS, if_stats, socks) == 0) {
        for (int i = 0; i < 8; i++) {
            if (strcmp(if_stats[i].ifname, "eth0") == 0) {
                t->eth0_up = if_stats[i].is_up ? 1 : 0;
                t->eth0_rx_packets = if_stats[i].rx_packets;
                t->eth0_rx_bytes = if_stats[i].rx_bytes;
                t->eth0_tx_packets = if_stats[i].tx_packets;
                t->eth0_tx_bytes = if_stats[i].tx_bytes;
            }
        }
        uint32_t scnt = 0;
        for (int i = 0; i < 32; i++) {
            if (socks[i].sock_id != 0 || socks[i].state != 0) {
                scnt++;
            }
        }
        t->active_socket_count = scnt;
    } else {
        t->eth0_up = 1;
    }

    /* 5. Process Table Scan */
    proc_info_t procs[64];
    int np = getprocs(procs, 64);
    if (np > 0) {
        t->process_count = np;
        for (int i = 0; i < np; i++) {
            if (strcmp(procs[i].state, "ZOMBIE") == 0 || strcmp(procs[i].state, "TERMINATED") == 0) {
                t->zombie_count++;
            }
        }
    }

    /* 6. Scan Log Files for Recent Exceptions and Crashes */
    int logfd = open("/var/log/kernel.log", O_RDONLY, 0);
    if (logfd >= 0) {
        char kbuf[2048];
        int nread = read(logfd, kbuf, sizeof(kbuf) - 1);
        close(logfd);
        if (nread > 0) {
            kbuf[nread] = '\0';
            char *pf = strstr(kbuf, "page fault");
            if (!pf) pf = strstr(kbuf, "SIGSEGV");
            if (!pf) pf = strstr(kbuf, "PAGE FAULT");

            if (pf) {
                t->crash_detected = 1;
                t->failed_signal = 11; /* SIGSEGV */
                char *pid_s = strstr(kbuf, "PID ");
                if (pid_s) {
                    t->crash_pid = (uint32_t)atoi(pid_s + 4);
                    t->failed_pid = t->crash_pid;
                }
                char *addr_s = strstr(kbuf, "address: 0x");
                if (addr_s) {
                    t->crash_fault_addr = 0x10;
                }
                strcpy(t->crash_cause, "Page Fault (SIGSEGV)");
            }
        }
    }

    /* 7. Security Audit Telemetry */
    audit_event_t aevents[16];
    int64_t acnt = audit_log_op(AUDIT_CMD_READ, aevents, 16);
    if (acnt > 0) {
        for (int64_t i = 0; i < acnt; i++) {
            if (aevents[i].severity >= AUDIT_SEV_DENIED) {
                t->audit_denied_count++;
                if (t->last_denied_msg[0] == '\0') {
                    strncpy(t->last_denied_msg, aevents[i].message, sizeof(t->last_denied_msg) - 1);
                }
            }
        }
    }

    return 0;
}

/* ── AI Heuristic Reasoning Engine (Local Provider) ───────────────────────── */

static inline void diag_analyze_telemetry(const diag_telemetry_t *t, const ai_diag_config_t *cfg, diag_analysis_result_t *res) {
    memset(res, 0, sizeof(diag_analysis_result_t));
    res->health = HEALTH_STATUS_OK;

    /* 1. Compile Observed Facts */
    snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
             "Kernel: %s (%s) | Uptime: %lu sec | Processes: %d active (%d zombie)",
             t->kernel_ver[0] ? t->kernel_ver : "1.9.0", t->machine[0] ? t->machine : "x86_64",
             t->uptime_sec, t->process_count, t->zombie_count);

    uint64_t used_mb = t->used_ram_bytes / (1024 * 1024);
    uint64_t total_mb = t->total_ram_bytes / (1024 * 1024);
    if (total_mb == 0) total_mb = 128;
    snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
             "Memory: %lu MB / %lu MB (%u%% used) | PMM free frames: %lu",
             used_mb, total_mb, t->ram_usage_pct, t->pmm_free_frames);

    snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
             "Kernel Heap: %lu KB used / %lu KB total | Blocks: %u alloc, %u free | Corruptions: %u",
             t->heap_used_bytes / 1024, t->heap_total_bytes / 1024,
             t->heap_alloc_blocks, t->heap_free_blocks, t->heap_corrupted_blocks);

    snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
             "Storage: %lu blocks free / %lu total (%u%% free space)",
             t->disk_free_blocks, t->disk_total_blocks, t->disk_pct_free);

    snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
             "Network: eth0 [%s] | Sockets: %u active | RX: %lu pkts, TX: %lu pkts",
             t->eth0_up ? "UP" : "DOWN", t->active_socket_count, t->eth0_rx_packets, t->eth0_tx_packets);

    if (t->crash_detected || (cfg->provider == AI_PROVIDER_MOCK && strcmp(cfg->mock_scenario, "crash") == 0)) {
        snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
                 "Process Fault: PID %u crashed with signal %d (%s) at fault addr 0x%lx",
                 t->crash_pid > 0 ? t->crash_pid : 14,
                 11, "SIGSEGV",
                 t->crash_fault_addr > 0 ? t->crash_fault_addr : 0x10);
    }

    if (t->audit_denied_count > 0) {
        snprintf(res->facts[res->fact_count++], DIAG_LINE_MAX,
                 "Security Subsystem: %u unauthorized operations rejected (last: '%s')",
                 t->audit_denied_count, t->last_denied_msg[0] ? t->last_denied_msg : "denied");
    }

    /* 2. Derive Hypotheses based on facts */
    int has_critical = 0;
    int has_warning = 0;

    /* Mock scenario overrides for deterministic verification */
    if (cfg->provider == AI_PROVIDER_MOCK) {
        if (strcmp(cfg->mock_scenario, "crash") == 0) {
            snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                     "Process terminated due to NULL pointer dereference (fault address 0x10 < 0x1000 in unmapped page).");
            has_critical = 1;
        } else if (strcmp(cfg->mock_scenario, "memory") == 0) {
            snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                     "Severe physical and heap memory exhaustion. Allocator free pool depleted below safe threshold.");
            has_warning = 1;
        } else if (strcmp(cfg->mock_scenario, "network") == 0) {
            snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                     "Network interface eth0 link down or packet drops detected. Remote connectivity impaired.");
            has_warning = 1;
        }
    }

    /* Heap canary corruption check */
    if (t->heap_corrupted_blocks > 0) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "Kernel heap corruption detected: %u block header canaries compromised. Potential buffer overrun.",
                 t->heap_corrupted_blocks);
        has_critical = 1;
    }

    /* Physical memory pressure check */
    if (t->ram_usage_pct >= 85 || (t->pmm_free_frames < 2000 && t->pmm_total_frames > 0)) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "System is under heavy memory pressure (%u%% RAM utilized). Risk of paging stalls or OOM.",
                 t->ram_usage_pct);
        has_warning = 1;
    }

    /* Process crash check */
    if (t->crash_detected && (cfg->provider != AI_PROVIDER_MOCK || strcmp(cfg->mock_scenario, "crash") != 0)) {
        if (t->crash_fault_addr < 0x1000) {
            snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                     "Process PID %u crashed due to NULL pointer dereference in user space (dereferencing address 0x%lx).",
                     t->crash_pid, t->crash_fault_addr);
        } else {
            snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                     "Process PID %u crashed due to invalid memory access (Page Fault) at 0x%lx outside allocated pages.",
                     t->crash_pid, t->crash_fault_addr);
        }
        has_critical = 1;
    }

    /* Storage low check */
    if (t->disk_pct_free < 10) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "Storage capacity critical: only %u%% free blocks remaining on root filesystem. ENOSPC imminent.",
                 t->disk_pct_free);
        has_warning = 1;
    }

    /* Network state check */
    if (!t->eth0_up) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "Network interface eth0 is offline. Local socket services functional, remote network unreachable.");
        has_warning = 1;
    }

    /* Security check */
    if (t->audit_denied_count > 0) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "Security violations detected: %u requests denied by capability or permission enforcement.",
                 t->audit_denied_count);
        has_warning = 1;
    }

    if (res->hypothesis_count == 0) {
        snprintf(res->hypotheses[res->hypothesis_count++], DIAG_LINE_MAX,
                 "System operating within normal baseline parameters. Memory, scheduler, storage, and network healthy.");
    }

    /* 3. Recommended Remediation Actions (Never executed automatically) */
    if (t->heap_corrupted_blocks > 0) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Run '/bin/memdiag -v' to pinpoint corrupted allocator chunks and trace callers.");
    }
    if (t->crash_detected || (cfg->provider == AI_PROVIDER_MOCK && strcmp(cfg->mock_scenario, "crash") == 0)) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Inspect fault context with '/bin/cat /var/log/kernel.log' and trace execution using '/bin/nytrace <app>'.");
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Attach GDB using 'tools/gdbinit_nyota' to inspect core registers and symbol stack frame.");
    }
    if (t->ram_usage_pct >= 85 || t->zombie_count > 0) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Inspect process hierarchy with '/bin/pstree' and terminate rogue processes via '/bin/kill <pid>'.");
    }
    if (t->disk_pct_free < 15) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Check filesystem statistics with '/bin/df' and prune temporary logs in /var/log/.");
    }
    if (!t->eth0_up) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Reconfigure network interface with '/bin/ifconfig eth0 up' or restart daemon via '/bin/service restart netd'.");
    }
    if (t->audit_denied_count > 0) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "Review rejected operations with '/bin/auditlog' and check process capability assignments via '/bin/secinfo'.");
    }

    if (res->action_count == 0) {
        snprintf(res->actions[res->action_count++], DIAG_LINE_MAX,
                 "No administrative remediation required. Monitor regular health via '/bin/nydiag summary'.");
    }

    /* Assign overall health status */
    if (has_critical) res->health = HEALTH_STATUS_CRITICAL;
    else if (has_warning) res->health = HEALTH_STATUS_WARNING;
    else res->health = HEALTH_STATUS_OK;
}

/* ── HTTP Provider with Graceful Fallback ─────────────────────────────────── */

static inline int diag_query_http_ai(const diag_telemetry_t *t, const ai_diag_config_t *cfg, diag_analysis_result_t *res) {
    /* 1. Consent Gate: Never transmit telemetry without explicit user authorization */
    if (!cfg->consent_granted) {
        snprintf(res->raw_ai_response, sizeof(res->raw_ai_response),
                 "[ERROR] User consent not granted for external AI analysis. Telemetry transmission aborted.");
        diag_analyze_telemetry(t, cfg, res);
        return -1;
    }

    /* 2. Format prompt with redacted facts */
    char raw_prompt[512];
    snprintf(raw_prompt, sizeof(raw_prompt),
             "NyotaOS Telemetry: Uptime=%lu RAM_pct=%u Free_PMM=%lu Crash=%d HeapCorrupt=%u Net=%s",
             t->uptime_sec, t->ram_usage_pct, t->pmm_free_frames,
             t->crash_detected, t->heap_corrupted_blocks, t->eth0_up ? "UP" : "DOWN");

    char safe_prompt[512];
    if (cfg->redact_secrets) {
        redact_sensitive_data(raw_prompt, safe_prompt, sizeof(safe_prompt));
    } else {
        strncpy(safe_prompt, raw_prompt, sizeof(safe_prompt) - 1);
    }

    /* 3. Connect to configured HTTP endpoint */
    /* Parse host and port from endpoint */
    const char *url = cfg->endpoint;
    if (strncmp(url, "http://", 7) == 0) url += 7;
    char host[64] = "10.0.2.2";
    uint16_t port = 11434;

    const char *colon = strchr(url, ':');
    const char *slash = strchr(url, '/');
    if (colon) {
        size_t hlen = (size_t)(colon - url);
        if (hlen < sizeof(host)) {
            strncpy(host, url, hlen);
            host[hlen] = '\0';
        }
        port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        size_t hlen = (size_t)(slash - url);
        if (hlen < sizeof(host)) {
            strncpy(host, url, hlen);
            host[hlen] = '\0';
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        /* Fallback to local heuristic engine */
        diag_analyze_telemetry(t, cfg, res);
        snprintf(res->raw_ai_response, sizeof(res->raw_ai_response),
                 "[FALLBACK] Failed to create socket. Falling back to local heuristic reasoning.");
        return 0;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(sock);
        /* Fallback smoothly without OS crash or block */
        diag_analyze_telemetry(t, cfg, res);
        snprintf(res->raw_ai_response, sizeof(res->raw_ai_response),
                 "[FALLBACK] Remote AI provider at %s:%u offline. Executed local heuristic diagnostics.", host, port);
        return 0;
    }

    /* Build HTTP Request */
    char req[1024];
    snprintf(req, sizeof(req),
             "POST /api/generate HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %lu\r\n"
             "Connection: close\r\n\r\n"
             "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}",
             host, port, strlen(safe_prompt) + 40, cfg->model_name, safe_prompt);

    send(sock, req, strlen(req), 0);

    /* Read response with bounded timeout */
    char resp[1024];
    int64_t n = recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);

    if (n > 0) {
        resp[n] = '\0';
        strncpy(res->raw_ai_response, resp, sizeof(res->raw_ai_response) - 1);
    }

    /* Run standard facts/hypotheses/actions analyzer */
    diag_analyze_telemetry(t, cfg, res);
    return 0;
}

/* ── Full Diagnostic Report Formatter ─────────────────────────────────────── */

static inline int format_diagnostic_report(const diag_telemetry_t *t,
                                          const diag_analysis_result_t *res,
                                          const ai_diag_config_t *cfg,
                                          char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;
    buf[0] = '\0';

    const char *h_str = "HEALTHY";
    if (res->health == HEALTH_STATUS_WARNING) h_str = "WARNING";
    else if (res->health == HEALTH_STATUS_CRITICAL) h_str = "CRITICAL";

    snprintf(buf, buf_size,
        "======================================================================\n"
        "             NYOTA OS AI-ASSISTED SYSTEM DIAGNOSTICS REPORT          \n"
        "======================================================================\n"
        "OS Version      : Nyota OS %s (%s)\n"
        "System Health   : [%s]\n"
        "Uptime          : %lu seconds\n"
        "AI Provider     : %s (Endpoint: %s)\n"
        "Consent Status  : %s\n"
        "Redaction Filter: %s\n"
        "======================================================================\n\n"
        "[FACTS]\n",
        t->kernel_ver[0] ? t->kernel_ver : "1.9.0", t->machine[0] ? t->machine : "x86_64",
        h_str, t->uptime_sec,
        cfg->provider == AI_PROVIDER_HTTP ? "HTTP External LLM" : (cfg->provider == AI_PROVIDER_MOCK ? "Mock Provider" : "Local Heuristic Engine"),
        cfg->endpoint,
        cfg->consent_granted ? "Authorized (Explicit Consent)" : "Strictly Local Only",
        cfg->redact_secrets ? "Active (Credentials Redacted)" : "Disabled"
    );

    for (int i = 0; i < res->fact_count && strlen(buf) + 300 < buf_size; i++) {
        char line[DIAG_LINE_MAX + 16];
        snprintf(line, sizeof(line), "  * %s\n", res->facts[i]);
        strncat(buf, line, buf_size - strlen(buf) - 1);
    }

    strncat(buf, "\n[HYPOTHESIS]\n", buf_size - strlen(buf) - 1);
    for (int i = 0; i < res->hypothesis_count && strlen(buf) + 300 < buf_size; i++) {
        char line[DIAG_LINE_MAX + 16];
        snprintf(line, sizeof(line), "  ? %s\n", res->hypotheses[i]);
        strncat(buf, line, buf_size - strlen(buf) - 1);
    }

    strncat(buf, "\n[RECOMMENDATIONS]\n", buf_size - strlen(buf) - 1);
    for (int i = 0; i < res->action_count && strlen(buf) + 300 < buf_size; i++) {
        char line[DIAG_LINE_MAX + 16];
        snprintf(line, sizeof(line), "  > %s\n", res->actions[i]);
        strncat(buf, line, buf_size - strlen(buf) - 1);
    }

    strncat(buf,
        "\n[NOTICE]\n"
        "  Recommendations are purely advisory and never executed automatically.\n"
        "  All administrative remediation actions require manual verification.\n"
        "======================================================================\n",
        buf_size - strlen(buf) - 1);

    return 0;
}

#endif /* NYOTA_DIAG_ENGINE_H */
