/* =============================================================================
 * Nyota OS — Native GUI Toolkit Widget Tree & Controls (libgui)
 * Panel, Label, Button, TextBox, List, Menu, Checkbox, ProgressBar, ScrollBar, Dialog.
 * =========================================================================== */

#include "gui.h"

static gui_widget_t *widget_alloc(gui_widget_type_t type, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    static gui_widget_t widget_pool[128];
    static uint32_t widget_pool_idx = 0;

    if (widget_pool_idx >= 128) return NULL;
    gui_widget_t *widget = &widget_pool[widget_pool_idx++];
    memset(widget, 0, sizeof(gui_widget_t));

    widget->type    = type;
    widget->x       = x;
    widget->y       = y;
    widget->width   = w;
    widget->height  = h;
    widget->visible = true;
    widget->enabled = true;
    widget->fg_color = COLOR_TEXT_PRIMARY;
    widget->bg_color = COLOR_WIDGET_BG;
    widget->border_color = COLOR_WIDGET_BORDER;

    return widget;
}

void gui_widget_add_child(gui_widget_t *parent, gui_widget_t *child) {
    if (!parent || !child || parent->child_count >= 16) return;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

gui_widget_t *gui_panel_create(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t bg_color) {
    gui_widget_t *p = widget_alloc(WIDGET_PANEL, x, y, w, h);
    if (!p) return NULL;
    p->bg_color = bg_color;
    p->border_color = 0;
    return p;
}

gui_widget_t *gui_label_create(int32_t x, int32_t y, const char *text, uint32_t fg_color) {
    uint32_t len = text ? (uint32_t)strlen(text) : 0;
    gui_widget_t *l = widget_alloc(WIDGET_LABEL, x, y, len * 8, 16);
    if (!l) return NULL;
    if (text) strncpy(l->text, text, sizeof(l->text) - 1);
    l->fg_color = fg_color ? fg_color : COLOR_TEXT_PRIMARY;
    l->bg_color = 0;
    l->border_color = 0;
    return l;
}

gui_widget_t *gui_button_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *text, gui_callback_t on_click) {
    gui_widget_t *b = widget_alloc(WIDGET_BUTTON, x, y, w, h);
    if (!b) return NULL;
    if (text) strncpy(b->text, text, sizeof(b->text) - 1);
    b->bg_color = COLOR_BUTTON_NORMAL;
    b->fg_color = COLOR_TEXT_PRIMARY;
    b->border_color = COLOR_WINDOW_BORDER;
    b->on_click = on_click;
    return b;
}

gui_widget_t *gui_textbox_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *initial_text) {
    gui_widget_t *tb = widget_alloc(WIDGET_TEXTBOX, x, y, w, h);
    if (!tb) return NULL;
    if (initial_text) {
        strncpy(tb->text, initial_text, sizeof(tb->text) - 1);
        tb->cursor_pos = (int32_t)strlen(tb->text);
    }
    tb->bg_color = COLOR_TEXTBOX_BG;
    tb->fg_color = COLOR_TEXT_PRIMARY;
    tb->border_color = COLOR_TEXTBOX_BORDER;
    return tb;
}

gui_widget_t *gui_checkbox_create(int32_t x, int32_t y, const char *text, bool initial_state, gui_callback_t on_change) {
    uint32_t len = text ? (uint32_t)strlen(text) : 0;
    gui_widget_t *cb = widget_alloc(WIDGET_CHECKBOX, x, y, 20 + len * 8, 18);
    if (!cb) return NULL;
    if (text) strncpy(cb->text, text, sizeof(cb->text) - 1);
    cb->checked = initial_state;
    cb->on_change = on_change;
    return cb;
}

gui_widget_t *gui_progressbar_create(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t min, int32_t max, int32_t val) {
    gui_widget_t *pb = widget_alloc(WIDGET_PROGRESSBAR, x, y, w, h);
    if (!pb) return NULL;
    pb->min_val = min;
    pb->max_val = max > min ? max : (min + 100);
    pb->value   = val;
    pb->bg_color = COLOR_PROGRESS_BG;
    pb->border_color = COLOR_WIDGET_BORDER;
    return pb;
}

gui_widget_t *gui_scrollbar_create(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t min, int32_t max, int32_t val) {
    gui_widget_t *sb = widget_alloc(WIDGET_SCROLLBAR, x, y, w, h);
    if (!sb) return NULL;
    sb->min_val = min;
    sb->max_val = max;
    sb->value = val;
    sb->bg_color = COLOR_WIDGET_BG;
    sb->border_color = COLOR_WIDGET_BORDER;
    return sb;
}

