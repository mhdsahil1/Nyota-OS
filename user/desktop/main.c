/* =============================================================================
 * Nyota OS — Desktop Environment, Window Server & Compositor (/bin/desktop)
 * Window registration, Z-order management, damage compositing, panel, launcher.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"
#include "theme.h"
#include "drivers/font8x16.h"

#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       768
#define TASKBAR_HEIGHT      32
#define TITLEBAR_HEIGHT     26
#define MAX_WINDOWS         32

static uint32_t screen_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint32_t win_pixel_buf[SCREEN_WIDTH * SCREEN_HEIGHT];

/* Cursor Arrow Bitmap (12x18) */
static const uint8_t cursor_mask[18][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0}
};

static void draw_cursor(int32_t cx, int32_t cy) {
    for (int r = 0; r < 18; r++) {
        int py = cy + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < 12; c++) {
            int px = cx + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            uint8_t m = cursor_mask[r][c];
            if (m == 1) {
                screen_buffer[py * SCREEN_WIDTH + px] = 0xFF000000; /* Black outline */
            } else if (m == 2) {
                screen_buffer[py * SCREEN_WIDTH + px] = 0xFFFFFFFF; /* White body */
            }
        }
    }
}

static void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x < 0) { if ((int32_t)w <= -x) return; w += x; x = 0; }
    if (y < 0) { if ((int32_t)h <= -y) return; h += y; y = 0; }
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;

    for (uint32_t r = 0; r < h; r++) {
        uint32_t *row = &screen_buffer[(y + r) * SCREEN_WIDTH + x];
        for (uint32_t c = 0; c < w; c++) {
            row[c] = color;
        }
    }
}

static void draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg) {
    uint8_t glyph = (uint8_t)c;
    const uint8_t *bitmap = font8x16[glyph];
    for (int r = 0; r < 16; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        uint8_t line = bitmap[r];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            if (line & (0x80 >> col)) {
                screen_buffer[py * SCREEN_WIDTH + px] = fg;
            } else if (bg != 0) {
                screen_buffer[py * SCREEN_WIDTH + px] = bg;
            }
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg) {
    if (!str) return;
    int32_t cx = x;
    while (*str) {
        char c = *str++;
        if (c == '\n') {
            cx = x;
            y += 18;
            continue;
        }
        draw_char(cx, y, c, fg, bg);
        cx += 8;
    }
}

/* ── Wallpaper Background ─────────────────────────────────────────────────── */

static void render_wallpaper(void) {
    /* Gradient: Deep space #11111B to #1E1E2E */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        uint8_t r = 0x11 + (y * (0x1E - 0x11)) / SCREEN_HEIGHT;
        uint8_t g = 0x11 + (y * (0x1E - 0x11)) / SCREEN_HEIGHT;
        uint8_t b = 0x1B + (y * (0x2E - 0x1B)) / SCREEN_HEIGHT;
        uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;

        uint32_t *row = &screen_buffer[y * SCREEN_WIDTH];
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            row[x] = col;
        }
    }

    /* Centered Nyota OS Typography Watermark */
    int32_t banner_x = (SCREEN_WIDTH - 240) / 2;
    int32_t banner_y = (SCREEN_HEIGHT - 60) / 2;

    /* Subtle geometric accent box */
    draw_rect(banner_x - 30, banner_y - 20, 300, 100, 0xFF313244);
    fill_rect(banner_x - 28, banner_y - 18, 296, 96, 0x22181825);

    draw_text(banner_x + 36, banner_y, "N Y O T A   O S", COLOR_ACCENT_PRIMARY, 0);
    draw_text(banner_x + 12, banner_y + 24, "Version 1.0.0 - Desktop Environment", COLOR_TEXT_MUTED, 0);
    draw_text(banner_x + 48, banner_y + 44, "x86_64 Long Mode Kernel", COLOR_TEXT_DIM, 0);
}

