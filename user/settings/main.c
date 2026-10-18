/* =============================================================================
 * Nyota OS — Native GUI Settings Application (/bin/settings)
 * System preferences, appearance themes, display mode, input devices, and networking.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define SETTINGS_WIDTH   520
#define SETTINGS_HEIGHT  380

static const char *tabs[] = {
    "Appearance",
    "Display",
    "Input Devices",
    "Network",
    "System"
};
#define TAB_COUNT (sizeof(tabs) / sizeof(tabs[0]))

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(SETTINGS_WIDTH, SETTINGS_HEIGHT, "Settings - Nyota OS");
    if (!win) {
        printf("[SETTINGS] Failed to create window\n");
        return 1;
    }

    int active_tab = 0;

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
                break;
            }

            if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {
                if (ev.mouse_x >= 8 && ev.mouse_x <= 138) {
                    int clicked_tab = (ev.mouse_y - 12) / 32;
                    if (clicked_tab >= 0 && clicked_tab < (int)TAB_COUNT) {
                        active_tab = clicked_tab;
                    }
                }
            }
        }

        /* ── Render Settings Window ───────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, SETTINGS_WIDTH, SETTINGS_HEIGHT, COLOR_WINDOW_BG);

        /* Sidebar */
        gui_fill_rect(win, 0, 0, 140, SETTINGS_HEIGHT, COLOR_PANEL_BG);
        gui_draw_line(win, 140, 0, 140, SETTINGS_HEIGHT, COLOR_PANEL_BORDER);

        for (int i = 0; i < (int)TAB_COUNT; i++) {
            int ty = 12 + i * 32;
            if (i == active_tab) {
                gui_fill_rect(win, 6, ty, 128, 26, COLOR_BUTTON_PRESSED);
                gui_draw_rect(win, 6, ty, 128, 26, COLOR_ACCENT_PRIMARY);
                gui_draw_text(win, 16, ty + 5, tabs[i], COLOR_TITLE_ACTIVE, 0);
            } else {
                gui_draw_text(win, 16, ty + 5, tabs[i], COLOR_TEXT_MUTED, 0);
            }
        }

        /* Detail Panel */
        int content_x = 156;
        int content_y = 20;

        gui_draw_text(win, content_x, content_y, tabs[active_tab], COLOR_ACCENT_PRIMARY, 0);
        gui_draw_line(win, content_x, content_y + 22, SETTINGS_WIDTH - 20, content_y + 22, COLOR_WIDGET_BORDER);

        content_y += 36;

        switch (active_tab) {
            case 0: /* Appearance */
                gui_draw_text(win, content_x, content_y, "Theme: Nyota Space Dark (Active)", COLOR_TEXT_PRIMARY, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Accent Colors:", COLOR_TEXT_MUTED, 0);
                content_y += 20;

                /* Color swatches */
                gui_fill_rect(win, content_x, content_y, 24, 24, COLOR_ACCENT_PRIMARY);
                gui_fill_rect(win, content_x + 32, content_y, 24, 24, COLOR_ACCENT_PURPLE);
                gui_fill_rect(win, content_x + 64, content_y, 24, 24, COLOR_SUCCESS);
                gui_fill_rect(win, content_x + 96, content_y, 24, 24, COLOR_WARNING);
                gui_fill_rect(win, content_x + 128, content_y, 24, 24, COLOR_DANGER);

                content_y += 40;
                gui_draw_text(win, content_x, content_y, "Wallpaper: Deep Navy Space Gradient", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Font: Fixed 8x16 Monospace System Bitmap", COLOR_TEXT_MUTED, 0);
                break;

            case 1: /* Display */
                gui_draw_text(win, content_x, content_y, "Display Mode: 1024 x 768 @ 60 Hz", COLOR_TEXT_PRIMARY, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Color Depth : 32 bits per pixel (ARGB8888)", COLOR_TEXT_MUTED, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Hardware    : Bochs VBE Graphics Adapter", COLOR_TEXT_MUTED, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Compositor  : Software Double-Buffered Flip", COLOR_TEXT_MUTED, 0);
                break;

            case 2: /* Input Devices */
                gui_draw_text(win, content_x, content_y, "Keyboard: PS/2 Controller (IRQ 1)", COLOR_TEXT_PRIMARY, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Layout  : US Standard QWERTY", COLOR_TEXT_MUTED, 0);
                content_y += 32;
                gui_draw_text(win, content_x, content_y, "Mouse   : PS/2 Auxiliary Device (IRQ 12)", COLOR_TEXT_PRIMARY, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Protocol: Standard 3-Byte Packet Streaming", COLOR_TEXT_MUTED, 0);
                break;

            case 3: { /* Network */
                size_t if_count = (size_t)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);
                netdev_info_t dev_info;
                memset(&dev_info, 0, sizeof(dev_info));

                /* Find primary non-loopback device or first device */
                for (size_t i = 0; i < if_count; i++) {
                    netdev_info_t temp;
                    if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&temp) == 0) {
                        if (!(temp.flags & 0x04) || if_count == 1) {
                            memcpy(&dev_info, &temp, sizeof(dev_info));
                            break;
                        }
                    }
                }

                char line[96];
                snprintf(line, sizeof(line), "Interface : %s (e1000/Intel Gigabit)", dev_info.name[0] ? dev_info.name : "eth0");
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_PRIMARY, 0);
                content_y += 18;

                const char *hex_digits = "0123456789abcdef";
                char mac_str[24];
                snprintf(mac_str, sizeof(mac_str), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                         hex_digits[(dev_info.mac[0] >> 4) & 0xF], hex_digits[dev_info.mac[0] & 0xF],
                         hex_digits[(dev_info.mac[1] >> 4) & 0xF], hex_digits[dev_info.mac[1] & 0xF],
                         hex_digits[(dev_info.mac[2] >> 4) & 0xF], hex_digits[dev_info.mac[2] & 0xF],
                         hex_digits[(dev_info.mac[3] >> 4) & 0xF], hex_digits[dev_info.mac[3] & 0xF],
                         hex_digits[(dev_info.mac[4] >> 4) & 0xF], hex_digits[dev_info.mac[4] & 0xF],
                         hex_digits[(dev_info.mac[5] >> 4) & 0xF], hex_digits[dev_info.mac[5] & 0xF]);
                snprintf(line, sizeof(line), "MAC Addr  : %s", mac_str);
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                struct in_addr in;
                in.s_addr = dev_info.ip_addr;
                char ip_str[32];
                strncpy(ip_str, inet_ntoa(in), sizeof(ip_str) - 1);
                in.s_addr = dev_info.netmask;
                snprintf(line, sizeof(line), "IPv4 Addr : %s / %s", ip_str, inet_ntoa(in));
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                if (dev_info.has_ip6) {
                    char ip6_str[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, dev_info.ip6_addr, ip6_str, sizeof(ip6_str));
                    snprintf(line, sizeof(line), "IPv6 Addr : %s", ip6_str);
                } else {
                    snprintf(line, sizeof(line), "IPv6 Addr : none");
                }
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                in.s_addr = dev_info.gateway;
                snprintf(line, sizeof(line), "Gateway   : %s", dev_info.gateway ? inet_ntoa(in) : "none");
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                in.s_addr = dev_info.dns;
                snprintf(line, sizeof(line), "DNS Server: %s", dev_info.dns ? inet_ntoa(in) : "10.0.2.3");
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                snprintf(line, sizeof(line), "Traffic   : RX %llu pkts (%llu B)  TX %llu pkts",
                         dev_info.rx_packets, dev_info.rx_bytes, dev_info.tx_packets);
                gui_draw_text(win, content_x, content_y, line, COLOR_TEXT_MUTED, 0);
                content_y += 18;

                firewall_status_t fw_st;
                memset(&fw_st, 0, sizeof(fw_st));
                sys_firewall_op(FW_CMD_GET_STATUS, (uint64_t)&fw_st, 0, 0);
                snprintf(line, sizeof(line), "Firewall  : %s (%u rules active)",
                         fw_st.enabled ? "ENABLED" : "DISABLED", fw_st.rule_count);
                gui_draw_text(win, content_x, content_y, line, fw_st.enabled ? COLOR_SUCCESS : COLOR_WARNING, 0);
                content_y += 18;

                gui_draw_text(win, content_x, content_y, "Link State: UP, BROADCAST, RUNNING", COLOR_SUCCESS, 0);
                break;
            }

            case 4: { /* System */
                gui_draw_text(win, content_x, content_y, "Nyota OS Release 2.0.0 (Stable Integrated Release)", COLOR_TITLE_ACTIVE, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Kernel Architecture : x86_64 Long Mode", COLOR_TEXT_MUTED, 0);
                content_y += 20;

                lib_vmm_info_t vmm_inf;
                memset(&vmm_inf, 0, sizeof(vmm_inf));
                char vmm_line[96];
                if (vmm_get_info(&vmm_inf) == 0 && vmm_inf.vendor != 0) {
                    snprintf(vmm_line, sizeof(vmm_line), "Virtualization      : %s (Active, %s)",
                             vmm_inf.vendor_name,
                             vmm_inf.has_nested_paging ? "EPT/NPT Paging" : "Direct Paging");
                } else {
                    snprintf(vmm_line, sizeof(vmm_line), "Virtualization      : Hardware-Assisted Hypervisor (Ready)");
                }
                gui_draw_text(win, content_x, content_y, vmm_line, COLOR_ACCENT_PRIMARY, 0);
                content_y += 20;

                gui_draw_text(win, content_x, content_y, "Network Security    : Packet Firewall (nyfw)", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Protocols Supported : IPv4, IPv6, ICMP/v6, TCP, UDP", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Filesystem          : NyotaFS on ATA/NVMe Storage", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Multitasking        : Preemptive SMP Round-Robin", COLOR_TEXT_MUTED, 0);
                break;
            }
        }

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
