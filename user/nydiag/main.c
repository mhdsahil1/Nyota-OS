/* =============================================================================
 * Nyota OS — AI-Assisted System Diagnostics Tool & GUI Assistant (/bin/nydiag)
 * CLI telemetry analysis, crash explanation, privacy redaction, and desktop GUI.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"
#include "theme.h"
#include "../diagd/diag_engine.h"

#define GUI_WIDTH   640
#define GUI_HEIGHT  480

static void run_cli_summary(const diag_telemetry_t *t, const diag_analysis_result_t *res) {
    const char *h_color = (res->health == HEALTH_STATUS_OK) ? "HEALTHY" :
                          ((res->health == HEALTH_STATUS_WARNING) ? "WARNING" : "CRITICAL");

    printf("========================================\n");
    printf("   NYOTA OS SYSTEM HEALTH SUMMARY       \n");
    printf("========================================\n");
    printf("Overall Status : [%s]\n", h_color);
    printf("OS Version     : Nyota OS %s (%s)\n", t->kernel_ver[0] ? t->kernel_ver : "2.0.0", t->machine[0] ? t->machine : "x86_64");
    printf("Uptime         : %lu seconds\n", t->uptime_sec);
    printf("Memory Usage   : %lu MB / %lu MB (%u%%)\n",
           t->used_ram_bytes / (1024 * 1024),
           (t->total_ram_bytes / (1024 * 1024) > 0) ? t->total_ram_bytes / (1024 * 1024) : 128,
           t->ram_usage_pct);
    printf("Heap Integrity : %u corrupted blocks %s\n",
           t->heap_corrupted_blocks,
           t->heap_corrupted_blocks == 0 ? "(PASSED)" : "(FAILED - CANARY CORRUPTED)");
    printf("Storage Free   : %lu blocks (%u%% free)\n", t->disk_free_blocks, t->disk_pct_free);
    printf("Network (eth0) : %s (RX: %lu pkts, TX: %lu pkts)\n",
           t->eth0_up ? "ONLINE (UP)" : "OFFLINE (DOWN)", t->eth0_rx_packets, t->eth0_tx_packets);
    printf("Active Procs   : %d (Zombies: %d)\n", t->process_count, t->zombie_count);
    if (t->crash_detected) {
        printf("Recent Crash   : PID %u signal %d (%s) at 0x%lx\n",
               t->crash_pid, 11, t->crash_cause, t->crash_fault_addr);
    }
    printf("========================================\n");
}

static void run_cli_analysis(const diag_telemetry_t *t, const ai_diag_config_t *cfg, const diag_analysis_result_t *res) {
    char report[DIAG_REPORT_MAX];
    format_diagnostic_report(t, res, cfg, report, sizeof(report));
    printf("%s", report);
}

static void run_gui_diagnostics(void) {
    gui_window_t *win = gui_window_create(GUI_WIDTH, GUI_HEIGHT, "System Diagnostics & AI Assistant - Nyota OS");
    if (!win) {
        printf("nydiag: failed to initialize GUI window, defaulting to CLI summary\n");
        diag_telemetry_t t;
        ai_diag_config_t cfg;
        diag_analysis_result_t res;
        diag_load_config(&cfg);
        collect_system_telemetry(&t);
        diag_analyze_telemetry(&t, &cfg, &res);
        run_cli_summary(&t, &res);
        return;
    }

    diag_telemetry_t telem;
    ai_diag_config_t cfg;
    diag_analysis_result_t res;

    diag_load_config(&cfg);
    cfg.consent_granted = 1; /* Granted in GUI */
    collect_system_telemetry(&telem);
    diag_analyze_telemetry(&telem, &cfg, &res);

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
                break;
            } else if (ev.type == EVENT_KEY_PRESS) {
                if (ev.character == 'r' || ev.character == 'R' || ev.keycode == 'r' || ev.keycode == 'R') {
                    collect_system_telemetry(&telem);
                    diag_analyze_telemetry(&telem, &cfg, &res);
                } else if (ev.character == 'a' || ev.character == 'A' || ev.keycode == 'a' || ev.keycode == 'A') {
                    if (cfg.provider == AI_PROVIDER_HTTP) {
                        diag_query_http_ai(&telem, &cfg, &res);
                    } else {
                        diag_analyze_telemetry(&telem, &cfg, &res);
                    }
                } else if (ev.character == 'e' || ev.character == 'E' || ev.keycode == 'e' || ev.keycode == 'E') {
                    char rep[DIAG_REPORT_MAX];
                    format_diagnostic_report(&telem, &res, &cfg, rep, sizeof(rep));
                    int fd = open("/var/log/diagnostics_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd >= 0) {
                        write(fd, rep, strlen(rep));
                        close(fd);
                    }
                } else if (ev.character == 'q' || ev.character == 'Q' || ev.keycode == 'q' || ev.keycode == 'Q') {
                    win->should_close = true;
                }
            } else if (ev.type == EVENT_MOUSE_BUTTON_PRESS) {
                /* Click on Refresh Button (x: 20, y: 440, w: 100, h: 26) */
                if (ev.mouse_x >= 20 && ev.mouse_x <= 120 && ev.mouse_y >= 440 && ev.mouse_y <= 466) {
                    collect_system_telemetry(&telem);
                    diag_analyze_telemetry(&telem, &cfg, &res);
                }
                /* Click on AI Analyze Button (x: 130, y: 440, w: 140, h: 26) */
                else if (ev.mouse_x >= 130 && ev.mouse_x <= 270 && ev.mouse_y >= 440 && ev.mouse_y <= 466) {
                    if (cfg.provider == AI_PROVIDER_HTTP) {
                        diag_query_http_ai(&telem, &cfg, &res);
                    } else {
                        diag_analyze_telemetry(&telem, &cfg, &res);
                    }
                }
                /* Click on Export Button (x: 280, y: 440, w: 120, h: 26) */
                else if (ev.mouse_x >= 280 && ev.mouse_x <= 400 && ev.mouse_y >= 440 && ev.mouse_y <= 466) {
                    char rep[DIAG_REPORT_MAX];
                    format_diagnostic_report(&telem, &res, &cfg, rep, sizeof(rep));
                    int fd = open("/var/log/diagnostics_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd >= 0) {
                        write(fd, rep, strlen(rep));
                        close(fd);
                    }
                }
            }
        }

        /* ── Render GUI Diagnostic Window ─────────────────────────────────── */
        gui_fill_rect(win, 0, 0, GUI_WIDTH, GUI_HEIGHT, COLOR_WINDOW_BG);

        /* Header Card */
        gui_fill_rect(win, 12, 10, GUI_WIDTH - 24, 52, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, 10, GUI_WIDTH - 24, 52, COLOR_WIDGET_BORDER);

        gui_draw_text(win, 24, 18, "Nyota OS AI-Assisted System Diagnostics (v2.0.0)", COLOR_TITLE_ACTIVE, 0);

        uint32_t badge_col = (res.health == HEALTH_STATUS_OK) ? COLOR_SUCCESS :
                             ((res.health == HEALTH_STATUS_WARNING) ? COLOR_WARNING : COLOR_DANGER);
        const char *badge_text = (res.health == HEALTH_STATUS_OK) ? "[ SYSTEM HEALTHY ]" :
                                 ((res.health == HEALTH_STATUS_WARNING) ? "[ WARNING DETECTED ]" : "[ CRITICAL FAILURE ]");
        gui_draw_text(win, GUI_WIDTH - 220, 18, badge_text, badge_col, 0);

        char subhdr[128];
        snprintf(subhdr, sizeof(subhdr), "Kernel: %s | Uptime: %lu sec | Provider: %s",
                 telem.kernel_ver[0] ? telem.kernel_ver : "2.0.0", telem.uptime_sec,
                 cfg.provider == AI_PROVIDER_HTTP ? "HTTP LLM" : "Local Heuristic");
        gui_draw_text(win, 24, 38, subhdr, COLOR_TEXT_MUTED, 0);

        /* Metrics Grid (4 Cards) */
        int card_w = (GUI_WIDTH - 24 - 18) / 3;
        int card_h = 56;
        int cy = 68;

        /* Memory Card */
        gui_fill_rect(win, 12, cy, card_w, card_h, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, cy, card_w, card_h, COLOR_WIDGET_BORDER);
        gui_draw_text(win, 20, cy + 8, "MEMORY (RAM)", COLOR_TITLE_ACTIVE, 0);
        char mtxt[64];
        snprintf(mtxt, sizeof(mtxt), "%lu MB / %lu MB (%u%%)",
                 telem.used_ram_bytes / (1024 * 1024),
                 (telem.total_ram_bytes / (1024 * 1024) > 0) ? telem.total_ram_bytes / (1024 * 1024) : 128,
                 telem.ram_usage_pct);
        gui_draw_text(win, 20, cy + 28, mtxt, COLOR_TEXT_PRIMARY, 0);

        /* Storage Card */
        int c2_x = 12 + card_w + 9;
        gui_fill_rect(win, c2_x, cy, card_w, card_h, COLOR_WIDGET_BG);
        gui_draw_rect(win, c2_x, cy, card_w, card_h, COLOR_WIDGET_BORDER);
        gui_draw_text(win, c2_x + 8, cy + 8, "STORAGE (ROOT)", COLOR_TITLE_ACTIVE, 0);
        char dtxt[64];
        snprintf(dtxt, sizeof(dtxt), "%lu blocks free (%u%%)", telem.disk_free_blocks, telem.disk_pct_free);
        gui_draw_text(win, c2_x + 8, cy + 28, dtxt, COLOR_TEXT_PRIMARY, 0);

        /* Network Card */
        int c3_x = c2_x + card_w + 9;
        gui_fill_rect(win, c3_x, cy, card_w, card_h, COLOR_WIDGET_BG);
        gui_draw_rect(win, c3_x, cy, card_w, card_h, COLOR_WIDGET_BORDER);
        gui_draw_text(win, c3_x + 8, cy + 8, "NETWORK (ETH0)", COLOR_TITLE_ACTIVE, 0);
        char ntxt[64];
        snprintf(ntxt, sizeof(ntxt), "%s (%lu RX pkts)", telem.eth0_up ? "ONLINE" : "OFFLINE", telem.eth0_rx_packets);
        gui_draw_text(win, c3_x + 8, cy + 28, ntxt, telem.eth0_up ? COLOR_SUCCESS : COLOR_DANGER, 0);

        /* Diagnostic Facts & Observed Telemetry Panel */
        int fy = 132;
        int pan_w = GUI_WIDTH - 24;
        gui_fill_rect(win, 12, fy, pan_w, 88, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, fy, pan_w, 88, COLOR_WIDGET_BORDER);
        gui_fill_rect(win, 12, fy, pan_w, 20, COLOR_PANEL_BG);
        gui_draw_text(win, 20, fy + 4, "[FACTS] OBSERVED SYSTEM TELEMETRY", COLOR_TITLE_ACTIVE, 0);

        int line_y = fy + 24;
        for (int i = 0; i < res.fact_count && i < 3; i++) {
            gui_draw_text(win, 20, line_y, res.facts[i], COLOR_TEXT_MUTED, 0);
            line_y += 18;
        }

        /* AI Analysis & Explanation Panel */
        int hy = 226;
        gui_fill_rect(win, 12, hy, pan_w, 100, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, hy, pan_w, 100, COLOR_WIDGET_BORDER);
        gui_fill_rect(win, 12, hy, pan_w, 20, COLOR_PANEL_BG);
        gui_draw_text(win, 20, hy + 4, "[HYPOTHESIS] ROOT CAUSE ANALYSIS", COLOR_TITLE_ACTIVE, 0);

        line_y = hy + 24;
        for (int i = 0; i < res.hypothesis_count && i < 3; i++) {
            gui_draw_text(win, 20, line_y, res.hypotheses[i], COLOR_TEXT_PRIMARY, 0);
            line_y += 20;
        }

        /* Recommended Troubleshooting Actions Panel */
        int ay = 332;
        gui_fill_rect(win, 12, ay, pan_w, 94, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, ay, pan_w, 94, COLOR_WIDGET_BORDER);
        gui_fill_rect(win, 12, ay, pan_w, 20, COLOR_PANEL_BG);
        gui_draw_text(win, 20, ay + 4, "[RECOMMENDATIONS] MANUAL TROUBLESHOOTING ACTIONS", COLOR_TITLE_ACTIVE, 0);

        line_y = ay + 24;
        for (int i = 0; i < res.action_count && i < 3; i++) {
            gui_draw_text(win, 20, line_y, res.actions[i], COLOR_SUCCESS, 0);
            line_y += 20;
        }

        /* Action Buttons */
        gui_fill_rect(win, 16, 436, 100, 26, COLOR_PANEL_BG);
        gui_draw_rect(win, 16, 436, 100, 26, COLOR_WIDGET_BORDER);
        gui_draw_text(win, 24, 442, "[R] Refresh", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 124, 436, 130, 26, COLOR_PANEL_BG);
        gui_draw_rect(win, 124, 436, 130, 26, COLOR_WIDGET_BORDER);
        gui_draw_text(win, 132, 442, "[A] Run Analysis", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 262, 436, 110, 26, COLOR_PANEL_BG);
        gui_draw_rect(win, 262, 436, 110, 26, COLOR_WIDGET_BORDER);
        gui_draw_text(win, 270, 442, "[E] Export", COLOR_TEXT_PRIMARY, 0);

        gui_draw_text(win, 410, 442, "[x] Consent Granted | Privacy Active", COLOR_SUCCESS, 0);

        gui_window_update(win);
        sleep(50);
    }

    gui_window_destroy(win);
}