/* ── Launcher Menu Items ─────────────────────────────────────────────────── */
typedef struct {
    const char *label;
    const char *binary;
} launcher_item_t;

static launcher_item_t launcher_items[] = {
    {"[>] Terminal",       "/bin/term"},
    {"[F] File Manager",   "/bin/files"},
    {"[E] Text Editor",    "/bin/editor"},
    {"[M] System Monitor", "/bin/sysmon"},
    {"[S] Settings",       "/bin/settings"},
    {"[?] About Nyota",    "/bin/about"},
    {"[X] Shutdown",       "SHUTDOWN"}
};
#define LAUNCHER_COUNT (sizeof(launcher_items) / sizeof(launcher_items[0]))

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* 1. Register as authoritative Window Server */
    if (win_server_op(WS_OP_REGISTER, NULL, NULL, NULL) < 0) {
        printf("[DESKTOP] Failed to register window server\n");
        return 1;
    }

    /* 2. Retrieve Framebuffer Information */
    fb_info_t fb;
    if (gfx_get_info(&fb) < 0) {
        printf("[DESKTOP] Graphics framebuffer not available\n");
        return 1;
    }

    printf("[DESKTOP] Window Server initialized (%dx%dx%d)\n", fb.width, fb.height, fb.bpp);

    /* Launch initial Welcome / About window */
    char *about_argv[] = {"/bin/about", NULL};
    spawn("/bin/about", about_argv);

    int32_t cursor_x = SCREEN_WIDTH / 2;
    int32_t cursor_y = SCREEN_HEIGHT / 2;

    bool launcher_open = false;
    uint32_t drag_win_id = 0;
    int32_t drag_off_x = 0;
    int32_t drag_off_y = 0;
    uint32_t next_z = 10;

    window_info_t win_list[MAX_WINDOWS];

    /* Main Desktop & Compositor Loop */
    while (1) {
        /* A. Process Input Events from Kernel */
        input_event_t ev;
        while (input_poll_event(&ev, 0) > 0) {
            if (ev.type == EVENT_MOUSE_MOVE) {
                cursor_x = ev.mouse_x;
                cursor_y = ev.mouse_y;

                if (cursor_x < 0) cursor_x = 0;
                if (cursor_x >= SCREEN_WIDTH) cursor_x = SCREEN_WIDTH - 1;
                if (cursor_y < 0) cursor_y = 0;
                if (cursor_y >= SCREEN_HEIGHT) cursor_y = SCREEN_HEIGHT - 1;

                if (drag_win_id > 0) {
                    /* Dragging window */
                    int32_t new_x = cursor_x - drag_off_x;
                    int32_t new_y = cursor_y - drag_off_y;
                    if (new_y < TASKBAR_HEIGHT) new_y = TASKBAR_HEIGHT;

                    uint64_t xy = ((uint64_t)(uint32_t)new_x << 32) | (uint32_t)new_y;
                    uint64_t prop = ((uint64_t)WIN_STATE_NORMAL << 48) | (1ULL << 32) | next_z;
                    win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)drag_win_id, (void *)xy, (void *)prop);
                }
            } else if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {

                /* 1. Check Taskbar Click */
                if (cursor_y < TASKBAR_HEIGHT) {
                    if (cursor_x >= 4 && cursor_x <= 84) {
                        /* Toggle Application Launcher */
                        launcher_open = !launcher_open;
                    } else {
                        /* Check taskbar window buttons */
                        int btn_x = 94;
                        int win_cnt = win_server_op(WS_OP_GET_WINDOWS, win_list, (void *)(uint64_t)MAX_WINDOWS, NULL);
                        for (int i = 0; i < win_cnt; i++) {
                            if (win_list[i].state == WIN_STATE_CLOSED) continue;
                            if (cursor_x >= btn_x && cursor_x < btn_x + 120) {
                                /* Restore or Focus window */
                                uint32_t new_state = (win_list[i].state == WIN_STATE_MINIMIZED) ? WIN_STATE_NORMAL : win_list[i].state;
                                uint64_t xy = ((uint64_t)(uint32_t)win_list[i].x << 32) | (uint32_t)win_list[i].y;
                                uint64_t prop = ((uint64_t)new_state << 48) | (1ULL << 32) | (++next_z);
                                win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)win_list[i].id, (void *)xy, (void *)prop);
                                break;
                            }
                            btn_x += 126;
                        }
                    }
                    continue;
                }

                /* 2. Check Launcher Menu Click */
                if (launcher_open && cursor_x >= 4 && cursor_x <= 184 &&
                    cursor_y >= TASKBAR_HEIGHT && cursor_y < TASKBAR_HEIGHT + (int32_t)(LAUNCHER_COUNT * 26 + 8)) {
                    int item_idx = (cursor_y - (TASKBAR_HEIGHT + 4)) / 26;
                    if (item_idx >= 0 && item_idx < (int)LAUNCHER_COUNT) {
                        launcher_open = false;
                        if (strcmp(launcher_items[item_idx].binary, "SHUTDOWN") == 0) {
                            reboot(REBOOT_CMD_POWEROFF);
                        } else {
                            char *sargv[] = {(char *)launcher_items[item_idx].binary, NULL};
                            spawn(launcher_items[item_idx].binary, sargv);
                        }
                    }
                    continue;
                }

                if (launcher_open) {
                    launcher_open = false;
                }

                /* 3. Check Window Hits in descending Z-order */
                int win_cnt = win_server_op(WS_OP_GET_WINDOWS, win_list, (void *)(uint64_t)MAX_WINDOWS, NULL);
                int hit_idx = -1;
                uint32_t best_z = 0;

                for (int i = 0; i < win_cnt; i++) {
                    if (win_list[i].state == WIN_STATE_MINIMIZED || win_list[i].state == WIN_STATE_CLOSED) continue;
                    int32_t wx = win_list[i].x;
                    int32_t wy = win_list[i].y;
                    int32_t ww = (int32_t)win_list[i].width;
                    int32_t wh = (int32_t)win_list[i].height + TITLEBAR_HEIGHT;

                    if (cursor_x >= wx && cursor_x < wx + ww && cursor_y >= wy && cursor_y < wy + wh) {
                        if (win_list[i].z_order >= best_z) {
                            best_z = win_list[i].z_order;
                            hit_idx = i;
                        }
                    }
                }

                if (hit_idx >= 0) {
                    window_info_t *hit = &win_list[hit_idx];
                    /* Bring to top */
                    uint64_t xy = ((uint64_t)(uint32_t)hit->x << 32) | (uint32_t)hit->y;
                    uint64_t prop = ((uint64_t)hit->state << 48) | (1ULL << 32) | (++next_z);
                    win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)hit->id, (void *)xy, (void *)prop);

                    /* Check Title Bar Buttons */
                    if (cursor_y < hit->y + TITLEBAR_HEIGHT) {
                        int32_t right = hit->x + (int32_t)hit->width;
                        if (cursor_x >= right - 22 && cursor_x <= right - 4) {
                            /* Close button [X] */
                            input_event_t close_ev = { .type = EVENT_WINDOW_CLOSE };
                            win_server_op(WS_OP_POST_EVENT, (void *)(uint64_t)hit->id, &close_ev, NULL);
                        } else if (cursor_x >= right - 42 && cursor_x <= right - 26) {
                            /* Maximize button [+] */
                            uint32_t new_state = (hit->state == WIN_STATE_MAXIMIZED) ? WIN_STATE_NORMAL : WIN_STATE_MAXIMIZED;
                            int32_t nx = (new_state == WIN_STATE_MAXIMIZED) ? 0 : 80;
                            int32_t ny = (new_state == WIN_STATE_MAXIMIZED) ? TASKBAR_HEIGHT : 60;
                            uint64_t nxy = ((uint64_t)(uint32_t)nx << 32) | (uint32_t)ny;
                            uint64_t nprop = ((uint64_t)new_state << 48) | (1ULL << 32) | (++next_z);
                            win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)hit->id, (void *)nxy, (void *)nprop);
                        } else if (cursor_x >= right - 62 && cursor_x <= right - 46) {
                            /* Minimize button [-] */
                            uint64_t nprop = ((uint64_t)WIN_STATE_MINIMIZED << 48) | (0ULL << 32) | hit->z_order;
                            win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)hit->id, (void *)xy, (void *)nprop);
                        } else {
                            /* Title Bar Drag */
                            drag_win_id = hit->id;
                            drag_off_x = cursor_x - hit->x;
                            drag_off_y = cursor_y - hit->y;
                        }
                    } else {
                        /* Client Area Click: translate to window local coordinates */
                        input_event_t client_ev = ev;
                        client_ev.mouse_x = cursor_x - hit->x;
                        client_ev.mouse_y = cursor_y - (hit->y + TITLEBAR_HEIGHT);
                        win_server_op(WS_OP_POST_EVENT, (void *)(uint64_t)hit->id, &client_ev, NULL);
                    }
                }
            } else if (ev.type == EVENT_MOUSE_BUTTON_RELEASE) {
                drag_win_id = 0;
            } else if (ev.type == EVENT_KEY_PRESS || ev.type == EVENT_KEY_RELEASE) {
                if (ev.type == EVENT_KEY_PRESS && (ev.modifiers & 2) &&
                    (ev.modifiers & 4) && (ev.character == 't' || ev.character == 'T')) {
                    char *term_argv[] = {"/bin/term", NULL};
                    spawn("/bin/term", term_argv);
                    continue;
                }

                /* Check Alt+Tab Shortcut to switch windows */
                if (ev.type == EVENT_KEY_PRESS && (ev.modifiers & 4) && ev.character == '\t') {
                    int win_cnt = win_server_op(WS_OP_GET_WINDOWS, win_list, (void *)(uint64_t)MAX_WINDOWS, NULL);
                    if (win_cnt > 1) {
                        /* Cycle focus to the next window */
                        for (int i = 0; i < win_cnt; i++) {
                            if (!win_list[i].focused && win_list[i].state != WIN_STATE_CLOSED) {
                                uint64_t xy = ((uint64_t)(uint32_t)win_list[i].x << 32) | (uint32_t)win_list[i].y;
                                uint64_t prop = ((uint64_t)WIN_STATE_NORMAL << 48) | (1ULL << 32) | (++next_z);
                                win_server_op(WS_OP_SET_WINDOW_PROP, (void *)(uint64_t)win_list[i].id, (void *)xy, (void *)prop);
                                break;
                            }
                        }
                    }
                    continue;
                }

                /* Forward Keystroke to focused window */
                int win_cnt = win_server_op(WS_OP_GET_WINDOWS, win_list, (void *)(uint64_t)MAX_WINDOWS, NULL);
                for (int i = 0; i < win_cnt; i++) {
                    if (win_list[i].focused && win_list[i].state != WIN_STATE_CLOSED) {
                        win_server_op(WS_OP_POST_EVENT, (void *)(uint64_t)win_list[i].id, &ev, NULL);
                        break;
                    }
                }
            }
        }

        /* B. Compositor Phase: Render Desktop & Windows to Back Buffer */

        /* 1. Wallpaper */
        render_wallpaper();

        /* 2. Retrieve Active Windows and sort by Z-order */
        int win_cnt = win_server_op(WS_OP_GET_WINDOWS, win_list, (void *)(uint64_t)MAX_WINDOWS, NULL);
        for (int i = 0; i < win_cnt - 1; i++) {
            for (int j = 0; j < win_cnt - i - 1; j++) {
                if (win_list[j].z_order > win_list[j + 1].z_order) {
                    window_info_t tmp = win_list[j];
                    win_list[j] = win_list[j + 1];
                    win_list[j + 1] = tmp;
                }
            }
        }

        /* 3. Render Windows in Z-Order */
        for (int i = 0; i < win_cnt; i++) {
            window_info_t *w = &win_list[i];
            if (w->state == WIN_STATE_MINIMIZED || w->state == WIN_STATE_CLOSED) continue;

            int32_t wx = w->x;
            int32_t wy = w->y;
            uint32_t ww = (w->state == WIN_STATE_MAXIMIZED) ? SCREEN_WIDTH : w->width;
            uint32_t wh = (w->state == WIN_STATE_MAXIMIZED) ? (SCREEN_HEIGHT - TASKBAR_HEIGHT - TITLEBAR_HEIGHT) : w->height;
            if (w->state == WIN_STATE_MAXIMIZED) {
                wx = 0;
                wy = TASKBAR_HEIGHT;
            }

            /* Window Drop Shadow */
            fill_rect(wx + 4, wy + 4, ww, wh + TITLEBAR_HEIGHT, 0x55000000);

            /* Window Title Bar */
            uint32_t bar_color = w->focused ? COLOR_TITLEBAR_ACTIVE : COLOR_TITLEBAR_INACTIVE;
            fill_rect(wx, wy, ww, TITLEBAR_HEIGHT, bar_color);
            draw_rect(wx, wy, ww, TITLEBAR_HEIGHT + wh, COLOR_WINDOW_BORDER);

            /* Window Title Text */
            uint32_t title_color = w->focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;
            draw_text(wx + 10, wy + 5, w->title, title_color, 0);

            /* Window Control Buttons */
            int32_t right = wx + (int32_t)ww;
            /* Minimize [-] */
            fill_rect(right - 58, wy + 6, 14, 14, 0xFF313244);
            draw_text(right - 54, wy + 6, "-", COLOR_TEXT_PRIMARY, 0);
            /* Maximize [+] */
            fill_rect(right - 38, wy + 6, 14, 14, 0xFF313244);
            draw_text(right - 34, wy + 6, "+", COLOR_TEXT_PRIMARY, 0);
            /* Close [X] */
            fill_rect(right - 18, wy + 6, 14, 14, COLOR_DANGER);
            draw_text(right - 14, wy + 6, "x", 0xFFFFFFFF, 0);

            /* Window Client Area: Blit Window Pixels */
            size_t req_bytes = (size_t)w->width * w->height * sizeof(uint32_t);
            int read_bytes = win_server_op(WS_OP_READ_PIXELS, (void *)(uint64_t)w->id, win_pixel_buf, (void *)req_bytes);
            if (read_bytes > 0) {
                uint32_t blit_w = (ww < w->width) ? ww : w->width;
                uint32_t blit_h = (wh < w->height) ? wh : w->height;

                for (uint32_t r = 0; r < blit_h; r++) {
                    int32_t py = wy + TITLEBAR_HEIGHT + r;
                    if (py >= SCREEN_HEIGHT) break;
                    uint32_t *dst = &screen_buffer[py * SCREEN_WIDTH + wx];
                    uint32_t *src = &win_pixel_buf[r * w->width];
                    memcpy(dst, src, blit_w * sizeof(uint32_t));
                }
            } else {
                fill_rect(wx, wy + TITLEBAR_HEIGHT, ww, wh, COLOR_WINDOW_BG);
            }
        }

        /* 4. Top Panel / Taskbar (Height 32px) */
        fill_rect(0, 0, SCREEN_WIDTH, TASKBAR_HEIGHT, COLOR_PANEL_BG);
        fill_rect(0, TASKBAR_HEIGHT - 1, SCREEN_WIDTH, 1, COLOR_PANEL_BORDER);

        /* Nyota Menu Button */
        uint32_t menu_btn_bg = launcher_open ? COLOR_ACCENT_PRIMARY : COLOR_BUTTON_NORMAL;
        uint32_t menu_btn_fg = launcher_open ? 0xFF000000 : COLOR_ACCENT_PRIMARY;
        fill_rect(4, 4, 80, 24, menu_btn_bg);
        draw_rect(4, 4, 80, 24, COLOR_ACCENT_PRIMARY);
        draw_text(12, 8, "NYOTA", menu_btn_fg, 0);

        /* Taskbar Window Items */
        int tb_x = 94;
        for (int i = 0; i < win_cnt; i++) {
            if (win_list[i].state == WIN_STATE_CLOSED) continue;
            uint32_t bg = win_list[i].focused ? COLOR_BUTTON_PRESSED : COLOR_BUTTON_NORMAL;
            fill_rect(tb_x, 4, 116, 24, bg);
            draw_rect(tb_x, 4, 116, 24, win_list[i].focused ? COLOR_ACCENT_PRIMARY : COLOR_WINDOW_BORDER);

            char short_title[14];
            strncpy(short_title, win_list[i].title, 12);
            short_title[12] = '\0';
            draw_text(tb_x + 6, 8, short_title, COLOR_TEXT_PRIMARY, 0);
            tb_x += 122;
        }

        /* Right Panel: Status & Live Clock */
        draw_text(SCREEN_WIDTH - 210, 8, "ETH0 [OK]", COLOR_SUCCESS, 0);

        time_t now = time(NULL);
        uint32_t secs = (uint32_t)(now % 60);
        uint32_t mins = (uint32_t)((now / 60) % 60);
        uint32_t hours = (uint32_t)((now / 3600) % 24);

        char time_str[16];
        time_str[0] = '0' + (hours / 10);
        time_str[1] = '0' + (hours % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (mins / 10);
        time_str[4] = '0' + (mins % 10);
        time_str[5] = ':';
        time_str[6] = '0' + (secs / 10);
        time_str[7] = '0' + (secs % 10);
        time_str[8] = '\0';
        draw_text(SCREEN_WIDTH - 84, 8, time_str, COLOR_TITLE_ACTIVE, 0);

        /* 5. Launcher Dropdown Menu */
        if (launcher_open) {
            int32_t menu_w = 180;
            int32_t menu_h = (int32_t)(LAUNCHER_COUNT * 26 + 8);
            fill_rect(4, TASKBAR_HEIGHT + 2, menu_w, menu_h, COLOR_PANEL_BG);
            draw_rect(4, TASKBAR_HEIGHT + 2, menu_w, menu_h, COLOR_ACCENT_PRIMARY);

            for (size_t i = 0; i < LAUNCHER_COUNT; i++) {
                int32_t item_y = TASKBAR_HEIGHT + 6 + (int32_t)(i * 26);
                bool item_hov = (cursor_x >= 4 && cursor_x <= 184 && cursor_y >= item_y && cursor_y < item_y + 24);
                if (item_hov) {
                    fill_rect(6, item_y, menu_w - 4, 22, COLOR_SELECTION);
                }
                draw_text(12, item_y + 3, launcher_items[i].label, item_hov ? COLOR_TITLE_ACTIVE : COLOR_TEXT_PRIMARY, 0);
            }
        }

        /* 6. Mouse Cursor */
        draw_cursor(cursor_x, cursor_y);

        /* 7. Double-Buffered Flip to Hardware Framebuffer */
        gfx_flip(screen_buffer, sizeof(screen_buffer));

        /* Sleep briefly for smooth ~40 FPS refresh rate */
        struct timespec req = { .tv_sec = 0, .tv_nsec = 25000000 }; /* 25ms */
        nanosleep(&req, NULL);
    }

    return 0;
}
