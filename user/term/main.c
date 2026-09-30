/* =============================================================================
 * Nyota OS — Native GUI Terminal Application (/bin/term)
 * Graphical terminal running interactive shell commands over standard VFS.
 * =========================================================================== */

#include "libnyota.h"
#include "gui.h"

#define TERM_WIDTH      640
#define TERM_HEIGHT     400
#define TERM_COLS       76
#define TERM_ROWS       22

static char term_lines[TERM_ROWS][TERM_COLS + 1];
static int term_cursor_row = 0;
static int term_cursor_col = 0;

static char input_line[256];
static int input_pos = 0;

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(term_lines[r], term_lines[r + 1], TERM_COLS + 1);
    }
    memset(term_lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    term_cursor_row = TERM_ROWS - 1;
}

static void term_print(const char *str) {
    while (*str) {
        char c = *str++;
        if (c == '\n') {
            term_cursor_col = 0;
            term_cursor_row++;
            if (term_cursor_row >= TERM_ROWS) {
                term_scroll();
            }
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') c = ' ';

        if (term_cursor_col >= TERM_COLS) {
            term_cursor_col = 0;
            term_cursor_row++;
            if (term_cursor_row >= TERM_ROWS) {
                term_scroll();
            }
        }

        term_lines[term_cursor_row][term_cursor_col++] = c;
    }
}

static void term_execute_cmd(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) return;

    if (strcmp(cmd, "clear") == 0) {
        memset(term_lines, 0, sizeof(term_lines));
        term_cursor_row = 0;
        term_cursor_col = 0;
        return;
    }

    if (strcmp(cmd, "exit") == 0) {
        exit(0);
    }

    /* Run the existing shell with pipe-backed stdin/stdout, preserving its parser. */
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) < 0) {
        term_print("Error: pipe creation failed\n");
        return;
    }
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        term_print("Error: pipe creation failed\n");
        return;
    }

    char *sh_argv[] = {"/bin/sh", NULL};
    int pid = spawn2("/bin/sh", sh_argv, in_pipe[0], out_pipe[1]);
    if (pid < 0) {
        term_print("Error: command not found\n");
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return;
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    write(in_pipe[1], cmd, strlen(cmd));
    write(in_pipe[1], "\nexit\n", 6);
    close(in_pipe[1]);
    char buf[128];
    int n;
    while ((n = read(out_pipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        term_print(buf);
    }
    close(out_pipe[0]);

    int status = 0;
    waitpid(pid, &status);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gui_window_t *win = gui_window_create(TERM_WIDTH, TERM_HEIGHT, "Nyota Terminal");
    if (!win) {
        printf("[TERM] Failed to create terminal window\n");
        return 1;
    }

    memset(term_lines, 0, sizeof(term_lines));
    memset(input_line, 0, sizeof(input_line));

    term_print("Nyota OS Graphical Terminal v1.0.0 (x86_64)\n");
    term_print("Type commands or 'exit' to close.\n\n");
    term_print("nyota$ ");

    while (!win->should_close) {
        input_event_t ev;
        while (gui_window_poll_event(win, &ev)) {
            if (ev.type == EVENT_WINDOW_CLOSE) {
                win->should_close = true;
                break;
            }

            if (ev.type == EVENT_KEY_PRESS) {
                char c = ev.character;
                if (c == '\r' || c == '\n') {
                    input_line[input_pos] = '\0';
                    term_print("\n");
                    term_execute_cmd(input_line);
                    input_pos = 0;
                    memset(input_line, 0, sizeof(input_line));
                    term_print("nyota$ ");
                } else if (c == '\b') {
                    if (input_pos > 0) {
                        input_pos--;
                        input_line[input_pos] = '\0';
                        if (term_cursor_col > 0) {
                            term_cursor_col--;
                            term_lines[term_cursor_row][term_cursor_col] = ' ';
                        }
                    }
                } else if (c >= 32 && c <= 126) {
                    if (input_pos < (int)sizeof(input_line) - 1) {
                        input_line[input_pos++] = c;
                        char s[2] = {c, '\0'};
                        term_print(s);
                    }
                }
            }
        }

        /* Render terminal screen */
        gui_fill_rect(win, 0, 0, TERM_WIDTH, TERM_HEIGHT, 0xFF11111B);

        for (int r = 0; r < TERM_ROWS; r++) {
            if (term_lines[r][0] != '\0') {
                gui_draw_text(win, 8, 8 + r * 17, term_lines[r], COLOR_TEXT_PRIMARY, 0);
            }
        }

        /* Draw block cursor */
        gui_fill_rect(win, 8 + term_cursor_col * 8, 8 + term_cursor_row * 17, 8, 16, COLOR_ACCENT_PRIMARY);

        gui_window_update(win);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30000000 };
        nanosleep(&ts, NULL);
    }

    gui_window_destroy(win);
    return 0;
}
