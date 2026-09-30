/* =============================================================================
 * Nyota OS — Native GUI Settings Application (/bin/settings)
 * System preferences, appearance themes, display mode, input devices, and networking.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define SETTINGS_WIDTH   480
#define SETTINGS_HEIGHT  360

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

            case 3: /* Network */
                gui_draw_text(win, content_x, content_y, "Interface : eth0 [Intel 82540EM (e1000)]", COLOR_TEXT_PRIMARY, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "IP Address: 10.0.2.15 / 255.255.255.0", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Gateway   : 10.0.2.2", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "MAC       : 52:54:00:12:34:56", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Status    : UP, BROADCAST, RUNNING", COLOR_SUCCESS, 0);
                break;

            case 4: /* System */
                gui_draw_text(win, content_x, content_y, "Nyota OS Release 1.0.0", COLOR_TITLE_ACTIVE, 0);
                content_y += 24;
                gui_draw_text(win, content_x, content_y, "Kernel Architecture : x86_64 Long Mode", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Security Model      : Ring 0/3 Separation", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Filesystem          : NyotaFS on ATA Storage", COLOR_TEXT_MUTED, 0);
                content_y += 20;
                gui_draw_text(win, content_x, content_y, "Multitasking        : Preemptive Round-Robin", COLOR_TEXT_MUTED, 0);
                break;
        }

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
