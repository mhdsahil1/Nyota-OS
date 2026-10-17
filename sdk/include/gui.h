/* =============================================================================
 * Nyota OS — Native GUI Toolkit Public Interface (libgui)
 * Window abstractions, widget tree, drawing primitives, and event dispatch.
 * =========================================================================== */

#ifndef NYOTA_LIBGUI_GUI_H
#define NYOTA_LIBGUI_GUI_H

#include "types.h"
#include "libnyota.h"
#include "theme.h"

/* Widget Type Enumeration */
typedef enum {
    WIDGET_PANEL = 0,
    WIDGET_LABEL,
    WIDGET_BUTTON,
    WIDGET_TEXTBOX,
    WIDGET_LIST,
    WIDGET_MENU,
    WIDGET_CHECKBOX,
    WIDGET_PROGRESSBAR,
    WIDGET_SCROLLBAR,
    WIDGET_DIALOG
} gui_widget_type_t;

struct gui_widget;
struct gui_window;

typedef void (*gui_callback_t)(struct gui_widget *w, void *user_data);

/* Base Widget Structure */
typedef struct gui_widget {
    gui_widget_type_t type;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;

    bool visible;
    bool enabled;
    bool focused;
    bool hovered;
    bool pressed;

    char text[128];
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t border_color;

    /* Specific widget state */
    bool checked;                      /* Checkbox */
    int32_t value;                     /* ProgressBar / ScrollBar */
    int32_t min_val;
    int32_t max_val;
    int32_t cursor_pos;                /* TextBox */

    /* List / Menu items */
    char items[16][64];
    int32_t item_count;
    int32_t selected_item;

    gui_callback_t on_click;
    gui_callback_t on_change;
    void *user_data;

    struct gui_widget *parent;
    struct gui_widget *children[16];
    uint32_t child_count;
} gui_widget_t;

/* Window Structure */
typedef struct gui_window {
    uint32_t win_id;
    uint32_t width;
    uint32_t height;
    char title[WIN_TITLE_MAX];
    uint32_t *buffer;          /* Local pixel buffer (width * height * 4) */
    size_t   buffer_size;

    gui_widget_t *root_widget;
    gui_widget_t *focused_widget;

    bool should_close;
} gui_window_t;

/* ── 2D Drawing Primitives ────────────────────────────────────────────────── */
void gui_draw_pixel(gui_window_t *win, int32_t x, int32_t y, uint32_t color);
void gui_fill_rect(gui_window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void gui_draw_rect(gui_window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void gui_draw_line(gui_window_t *win, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void gui_draw_char(gui_window_t *win, int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);
void gui_draw_text(gui_window_t *win, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg);

/* ── Window Lifecycle ─────────────────────────────────────────────────────── */
gui_window_t *gui_window_create(uint32_t width, uint32_t height, const char *title);
void          gui_window_destroy(gui_window_t *win);
void          gui_window_update(gui_window_t *win);
bool          gui_window_poll_event(gui_window_t *win, input_event_t *ev);
bool          gui_window_wait_event(gui_window_t *win, input_event_t *ev);
void          gui_window_draw(gui_window_t *win);
void          gui_window_dispatch_event(gui_window_t *win, const input_event_t *ev);

/* ── Widget Tree Constructors ─────────────────────────────────────────────── */
gui_widget_t *gui_panel_create(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t bg_color);
gui_widget_t *gui_label_create(int32_t x, int32_t y, const char *text, uint32_t fg_color);
gui_widget_t *gui_button_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *text, gui_callback_t on_click);
gui_widget_t *gui_textbox_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *initial_text);
gui_widget_t *gui_checkbox_create(int32_t x, int32_t y, const char *text, bool initial_state, gui_callback_t on_change);
gui_widget_t *gui_progressbar_create(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t min, int32_t max, int32_t val);
gui_widget_t *gui_scrollbar_create(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t min, int32_t max, int32_t val);
gui_widget_t *gui_list_create(int32_t x, int32_t y, uint32_t w, uint32_t h);
gui_widget_t *gui_menu_create(int32_t x, int32_t y, uint32_t w);
gui_widget_t *gui_dialog_create(const char *title, const char *message);

void gui_widget_add_child(gui_widget_t *parent, gui_widget_t *child);
void gui_list_add_item(gui_widget_t *list, const char *item);
void gui_widget_draw(gui_window_t *win, gui_widget_t *w);
bool gui_widget_handle_event(gui_window_t *win, gui_widget_t *w, const input_event_t *ev);

#endif /* NYOTA_LIBGUI_GUI_H */