gui_widget_t *gui_list_create(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    gui_widget_t *l = widget_alloc(WIDGET_LIST, x, y, w, h);
    if (!l) return NULL;
    l->item_count = 0;
    l->selected_item = -1;
    l->bg_color = COLOR_TEXTBOX_BG;
    l->border_color = COLOR_WIDGET_BORDER;
    return l;
}

void gui_list_add_item(gui_widget_t *list, const char *item) {
    if (!list || !item || list->item_count >= 16) return;
    strncpy(list->items[list->item_count++], item, 63);
}

gui_widget_t *gui_menu_create(int32_t x, int32_t y, uint32_t w) {
    gui_widget_t *m = widget_alloc(WIDGET_MENU, x, y, w, 24);
    if (!m) return NULL;
    m->bg_color = COLOR_PANEL_BG;
    m->border_color = COLOR_PANEL_BORDER;
    return m;
}

gui_widget_t *gui_dialog_create(const char *title, const char *message) {
    gui_widget_t *dlg = widget_alloc(WIDGET_DIALOG, 20, 20, 360, 180);
    if (!dlg) return NULL;
    if (title) strncpy(dlg->text, title, sizeof(dlg->text) - 1);

    /* Message label */
    gui_widget_t *lbl = gui_label_create(16, 40, message, COLOR_TEXT_PRIMARY);
    gui_widget_add_child(dlg, lbl);

    /* OK button */
    gui_widget_t *btn_ok = gui_button_create(130, 120, 100, 32, "OK", NULL);
    gui_widget_add_child(dlg, btn_ok);

    return dlg;
}

/* ── Widget Drawing ───────────────────────────────────────────────────────── */

void gui_widget_draw(gui_window_t *win, gui_widget_t *w) {
    if (!win || !w || !w->visible) return;

    /* Compute absolute coordinates within window */
    int32_t abs_x = w->x;
    int32_t abs_y = w->y;
    gui_widget_t *p = w->parent;
    while (p) {
        abs_x += p->x;
        abs_y += p->y;
        p = p->parent;
    }

    switch (w->type) {
        case WIDGET_PANEL:
            if (w->bg_color) {
                gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            }
            if (w->border_color) {
                gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);
            }
            break;

        case WIDGET_LABEL:
            gui_draw_text(win, abs_x, abs_y, w->text, w->fg_color, 0);
            break;

        case WIDGET_BUTTON: {
            uint32_t bg = w->pressed ? COLOR_BUTTON_PRESSED :
                          (w->hovered ? COLOR_BUTTON_HOVER : w->bg_color);
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, bg);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);

            /* Center text inside button */
            size_t text_w = strlen(w->text) * 8;
            int32_t tx = abs_x + (int32_t)(w->width - text_w) / 2;
            int32_t ty = abs_y + (int32_t)(w->height - 16) / 2;
            if (tx < abs_x + 4) tx = abs_x + 4;
            gui_draw_text(win, tx, ty, w->text, w->fg_color, 0);
            break;
        }

        case WIDGET_TEXTBOX: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            uint32_t border = w->focused ? COLOR_ACCENT_PRIMARY : w->border_color;
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, border);

            gui_draw_text(win, abs_x + 6, abs_y + (int32_t)(w->height - 16) / 2, w->text, w->fg_color, 0);

            /* Draw text cursor */
            if (w->focused) {
                int32_t cx = abs_x + 6 + (w->cursor_pos * 8);
                int32_t cy = abs_y + (int32_t)(w->height - 16) / 2;
                if ((uint32_t)cx < (uint32_t)(abs_x + w->width - 8)) {
                    gui_fill_rect(win, cx, cy, 2, 16, COLOR_ACCENT_PRIMARY);
                }
            }
            break;
        }

        case WIDGET_CHECKBOX: {
            /* Draw square box */
            gui_fill_rect(win, abs_x, abs_y, 16, 16, COLOR_TEXTBOX_BG);
            gui_draw_rect(win, abs_x, abs_y, 16, 16, w->checked ? COLOR_ACCENT_PRIMARY : COLOR_WIDGET_BORDER);
            if (w->checked) {
                gui_draw_text(win, abs_x + 4, abs_y, "x", COLOR_ACCENT_PRIMARY, 0);
            }
            gui_draw_text(win, abs_x + 24, abs_y, w->text, w->fg_color, 0);
            break;
        }

        case WIDGET_PROGRESSBAR: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);

            int32_t range = w->max_val - w->min_val;
            if (range <= 0) range = 100;
            int32_t current = w->value - w->min_val;
            if (current < 0) current = 0;
            if (current > range) current = range;

            uint32_t fill_w = (uint32_t)(((uint64_t)current * (w->width - 4)) / (uint64_t)range);
            if (fill_w > 0) {
                gui_fill_rect(win, abs_x + 2, abs_y + 2, fill_w, w->height - 4, COLOR_PROGRESS_BAR);
            }
            break;
        }

        case WIDGET_SCROLLBAR: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);
            /* Draw scroll thumb */
            gui_fill_rect(win, abs_x + 2, abs_y + 4, w->width - 4, 30, COLOR_BUTTON_NORMAL);
            break;
        }

        case WIDGET_LIST: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);

            int32_t item_y = abs_y + 4;
            for (int i = 0; i < w->item_count; i++) {
                if (item_y + 20 > abs_y + (int32_t)w->height) break;
                if (i == w->selected_item) {
                    gui_fill_rect(win, abs_x + 2, item_y - 2, w->width - 4, 20, COLOR_SELECTION);
                    gui_draw_text(win, abs_x + 6, item_y, w->items[i], COLOR_TITLE_ACTIVE, 0);
                } else {
                    gui_draw_text(win, abs_x + 6, item_y, w->items[i], w->fg_color, 0);
                }
                item_y += 22;
            }
            break;
        }

        case WIDGET_MENU: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, w->bg_color);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, w->border_color);
            gui_draw_text(win, abs_x + 8, abs_y + 4, w->text, w->fg_color, 0);
            break;
        }

        case WIDGET_DIALOG: {
            gui_fill_rect(win, abs_x, abs_y, w->width, w->height, COLOR_WINDOW_BG);
            gui_draw_rect(win, abs_x, abs_y, w->width, w->height, COLOR_ACCENT_PRIMARY);
            /* Title header */
            gui_fill_rect(win, abs_x, abs_y, w->width, 24, COLOR_TITLEBAR_ACTIVE);
            gui_draw_text(win, abs_x + 8, abs_y + 4, w->text, COLOR_TITLE_ACTIVE, 0);
            break;
        }
    }

    /* Draw children recursively */
    for (uint32_t i = 0; i < w->child_count; i++) {
        gui_widget_draw(win, w->children[i]);
    }
}

