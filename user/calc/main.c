/* =============================================================================
 * Nyota OS — Native GUI Calculator Application (/bin/calc)
 * Robust fixed-point arithmetic calculator with display, button grid, and keyboard support.
 * Freestanding-compatible (no hardware SSE / floating-point dependency).
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define CALC_WIDTH      280
#define CALC_HEIGHT     380

#define SCALE           1000000LL  /* 6 decimal places */

/* State */
static char display[32] = "0";
static int64_t accumulator = 0;
static char pending_op = 0;
static bool new_input = true;
static bool has_dot = false;
static bool has_error = false;

/* ── Fixed-Point Arithmetic ───────────────────────────────────────────────── */

static int64_t parse_display(void) {
    int64_t int_part = 0;
    int64_t frac_part = 0;
    int64_t frac_div = 1;
    bool neg = false;
    const char *p = display;

    if (*p == '-') {
        neg = true;
        p++;
    }

    while (*p && *p != '.') {
        if (*p >= '0' && *p <= '9') {
            int_part = int_part * 10 + (*p - '0');
        }
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p && frac_div < SCALE) {
            if (*p >= '0' && *p <= '9') {
                frac_part = frac_part * 10 + (*p - '0');
                frac_div *= 10;
            }
            p++;
        }
        /* Scale up fractional part to full SCALE */
        while (frac_div < SCALE) {
            frac_part *= 10;
            frac_div *= 10;
        }
    }

    int64_t res = int_part * SCALE + frac_part;
    return neg ? -res : res;
}

static void format_display(int64_t val, char *buf, size_t len) {
    if (len == 0) return;
    if (val < 0) {
        *buf++ = '-';
        len--;
        val = -val;
    }

    int64_t int_part = val / SCALE;
    int64_t frac_part = val % SCALE;

    /* Format integer portion */
    char tmp[32];
    int ti = 0;
    if (int_part == 0) {
        tmp[ti++] = '0';
    } else {
        while (int_part > 0 && ti < 30) {
            tmp[ti++] = '0' + (int)(int_part % 10);
            int_part /= 10;
        }
    }

    for (int i = ti - 1; i >= 0 && len > 1; i--) {
        *buf++ = tmp[i];
        len--;
    }

    /* Format fractional part if nonzero */
    if (frac_part > 0 && len > 2) {
        *buf++ = '.';
        len--;

        /* 6 digits */
        char frac_str[8];
        snprintf(frac_str, sizeof(frac_str), "%06llu", frac_part);

        /* Strip trailing zeros */
        int last = 5;
        while (last > 0 && frac_str[last] == '0') {
            last--;
        }

        for (int i = 0; i <= last && len > 1; i++) {
            *buf++ = frac_str[i];
            len--;
        }
    }

    *buf = '\0';
}

static int64_t calc_apply(int64_t lhs, char op, int64_t rhs) {
    switch (op) {
        case '+': return lhs + rhs;
        case '-': return lhs - rhs;
        case '*': return (lhs * rhs) / SCALE;
        case '/':
            if (rhs == 0) {
                has_error = true;
                return 0;
            }
            return (lhs * SCALE) / rhs;
        default: return rhs;
    }
}

/* ── UI Interaction ───────────────────────────────────────────────────────── */

static void calc_input(const char *key) {
    if (has_error) {
        /* Reset on error unless key is clear */
        strncpy(display, "0", sizeof(display));
        accumulator = 0;
        pending_op = 0;
        new_input = true;
        has_dot = false;
        has_error = false;
    }

    if (strcmp(key, "C") == 0) {
        strncpy(display, "0", sizeof(display));
        accumulator = 0;
        pending_op = 0;
        new_input = true;
        has_dot = false;
        has_error = false;
        return;
    }

    if (strcmp(key, "CE") == 0) {
        strncpy(display, "0", sizeof(display));
        new_input = true;
        has_dot = false;
        return;
    }

    if (strcmp(key, "<-") == 0) {
        size_t len = strlen(display);
        if (len > 1) {
            if (display[len - 1] == '.') has_dot = false;
            display[len - 1] = '\0';
        } else {
            strncpy(display, "0", sizeof(display));
            new_input = true;
        }
        return;
    }

    if (strcmp(key, "+/-") == 0) {
        if (strcmp(display, "0") != 0) {
            if (display[0] == '-') {
                memmove(display, display + 1, strlen(display));
            } else {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "-%s", display);
                strncpy(display, tmp, sizeof(display));
            }
        }
        return;
    }

    if (strcmp(key, ".") == 0) {
        if (new_input) {
            strncpy(display, "0.", sizeof(display));
            new_input = false;
            has_dot = true;
        } else if (!has_dot) {
            size_t len = strlen(display);
            if (len < sizeof(display) - 2) {
                display[len] = '.';
                display[len + 1] = '\0';
                has_dot = true;
            }
        }
        return;
    }

    /* Digit 0-9 */
    if (key[0] >= '0' && key[0] <= '9' && key[1] == '\0') {
        if (new_input || (strcmp(display, "0") == 0 && !has_dot)) {
            display[0] = key[0];
            display[1] = '\0';
            new_input = false;
        } else {
            size_t len = strlen(display);
            if (len < sizeof(display) - 2) {
                display[len] = key[0];
                display[len + 1] = '\0';
            }
        }
        return;
    }

    /* Operators: +, -, *, / */
    if (strcmp(key, "+") == 0 || strcmp(key, "-") == 0 ||
        strcmp(key, "*") == 0 || strcmp(key, "/") == 0) {
        int64_t current = parse_display();
        if (pending_op != 0 && !new_input) {
            accumulator = calc_apply(accumulator, pending_op, current);
            if (has_error) {
                strncpy(display, "Error: Div by zero", sizeof(display));
                return;
            }
            format_display(accumulator, display, sizeof(display));
        } else {
            accumulator = current;
        }
        pending_op = key[0];
        new_input = true;
        has_dot = false;
        return;
    }

    /* Equals */
    if (strcmp(key, "=") == 0) {
        if (pending_op != 0) {
            int64_t current = parse_display();
            int64_t result = calc_apply(accumulator, pending_op, current);
            if (has_error) {
                strncpy(display, "Error: Div by zero", sizeof(display));
            } else {
                format_display(result, display, sizeof(display));
                accumulator = result;
            }
            pending_op = 0;
            new_input = true;
            has_dot = (strchr(display, '.') != NULL);
        }
        return;
    }
}

