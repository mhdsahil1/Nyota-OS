/* =============================================================================
 * Nyota OS — Native GUI Task Manager Application (/bin/taskmgr)
 * Interactive process viewer with kill, priority display, and memory stats.
 * Distinct from sysmon: focused on per-process management and control.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define TM_WIDTH    560
#define TM_HEIGHT   420

static proc_info_t procs[32];
static int proc_count = 0;
static int selected_pid = -1;
static int selected_row = -1;
static char status_msg[128] = "Select a process to manage";
static int scroll_offset = 0;

#define VISIBLE_ROWS 12
#define ROW_HEIGHT   20
#define TABLE_Y      80
#define TABLE_BOTTOM (TABLE_Y + VISIBLE_ROWS * ROW_HEIGHT)

static void refresh_processes(void) {
    proc_count = getprocs(procs, 32);
    if (proc_count < 0) proc_count = 0;
}

static void kill_selected(void) {
    if (selected_pid <= 0) {
        strncpy(status_msg, "No process selected", sizeof(status_msg));
        return;
    }

    /* Prevent killing critical system processes (PID 1, 2) */
    if (selected_pid <= 2) {
        snprintf(status_msg, sizeof(status_msg), "Cannot kill system process (PID %d)", selected_pid);
        return;
    }

    int ret = kill(selected_pid, 9); /* SIGKILL */
    if (ret == 0) {
        snprintf(status_msg, sizeof(status_msg), "Sent SIGKILL to PID %d", selected_pid);
        selected_pid = -1;
        selected_row = -1;
    } else {
        snprintf(status_msg, sizeof(status_msg), "Failed to kill PID %d (error %d)", selected_pid, ret);
    }
    refresh_processes();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(TM_WIDTH, TM_HEIGHT, "Task Manager - Nyota OS");
    if (!win) {
        printf("[TASKMGR] Failed to create window\n");
        return 1;
    }

    refresh_processes();

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

                /* Toolbar buttons */
                if (my >= 10 && my <= 40) {
                    if (mx >= 12 && mx <= 92) {
                        /* Refresh */
                        refresh_processes();
                        strncpy(status_msg, "Process list refreshed", sizeof(status_msg));
                    } else if (mx >= 100 && mx <= 200) {
                        /* End Task */
                        kill_selected();
                    }
                }

                /* Process table rows */
                if (my >= TABLE_Y && my < TABLE_BOTTOM) {
                    int row = (my - TABLE_Y) / ROW_HEIGHT;
                    int idx = row + scroll_offset;
                    if (idx >= 0 && idx < proc_count) {
                        selected_row = idx;
                        selected_pid = (int)procs[idx].pid;
                        snprintf(status_msg, sizeof(status_msg), "Selected: %s (PID %u)", procs[idx].name, procs[idx].pid);
                    }
                }

                /* Scroll buttons */
                if (mx >= TM_WIDTH - 30 && mx <= TM_WIDTH - 10) {
                    if (my >= TABLE_Y && my <= TABLE_Y + 20) {
                        /* Scroll up */
                        if (scroll_offset > 0) scroll_offset--;
                    } else if (my >= TABLE_BOTTOM - 22 && my <= TABLE_BOTTOM) {
                        /* Scroll down */
                        if (scroll_offset + VISIBLE_ROWS < proc_count) scroll_offset++;
                    }
                }
            }

            if (ev.type == EVENT_KEY_PRESS) {
                if (ev.character == 'k' || ev.character == 'K') {
                    kill_selected();
                } else if (ev.character == 'r' || ev.character == 'R') {
                    refresh_processes();
                    strncpy(status_msg, "Refreshed", sizeof(status_msg));
                }
            }
        }

        /* Auto-refresh every ~2 seconds (at 30ms poll = ~66 frames) */
        if (++refresh_counter >= 66) {
            refresh_processes();
            refresh_counter = 0;
        }

        /* ── Render Task Manager ───────────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, TM_WIDTH, TM_HEIGHT, COLOR_WINDOW_BG);

        /* Toolbar */
        gui_fill_rect(win, 0, 0, TM_WIDTH, 50, COLOR_PANEL_BG);
        gui_draw_line(win, 0, 50, TM_WIDTH, 50, COLOR_PANEL_BORDER);

        /* Refresh Button */
        gui_fill_rect(win, 12, 10, 80, 30, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 12, 10, 80, 30, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 20, 17, "Refresh", COLOR_TEXT_PRIMARY, 0);

        /* End Task Button */
        uint32_t kill_bg = (selected_pid > 0) ? COLOR_DANGER : COLOR_BUTTON_NORMAL;
        gui_fill_rect(win, 100, 10, 100, 30, kill_bg);
        gui_draw_rect(win, 100, 10, 100, 30, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 112, 17, "End Task", (selected_pid > 0) ? 0xFFFFFFFF : COLOR_TEXT_DIM, 0);

        /* Process count summary */
        char summary[64];
        sysinfo_data_t si;
        memset(&si, 0, sizeof(si));
        sysinfo(&si);
        snprintf(summary, sizeof(summary), "Processes: %d  |  Total: %u", proc_count, si.process_count);
        gui_draw_text(win, 220, 17, summary, COLOR_TEXT_MUTED, 0);

        /* Column headers */
        gui_fill_rect(win, 8, 56, TM_WIDTH - 16, 20, COLOR_WIDGET_BG);
        gui_draw_text(win, 16,  58, "PID",   COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 70,  58, "PPID",  COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 130, 58, "UID",   COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 180, 58, "STATE", COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 300, 58, "PROCESS NAME", COLOR_TEXT_MUTED, 0);

        /* Process table */
        gui_fill_rect(win, 8, TABLE_Y, TM_WIDTH - 16, VISIBLE_ROWS * ROW_HEIGHT, COLOR_TEXTBOX_BG);
        gui_draw_rect(win, 8, TABLE_Y, TM_WIDTH - 16, VISIBLE_ROWS * ROW_HEIGHT, COLOR_WIDGET_BORDER);

        for (int i = 0; i < VISIBLE_ROWS && (i + scroll_offset) < proc_count; i++) {
            int idx = i + scroll_offset;
            int row_y = TABLE_Y + i * ROW_HEIGHT;

            /* Highlight selected row */
            if (idx == selected_row) {
                gui_fill_rect(win, 10, row_y, TM_WIDTH - 20, ROW_HEIGHT, COLOR_SELECTION);
            }

            /* Alternating subtle row background */
            if (idx != selected_row && (i % 2 == 1)) {
                gui_fill_rect(win, 10, row_y, TM_WIDTH - 20, ROW_HEIGHT, 0xFF1A1A2E);
            }

            char buf[16];
            snprintf(buf, sizeof(buf), "%u", procs[idx].pid);
            gui_draw_text(win, 16, row_y + 2, buf, COLOR_TEXT_PRIMARY, 0);

            snprintf(buf, sizeof(buf), "%u", procs[idx].ppid);
            gui_draw_text(win, 70, row_y + 2, buf, COLOR_TEXT_MUTED, 0);

            snprintf(buf, sizeof(buf), "%u", procs[idx].uid);
            gui_draw_text(win, 130, row_y + 2, buf, COLOR_TEXT_MUTED, 0);

            /* State with color coding */
            const char *st = procs[idx].state;
            uint32_t st_col = COLOR_SUCCESS;
            if (strcmp(st, "SLEEP") == 0 || strcmp(st, "SLEEPING") == 0) st_col = COLOR_ACCENT_PRIMARY;
            else if (strcmp(st, "ZOMBIE") == 0) st_col = COLOR_DANGER;
            else if (strcmp(st, "STOPPED") == 0) st_col = COLOR_WARNING;
            gui_draw_text(win, 180, row_y + 2, st, st_col, 0);

            gui_draw_text(win, 300, row_y + 2, procs[idx].name, COLOR_TITLE_ACTIVE, 0);
        }

        /* Scroll indicators */
        if (scroll_offset > 0) {
            gui_fill_rect(win, TM_WIDTH - 28, TABLE_Y + 2, 16, 16, COLOR_BUTTON_NORMAL);
            gui_draw_text(win, TM_WIDTH - 26, TABLE_Y + 2, "^", COLOR_TEXT_PRIMARY, 0);
        }
        if (scroll_offset + VISIBLE_ROWS < proc_count) {
            gui_fill_rect(win, TM_WIDTH - 28, TABLE_BOTTOM - 18, 16, 16, COLOR_BUTTON_NORMAL);
            gui_draw_text(win, TM_WIDTH - 26, TABLE_BOTTOM - 18, "v", COLOR_TEXT_PRIMARY, 0);
        }

        /* Memory summary bar */
        int bar_y = TABLE_BOTTOM + 8;
        gui_fill_rect(win, 8, bar_y, TM_WIDTH - 16, 40, COLOR_WIDGET_BG);
        gui_draw_rect(win, 8, bar_y, TM_WIDTH - 16, 40, COLOR_WIDGET_BORDER);

        uint64_t total_mb = si.total_ram / (1024 * 1024);
        if (total_mb == 0) total_mb = 128;
        uint64_t free_mb = si.free_ram / (1024 * 1024);
        uint64_t used_mb = (total_mb > free_mb) ? (total_mb - free_mb) : 16;
        uint32_t pct = (uint32_t)((used_mb * 100) / total_mb);

        char mem_line[80];
        snprintf(mem_line, sizeof(mem_line), "Memory: %u MB / %u MB (%u%%)", (uint32_t)used_mb, (uint32_t)total_mb, pct);
        gui_draw_text(win, 16, bar_y + 4, mem_line, COLOR_TITLE_ACTIVE, 0);

        int bar_w = TM_WIDTH - 32;
        gui_fill_rect(win, 16, bar_y + 22, bar_w, 10, COLOR_TEXTBOX_BG);
        uint32_t fill_w = (bar_w - 4) * pct / 100;
        uint32_t bar_col = (pct < 70) ? COLOR_SUCCESS : ((pct < 90) ? COLOR_WARNING : COLOR_DANGER);
        gui_fill_rect(win, 18, bar_y + 24, fill_w, 6, bar_col);

        /* Status bar */
        gui_fill_rect(win, 0, TM_HEIGHT - 24, TM_WIDTH, 24, COLOR_PANEL_BG);
        gui_draw_line(win, 0, TM_HEIGHT - 24, TM_WIDTH, TM_HEIGHT - 24, COLOR_PANEL_BORDER);
        gui_draw_text(win, 12, TM_HEIGHT - 18, status_msg, COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, TM_WIDTH - 140, TM_HEIGHT - 18, "[K] Kill  [R] Refresh", COLOR_TEXT_DIM, 0);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