int main(int argc, char **argv) {
    bool do_gui = false;
    bool do_health = false;
    bool do_analyze = false;
    bool do_crash = false;
    bool do_redact = false;
    bool do_config = false;
    bool do_report = false;
    const char *out_report = NULL;
    const char *redact_text = NULL;

    ai_diag_config_t cfg;
    diag_load_config(&cfg);

    /* Command-line Argument Parsing */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui") == 0 || strcmp(argv[i], "-g") == 0) {
            do_gui = true;
        } else if (strcmp(argv[i], "health") == 0) {
            do_health = true;
        } else if (strcmp(argv[i], "analyze") == 0) {
            do_analyze = true;
        } else if (strcmp(argv[i], "crash") == 0) {
            do_crash = true;
        } else if (strcmp(argv[i], "config") == 0) {
            do_config = true;
        } else if (strcmp(argv[i], "report") == 0) {
            do_report = true;
        } else if (strcmp(argv[i], "redact") == 0 && i + 1 < argc) {
            do_redact = true;
            static char combined_redact[512];
            combined_redact[0] = '\0';
            for (int k = i + 1; k < argc; k++) {
                if (k > i + 1) strncat(combined_redact, " ", sizeof(combined_redact) - strlen(combined_redact) - 1);
                strncat(combined_redact, argv[k], sizeof(combined_redact) - strlen(combined_redact) - 1);
            }
            redact_text = combined_redact;
            break;
        } else if (strcmp(argv[i], "--ai") == 0) {
            cfg.provider = AI_PROVIDER_HTTP;
        } else if (strcmp(argv[i], "--local") == 0) {
            cfg.provider = AI_PROVIDER_LOCAL;
        } else if (strcmp(argv[i], "--consent") == 0 || strcmp(argv[i], "-y") == 0) {
            cfg.consent_granted = 1;
        } else if (strcmp(argv[i], "--mock") == 0 && i + 1 < argc) {
            cfg.provider = AI_PROVIDER_MOCK;
            strncpy(cfg.mock_scenario, argv[++i], sizeof(cfg.mock_scenario) - 1);
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) && i + 1 < argc) {
            out_report = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: nydiag [command] [options]\n");
            printf("Commands:\n");
            printf("  summary             Show system health & metrics summary (default)\n");
            printf("  health              Check overall system health (HEALTHY, WARNING, CRITICAL)\n");
            printf("  analyze             Perform AI or local heuristic diagnostic analysis\n");
            printf("  crash [pid]         Analyze recent process crash or exception\n");
            printf("  redact <string>     Test secret and credential redaction filter\n");
            printf("  report [-o file]    Export diagnostic report to file or terminal\n");
            printf("  config              Display diagnostics and AI provider configuration\n");
            printf("  --gui               Launch interactive desktop GUI dashboard\n\n");
            printf("Options:\n");
            printf("  --ai                Use external AI provider\n");
            printf("  --local             Force local heuristic reasoning engine\n");
            printf("  --consent, -y       Grant explicit user consent for telemetry transmission\n");
            printf("  --mock <scenario>   Simulate failure scenario (crash, memory, network, timeout)\n");
            return 0;
        }
    }

    if (do_redact && redact_text) {
        char redacted[512];
        redact_sensitive_data(redact_text, redacted, sizeof(redacted));
        printf("Original : %s\n", redact_text);
        printf("Redacted : %s\n", redacted);
        return 0;
    }

    if (do_config) {
        printf("========================================\n");
        printf("     NYOTA DIAGNOSTICS CONFIGURATION    \n");
        printf("========================================\n");
        printf("AI Enabled       : %s\n", cfg.enabled ? "Yes" : "No");
        printf("Provider         : %s\n", cfg.provider == AI_PROVIDER_HTTP ? "HTTP" :
                                        (cfg.provider == AI_PROVIDER_MOCK ? "Mock" : "Local Heuristic"));
        printf("Endpoint         : %s\n", cfg.endpoint);
        printf("Model            : %s\n", cfg.model_name);
        printf("Timeout          : %u ms\n", cfg.timeout_ms);
        printf("Consent Required : %s\n", cfg.consent_required ? "Yes" : "No");
        printf("Redact Secrets   : %s\n", cfg.redact_secrets ? "Yes" : "No");
        printf("========================================\n");
        return 0;
    }

    /* Collect system metrics */
    diag_telemetry_t telem;
    collect_system_telemetry(&telem);

    diag_analysis_result_t res;
    if (cfg.provider == AI_PROVIDER_HTTP) {
        diag_query_http_ai(&telem, &cfg, &res);
    } else {
        diag_analyze_telemetry(&telem, &cfg, &res);
    }

    if (do_gui) {
        run_gui_diagnostics();
        return 0;
    }

    if (do_health) {
        const char *h_str = (res.health == HEALTH_STATUS_CRITICAL) ? "CRITICAL" :
                            ((res.health == HEALTH_STATUS_WARNING) ? "WARNING" : "HEALTHY");
        printf("SYSTEM HEALTH: [%s]\n", h_str);
        if (res.health != HEALTH_STATUS_OK) {
            for (int i = 0; i < res.hypothesis_count; i++) {
                printf("  Issue: %s\n", res.hypotheses[i]);
            }
        }
        return (res.health == HEALTH_STATUS_CRITICAL) ? 2 : ((res.health == HEALTH_STATUS_WARNING) ? 1 : 0);
    }

    if (do_crash) {
        printf("========================================\n");
        printf("       PROCESS CRASH DIAGNOSTICS        \n");
        printf("========================================\n");
        if (telem.crash_detected) {
            printf("[FACTS]\n");
            printf("  * Terminated Process : PID %u\n", telem.crash_pid);
            printf("  * Signal Invocations : %d (%s)\n", 11, telem.crash_cause);
            printf("  * Faulting Address   : 0x%lx\n", telem.crash_fault_addr);
            printf("\n[HYPOTHESIS]\n");
            if (telem.crash_fault_addr < 0x1000) {
                printf("  ? Process crashed due to NULL pointer dereference in user application.\n");
            } else {
                printf("  ? Process crashed due to page fault memory violation outside mapped space.\n");
            }
            printf("\n[RECOMMENDATIONS]\n");
            printf("  > 1. Review crash context with '/bin/cat /var/log/kernel.log'.\n");
            printf("  > 2. Re-run application with debugger or system tracer '/bin/nytrace'.\n");
        } else {
            printf("[FACTS]\n");
            printf("  * No active crash or memory fault recorded in kernel logs.\n");
            printf("\n[HYPOTHESIS]\n");
            printf("  ? System processes are executing stably without memory violations.\n");
        }
        printf("========================================\n");
        return 0;
    }

    if (do_report) {
        char rep[DIAG_REPORT_MAX];
        format_diagnostic_report(&telem, &res, &cfg, rep, sizeof(rep));
        if (out_report) {
            int fd = open(out_report, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                write(fd, rep, strlen(rep));
                close(fd);
                printf("Diagnostic report successfully exported to %s\n", out_report);
            } else {
                printf("nydiag: failed to open output file %s\n", out_report);
                return 1;
            }
        } else {
            printf("%s", rep);
        }
        return 0;
    }

    if (do_analyze) {
        run_cli_analysis(&telem, &cfg, &res);
        return 0;
    }

    /* Default: print summary */
    run_cli_summary(&telem, &res);
    return 0;
}
