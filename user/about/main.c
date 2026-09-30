/* =============================================================================
 * Nyota OS — Native GUI About Nyota Application (/bin/about)
 * System version, architectural breakdown, credits, and specifications.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define ABOUT_WIDTH     420
#define ABOUT_HEIGHT    280

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(ABOUT_WIDTH, ABOUT_HEIGHT, "About Nyota OS");
    if (!win) {
        printf("[ABOUT] Failed to create window\n");
        return 1;
    }

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
                break;
            }

            if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {
                /* Check Close button click */
                if (ev.mouse_x >= 160 && ev.mouse_x <= 260 &&
                    ev.mouse_y >= 230 && ev.mouse_y <= 260) {
                    win->should_close = true;
                    break;
                }
            }
        }

        /* ── Render About Nyota Window ────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, ABOUT_WIDTH, ABOUT_HEIGHT, COLOR_WINDOW_BG);

        /* Header Accent Card */
        gui_fill_rect(win, 12, 12, ABOUT_WIDTH - 24, 60, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, 12, ABOUT_WIDTH - 24, 60, COLOR_ACCENT_PRIMARY);

        gui_draw_text(win, 24, 22, "N Y O T A   O S", COLOR_ACCENT_PRIMARY, 0);
        gui_draw_text(win, 24, 42, "Version 1.0.0 (Release Milestone)", COLOR_TITLE_ACTIVE, 0);

        /* Subsystems Card */
        int cy = 84;
        gui_draw_text(win, 24, cy, "Architecture : x86_64 Long Mode", COLOR_TEXT_PRIMARY, 0);
        cy += 20;
        gui_draw_text(win, 24, cy, "Kernel       : Preemptive Scheduler & 4-Level Paging", COLOR_TEXT_MUTED, 0);
        cy += 20;
        gui_draw_text(win, 24, cy, "Security     : Ring 0/3 Separation & Capability Model", COLOR_TEXT_MUTED, 0);
        cy += 20;
        gui_draw_text(win, 24, cy, "Storage      : NyotaFS on ATA PIO Block Storage", COLOR_TEXT_MUTED, 0);
        cy += 20;
        gui_draw_text(win, 24, cy, "Networking   : TCP/IP Stack & Sockets (E1000 Driver)", COLOR_TEXT_MUTED, 0);
        cy += 20;
        gui_draw_text(win, 24, cy, "Interface    : Native Desktop Environment & Compositor", COLOR_SUCCESS, 0);

        /* Close Button */
        gui_fill_rect(win, 160, 230, 100, 30, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 160, 230, 100, 30, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 192, 237, "Close", COLOR_TITLE_ACTIVE, 0);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