/* ── Layout & Button Grid ─────────────────────────────────────────────────── */

typedef struct {
    const char *label;
    int col;
    int row;
    uint32_t bg;
    uint32_t fg;
} btn_def_t;

static const btn_def_t buttons[] = {
    {"C",   0, 0, 0xFF45475A, COLOR_DANGER},
    {"CE",  1, 0, 0xFF45475A, COLOR_TEXT_PRIMARY},
    {"<-",  2, 0, 0xFF45475A, COLOR_WARNING},
    {"/",   3, 0, 0xFF585B70, COLOR_ACCENT_PRIMARY},

    {"7",   0, 1, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"8",   1, 1, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"9",   2, 1, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"*",   3, 1, 0xFF585B70, COLOR_ACCENT_PRIMARY},

    {"4",   0, 2, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"5",   1, 2, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"6",   2, 2, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"-",   3, 2, 0xFF585B70, COLOR_ACCENT_PRIMARY},

    {"1",   0, 3, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"2",   1, 3, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"3",   2, 3, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"+",   3, 3, 0xFF585B70, COLOR_ACCENT_PRIMARY},

    {"+/-", 0, 4, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"0",   1, 4, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {".",   2, 4, COLOR_BUTTON_NORMAL, COLOR_TEXT_PRIMARY},
    {"=",   3, 4, COLOR_ACCENT_PRIMARY, 0xFF181825},
};
#define NUM_BUTTONS (sizeof(buttons) / sizeof(buttons[0]))

#define GRID_X      12
#define GRID_Y      80
#define BTN_W       58
#define BTN_H       48
#define BTN_GAP     8

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(CALC_WIDTH, CALC_HEIGHT, "Calculator");
    if (!win) {
        printf("[CALC] Failed to create GUI window\n");
        return 1;
    }

    int pressed_btn = -1;

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
            } else if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {
                int mx = ev.mouse_x;
                int my = ev.mouse_y;

                for (size_t i = 0; i < NUM_BUTTONS; i++) {
                    int bx = GRID_X + buttons[i].col * (BTN_W + BTN_GAP);
                    int by = GRID_Y + buttons[i].row * (BTN_H + BTN_GAP);
                    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
                        pressed_btn = (int)i;
                        calc_input(buttons[i].label);
                        break;
                    }
                }
            } else if (ev.type == EVENT_MOUSE_BUTTON_RELEASE) {
                pressed_btn = -1;
            } else if (ev.type == EVENT_KEY_PRESS) {
                char ch = ev.character;
                if (ch >= '0' && ch <= '9') {
                    char s[2] = { ch, '\0' };
                    calc_input(s);
                } else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
                    char s[2] = { ch, '\0' };
                    calc_input(s);
                } else if (ch == '=' || ch == '\n' || ch == '\r') {
                    calc_input("=");
                } else if (ch == '.') {
                    calc_input(".");
                } else if (ch == '\b') {
                    calc_input("<-");
                } else if (ch == 27) { /* ESC */
                    calc_input("C");
                }
            }
        }

        /* ── Render ───────────────────────────────────────────────────────── */
        gui_fill_rect(win, 0, 0, CALC_WIDTH, CALC_HEIGHT, COLOR_WINDOW_BG);

        /* Display Panel */
        gui_fill_rect(win, 12, 12, CALC_WIDTH - 24, 52, COLOR_TEXTBOX_BG);
        gui_draw_rect(win, 12, 12, CALC_WIDTH - 24, 52, COLOR_TEXTBOX_BORDER);

        /* Pending operator watermark */
        if (pending_op) {
            char op_str[4] = { pending_op, '\0' };
            gui_draw_text(win, 20, 18, op_str, COLOR_ACCENT_PRIMARY, 0);
        }

        /* Right-aligned display text */
        size_t dlen = strlen(display);
        int text_x = (CALC_WIDTH - 24) - (int)(dlen * GUI_FONT_WIDTH) - 4;
        if (text_x < 20) text_x = 20;
        gui_draw_text(win, text_x, 32, display, has_error ? COLOR_DANGER : COLOR_TITLE_ACTIVE, 0);

        /* Button Grid */
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            int bx = GRID_X + buttons[i].col * (BTN_W + BTN_GAP);
            int by = GRID_Y + buttons[i].row * (BTN_H + BTN_GAP);

            bool is_pressed = (pressed_btn == (int)i);
            uint32_t bg = is_pressed ? COLOR_BUTTON_PRESSED : buttons[i].bg;
            uint32_t fg = buttons[i].fg;

            gui_fill_rect(win, bx, by, BTN_W, BTN_H, bg);
            gui_draw_rect(win, bx, by, BTN_W, BTN_H, COLOR_WIDGET_BORDER);

            /* Centered label */
            size_t llen = strlen(buttons[i].label);
            int lx = bx + (BTN_W - (int)(llen * GUI_FONT_WIDTH)) / 2;
            int ly = by + (BTN_H - GUI_FONT_HEIGHT) / 2;
            gui_draw_text(win, lx, ly, buttons[i].label, fg, 0);
        }

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
