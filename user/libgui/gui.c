/* =============================================================================
 * Nyota OS — Native GUI Toolkit Core Implementation (libgui)
 * 2D rasterizer, font rendering, window lifecycle, and event dispatch.
 * =========================================================================== */

#include "gui.h"
#include "drivers/font8x16.h"

/* ── Lightweight Static Heap Allocator for GUI Userland ───────────────────── */
#define GUI_HEAP_CAPACITY (1536 * 1024) /* 1.5 MB BSS Heap */
static uint8_t g_gui_heap[GUI_HEAP_CAPACITY];
static size_t  g_gui_heap_offset = 0;

static void *gui_malloc(size_t size) {
    /* 8-byte alignment */
    size = (size + 7) & ~7;
    if (g_gui_heap_offset + size > GUI_HEAP_CAPACITY) {
        return NULL;
    }
    void *ptr = &g_gui_heap[g_gui_heap_offset];
    g_gui_heap_offset += size;
    return ptr;
}

static void gui_free(void *ptr) {
    (void)ptr;
    /* Static bump allocator; blocks are retained for process lifetime */
}

/* ── 2D Drawing Primitives ────────────────────────────────────────────────── */

void gui_draw_pixel(gui_window_t *win, int32_t x, int32_t y, uint32_t color) {
    if (!win || !win->buffer) return;
    if (x < 0 || (uint32_t)x >= win->width || y < 0 || (uint32_t)y >= win->height) {
        return;
    }
    win->buffer[y * win->width + x] = color;
}

void gui_fill_rect(gui_window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->buffer || w == 0 || h == 0) return;

    if (x < 0) {
        if ((int32_t)w <= -x) return;
        w += x;
        x = 0;
    }
    if (y < 0) {
        if ((int32_t)h <= -y) return;
        h += y;
        y = 0;
    }

    if ((uint32_t)x >= win->width || (uint32_t)y >= win->height) return;
    if (x + w > win->width)  w = win->width - x;
    if (y + h > win->height) h = win->height - y;

    for (uint32_t r = 0; r < h; r++) {
        uint32_t *row = &win->buffer[(y + r) * win->width + x];
        for (uint32_t c = 0; c < w; c++) {
            row[c] = color;
        }
    }
}

void gui_draw_rect(gui_window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->buffer || w == 0 || h == 0) return;
    gui_fill_rect(win, x, y, w, 1, color);
    gui_fill_rect(win, x, y + h - 1, w, 1, color);
    gui_fill_rect(win, x, y, 1, h, color);
    gui_fill_rect(win, x + w - 1, y, 1, h, color);
}

void gui_draw_line(gui_window_t *win, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!win || !win->buffer) return;

    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gui_draw_pixel(win, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gui_draw_char(gui_window_t *win, int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!win || !win->buffer) return;
    uint8_t glyph = (uint8_t)c;
    const uint8_t *bitmap = font8x16[glyph];

    for (int row = 0; row < 16; row++) {
        uint8_t line = bitmap[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                gui_draw_pixel(win, x + col, y + row, fg);
            } else if (bg != 0) {
                gui_draw_pixel(win, x + col, y + row, bg);
            }
        }
    }
}

void gui_draw_text(gui_window_t *win, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg) {
    if (!win || !win->buffer || !str) return;

    int32_t cur_x = x;
    int32_t cur_y = y;

    while (*str) {
        char c = *str++;
        if (c == '\n') {
            cur_x = x;
            cur_y += 18;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') {
            cur_x += 8 * 4;
            continue;
        }

        gui_draw_char(win, cur_x, cur_y, c, fg, bg);
        cur_x += 8;
    }
}

/* ── Window Lifecycle ─────────────────────────────────────────────────────── */

gui_window_t *gui_window_create(uint32_t width, uint32_t height, const char *title) {
    if (width == 0 || height == 0) return NULL;

    int win_id = win_create(width, height, title);
    if (win_id <= 0) return NULL;

    gui_window_t *win = (gui_window_t *)gui_malloc(sizeof(gui_window_t));
    if (!win) {
        win_destroy((uint32_t)win_id);
        return NULL;
    }
    memset(win, 0, sizeof(gui_window_t));

    win->win_id = (uint32_t)win_id;
    win->width  = width;
    win->height = height;
    strncpy(win->title, title ? title : "Nyota App", WIN_TITLE_MAX - 1);

    win->buffer_size = (size_t)width * height * sizeof(uint32_t);
    win->buffer = (uint32_t *)gui_malloc(win->buffer_size);
    if (!win->buffer) {
        gui_free(win);
        win_destroy((uint32_t)win_id);
        return NULL;
    }

    /* Initialize background */
    gui_fill_rect(win, 0, 0, width, height, COLOR_WINDOW_BG);

    /* Create root panel */
    /* Keep the root panel transparent so immediate-mode app drawing survives updates. */
    win->root_widget = gui_panel_create(0, 0, width, height, 0);

    return win;
}

void gui_window_destroy(gui_window_t *win) {
    if (!win) return;
    if (win->win_id > 0) {
        win_destroy(win->win_id);
    }
    if (win->buffer) {
        gui_free(win->buffer);
    }
    gui_free(win);
}

void gui_window_update(gui_window_t *win) {
    if (!win || !win->buffer) return;

    /* Draw full widget tree into local buffer */
    gui_window_draw(win);

    /* Blit to kernel backing store */
    win_update(win->win_id, win->buffer, 0, 0, win->width, win->height);
}

bool gui_window_poll_event(gui_window_t *win, input_event_t *ev) {
    if (!win || !ev) return false;
    int ret = win_get_event(win->win_id, ev, 0);
    return (ret > 0);
}

bool gui_window_wait_event(gui_window_t *win, input_event_t *ev) {
    if (!win || !ev) return false;
    int ret = win_get_event(win->win_id, ev, 1);
    return (ret > 0);
}

void gui_window_draw(gui_window_t *win) {
    if (!win || !win->buffer) return;
    if (win->root_widget) {
        gui_widget_draw(win, win->root_widget);
    }
}

void gui_window_dispatch_event(gui_window_t *win, const input_event_t *ev) {
    if (!win || !ev) return;

    if (ev->type == EVENT_WINDOW_CLOSE) {
        win->should_close = true;
        return;
    }

    if (win->root_widget) {
        gui_widget_handle_event(win, win->root_widget, ev);
    }
}
