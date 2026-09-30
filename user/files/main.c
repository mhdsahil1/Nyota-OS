/* =============================================================================
 * Nyota OS — Native GUI File Manager Application (/bin/files)
 * Browse directories, inspect files, navigate filesystem hierarchy using VFS.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define FM_WIDTH        560
#define FM_HEIGHT       380

typedef struct {
    char name[64];
    uint32_t size;
    bool is_dir;
} file_item_t;

static char current_path[128] = "/";
static file_item_t items[32];
static int item_count = 0;
static int selected_idx = -1;
static char status_msg[128] = "Ready";

static void load_directory(const char *path) {
    item_count = 0;
    selected_idx = -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(status_msg, sizeof(status_msg), "Failed to open directory: %s", path);
        return;
    }

    dirent_t dent;
    while (getdents(fd, &dent) > 0 && item_count < 32) {
        if (strcmp(dent.name, ".") == 0 || strcmp(dent.name, "..") == 0) continue;

        strncpy(items[item_count].name, dent.name, 63);
        items[item_count].is_dir = (dent.type == 2); /* 2 = DIR */

        /* Stat file size */
        char full_path[256];
        if (strcmp(path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", dent.name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, dent.name);
        }

        stat_t st;
        if (stat(full_path, &st) == 0) {
            items[item_count].size = (uint32_t)st.size;
            if (st.mode & 0040000) items[item_count].is_dir = true;
        } else {
            items[item_count].size = 0;
        }

        item_count++;
    }
    close(fd);

    snprintf(status_msg, sizeof(status_msg), "%d items in %s", item_count, path);
}

static void navigate_back(void) {
    if (strcmp(current_path, "/") == 0) return;
    char *last_slash = strrchr(current_path, '/');
    if (last_slash == current_path) {
        current_path[1] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    }
    load_directory(current_path);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(FM_WIDTH, FM_HEIGHT, "Files - Nyota VFS");
    if (!win) {
        printf("[FILES] Failed to create window\n");
        return 1;
    }

    load_directory(current_path);

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

                /* Navigation buttons at y: 8..34 */
                if (my >= 8 && my <= 34) {
                    if (mx >= 8 && mx <= 70) {
                        navigate_back();
                    } else if (mx >= 76 && mx <= 138) {
                        strcpy(current_path, "/");
                        load_directory(current_path);
                    } else if (mx >= 144 && mx <= 214) {
                        load_directory(current_path);
                    }
                } else if (my >= 70 && my < 340) {
                    /* Click in file list */
                    int idx = (my - 70) / 24;
                    if (idx >= 0 && idx < item_count) {
                        if (selected_idx == idx && items[idx].is_dir) {
                            /* Open directory */
                            if (strcmp(current_path, "/") == 0) {
                                snprintf(current_path, sizeof(current_path), "/%s", items[idx].name);
                            } else {
                                snprintf(current_path, sizeof(current_path), "%s/%s", current_path, items[idx].name);
                            }
                            load_directory(current_path);
                        } else {
                            selected_idx = idx;
                            snprintf(status_msg, sizeof(status_msg), "Selected: %s (%u bytes)", items[idx].name, items[idx].size);
                        }
                    }
                }
            }
        }

        /* ── Render File Manager Window ───────────────────────────────────── */
        gui_fill_rect(win, 0, 0, FM_WIDTH, FM_HEIGHT, COLOR_WINDOW_BG);

        /* Top Toolbar */
        gui_fill_rect(win, 0, 0, FM_WIDTH, 42, COLOR_PANEL_BG);
        gui_draw_line(win, 0, 42, FM_WIDTH, 42, COLOR_PANEL_BORDER);

        /* Navigation Buttons */
        gui_fill_rect(win, 8, 8, 62, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 8, 8, 62, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 18, 14, "< Back", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 76, 8, 62, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 76, 8, 62, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 88, 14, "/ Root", COLOR_TEXT_PRIMARY, 0);

        gui_fill_rect(win, 144, 8, 70, 26, COLOR_BUTTON_NORMAL);
        gui_draw_rect(win, 144, 8, 70, 26, COLOR_WINDOW_BORDER);
        gui_draw_text(win, 154, 14, "Refresh", COLOR_TEXT_PRIMARY, 0);

        /* Path Bar */
        gui_fill_rect(win, 222, 8, FM_WIDTH - 230, 26, COLOR_TEXTBOX_BG);
        gui_draw_rect(win, 222, 8, FM_WIDTH - 230, 26, COLOR_TEXTBOX_BORDER);
        gui_draw_text(win, 230, 14, current_path, COLOR_ACCENT_PRIMARY, 0);

        /* List Headers */
        gui_fill_rect(win, 8, 48, FM_WIDTH - 16, 20, COLOR_PANEL_BG);
        gui_draw_text(win, 16, 50, "Type", COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 80, 50, "Name", COLOR_TEXT_MUTED, 0);
        gui_draw_text(win, 380, 50, "Size", COLOR_TEXT_MUTED, 0);

        /* File List Items */
        for (int i = 0; i < item_count; i++) {
            int iy = 70 + i * 24;
            if (iy + 24 > FM_HEIGHT - 30) break;

            if (i == selected_idx) {
                gui_fill_rect(win, 8, iy, FM_WIDTH - 16, 22, COLOR_SELECTION);
            }

            if (items[i].is_dir) {
                gui_draw_text(win, 16, iy + 4, "[DIR]", COLOR_WARNING, 0);
                gui_draw_text(win, 80, iy + 4, items[i].name, COLOR_TITLE_ACTIVE, 0);
                gui_draw_text(win, 380, iy + 4, "<DIR>", COLOR_TEXT_DIM, 0);
            } else {
                gui_draw_text(win, 16, iy + 4, "[FILE]", COLOR_ACCENT_PRIMARY, 0);
                gui_draw_text(win, 80, iy + 4, items[i].name, COLOR_TEXT_PRIMARY, 0);

                char sz_str[32];
                snprintf(sz_str, sizeof(sz_str), "%u B", items[i].size);
                gui_draw_text(win, 380, iy + 4, sz_str, COLOR_TEXT_MUTED, 0);
            }
        }

        /* Bottom Status Bar */
        gui_fill_rect(win, 0, FM_HEIGHT - 26, FM_WIDTH, 26, COLOR_PANEL_BG);
        gui_draw_line(win, 0, FM_HEIGHT - 26, FM_WIDTH, FM_HEIGHT - 26, COLOR_PANEL_BORDER);
        gui_draw_text(win, 12, FM_HEIGHT - 20, status_msg, COLOR_TEXT_MUTED, 0);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
