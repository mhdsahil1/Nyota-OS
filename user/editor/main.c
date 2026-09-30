/* =============================================================================
 * Nyota OS — Native GUI Text Editor Application (/bin/editor)
 * Simple, responsive text editor with open, edit, save, and cursor tracking.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define EDIT_WIDTH      540
#define EDIT_HEIGHT     360
#define MAX_TEXT_LEN    4096

static char text_buffer[MAX_TEXT_LEN];
static int  text_len = 0;
static int  cursor_pos = 0;
static char file_path[128] = "/home/note.txt";
static char status_text[128] = "Ready";

static void save_file(void) {
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        snprintf(status_text, sizeof(status_text), "Error: Failed to save to %s", file_path);
        return;
    }
    int w = write(fd, text_buffer, text_len);
    close(fd);
    if (w >= 0) {
        snprintf(status_text, sizeof(status_text), "Saved %d bytes to %s", text_len, file_path);
    } else {
        snprintf(status_text, sizeof(status_text), "Write error occurred");
    }
}

static void open_file(void) {
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        snprintf(status_text, sizeof(status_text), "File not found: %s", file_path);
        return;
    }
    memset(text_buffer, 0, sizeof(text_buffer));
    text_len = read(fd, text_buffer, MAX_TEXT_LEN - 1);
    if (text_len < 0) text_len = 0;
    text_buffer[text_len] = '\0';
    cursor_pos = text_len;
    close(fd);
    snprintf(status_text, sizeof(status_text), "Loaded %d bytes from %s", text_len, file_path);
}

int main(int argc, char **argv) {
    if (argc > 1 && argv[1]) {
        strncpy(file_path, argv[1], sizeof(file_path) - 1);
        open_file();
    } else {
        strcpy(text_buffer, "Welcome to Nyota Text Editor!\nType here to edit notes and documents.\n");
        text_len = strlen(text_buffer);
        cursor_pos = text_len;
    }

    gui_window_t *win = gui_window_create(EDIT_WIDTH, EDIT_HEIGHT, "Text Editor - Nyota OS");
    if (!win) {
        printf("[EDITOR] Failed to create editor window\n");
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
                int mx = ev.mouse_x;
                int my = ev.mouse_y;

                /* Toolbar buttons */
                if (my >= 6 && my <= 32) {
                    if (mx >= 8 && mx <= 68) {
                        /* New */
                        memset(text_buffer, 0, sizeof(text_buffer));
                        text_len = 0;
                        cursor_pos = 0;
                        strcpy(status_text, "New file created");
                    } else if (mx >= 74 && mx <= 134) {
                        /* Open */
                        open_file();
                    } else if (mx >= 140 && mx <= 200) {
                        /* Save */
                        save_file();
                    }
                }
            }

            if (ev.type == EVENT_KEY_PRESS) {
                char c = ev.character;
                if (c == '\b') {
                    if (cursor_pos > 0 && text_len > 0) {
                        memmove(&text_buffer[cursor_pos - 1], &text_buffer[cursor_pos], text_len - cursor_pos + 1);
                        cursor_pos--;
                        text_len--;
                    }
                } else if (c == '\r' || c == '\n') {
                    if (text_len < MAX_TEXT_LEN - 2) {
                        memmove(&text_buffer[cursor_pos + 1], &text_buffer[cursor_pos], text_len - cursor_pos + 1);
                        text_buffer[cursor_pos] = '\n';
                        cursor_pos++;
                        text_len++;
                    }
                } else if (c >= 32 && c <= 126) {
                    if (text_len < MAX_TEXT_LEN - 2) {
                        memmove(&text_buffer[cursor_pos + 1], &text_buffer[cursor_pos], text_len - cursor_pos + 1);
                        text_buffer[cursor_pos] = c;
                        cursor_pos++;
                        text_len++;
                    }
                }
            }
        }

        /* ── Render Text Editor ───────────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, EDIT_WIDTH, EDIT_HEIGHT, COLOR_WINDOW_BG);

        /* Toolbar */
        gui_fill_rect(win, 0, 0, EDIT_WIDTH, 38, COLOR_PANEL_BG);
        gui_draw_line(win, 0, 38, EDIT_WIDTH, 38, COLOR_PANEL_BORDER);

        gui_fill_rect(win, 8, 6, 60, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 8, 6, 60, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 20, 11, "New", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 74, 6, 60, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 74, 6, 60, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 84, 11, "Open", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 140, 6, 60, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 140, 6, 60, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 150, 11, "Save", COLOR_SUCCESS, 0);

        /* Path info */
        gui_fill_rect(win, 210, 6, EDIT_WIDTH - 218, 26, COLOR_TEXTBOX_BG);
        gui_draw_rect(win, 210, 6, EDIT_WIDTH - 218, 26, COLOR_TEXTBOX_BORDER);
        gui_draw_text(win, 218, 11, file_path, COLOR_TEXT_MUTED, 0);

        /* Text Area */
        gui_fill_rect(win, 8, 46, EDIT_WIDTH - 16, EDIT_HEIGHT - 78, COLOR_TEXTBOX_BG);
        gui_draw_rect(win, 8, 46, EDIT_WIDTH - 16, EDIT_HEIGHT - 78, COLOR_TEXTBOX_BORDER);

        /* Draw Text Lines and calculate cursor x, y */
        int cur_x = 16;
        int cur_y = 54;
        int cur_line = 1;
        int cur_col = 1;
        int target_cursor_x = cur_x;
        int target_cursor_y = cur_y;

        for (int i = 0; i <= text_len; i++) {
            if (i == cursor_pos) {
                target_cursor_x = cur_x;
                target_cursor_y = cur_y;
            }
            if (i == text_len) break;

            char c = text_buffer[i];
            if (c == '\n') {
                cur_x = 16;
                cur_y += 18;
                if (i < cursor_pos) {
                    cur_line++;
                    cur_col = 1;
                }
                continue;
            }

            if (cur_y + 16 < EDIT_HEIGHT - 35) {
                gui_draw_char(win, cur_x, cur_y, c, COLOR_TEXT_PRIMARY, 0);
            }
            cur_x += 8;
            if (i < cursor_pos) cur_col++;
        }

        /* Draw Blinking/Solid Cursor */
        gui_fill_rect(win, target_cursor_x, target_cursor_y, 2, 16, COLOR_ACCENT_PRIMARY);

        /* Status Bar */
        gui_fill_rect(win, 0, EDIT_HEIGHT - 26, EDIT_WIDTH, 26, COLOR_PANEL_BG);
        gui_draw_line(win, 0, EDIT_HEIGHT - 26, EDIT_WIDTH, EDIT_HEIGHT - 26, COLOR_PANEL_BORDER);

        char pos_str[32];
        snprintf(pos_str, sizeof(pos_str), "Ln %d, Col %d", cur_line, cur_col);
        gui_draw_text(win, 12, EDIT_HEIGHT - 20, pos_str, COLOR_ACCENT_PRIMARY, 0);
        gui_draw_text(win, 140, EDIT_HEIGHT - 20, status_text, COLOR_TEXT_MUTED, 0);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
