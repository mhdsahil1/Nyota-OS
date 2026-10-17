/* =============================================================================
 * Nyota OS SDK — Example GUI Application (hello_gui.c)
 * Demonstrates the native libgui toolkit: window creation, drawing primitives,
 * widget layout, event polling, and interactive button handling.
 * =========================================================================== */

#include <libnyota.h>
#include <gui.h>

#define WIN_W   320
#define WIN_H   240

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* 1. Create a native GUI window */
    gui_window_t *win = gui_window_create(WIN_W, WIN_H, "SDK GUI Demo");
    if (!win) {
        printf("[ERROR] Failed to open GUI window on Nyota desktop\n");
        return 1;
    }

    int click_count = 0;
    bool btn_hover = false;
    bool btn_pressed = false;

    /* Button geometry */
    int btn_x = 40;
    int btn_y = 140;
    int btn_w = 240;
    int btn_h = 40;

    /* 2. Main GUI Event Loop */
    while (!win->should_close) {
        input_event_t ev;

        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
            } else if (ev.type == EVENT_MOUSE_MOVE) {
                int mx = ev.mouse_x;
                int my = ev.mouse_y;
                btn_hover = (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h);
            } else if (ev.type == EVENT_MOUSE_BUTTON_PRESS && (ev.mouse_buttons & MOUSE_BTN_LEFT)) {
                int mx = ev.mouse_x;
                int my = ev.mouse_y;
                if (mx >= btn_x && mx < btn_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
                    btn_pressed = true;
                    click_count++;
                }
            } else if (ev.type == EVENT_MOUSE_BUTTON_RELEASE) {
                btn_pressed = false;
            } else if (ev.type == EVENT_KEY_PRESS) {
                if (ev.character == 27 || ev.character == 'q' || ev.character == 'Q') {
                    win->should_close = true;
                }
            }
        }

        /* 3. Render Window Content */
        gui_fill_rect(win, 0, 0, WIN_W, WIN_H, COLOR_WINDOW_BG);

        /* Header Card */
        gui_fill_rect(win, 12, 12, WIN_W - 24, 60, COLOR_WIDGET_BG);
        gui_draw_rect(win, 12, 12, WIN_W - 24, 60, COLOR_WIDGET_BORDER);
        gui_draw_text(win, 24, 22, "Nyota Native GUI Application", COLOR_TITLE_ACTIVE, 0);
        gui_draw_text(win, 24, 44, "Built using libgui & SDK v1.6.0", COLOR_TEXT_MUTED, 0);

        /* Click Counter Label */
        char counter_str[48];
        snprintf(counter_str, sizeof(counter_str), "Button Clicks: %d", click_count);
        gui_draw_text(win, 40, 96, counter_str, COLOR_ACCENT_PRIMARY, 0);

        /* Interactive Button */
        uint32_t btn_bg = btn_pressed ? COLOR_BUTTON_PRESSED : (btn_hover ? COLOR_BUTTON_HOVER : COLOR_BUTTON_NORMAL);
        gui_fill_rect(win, btn_x, btn_y, btn_w, btn_h, btn_bg);
        gui_draw_rect(win, btn_x, btn_y, btn_w, btn_h, btn_hover ? COLOR_ACCENT_PRIMARY : COLOR_WINDOW_BORDER);

        const char *btn_text = "Click Me!";
        int tx = btn_x + (btn_w - (int)(strlen(btn_text) * GUI_FONT_WIDTH)) / 2;
        int ty = btn_y + (btn_h - GUI_FONT_HEIGHT) / 2;
        gui_draw_text(win, tx, ty, btn_text, COLOR_TEXT_PRIMARY, 0);

        /* Hint */
        gui_draw_text(win, 24, 204, "Press 'Q' or [X] to exit", COLOR_TEXT_DIM, 0);

        /* 4. Update window buffer and present */
        gui_window_update(win);

        /* Sleep briefly (~30 FPS) */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 33000000 };
        nanosleep(&ts, NULL);
    }

    /* 5. Destroy window and clean up */
    gui_window_destroy(win);
    return 0;
}
