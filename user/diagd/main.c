/* =============================================================================
 * Nyota OS — System Diagnostics Daemon (/sbin/diagd)
 * User-space background service for telemetry collection, anomaly monitoring,
 * and AI-assisted diagnostics IPC interface at /run/diag.sock.
 * =========================================================================== */

#include "diag_engine.h"

#define DIAG_LOG_PATH "/var/log/diagnostics.log"
#define DIAG_SOCK_PATH "/run/diag.sock"
#define DIAG_PID_PATH  "/run/diagd.pid"

static void log_diag_event(const char *msg) {
    int fd = open(DIAG_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, "[diagd] ", 8);
        write(fd, msg, strlen(msg));
        write(fd, "\n", 1);
        close(fd);
    }
}

static void handle_client(int cfd, const ai_diag_config_t *cfg) {
    char req[256];
    int64_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(cfd);
        return;
    }
    req[n] = '\0';

    diag_telemetry_t telem;
    collect_system_telemetry(&telem);

    diag_analysis_result_t res;
    memset(&res, 0, sizeof(res));

    char resp[DIAG_REPORT_MAX];
    resp[0] = '\0';

    if (strncmp(req, "PING", 4) == 0) {
        strcpy(resp, "PONG\n");
    } else if (strncmp(req, "HEALTH", 6) == 0) {
        diag_analyze_telemetry(&telem, cfg, &res);
        const char *h = (res.health == HEALTH_STATUS_CRITICAL) ? "CRITICAL" :
                        ((res.health == HEALTH_STATUS_WARNING) ? "WARNING" : "HEALTHY");
        snprintf(resp, sizeof(resp), "HEALTH: %s (Mem: %u%% used, %lu MB free, %u Corrupted Blocks)\n",
                 h, telem.ram_usage_pct, telem.free_ram_bytes / (1024 * 1024), telem.heap_corrupted_blocks);
    } else if (strncmp(req, "METRICS", 7) == 0) {
        snprintf(resp, sizeof(resp),
                 "METRICS: Uptime=%lu RAM_used=%luMB RAM_free=%luMB Blocks_free=%lu Pkts_rx=%lu Crash=%d HeapCorrupt=%u\n",
                 telem.uptime_sec, telem.used_ram_bytes / (1024 * 1024), telem.free_ram_bytes / (1024 * 1024),
                 telem.disk_free_blocks, telem.eth0_rx_packets, telem.crash_detected, telem.heap_corrupted_blocks);
    } else if (strncmp(req, "ANALYZE", 7) == 0) {
        diag_analyze_telemetry(&telem, cfg, &res);
        format_diagnostic_report(&telem, &res, cfg, resp, sizeof(resp));
    } else if (strncmp(req, "REPORT", 6) == 0) {
        diag_analyze_telemetry(&telem, cfg, &res);
        format_diagnostic_report(&telem, &res, cfg, resp, sizeof(resp));
    } else {
        strcpy(resp, "ERR_UNKNOWN_COMMAND\n");
    }

    send(cfd, resp, strlen(resp), 0);
    close(cfd);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_diag_event("Starting Nyota OS diagnostics daemon");

    /* Create PID file */
    int pfd = open(DIAG_PID_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pfd >= 0) {
        char pstr[16];
        snprintf(pstr, sizeof(pstr), "%d\n", getpid());
        write(pfd, pstr, strlen(pstr));
        close(pfd);
    }

    /* Load initial configuration */
    ai_diag_config_t cfg;
    diag_load_config(&cfg);

    /* Initialize Unix domain socket */
    unlink(DIAG_SOCK_PATH);
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) {
        printf("[diagd] Failed to create socket\n");
        return 1;
    }

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, DIAG_SOCK_PATH, sizeof(sun.sun_path) - 1);

    if (bind(sfd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        printf("[diagd] Failed to bind %s\n", DIAG_SOCK_PATH);
        close(sfd);
        return 1;
    }

    listen(sfd, 5);

    /* Main daemon loop */
    while (1) {
        struct sockaddr client_addr;
        size_t addrlen = sizeof(client_addr);
        int cfd = accept(sfd, &client_addr, &addrlen);
        if (cfd >= 0) {
            handle_client(cfd, &cfg);
        }
        sleep(50);
    }

    close(sfd);
    unlink(DIAG_SOCK_PATH);
    unlink(DIAG_PID_PATH);
    return 0;
}