/* ── Widget Event Handling ────────────────────────────────────────────────── */

bool gui_widget_handle_event(gui_window_t *win, gui_widget_t *w, const input_event_t *ev) {
    if (!win || !w || !w->visible || !w->enabled) return false;

    /* Compute absolute coordinates within window */
    int32_t abs_x = w->x;
    int32_t abs_y = w->y;
    gui_widget_t *p = w->parent;
    while (p) {
        abs_x += p->x;
        abs_y += p->y;
        p = p->parent;
    }

    bool inside = (ev->mouse_x >= abs_x && ev->mouse_x < abs_x + (int32_t)w->width &&
                   ev->mouse_y >= abs_y && ev->mouse_y < abs_y + (int32_t)w->height);

    /* Try children first (top-down) */
    for (int i = (int)w->child_count - 1; i >= 0; i--) {
        if (gui_widget_handle_event(win, w->children[i], ev)) {
            return true;
        }
    }

    if (ev->type == EVENT_MOUSE_MOVE) {
        w->hovered = inside;
        return inside;
    }

    if (ev->type == EVENT_MOUSE_BUTTON_PRESS && (ev->mouse_buttons & MOUSE_BTN_LEFT)) {
        if (inside) {
            w->focused = true;
            win->focused_widget = w;
            w->pressed = true;

            if (w->type == WIDGET_CHECKBOX) {
                w->checked = !w->checked;
                if (w->on_change) w->on_change(w, w->user_data);
            } else if (w->type == WIDGET_LIST) {
                int item_idx = (ev->mouse_y - (abs_y + 4)) / 22;
                if (item_idx >= 0 && item_idx < w->item_count) {
                    w->selected_item = item_idx;
                    if (w->on_change) w->on_change(w, w->user_data);
                }
            } else if (w->on_click) {
                w->on_click(w, w->user_data);
            }
            return true;
        } else {
            w->focused = false;
        }
    }

    if (ev->type == EVENT_MOUSE_BUTTON_RELEASE) {
        if (w->pressed) {
            w->pressed = false;
            return true;
        }
    }

    if (ev->type == EVENT_KEY_PRESS && w->focused && w->type == WIDGET_TEXTBOX) {
        char c = ev->character;
        size_t len = strlen(w->text);

        if (c == '\b') {
            /* Backspace */
            if (len > 0) {
                w->text[len - 1] = '\0';
                w->cursor_pos = (int32_t)len - 1;
                if (w->on_change) w->on_change(w, w->user_data);
            }
            return true;
        } else if (c >= 32 && c <= 126) {
            /* Append character */
            if (len + 1 < sizeof(w->text) - 1) {
                w->text[len] = c;
                w->text[len + 1] = '\0';
                w->cursor_pos = (int32_t)len + 1;
                if (w->on_change) w->on_change(w, w->user_data);
            }
            return true;
        }
    }

    return false;
}
