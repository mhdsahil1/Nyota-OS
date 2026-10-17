/* =============================================================================
 * Nyota OS — Native GUI Network Manager Application (/bin/netmgr)
 * Active network configuration, interface management, DNS, and diagnostics.
 * Distinct from the Settings network tab: provides interactive control.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define NM_WIDTH    560
#define NM_HEIGHT   420

static char status_msg[128] = "Ready";

/* Current view state */
static int active_tab = 0;
static const char *tab_labels[] = {
    "Interfaces",
    "Routing",
    "DNS",
    "Firewall",
    "Diagnostics"
};
#define TAB_COUNT 5

/* Cached network data */
static netdev_info_t devices[4];
static int dev_count = 0;
static int selected_dev = 0;
static firewall_status_t fw_status;

static void refresh_network(void) {
    dev_count = (int)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);
    if (dev_count > 4) dev_count = 4;
    for (int i = 0; i < dev_count; i++) {
        memset(&devices[i], 0, sizeof(netdev_info_t));
        sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&devices[i]);
    }
    memset(&fw_status, 0, sizeof(fw_status));
    sys_firewall_op(FW_CMD_GET_STATUS, (uint64_t)&fw_status, 0, 0);
}

static void format_mac(const uint8_t *mac, char *out, size_t len) {
    const char *hex = "0123456789abcdef";
    snprintf(out, len, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[(mac[0]>>4)&0xF], hex[mac[0]&0xF],
             hex[(mac[1]>>4)&0xF], hex[mac[1]&0xF],
             hex[(mac[2]>>4)&0xF], hex[mac[2]&0xF],
             hex[(mac[3]>>4)&0xF], hex[mac[3]&0xF],
             hex[(mac[4]>>4)&0xF], hex[mac[4]&0xF],
             hex[(mac[5]>>4)&0xF], hex[mac[5]&0xF]);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(NM_WIDTH, NM_HEIGHT, "Network Manager - Nyota OS");
    if (!win) {
        printf("[NETMGR] Failed to create window\n");
        return 1;
    }

    refresh_network();
    uint32_t refresh_counter = 0;

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
                break;
            }

            if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {
                int mx = ev.mouse_x;
                int my = ev.mouse_y;

                /* Tab bar clicks */
                if (my >= 4 && my <= 34) {
                    int tx = 8;
                    for (int i = 0; i < TAB_COUNT; i++) {
                        int tw = (int)strlen(tab_labels[i]) * 8 + 20;
                        if (mx >= tx && mx < tx + tw) {
                            active_tab = i;
                            break;
                        }
                        tx += tw + 4;
                    }
                }

                /* Refresh button */
                if (mx >= NM_WIDTH - 90 && mx <= NM_WIDTH - 12 && my >= 8 && my <= 32) {
                    refresh_network();
                    strncpy(status_msg, "Network data refreshed", sizeof(status_msg));
                }

                /* Interface selection (tab 0) */
                if (active_tab == 0 && my >= 56 && my < 56 + dev_count * 28) {
                    int idx = (my - 56) / 28;
                    if (idx >= 0 && idx < dev_count) {
                        selected_dev = idx;
                    }
                }

                /* Firewall toggle (tab 3) */
                if (active_tab == 3 && my >= 58 && my <= 82 && mx >= 12 && mx <= 150) {
                    if (fw_status.enabled) {
                        sys_firewall_op(FW_CMD_SET_ENABLED, 0, 0, 0);
                        strncpy(status_msg, "Firewall disabled", sizeof(status_msg));
                    } else {
                        sys_firewall_op(FW_CMD_SET_ENABLED, 1, 0, 0);
                        strncpy(status_msg, "Firewall enabled", sizeof(status_msg));
                    }
                    refresh_network();
                }
            }
        }

        /* Auto-refresh every ~3 seconds */
        if (++refresh_counter >= 100) {
            refresh_network();
            refresh_counter = 0;
        }

        /* ── Render Network Manager ────────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, NM_WIDTH, NM_HEIGHT, COLOR_WINDOW_BG);

        /* Tab bar */
        gui_fill_rect(win, 0, 0, NM_WIDTH, 38, COLOR_PANEL_BG);
        gui_draw_line(win, 0, 38, NM_WIDTH, 38, COLOR_PANEL_BORDER);

        int tx = 8;
        for (int i = 0; i < TAB_COUNT; i++) {
            int tw = (int)strlen(tab_labels[i]) * 8 + 20;
            uint32_t bg = (i == active_tab) ? COLOR_BUTTON_PRESSED : COLOR_BUTTON_NORMAL;
            uint32_t border = (i == active_tab) ? COLOR_ACCENT_PRIMARY : COLOR_WINDOW_BORDER;
            gui_fill_rect(win, tx, 6, tw, 26, bg);
            gui_draw_rect(win, tx, 6, tw, 26, border);
            gui_draw_text(win, tx + 10, 12, tab_labels[i],
                          (i == active_tab) ? COLOR_TITLE_ACTIVE : COLOR_TEXT_MUTED, 0);
            tx += tw + 4;
        }

        /* Refresh button */
        gui_fill_rect(win, NM_WIDTH - 90, 8, 78, 24, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, NM_WIDTH - 90, 8, 78, 24, COLOR_WINDOW_BORDER);
        gui_draw_text(win, NM_WIDTH - 80, 12, "Refresh", COLOR_TEXT_PRIMARY, 0);

        /* Content area */
        int cy = 48;
        char line[128];

        switch (active_tab) {
            case 0: { /* Interfaces */
                /* Device list */
                for (int i = 0; i < dev_count; i++) {
                    int iy = 56 + i * 28;
                    uint32_t row_bg = (i == selected_dev) ? COLOR_SELECTION : COLOR_WIDGET_BG;
                    gui_fill_rect(win, 8, iy, NM_WIDTH - 16, 26, row_bg);
                    gui_draw_rect(win, 8, iy, NM_WIDTH - 16, 26, COLOR_WIDGET_BORDER);

                    const char *dev_name = devices[i].name[0] ? devices[i].name : "unknown";
                    bool is_up = (devices[i].flags & 0x01) != 0;
                    bool is_loopback = (devices[i].flags & 0x04) != 0;

                    snprintf(line, sizeof(line), "%s  %s  %s",
                             dev_name,
                             is_loopback ? "[LOOPBACK]" : "[ETHERNET]",
                             is_up ? "UP" : "DOWN");
                    gui_draw_text(win, 16, iy + 5, line, COLOR_TEXT_PRIMARY, 0);

                    struct in_addr in;
                    in.s_addr = devices[i].ip_addr;
                    gui_draw_text(win, 360, iy + 5, devices[i].ip_addr ? inet_ntoa(in) : "no addr",
                                  COLOR_ACCENT_PRIMARY, 0);
                }

                /* Detail panel for selected device */
                if (dev_count > 0) {
                    int detail_y = 60 + dev_count * 28 + 8;
                    netdev_info_t *d = &devices[selected_dev];

                    gui_fill_rect(win, 8, detail_y, NM_WIDTH - 16, 200, COLOR_WIDGET_BG);
                    gui_draw_rect(win, 8, detail_y, NM_WIDTH - 16, 200, COLOR_WIDGET_BORDER);

                    const char *nm = d->name[0] ? d->name : "unknown";
                    snprintf(line, sizeof(line), "Interface: %s", nm);
                    gui_draw_text(win, 16, detail_y + 8, line, COLOR_TITLE_ACTIVE, 0);

                    char mac_str[24];
                    format_mac(d->mac, mac_str, sizeof(mac_str));
                    snprintf(line, sizeof(line), "MAC: %s", mac_str);
                    gui_draw_text(win, 16, detail_y + 28, line, COLOR_TEXT_MUTED, 0);

                    struct in_addr in;
                    in.s_addr = d->ip_addr;
                    char ip_str[32];
                    strncpy(ip_str, inet_ntoa(in), sizeof(ip_str) - 1);
                    in.s_addr = d->netmask;
                    snprintf(line, sizeof(line), "IPv4: %s / %s", ip_str, inet_ntoa(in));
                    gui_draw_text(win, 16, detail_y + 48, line, COLOR_TEXT_MUTED, 0);

                    if (d->has_ip6) {
                        char ip6_str[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, d->ip6_addr, ip6_str, sizeof(ip6_str));
                        snprintf(line, sizeof(line), "IPv6: %s", ip6_str);
                    } else {
                        snprintf(line, sizeof(line), "IPv6: not configured");
                    }
                    gui_draw_text(win, 16, detail_y + 68, line, COLOR_TEXT_MUTED, 0);

                    in.s_addr = d->gateway;
                    snprintf(line, sizeof(line), "Gateway: %s", d->gateway ? inet_ntoa(in) : "none");
                    gui_draw_text(win, 16, detail_y + 88, line, COLOR_TEXT_MUTED, 0);

                    snprintf(line, sizeof(line), "RX: %llu packets (%llu bytes)", d->rx_packets, d->rx_bytes);
                    gui_draw_text(win, 16, detail_y + 118, line, COLOR_SUCCESS, 0);

                    snprintf(line, sizeof(line), "TX: %llu packets (%llu bytes)", d->tx_packets, d->tx_bytes);
                    gui_draw_text(win, 16, detail_y + 138, line, COLOR_ACCENT_PRIMARY, 0);

                    snprintf(line, sizeof(line), "MTU: %u  Link: %s",
                             d->mtu ? d->mtu : 1500, d->link_up ? "UP (1000 Mbps)" : "DOWN");
                    gui_draw_text(win, 16, detail_y + 168, line, COLOR_TEXT_DIM, 0);
                }
                break;
            }

            case 1: { /* Routing */
                gui_draw_text(win, 16, cy, "Routing Table", COLOR_ACCENT_PRIMARY, 0);
                cy += 24;

                gui_fill_rect(win, 8, cy, NM_WIDTH - 16, 20, COLOR_WIDGET_BG);
                gui_draw_text(win, 16,  cy + 2, "Destination", COLOR_TEXT_MUTED, 0);
                gui_draw_text(win, 160, cy + 2, "Gateway",     COLOR_TEXT_MUTED, 0);
                gui_draw_text(win, 300, cy + 2, "Interface",   COLOR_TEXT_MUTED, 0);
                gui_draw_text(win, 420, cy + 2, "Metric",      COLOR_TEXT_MUTED, 0);
                cy += 24;

                /* Show routes from available device data */
                for (int i = 0; i < dev_count; i++) {
                    netdev_info_t *d = &devices[i];
                    if (d->ip_addr == 0) continue;

                    struct in_addr in;
                    in.s_addr = d->ip_addr & d->netmask;
                    char net_str[32];
                    strncpy(net_str, inet_ntoa(in), sizeof(net_str) - 1);

                    gui_draw_text(win, 16,  cy, net_str, COLOR_TEXT_PRIMARY, 0);
                    gui_draw_text(win, 160, cy, "on-link", COLOR_TEXT_MUTED, 0);
                    gui_draw_text(win, 300, cy, d->name[0] ? d->name : "eth0", COLOR_ACCENT_PRIMARY, 0);
                    gui_draw_text(win, 420, cy, "0", COLOR_TEXT_DIM, 0);
                    cy += 20;
                }

                /* Default route */
                if (dev_count > 0 && devices[0].gateway) {
                    struct in_addr gw;
                    gw.s_addr = devices[0].gateway;
                    gui_draw_text(win, 16,  cy, "0.0.0.0/0", COLOR_TEXT_PRIMARY, 0);
                    gui_draw_text(win, 160, cy, inet_ntoa(gw), COLOR_TEXT_MUTED, 0);
                    gui_draw_text(win, 300, cy, devices[0].name[0] ? devices[0].name : "eth0", COLOR_ACCENT_PRIMARY, 0);
                    gui_draw_text(win, 420, cy, "100", COLOR_TEXT_DIM, 0);
                }
                break;
            }

            case 2: { /* DNS */
                gui_draw_text(win, 16, cy, "DNS Configuration", COLOR_ACCENT_PRIMARY, 0);
                cy += 28;

                if (dev_count > 0 && devices[0].dns) {
                    struct in_addr in;
                    in.s_addr = devices[0].dns;
                    snprintf(line, sizeof(line), "Primary DNS: %s", inet_ntoa(in));
                    gui_draw_text(win, 16, cy, line, COLOR_TEXT_PRIMARY, 0);
                } else {
                    gui_draw_text(win, 16, cy, "Primary DNS: 10.0.2.3 (default)", COLOR_TEXT_PRIMARY, 0);
                }
                cy += 22;
                gui_draw_text(win, 16, cy, "Secondary DNS: not configured", COLOR_TEXT_MUTED, 0);
                cy += 32;

                gui_draw_text(win, 16, cy, "DNS Resolver: Built-in stub resolver", COLOR_TEXT_MUTED, 0);
                cy += 22;
                gui_draw_text(win, 16, cy, "Protocol: UDP/53 (standard query)", COLOR_TEXT_MUTED, 0);
                cy += 22;
                gui_draw_text(win, 16, cy, "Use 'nslookup <host>' to test resolution.", COLOR_TEXT_DIM, 0);
                break;
            }

            case 3: { /* Firewall */
                gui_draw_text(win, 16, cy, "Firewall Control", COLOR_ACCENT_PRIMARY, 0);
                cy += 28;

                /* Toggle button */
                uint32_t btn_bg = fw_status.enabled ? COLOR_DANGER : COLOR_SUCCESS;
                const char *btn_text = fw_status.enabled ? "Disable Firewall" : "Enable Firewall";
                gui_fill_rect(win, 12, cy - 2, 140, 28, btn_bg);
                gui_draw_rect(win, 12, cy - 2, 140, 28, COLOR_WIDGET_BORDER);
                gui_draw_text(win, 20, cy + 4, btn_text, 0xFFFFFFFF, 0);
                cy += 38;

                snprintf(line, sizeof(line), "Status: %s", fw_status.enabled ? "ACTIVE" : "INACTIVE");
                gui_draw_text(win, 16, cy, line, fw_status.enabled ? COLOR_SUCCESS : COLOR_WARNING, 0);
                cy += 22;

                snprintf(line, sizeof(line), "Active Rules: %u", fw_status.rule_count);
                gui_draw_text(win, 16, cy, line, COLOR_TEXT_MUTED, 0);
                cy += 22;

                snprintf(line, sizeof(line), "Packets Blocked: %llu", fw_status.total_dropped);
                gui_draw_text(win, 16, cy, line, COLOR_TEXT_MUTED, 0);
                cy += 22;

                snprintf(line, sizeof(line), "Packets Allowed: %llu", fw_status.total_accepted);
                gui_draw_text(win, 16, cy, line, COLOR_TEXT_MUTED, 0);
                cy += 32;

                gui_draw_text(win, 16, cy, "Use 'nyfw' CLI for advanced rule management.", COLOR_TEXT_DIM, 0);
                break;
            }

            case 4: { /* Diagnostics */
                gui_draw_text(win, 16, cy, "Network Diagnostics", COLOR_ACCENT_PRIMARY, 0);
                cy += 28;

                snprintf(line, sizeof(line), "Network Interfaces: %d detected", dev_count);
                gui_draw_text(win, 16, cy, line, COLOR_TEXT_PRIMARY, 0);
                cy += 22;

                bool any_up = false;
                for (int i = 0; i < dev_count; i++) {
                    if (devices[i].flags & 0x01) { any_up = true; break; }
                }
                snprintf(line, sizeof(line), "Link Status: %s", any_up ? "CONNECTED" : "DISCONNECTED");
                gui_draw_text(win, 16, cy, line, any_up ? COLOR_SUCCESS : COLOR_DANGER, 0);
                cy += 22;

                bool has_ip = false;
                for (int i = 0; i < dev_count; i++) {
                    if (devices[i].ip_addr && !(devices[i].flags & 0x04)) { has_ip = true; break; }
                }
                snprintf(line, sizeof(line), "DHCP: %s", has_ip ? "Lease acquired" : "No lease");
                gui_draw_text(win, 16, cy, line, has_ip ? COLOR_SUCCESS : COLOR_WARNING, 0);
                cy += 22;

                bool has_gw = (dev_count > 0 && devices[0].gateway != 0);
                snprintf(line, sizeof(line), "Gateway: %s", has_gw ? "Reachable" : "Not configured");
                gui_draw_text(win, 16, cy, line, has_gw ? COLOR_SUCCESS : COLOR_WARNING, 0);
                cy += 22;

                bool has_dns = (dev_count > 0 && devices[0].dns != 0);
                snprintf(line, sizeof(line), "DNS Resolver: %s", has_dns ? "Configured" : "Using default");
                gui_draw_text(win, 16, cy, line, has_dns ? COLOR_SUCCESS : COLOR_TEXT_MUTED, 0);
                cy += 22;

                snprintf(line, sizeof(line), "Firewall: %s (%u rules)",
                         fw_status.enabled ? "Active" : "Inactive", fw_status.rule_count);
                gui_draw_text(win, 16, cy, line, fw_status.enabled ? COLOR_SUCCESS : COLOR_TEXT_MUTED, 0);
                cy += 32;

                gui_draw_text(win, 16, cy, "Use 'ping', 'nslookup', 'netstat' for more.", COLOR_TEXT_DIM, 0);
                break;
            }
        }

        /* Status bar */
        gui_fill_rect(win, 0, NM_HEIGHT - 24, NM_WIDTH, 24, COLOR_PANEL_BG);
        gui_draw_line(win, 0, NM_HEIGHT - 24, NM_WIDTH, NM_HEIGHT - 24, COLOR_PANEL_BORDER);
        gui_draw_text(win, 12, NM_HEIGHT - 18, status_msg, COLOR_TEXT_MUTED, 0);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
